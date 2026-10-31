#include "hal.h"

#include <esp_system.h>
#include <Wire.h>

#include "app_config.h"
#include "board_config.h"

namespace hal {

namespace {
Preferences gPrefs;
esp_reset_reason_t gReset = ESP_RST_UNKNOWN;
bool gPcfOnline = false;
uint8_t gPcfLeds = 0;
bool gAdsOnline = false;
uint8_t gAdsAddress = cyd::ADS1115_ADDRESS;
uint8_t gPcfSwitchMask = 0;
bool gPcfSwitchValid = false;
uint32_t gPcfPollAt = 0;
int16_t gAdsRaw[4] = {0, 0, 0, 0};
uint32_t gAdsSampleCount[4] = {0, 0, 0, 0};
uint8_t gAdsChannel = 0;
bool gAdsPending = false;
uint32_t gAdsConversionAt = 0;
uint32_t gAdsReconnectAt = 0;
}  // namespace

Preferences &prefs()
{
    return gPrefs;
}

void begin()
{
    gReset = esp_reset_reason();
    gPrefs.begin(app::NVS_NAMESPACE, false);
    pinMode(cyd::BOOT_BUTTON_PIN, INPUT_PULLUP);
    Wire.begin(cyd::I2C_PIN_SDA, cyd::I2C_PIN_SCL);
    Wire.setClock(100000);
    pcfBegin();
    adsBegin();
}

bool pcfBegin()
{
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    // Quasi-bidirectional inputs require HIGH in the output latch. LEDs are
    // active-low current sinks, so an ON bit is written as LOW on P0-P3.
    const uint8_t port = static_cast<uint8_t>((~gPcfLeds & 0x0F) | 0xF0);
    Wire.write(port);
    gPcfOnline = Wire.endTransmission() == 0;
    return gPcfOnline;
}

bool pcfOnline() { return gPcfOnline; }
uint8_t pcfLedMask() { return gPcfLeds; }

bool pcfWriteLeds(uint8_t onMask)
{
    gPcfLeds = onMask & 0x0F;
    Wire.beginTransmission(cyd::PCF8574_ADDRESS);
    Wire.write(static_cast<uint8_t>((~gPcfLeds & 0x0F) | 0xF0));
    gPcfOnline = Wire.endTransmission() == 0;
    return gPcfOnline;
}

bool pcfReadSwitches(uint8_t &pressedMask)
{
    Wire.requestFrom(static_cast<uint8_t>(cyd::PCF8574_ADDRESS), static_cast<uint8_t>(1));
    if (!Wire.available()) { gPcfOnline = false; return false; }
    const uint8_t port = Wire.read();
    gPcfOnline = true;
    // Switches pull the pin LOW when pressed; normalize to 1 = pressed.
    pressedMask = static_cast<uint8_t>((~(port >> 4)) & 0x0F);
    return true;
}

void pcfPoll(uint32_t now)
{
    if (now - gPcfPollAt < 30) return;
    gPcfPollAt = now;
    gPcfSwitchValid = pcfReadSwitches(gPcfSwitchMask);
}

uint8_t pcfSwitchMask() { return gPcfSwitchMask; }
bool pcfSwitchValid() { return gPcfSwitchValid; }

bool adsBegin()
{
    gAdsOnline = false;
    // ADS1115 address follows ADDR wiring: GND, VDD, SDA, SCL -> 0x48..0x4B.
    for (uint8_t offset = 0; offset < 4; ++offset) {
        const uint8_t address = static_cast<uint8_t>(cyd::ADS1115_ADDRESS + offset);
        Wire.beginTransmission(address);
        Wire.write(0x01);  // configuration register pointer
        if (Wire.endTransmission(false) != 0) continue;
        if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) continue;
        while (Wire.available()) Wire.read();
        gAdsAddress = address;
        gAdsOnline = true;
        return true;
    }
    return false;
}

bool adsOnline() { return gAdsOnline; }
uint8_t adsAddress() { return gAdsAddress; }

bool adsStart(uint8_t channel)
{
    if (!gAdsOnline || channel > 3) return false;
    // OS=1, single-ended MUX AINx/GND, PGA +/-4.096 V, single-shot,
    // 860 SPS, comparator disabled. Keep each input within GND..VDD.
    const uint16_t config = static_cast<uint16_t>(0xC3E3 | (channel << 12));
    Wire.beginTransmission(gAdsAddress);
    Wire.write(0x01);
    Wire.write(static_cast<uint8_t>(config >> 8));
    Wire.write(static_cast<uint8_t>(config & 0xFF));
    gAdsOnline = Wire.endTransmission() == 0;
    return gAdsOnline;
}

bool adsRead(int16_t &raw)
{
    if (!gAdsOnline) return false;
    Wire.beginTransmission(gAdsAddress);
    Wire.write(0x00);  // conversion register
    if (Wire.endTransmission(false) != 0) { gAdsOnline = false; return false; }
    if (Wire.requestFrom(gAdsAddress, static_cast<uint8_t>(2)) != 2) { gAdsOnline = false; return false; }
    const uint16_t value = static_cast<uint16_t>(Wire.read()) << 8 | Wire.read();
    raw = static_cast<int16_t>(value);
    return true;
}

void adsPoll(uint32_t now)
{
    if (!gAdsOnline) {
        if (now - gAdsReconnectAt >= 2000) {
            gAdsReconnectAt = now;
            adsBegin();
        }
        return;
    }
    if (gAdsPending) {
        if (now - gAdsConversionAt < 4) return;
        int16_t raw = 0;
        if (!adsRead(raw)) { gAdsPending = false; return; }
        gAdsRaw[gAdsChannel] = raw;
        ++gAdsSampleCount[gAdsChannel];
        gAdsPending = false;
        gAdsChannel = static_cast<uint8_t>((gAdsChannel + 1) % 4);
    }
    if (adsStart(gAdsChannel)) {
        gAdsPending = true;
        gAdsConversionAt = now;
    }
}

int16_t adsRaw(uint8_t channel) { return channel < 4 ? gAdsRaw[channel] : 0; }
uint32_t adsSamples(uint8_t channel) { return channel < 4 ? gAdsSampleCount[channel] : 0; }

const char *resetReasonText()
{
    switch (gReset) {
        case ESP_RST_POWERON: return "POWER ON";
        case ESP_RST_EXT: return "EXT PIN";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC (crash)";
        case ESP_RST_INT_WDT: return "INT WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
        default: return "UNKNOWN";
    }
}

int ldrRaw()
{
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(cyd::LDR_PIN);
    return static_cast<int>(sum / 8);
}

}  // namespace hal
