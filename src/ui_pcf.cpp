#include "board_config.h"
#include "hal.h"
#include "ui.h"

namespace {
constexpr uint8_t COUNT = 4;
constexpr const char *LED_NAMES[COUNT] = {"LED 0", "LED 1", "LED 2", "LED 3"};
constexpr const char *SW_NAMES[COUNT] = {"SWITCH 0", "SWITCH 1", "SWITCH 2", "SWITCH 3"};
lv_obj_t *gStatus, *gLedTile[COUNT], *gLedDot[COUNT], *gLedLabel[COUNT];
lv_obj_t *gSwTile[COUNT], *gSwLabel[COUNT];
uint8_t gSwitches = 0xFF, gCandidate = 0, gCandidateCount = 0;

void animate(lv_obj_t *obj)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *target, int32_t value) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(target), static_cast<lv_opa_t>(value), 0);
    });
    lv_anim_start(&a);
}

lv_obj_t *tile(lv_obj_t *parent, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x172B3B), 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

void refresh(lv_timer_t *)
{
    static uint8_t prevLeds = 0xFF;
    const bool online = hal::pcfOnline();
    lv_label_set_text_fmt(gStatus, "PCF8574 0x20  |  %s  |  SDA %u / SCL %u",
                          online ? "ONLINE" : "DEVICE NOT FOUND", cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
    lv_obj_set_style_text_color(gStatus, online ? lv_color_hex(0x42E6A4) : lv_color_hex(0xFF7183), 0);

    const uint8_t leds = hal::pcfLedMask();
    for (uint8_t i = 0; i < COUNT; ++i) {
        const bool on = (leds & (1U << i)) != 0;
        lv_obj_set_style_bg_color(gLedDot[i], on ? lv_color_hex(0x42E6A4) : lv_color_hex(0x52677A), 0);
        lv_obj_set_style_bg_color(gLedTile[i], on ? lv_color_hex(0x174638) : lv_color_hex(0x172B3B), 0);
        lv_label_set_text(gLedLabel[i], on ? "OUTPUT ON" : "OUTPUT OFF");
        lv_obj_set_style_text_color(gLedLabel[i], on ? lv_color_hex(0xDFFFF2) : lv_color_hex(0x839DAF), 0);
        if ((prevLeds ^ leds) & (1U << i)) animate(gLedDot[i]);
    }
    prevLeds = leds;

    if (!hal::pcfSwitchValid()) return;
    const uint8_t sample = hal::pcfSwitchMask();
    if (sample != gCandidate) { gCandidate = sample; gCandidateCount = 1; return; }
    if (gCandidateCount < 2) ++gCandidateCount;
    if (gCandidateCount < 2 || sample == gSwitches) return;
    const uint8_t changed = sample ^ gSwitches;
    gSwitches = sample;
    for (uint8_t i = 0; i < COUNT; ++i) {
        if (!(changed & (1U << i))) continue;
        const bool down = (sample & (1U << i)) != 0;
        lv_label_set_text(gSwLabel[i], down ? "PRESSED" : "RELEASED");
        lv_obj_set_style_bg_color(gSwTile[i], down ? lv_color_hex(0x3C3024) : lv_color_hex(0x172B3B), 0);
        lv_obj_set_style_text_color(gSwLabel[i], down ? lv_color_hex(0xFFB454) : lv_color_hex(0x839DAF), 0);
        animate(gSwTile[i]);
    }
}
}  // namespace

void ui_pcf_tab(lv_obj_t *tab)
{
    lv_obj_t *hero = ui_card(tab, "PCF8574  /  I/O DIAGNOSTICS");
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x102B2B), 0);
    ui_label(hero, "RED BOARD  |  ADDRESS 0x20", &font_th_16, lv_color_hex(0xDDFCEF));
    gStatus = ui_label(hero, "กำลังอ่านสถานะ...", nullptr, lv_color_hex(0x42E6A4));

    lv_obj_t *section = ui_card(tab, "OUTPUT MONITOR  /  P0-P3");
    ui_label(section, "สถานะไฟควบคุมจากหน้า Smart Farm", nullptr, lv_color_hex(0x839DAF));
    lv_obj_t *row = ui_row(section);
    for (uint8_t i = 0; i < COUNT; ++i) {
        gLedTile[i] = tile(row, 136, 44);
        lv_obj_set_flex_flow(gLedTile[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(gLedTile[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        gLedDot[i] = tile(gLedTile[i], 16, 16);
        lv_obj_set_style_radius(gLedDot[i], LV_RADIUS_CIRCLE, 0);
        gLedLabel[i] = ui_label(gLedTile[i], "OUTPUT OFF", nullptr, lv_color_hex(0x839DAF));
        ui_label(gLedTile[i], LED_NAMES[i], nullptr, lv_color_hex(0xD7E5ED));
    }

    section = ui_card(tab, "INPUT MONITOR  /  P4-P7");
    ui_label(section, "Pull-up 10k  |  Active Low  |  กดแล้วเป็น LOW", nullptr, lv_color_hex(0x839DAF));
    row = ui_row(section);
    for (uint8_t i = 0; i < COUNT; ++i) {
        gSwTile[i] = tile(row, 136, 52);
        lv_obj_set_flex_flow(gSwTile[i], LV_FLEX_FLOW_COLUMN);
        ui_label(gSwTile[i], SW_NAMES[i], nullptr, lv_color_hex(0xD7E5ED));
        gSwLabel[i] = ui_label(gSwTile[i], "RELEASED", nullptr, lv_color_hex(0x839DAF));
    }
    refresh(nullptr);
    lv_timer_create(refresh, 80, nullptr);
}
