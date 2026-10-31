#include "app_config.h"
#include "board_config.h"
#include "hal.h"
#include "ui.h"

namespace {
uint8_t gLeds = 0;
uint8_t gSwitches = 0xFF;  // force first stable sample to populate every switch row
uint8_t gCandidate = 0;
uint8_t gCandidateCount = 0;
lv_obj_t *gLedCard, *gSwitchCard, *gStatus, *gSwitchLabel[4], *gLedButton[4];
lv_obj_t *gLedDot[4], *gLedLabel[4], *gSwitchDot[4], *gSwitchTile[4];
uint32_t gPollAt = 0;
constexpr const char *LED_NAMES[4] = {"LED 0", "LED 1", "LED 2", "LED 3"};
constexpr const char *SW_NAMES[4] = {"SWITCH 0", "SWITCH 1", "SWITCH 2", "SWITCH 3"};

void animate(lv_obj_t *obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&a, 180);
    lv_anim_set_exec_cb(&a, [](void *target, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(target), static_cast<lv_opa_t>(value), 0);
    });
    lv_anim_start(&a);
}

void renderLeds()
{
    for (int i = 0; i < 4; ++i) {
        const bool on = (gLeds & (1U << i)) != 0;
        lv_obj_set_style_bg_color(gLedDot[i], on ? lv_color_hex(0x43E89A) : lv_color_hex(0x52677A), 0);
        lv_obj_set_style_shadow_color(gLedDot[i], lv_color_hex(0x43E89A), 0);
        lv_obj_set_style_shadow_width(gLedDot[i], on ? 14 : 0, 0);
        lv_label_set_text(gLedLabel[i], LED_NAMES[i]);
        lv_obj_set_style_text_color(gLedLabel[i], on ? lv_color_hex(0xE9FFF5) : lv_color_hex(0xA8B7C8), 0);
        lv_obj_set_style_bg_color(gLedButton[i], on ? lv_color_hex(0x174B3E) : lv_color_hex(0x1A2B3C), 0);
    }
}

void setLeds(uint8_t mask)
{
    const uint8_t changed = gLeds ^ (mask & 0x0F);
    gLeds = mask & 0x0F;
    hal::pcfWriteLeds(gLeds);
    renderLeds();
    for (int i = 0; i < 4; ++i) if (changed & (1U << i)) animate(gLedDot[i]);
    hal::soundTone(880, 45);
}

void onLed(lv_event_t *e)
{
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    setLeds(gLeds ^ (1U << i));
}
void onAllOn(lv_event_t *) { setLeds(0x0F); }
void onAllOff(lv_event_t *) { setLeds(0x00); }

void poll(lv_timer_t *)
{
    const uint32_t now = millis();
    if (now - gPollAt < 60) return;
    gPollAt = now;
    uint8_t pressed = 0;
    const bool ok = hal::pcfReadSwitches(pressed);
    lv_label_set_text_fmt(gStatus, "I2C: %s   0x%02X   SDA %u / SCL %u",
                          ok ? "CONNECTED" : "DEVICE NOT FOUND", cyd::PCF8574_ADDRESS, cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
    lv_obj_set_style_text_color(gStatus, ok ? lv_color_hex(0x43E89A) : lv_color_hex(0xFF6878), 0);
    if (!ok) { gCandidateCount = 0; return; }
    if (pressed != gCandidate) { gCandidate = pressed; gCandidateCount = 1; return; }
    if (gCandidateCount < 2) ++gCandidateCount;
    if (gCandidateCount < 2 || pressed == gSwitches) return;
    const uint8_t changed = pressed ^ gSwitches;
    gSwitches = pressed;
    for (int i = 0; i < 4; ++i) {
        if (!(changed & (1U << i))) continue;
        const bool down = (pressed & (1U << i)) != 0;
        lv_label_set_text(gSwitchLabel[i], down ? "PRESSED" : "RELEASED");
        lv_obj_set_style_text_color(gSwitchLabel[i], down ? lv_color_hex(0xFFB454) : lv_color_hex(0x8195A8), 0);
        lv_obj_set_style_bg_color(gSwitchDot[i], down ? lv_color_hex(0xFFB454) : lv_color_hex(0x52677A), 0);
        lv_obj_set_style_bg_color(gSwitchTile[i], down ? lv_color_hex(0x3C3024) : lv_color_hex(0x1A2B3C), 0);
        animate(gSwitchDot[i]);
        hal::soundTone(down ? 1320 : 660, 35);
    }
}

lv_obj_t *indicator(lv_obj_t *parent)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}
}  // namespace

void ui_pcf_tab(lv_obj_t *tab)
{
    lv_obj_t *hero = ui_card(tab, "PCF8574  /  CONTROL CENTER");
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x102335), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x24435A), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    ui_label(hero, "I/O DASHBOARD", &font_th_24, lv_color_hex(0xEAF6FF));
    ui_label(hero, "4 outputs  |  4 inputs  |  Address 0x20", nullptr, lv_color_hex(0x9FB7C9));
    gStatus = ui_label(hero, "I2C: กำลังเชื่อมต่อ...", nullptr, lv_color_hex(0x43E89A));

    gLedCard = ui_card(tab, "01  OUTPUT  /  P0-P3");
    ui_label(gLedCard, "แตะการ์ดเพื่อสลับ LED  |  Isink LOW = ON", nullptr, lv_color_hex(0x91A9BC));
    lv_obj_t *row = ui_row(gLedCard);
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *b = lv_btn_create(row);
        gLedButton[i] = b;
        lv_obj_set_size(b, 136, 48);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1A2B3C), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x2D465B), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_radius(b, 14, 0);
        gLedLabel[i] = lv_label_create(b);
        lv_obj_align(gLedLabel[i], LV_ALIGN_CENTER, 8, 0);
        lv_obj_add_event_cb(b, onLed, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        gLedDot[i] = indicator(b);
        lv_obj_align(gLedDot[i], LV_ALIGN_LEFT_MID, 9, 0);
    }
    row = ui_row(gLedCard);
    lv_obj_t *allOn = ui_btn(row, "เปิดทั้งหมด", onAllOn);
    lv_obj_set_style_bg_color(allOn, lv_color_hex(0x147B5D), 0);
    lv_obj_t *allOff = ui_btn(row, "ปิดทั้งหมด", onAllOff);
    lv_obj_set_style_bg_color(allOff, lv_color_hex(0x35485A), 0);

    gSwitchCard = ui_card(tab, "02  INPUT  /  P4-P7");
    ui_label(gSwitchCard, "Pull-up 10k  |  Active Low", nullptr, lv_color_hex(0x91A9BC));
    row = ui_row(gSwitchCard);
    for (int i = 0; i < 4; ++i) {
        gSwitchTile[i] = lv_obj_create(row);
        lv_obj_remove_style_all(gSwitchTile[i]);
        lv_obj_set_size(gSwitchTile[i], 136, 58);
        lv_obj_set_style_bg_opa(gSwitchTile[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(gSwitchTile[i], lv_color_hex(0x1A2B3C), 0);
        lv_obj_set_style_radius(gSwitchTile[i], 12, 0);
        lv_obj_set_style_pad_hor(gSwitchTile[i], 8, 0);
        lv_obj_set_style_pad_row(gSwitchTile[i], 3, 0);
        lv_obj_set_flex_flow(gSwitchTile[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(gSwitchTile[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t *name = lv_label_create(gSwitchTile[i]);
        lv_obj_set_style_text_color(name, lv_color_hex(0xEAF6FF), 0);
        lv_label_set_text(name, SW_NAMES[i]);
        lv_obj_t *stateRow = lv_obj_create(gSwitchTile[i]);
        lv_obj_remove_style_all(stateRow);
        lv_obj_set_size(stateRow, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(stateRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(stateRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(stateRow, 7, 0);
        lv_obj_clear_flag(stateRow, LV_OBJ_FLAG_SCROLLABLE);
        gSwitchDot[i] = indicator(stateRow);
        gSwitchLabel[i] = lv_label_create(stateRow);
        lv_obj_set_style_text_font(gSwitchLabel[i], &font_th_16, 0);
        lv_obj_set_style_text_color(gSwitchLabel[i], lv_color_hex(0x8195A8), 0);
        lv_label_set_text(gSwitchLabel[i], "RELEASED");
    }
    ui_label(gSwitchCard, "แตะสวิตช์จริงเพื่อดูสถานะ  |  มีเสียงตอบรับเมื่อเปลี่ยน", nullptr, lv_color_hex(0x6E879B));
    renderLeds();
    poll(nullptr);
    lv_timer_create(poll, 60, nullptr);
}
