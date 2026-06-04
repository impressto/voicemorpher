#pragma once

// ST7789V 240x320 display on ESP32-S3
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// SPI pins
#define TFT_MISO -1
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  13
#define TFT_BL    8

// Fonts: built-in bitmap + smooth font support for future use
#define LOAD_GLCD
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
