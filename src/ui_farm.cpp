#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

namespace {
constexpr uint8_t COUNT = 4;
constexpr const char *LED_NAMES[COUNT] = {"LED 0", "LED 1", "LED 2", "LED 3"};
constexpr const char *SW_NAMES[COUNT] = {"SW 0", "SW 1", "SW 2", "SW 3"};
constexpr const char *SOURCE_OPTIONS = "OFF\nA0\nA1\nA2\nA3\nSW0\nSW1\nSW2\nSW3";
constexpr const char *OP_OPTIONS = "< threshold\n> threshold";

struct RuleRow {
    lv_obj_t *enable;
    lv_obj_t *source;
    lv_obj_t *op;
    lv_obj_t *action;
    lv_obj_t *slider;
    lv_obj_t *threshold;
};
RuleRow gRuleUi[COUNT];
bool gRuleEnabled[COUNT] = {false, false, false, false};
uint8_t gRuleSource[COUNT] = {0, 0, 0, 0};  // 1-4 ADC0-3, 5-8 SW0-3
bool gRuleGreater[COUNT] = {false, false, false, false};
bool gRuleInvert[COUNT] = {false, false, false, false};
bool gRuleCondition[COUNT] = {false, false, false, false};
uint16_t gRuleThreshold[COUNT] = {1600, 1600, 1600, 1600};

lv_obj_t *gStatus, *gAdcValue[COUNT], *gAdcBar[COUNT], *gSwTile[COUNT], *gSwLabel[COUNT];
lv_obj_t *gLedButton[COUNT], *gLedLabel[COUNT];
int32_t gMilliVolts[COUNT] = {-1, -1, -1, -1};
uint32_t gAdcSamples[COUNT] = {0, 0, 0, 0};
uint8_t gManualLeds = 0;
uint8_t gOutputMask = 0xFF;
bool gOutputSynced = false;
uint8_t gSwitches = 0xFF;
uint8_t gSwitchCandidate = 0;
uint8_t gSwitchDebounce = 0;

lv_color_t green() { return lv_color_hex(0x42E6A4); }
lv_color_t muted() { return lv_color_hex(0x839DAF); }

void setButtonOpacity(void *button, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(button), static_cast<lv_opa_t>(value), 0);
}

void pulseButton(lv_obj_t *button)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, button);
    lv_anim_set_values(&anim, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, setButtonOpacity);
    lv_anim_start(&anim);
}

int32_t adcMillivolts(uint8_t ch)
{
    int32_t mv = static_cast<int32_t>(hal::adsRaw(ch)) * 4096L / 32768L;
    if (mv < 0) mv = 0;
    if (mv > cyd::ADS1115_DISPLAY_MAX_MV) mv = cyd::ADS1115_DISPLAY_MAX_MV;
    return mv;
}

void saveRule(uint8_t i)
{
    char key[5];
    snprintf(key, sizeof(key), "r%ue", i); hal::prefs().putBool(key, gRuleEnabled[i]);
    snprintf(key, sizeof(key), "r%us", i); hal::prefs().putUChar(key, gRuleSource[i]);
    snprintf(key, sizeof(key), "r%ug", i); hal::prefs().putBool(key, gRuleGreater[i]);
    snprintf(key, sizeof(key), "r%ui", i); hal::prefs().putBool(key, gRuleInvert[i]);
    snprintf(key, sizeof(key), "r%ut", i); hal::prefs().putUShort(key, gRuleThreshold[i]);
}

void loadRules()
{
    char key[5];
    for (uint8_t i = 0; i < COUNT; ++i) {
        snprintf(key, sizeof(key), "r%ue", i); gRuleEnabled[i] = hal::prefs().getBool(key, false);
        snprintf(key, sizeof(key), "r%us", i); gRuleSource[i] = hal::prefs().getUChar(key, 0);
        snprintf(key, sizeof(key), "r%ug", i); gRuleGreater[i] = hal::prefs().getBool(key, false);
        snprintf(key, sizeof(key), "r%ui", i); gRuleInvert[i] = hal::prefs().getBool(key, false);
        snprintf(key, sizeof(key), "r%ut", i); gRuleThreshold[i] = hal::prefs().getUShort(key, 1600);
        if (gRuleSource[i] > 8) gRuleSource[i] = 0;
    }
}

void renderLeds(uint8_t mask)
{
    for (uint8_t i = 0; i < COUNT; ++i) {
        const bool on = (mask & (1U << i)) != 0;
        const bool automatic = gRuleEnabled[i] && gRuleSource[i] != 0;
        lv_label_set_text_fmt(gLedLabel[i], "%s  %s", LED_NAMES[i], automatic ? "AUTO" : (on ? "ON" : "OFF"));
        lv_obj_set_style_bg_color(gLedButton[i], on ? lv_color_hex(0x17694F) : lv_color_hex(0x1B3040), 0);
        lv_obj_set_style_text_color(gLedLabel[i], on ? lv_color_hex(0xDFFFF2) : muted(), 0);
    }
}

uint8_t evaluateRules()
{
    uint8_t desired = gManualLeds;
    for (uint8_t i = 0; i < COUNT; ++i) {
        if (!gRuleEnabled[i] || gRuleSource[i] == 0) continue;
        bool condition = false;
        if (gRuleSource[i] <= 4) {
            const uint8_t ch = gRuleSource[i] - 1;
            if (!hal::adsOnline() || gMilliVolts[ch] < 0) {
                gRuleCondition[i] = false;
                desired &= ~(1U << i);
                continue;
            } else {
            // Small hysteresis prevents relay/LED chatter near the setpoint.
                if (gRuleGreater[i])
                    condition = gRuleCondition[i] ? gMilliVolts[ch] > static_cast<int32_t>(gRuleThreshold[i]) - 20
                                                   : gMilliVolts[ch] > gRuleThreshold[i];
                else
                    condition = gRuleCondition[i] ? gMilliVolts[ch] < static_cast<int32_t>(gRuleThreshold[i]) + 20
                                                   : gMilliVolts[ch] < gRuleThreshold[i];
                gRuleCondition[i] = condition;
            }
        } else {
            if (!hal::pcfSwitchValid()) {
                desired &= ~(1U << i);
                continue;
            }
            const uint8_t sw = gRuleSource[i] - 5;
            condition = (gSwitches & (1U << sw)) != 0;
        }
        if (condition != gRuleInvert[i]) desired |= (1U << i);
        else desired &= ~(1U << i);
    }
    return desired;
}

void applyOutputs()
{
    const uint8_t desired = evaluateRules();
    if (desired == gOutputMask && gOutputSynced && hal::pcfOnline() && hal::pcfLedMask() == desired) return;
    const uint8_t changed = desired ^ gOutputMask;
    gOutputMask = desired;
    gOutputSynced = hal::pcfWriteLeds(desired);
    renderLeds(desired);
    for (uint8_t i = 0; i < COUNT; ++i) {
        if (changed & (1U << i)) pulseButton(gLedButton[i]);
        const bool automatic = gRuleEnabled[i] && gRuleSource[i] != 0;
        if (automatic) lv_obj_add_state(gLedButton[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(gLedButton[i], LV_STATE_DISABLED);
    }
    if (changed && gOutputSynced) hal::soundTone(1175, 45);
}

void toggleLed(lv_event_t *e)
{
    const uint8_t i = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (gRuleEnabled[i] && gRuleSource[i]) return;
    gManualLeds ^= (1U << i);
    applyOutputs();
}

void updateRule(uint8_t i, bool persist)
{
    gRuleEnabled[i] = lv_obj_has_state(gRuleUi[i].enable, LV_STATE_CHECKED);
    gRuleSource[i] = static_cast<uint8_t>(lv_dropdown_get_selected(gRuleUi[i].source));
    gRuleGreater[i] = lv_dropdown_get_selected(gRuleUi[i].op) == 1;
    gRuleInvert[i] = lv_dropdown_get_selected(gRuleUi[i].action) == 1;
    gRuleThreshold[i] = static_cast<uint16_t>(lv_slider_get_value(gRuleUi[i].slider));
    lv_label_set_text_fmt(gRuleUi[i].threshold, "%u mV", gRuleThreshold[i]);
    if (gRuleSource[i] >= 5) {
        lv_obj_add_flag(gRuleUi[i].slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gRuleUi[i].op, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gRuleUi[i].threshold, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(gRuleUi[i].slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gRuleUi[i].op, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(gRuleUi[i].threshold, LV_OBJ_FLAG_HIDDEN);
    }
    if (persist) saveRule(i);
    applyOutputs();
}

void onRuleChanged(lv_event_t *e)
{
    const uint8_t i = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    updateRule(i, lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || lv_event_get_target(e) != gRuleUi[i].slider);
}

void tick(lv_timer_t *)
{
    for (uint8_t i = 0; i < COUNT; ++i) {
        const uint32_t samples = hal::adsSamples(i);
        if (samples != gAdcSamples[i]) {
            gAdcSamples[i] = samples;
            gMilliVolts[i] = adcMillivolts(i);
            lv_label_set_text_fmt(gAdcValue[i], "%ld mV", static_cast<long>(gMilliVolts[i]));
            lv_bar_set_value(gAdcBar[i], gMilliVolts[i], LV_ANIM_ON);
        }
    }

    const uint8_t sample = hal::pcfSwitchMask();
    if (hal::pcfSwitchValid()) {
        if (sample != gSwitchCandidate) { gSwitchCandidate = sample; gSwitchDebounce = 1; }
        else if (gSwitchDebounce < 2) ++gSwitchDebounce;
        if (gSwitchDebounce >= 2 && sample != gSwitches) {
            const uint8_t changed = sample ^ gSwitches;
            gSwitches = sample;
            for (uint8_t i = 0; i < COUNT; ++i) {
                if (!(changed & (1U << i))) continue;
                const bool down = (gSwitches & (1U << i)) != 0;
                lv_label_set_text(gSwLabel[i], down ? "PRESSED" : "RELEASED");
                lv_obj_set_style_bg_color(gSwTile[i], down ? lv_color_hex(0x264B3B) : lv_color_hex(0x172B3B), 0);
                lv_obj_set_style_text_color(gSwLabel[i], down ? green() : muted(), 0);
            }
        }
    }

    const bool devices = hal::pcfOnline() && hal::adsOnline();
    lv_label_set_text_fmt(gStatus, "%s  |  PCF 0x20  |  ADS 0x%02X",
                          devices ? "FARM SYSTEM ONLINE" : "CHECK I2C DEVICES", hal::adsAddress());
    lv_obj_set_style_text_color(gStatus, devices ? green() : lv_color_hex(0xFF7183), 0);
    applyOutputs();
}

lv_obj_t *makeMetric(lv_obj_t *parent, uint8_t i)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 136, 68);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x142A39), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x255344), 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 13, 0);
    lv_obj_set_style_pad_all(tile, 8, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 2, 0);
    lv_obj_t *title = ui_label(tile, "", &font_th_16, green());
    lv_label_set_text_fmt(title, "SENSOR  A%u", i);
    gAdcValue[i] = ui_label(tile, "-- mV", &lv_font_montserrat_14, lv_color_white());
    gAdcBar[i] = lv_bar_create(tile);
    lv_obj_set_size(gAdcBar[i], LV_PCT(100), 5);
    lv_bar_set_range(gAdcBar[i], 0, cyd::ADS1115_DISPLAY_MAX_MV);
    lv_obj_set_style_bg_color(gAdcBar[i], lv_color_hex(0x243B49), LV_PART_MAIN);
    lv_obj_set_style_bg_color(gAdcBar[i], green(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(gAdcBar[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(gAdcBar[i], LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    return tile;
}

void addRuleCard(lv_obj_t *parent, uint8_t i)
{
    lv_obj_t *card = ui_card(parent, nullptr);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x132634), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x244238), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_t *top = ui_row(card);
    lv_obj_t *title = ui_label(top, "", &font_th_16, green());
    lv_label_set_text_fmt(title, "RULE  %u  ->  LED %u", i + 1, i);
    gRuleUi[i].enable = lv_switch_create(top);
    lv_obj_set_size(gRuleUi[i].enable, 42, 23);

    lv_obj_t *choices = ui_row(card);
    gRuleUi[i].source = lv_dropdown_create(choices);
    lv_dropdown_set_options(gRuleUi[i].source, SOURCE_OPTIONS);
    lv_obj_set_width(gRuleUi[i].source, 112);
    gRuleUi[i].op = lv_dropdown_create(choices);
    lv_dropdown_set_options(gRuleUi[i].op, OP_OPTIONS);
    lv_obj_set_width(gRuleUi[i].op, 126);

    lv_obj_t *actionRow = ui_row(card);
    ui_label(actionRow, "เมื่อเงื่อนไขจริง ให้", nullptr, muted());
    gRuleUi[i].action = lv_dropdown_create(actionRow);
    lv_dropdown_set_options(gRuleUi[i].action, "LED ON\nLED OFF");
    lv_obj_set_width(gRuleUi[i].action, 104);

    lv_obj_t *thresholdRow = ui_row(card);
    gRuleUi[i].slider = lv_slider_create(thresholdRow);
    lv_obj_set_width(gRuleUi[i].slider, 190);
    lv_slider_set_range(gRuleUi[i].slider, 0, cyd::ADS1115_DISPLAY_MAX_MV);
    gRuleUi[i].threshold = ui_label(thresholdRow, "1600 mV", nullptr, lv_color_hex(0xB1C6D4));
    lv_obj_add_event_cb(gRuleUi[i].enable, onRuleChanged, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(gRuleUi[i].source, onRuleChanged, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(gRuleUi[i].op, onRuleChanged, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(gRuleUi[i].action, onRuleChanged, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(gRuleUi[i].slider, onRuleChanged, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_event_cb(gRuleUi[i].slider, onRuleChanged, LV_EVENT_RELEASED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
}
}  // namespace

void ui_farm_tab(lv_obj_t *tab)
{
    loadRules();
    lv_obj_t *hero = ui_card(tab, "SMART FARM  /  GREEN CONTROL");
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x0F2D2A), 0);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0x112333), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x28614C), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    ui_label(hero, "SMART FARM", &font_th_24, lv_color_hex(0xDDFCEF));
    ui_label(hero, "SENSE  /  DECIDE  /  GROW", nullptr, lv_color_hex(0x8AB7A4));
    gStatus = ui_label(hero, "กำลังตรวจสอบอุปกรณ์...", nullptr, green());

    lv_obj_t *section = ui_card(tab, "LIVE SENSORS  /  ADC 0-3.30 V");
    lv_obj_t *row = ui_row(section);
    for (uint8_t i = 0; i < COUNT; ++i) makeMetric(row, i);

    section = ui_card(tab, "FIELD SWITCHES  /  ACTIVE LOW");
    row = ui_row(section);
    for (uint8_t i = 0; i < COUNT; ++i) {
        gSwTile[i] = lv_obj_create(row);
        lv_obj_remove_style_all(gSwTile[i]);
        lv_obj_set_size(gSwTile[i], 66, 38);
        lv_obj_set_style_bg_opa(gSwTile[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(gSwTile[i], lv_color_hex(0x172B3B), 0);
        lv_obj_set_style_radius(gSwTile[i], 10, 0);
        lv_obj_set_style_pad_all(gSwTile[i], 4, 0);
        lv_obj_set_flex_flow(gSwTile[i], LV_FLEX_FLOW_COLUMN);
        ui_label(gSwTile[i], SW_NAMES[i], nullptr, lv_color_hex(0xC8D8E1));
        gSwLabel[i] = ui_label(gSwTile[i], "RELEASED", nullptr, muted());
    }

    section = ui_card(tab, "OUTPUT CONTROL  /  P0-P3");
    ui_label(section, "แตะ LED เพื่อควบคุมเอง  |  เมื่อเปิด Rule จะควบคุมอัตโนมัติ", nullptr, muted());
    row = ui_row(section);
    for (uint8_t i = 0; i < COUNT; ++i) {
        gLedButton[i] = lv_btn_create(row);
        lv_obj_set_size(gLedButton[i], 136, 40);
        lv_obj_set_style_radius(gLedButton[i], 11, 0);
        gLedLabel[i] = lv_label_create(gLedButton[i]);
        lv_obj_center(gLedLabel[i]);
        lv_label_set_text(gLedLabel[i], LED_NAMES[i]);
        lv_obj_add_event_cb(gLedButton[i], toggleLed, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    renderLeds(0);

    section = ui_card(tab, "AUTOMATION RULES  /  INPUT CONDITIONS CONTROL LED");
    ui_label(section, "เลือกแหล่งข้อมูล แล้วกำหนดเงื่อนไขแรงดัน หรือใช้สวิตช์กดเป็นเงื่อนไข", nullptr, muted());
    for (uint8_t i = 0; i < COUNT; ++i) addRuleCard(section, i);
    for (uint8_t i = 0; i < COUNT; ++i) {
        lv_dropdown_set_selected(gRuleUi[i].source, gRuleSource[i]);
        lv_dropdown_set_selected(gRuleUi[i].op, gRuleGreater[i] ? 1 : 0);
        lv_dropdown_set_selected(gRuleUi[i].action, gRuleInvert[i] ? 1 : 0);
        lv_slider_set_value(gRuleUi[i].slider, gRuleThreshold[i], LV_ANIM_OFF);
        lv_label_set_text_fmt(gRuleUi[i].threshold, "%u mV", gRuleThreshold[i]);
        if (gRuleEnabled[i]) lv_obj_add_state(gRuleUi[i].enable, LV_STATE_CHECKED);
        if (gRuleSource[i] >= 5) {
            lv_obj_add_flag(gRuleUi[i].slider, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gRuleUi[i].op, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gRuleUi[i].threshold, LV_OBJ_FLAG_HIDDEN);
        }
    }
    gOutputMask = 0xFF;
    tick(nullptr);
    lv_timer_create(tick, 100, nullptr);
}
