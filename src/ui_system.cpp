#include <esp_chip_info.h>

#include "app_config.h"
#include "hal.h"
#include "ui.h"

// =====================================================================
//  แท็บ "ระบบ": ข้อมูลบอร์ด/หน่วยความจำ/เซนเซอร์แสง และคำสั่ง Serial
// =====================================================================

namespace {

lv_obj_t *gLive;

void tick(lv_timer_t *)
{
    const uint32_t s = millis() / 1000;
    lv_label_set_text_fmt(gLive, "เปิดเครื่องมา %lu:%02lu:%02lu\nheap ว่าง %u KB (ต่ำสุด %u KB)\nแสง LDR ดิบ %d (ยิ่งสว่างยิ่งต่ำ)\nไฟหลังจอ %d%%   เสียง %s",
                          static_cast<unsigned long>(s / 3600), static_cast<unsigned long>((s / 60) % 60), static_cast<unsigned long>(s % 60),
                          static_cast<unsigned>(ESP.getFreeHeap() / 1024), static_cast<unsigned>(ESP.getMinFreeHeap() / 1024), hal::ldrRaw(),
                          hal::backlight(), hal::VOLUME_NAMES[hal::soundVolume()]);
}

}  // namespace

void ui_system_info_tab(lv_obj_t *tab)
{
    lv_obj_t *card = ui_card(tab, "บอร์ด");
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s %s\nชิป %s rev %d  %d คอร์  %lu MHz\nflash %u MB  โปรแกรม %u KB\nจอ %s 320x240  LVGL %d.%d.%d\nเริ่มทำงานเพราะ: %s",
             app::APP_NAME, app::FW_VERSION, ESP.getChipModel(), chip.revision, chip.cores, static_cast<unsigned long>(ESP.getCpuFreqMHz()),
             static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)), static_cast<unsigned>(ESP.getSketchSize() / 1024), hal::displayDriverName(),
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, hal::resetReasonText());
    ui_label(card, buf);

    card = ui_card(tab, "สถานะ");
    gLive = ui_label(card, "");

    card = ui_card(tab, "คำสั่ง Serial (115200)");
    ui_label(card,
             "h ช่วยเหลือ   i สถานะ\nc calibrate จอสัมผัส   k ค่า calibrate\nd ค่าจอ   r รีเซ็ตค่าจอ\nt พิมพ์ค่าดิบจอสัมผัส (เปิด/ปิด)\nl ทดสอบ LED   s ทดสอบเสียง",
             nullptr, lv_palette_lighten(LV_PALETTE_GREY, 2));
    ui_label(card, "จอเพี้ยนจนใช้ไม่ได้: กดปุ่ม BOOT\nค้าง 1 วินาที = รีเซ็ตค่าจอ", nullptr, lv_palette_main(LV_PALETTE_AMBER));

    lv_timer_create(tick, 1000, nullptr);
    tick(nullptr);
}

void ui_system_tab(lv_obj_t *tab)
{
    lv_obj_t *settings = lv_tabview_create(tab, LV_DIR_TOP, 30);
    lv_obj_set_size(settings, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(lv_tabview_get_content(settings), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *buttons = lv_tabview_get_tab_btns(settings);
    lv_obj_set_style_text_font(buttons, &font_th_16, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);

    struct Group { const char *name; void (*build)(lv_obj_t *); };
    const Group groups[] = {
        {"จอ/สัมผัส", [](lv_obj_t *p) { ui_display_tab(p); ui_touch_tab(p); }},
        {"RGB/เสียง", [](lv_obj_t *p) { ui_led_tab(p); ui_sound_tab(p); }},
        {"PCF/ADC", [](lv_obj_t *p) { ui_pcf_tab(p); ui_adc_tab(p); }},
        {"ข้อมูล", ui_system_info_tab},
    };
    for (const Group &g : groups) {
        lv_obj_t *page = lv_tabview_add_tab(settings, g.name);
        lv_obj_set_style_pad_all(page, 6, 0);
        lv_obj_set_style_pad_row(page, 6, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_ACTIVE);
        g.build(page);
    }
}
