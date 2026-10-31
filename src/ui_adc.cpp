#include "board_config.h"
#include "hal.h"
#include "ui.h"

namespace {
constexpr uint8_t CHANNELS = 4;
constexpr uint8_t HISTORY_POINTS = 24;
lv_obj_t *gStatus, *gRunButton, *gRunLabel;
lv_obj_t *gValueLabel[CHANNELS], *gArc[CHANNELS], *gChart[CHANNELS];
lv_chart_series_t *gSeries[CHANNELS];
bool gRunning = true;
uint32_t gLastSample[CHANNELS] = {0, 0, 0, 0};

lv_color_t channelColor(uint8_t i)
{
    static const uint32_t colors[CHANNELS] = {0x45D6C5, 0x62A8FF, 0xB58CFF, 0xFFB45E};
    return lv_color_hex(colors[i]);
}

void onRun(lv_event_t *)
{
    gRunning = !gRunning;
    lv_label_set_text(gRunLabel, gRunning ? "PAUSE" : "RESUME");
    lv_obj_set_style_bg_color(gRunButton, gRunning ? lv_color_hex(0x214A5A) : lv_color_hex(0x177B61), 0);
    lv_label_set_text(gStatus, gRunning ? "LIVE  |  กำลังอ่านทั้ง 4 ช่อง" : "HOLD  |  แตะ RESUME เพื่ออ่านต่อ");
    lv_obj_set_style_text_color(gStatus, gRunning ? lv_color_hex(0x45D6C5) : lv_color_hex(0xFFB45E), 0);
    hal::soundTone(gRunning ? 1046 : 523, 55);
}

void showOffline()
{
    lv_label_set_text_fmt(gStatus, "NO ADS1115  |  ตรวจ I2C 0x48-0x4B  |  GPIO27/22");
    lv_obj_set_style_text_color(gStatus, lv_color_hex(0xFF7183), 0);
}

void gaugeValue(void *arc, int32_t value)
{
    lv_arc_set_value(static_cast<lv_obj_t *>(arc), static_cast<int16_t>(value));
}

void animateGauge(lv_obj_t *arc, int32_t value)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, lv_arc_get_value(arc), value);
    lv_anim_set_time(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, gaugeValue);
    lv_anim_start(&a);
}

void updateChannel(uint8_t channel, int16_t raw)
{
    int32_t mv = static_cast<int32_t>(raw) * 4096L / 32768L;
    if (mv < 0) mv = 0;  // single-ended AINx-to-GND reading
    const int32_t shown = mv > cyd::ADS1115_DISPLAY_MAX_MV ? cyd::ADS1115_DISPLAY_MAX_MV : mv;
    lv_label_set_text_fmt(gValueLabel[channel], "%ld.%02ldV", static_cast<long>(mv / 1000), static_cast<long>((mv % 1000) / 10));
    animateGauge(gArc[channel], shown);
    lv_chart_set_next_value(gChart[channel], gSeries[channel], static_cast<lv_coord_t>(shown));
    lv_chart_refresh(gChart[channel]);
    lv_label_set_text_fmt(gStatus, "LIVE  |  ADS1115 0x%02X  |  A0-A3  |  SHARED SCAN", hal::adsAddress());
    lv_obj_set_style_text_color(gStatus, lv_color_hex(0x45D6C5), 0);
}

void tick(lv_timer_t *)
{
    if (!hal::adsOnline()) {
        showOffline();
        return;
    }
    if (!gRunning) return;
    for (uint8_t i = 0; i < CHANNELS; ++i) {
        const uint32_t samples = hal::adsSamples(i);
        if (samples == gLastSample[i]) continue;
        gLastSample[i] = samples;
        updateChannel(i, hal::adsRaw(i));
    }
}

lv_obj_t *makeGauge(lv_obj_t *parent, uint8_t i)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, 70, 70);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *arc = lv_arc_create(wrap);
    gArc[i] = arc;
    lv_obj_set_size(arc, 68, 68);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_range(arc, 0, cyd::ADS1115_DISPLAY_MAX_MV);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x24394B), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, channelColor(i), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *value = lv_label_create(wrap);
    gValueLabel[i] = value;
    lv_label_set_text(value, "--.--V");
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0xF1F7FC), 0);
    lv_obj_center(value);
    return wrap;
}

void makeChannelCard(lv_obj_t *parent, uint8_t i)
{
    lv_obj_t *card = ui_card(parent, nullptr);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x172A3B), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x10202F), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(card, lv_color_mix(channelColor(i), lv_color_hex(0x263F55), 48), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_pad_all(card, 9, 0);

    lv_obj_t *head = ui_row(card);
    lv_obj_t *heading = ui_label(head, "", &font_th_16, channelColor(i));
    lv_label_set_text_fmt(heading, "AIN%u  /  A%u", i, i);
    ui_label(head, "LIVE  /  24 SAMPLES", nullptr, lv_color_hex(0x7892A8));

    lv_obj_t *body = ui_row(card);
    lv_obj_t *chart = lv_chart_create(body);
    gChart[i] = chart;
    lv_obj_set_size(chart, 174, 76);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(chart, HISTORY_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, cyd::ADS1115_DISPLAY_MAX_MV);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0A1723), 0);
    lv_obj_set_style_bg_grad_color(chart, lv_color_hex(0x112536), 0);
    lv_obj_set_style_bg_grad_dir(chart, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 12, 0);
    lv_obj_set_style_pad_all(chart, 4, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x294055), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, channelColor(i), LV_PART_ITEMS);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_width(chart, 4, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 4, LV_PART_INDICATOR);
    lv_obj_set_style_radius(chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(chart, channelColor(i), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_INDICATOR);
    gSeries[i] = lv_chart_add_series(chart, channelColor(i), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, gSeries[i], 0);
    lv_obj_t *gauge = makeGauge(body, i);
    (void)gauge;
}
}  // namespace

void ui_adc_tab(lv_obj_t *tab)
{
    lv_obj_t *hero = ui_card(tab, "ADS1115  /  ANALOG STUDIO");
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x102335), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x28445B), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    ui_label(hero, "4-CHANNEL VOLTAGE MONITOR", &font_th_24, lv_color_hex(0xEAF6FF));
    ui_label(hero, "I2C  |  A0-A3  |  PGA +/-4.096 V  |  24-point history", nullptr, lv_color_hex(0x91A9BC));
    gStatus = ui_label(hero, "กำลังตรวจหา ADS1115...", nullptr, lv_color_hex(0x45D6C5));
    gRunButton = ui_btn(hero, "", onRun);
    gRunLabel = lv_obj_get_child(gRunButton, 0);
    lv_label_set_text(gRunLabel, "PAUSE");
    lv_obj_set_style_bg_color(gRunButton, lv_color_hex(0x214A5A), 0);

    ui_label(tab, "LIVE CHANNELS  /  0-3.30 V", &font_th_16, lv_color_hex(0xA9C5D8));
    for (uint8_t i = 0; i < CHANNELS; ++i) makeChannelCard(tab, i);
    if (!hal::adsOnline()) showOffline();
    lv_timer_create(tick, 20, nullptr);
}
