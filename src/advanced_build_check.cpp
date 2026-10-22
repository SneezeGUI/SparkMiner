// Compile-time regression checks; no runtime task or network endpoint.
#ifdef ADVANCED_BUILD_CHECK
#include <board_config.h>
#include <TFT_eSPI.h>
#include <advanced_statistics.hpp>
#if defined(ILI9341_DRIVER) || defined(ILI9341_2_DRIVER)
#error "The ST7789 candidate must not select an ILI9341 driver"
#endif
static_assert(TFT_DRIVER == 0x7789, "Wrong TFT driver selected");
static_assert(TFT_WIDTH == 240 && TFT_HEIGHT == 320, "Native portrait dimensions required");
static_assert(TFT_SPI_MODE == SPI_MODE3, "ST7789 candidate uses SPI mode 3");
static_assert(SPI_FREQUENCY == 27000000, "Candidate clock must be explicit");
#ifdef ADVANCED_SOFTWARE_RESET_CHECK
static_assert(TFT_RST == -1, "Software-reset candidate must not drive a reset GPIO");
#else
static_assert(TFT_RST == 12, "Legacy candidate reset assumption changed");
#endif
static_assert(TFT_MOSI == 13 && TFT_SCLK == 14 && TFT_CS == 15 && TFT_DC == 2 && TFT_BL == 21,
              "Candidate pin assumptions changed: review the physical board");
#endif
