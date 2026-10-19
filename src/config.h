#pragma once
#include <Arduino.h>

// ---------- ESP32-2432S028R "Cheap Yellow Display" pins ----------
// Display (ILI9341 on HSPI) is configured through TFT_eSPI build flags (platformio.ini).
#define PIN_BACKLIGHT 21

// XPT2046 touch: bit-banged so SD can own the VSPI bus
#define PIN_T_CLK  25
#define PIN_T_MOSI 32
#define PIN_T_MISO 39
#define PIN_T_CS   33
#define PIN_T_IRQ  36

// microSD (VSPI defaults)
#define PIN_SD_CS 5

// RGB LED on the back, active LOW
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

// light sensor (LDR), for auto brightness
#define PIN_LDR 34

// speaker: internal DAC1 = GPIO26 -> on-board amp (JST "SPEAK")

// ---------- the Den palette (from neco-ai openwebui/overlay/index.html) ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
#define C_BG    rgb(0x02, 0x06, 0x03)  // --ech0-bg
#define C_SCAN  rgb(0x08, 0x15, 0x0b)  // faint phosphor scanline
#define C_PANEL rgb(0x0e, 0x16, 0x0f)  // gray-900
#define C_LINE  rgb(0x46, 0x56, 0x3c)  // gray-600 borders
#define C_GREEN rgb(0x97, 0xf8, 0xa6)  // --ech0-green
#define C_HOT   rgb(0xd5, 0xff, 0xd9)  // --ech0-hot
#define C_DIM   rgb(0x5c, 0x94, 0x65)  // --ech0-dim
#define C_LIME  rgb(0x8c, 0xff, 0x5a)  // text accent
#define C_AMBER rgb(0xe8, 0xc5, 0x47)  // focus/outline amber
#define C_RUST  rgb(0x98, 0x37, 0x24)  // theme color / warnings

#define SCREEN_W 320
#define SCREEN_H 240
#define FW_VERSION "0.6.1"
