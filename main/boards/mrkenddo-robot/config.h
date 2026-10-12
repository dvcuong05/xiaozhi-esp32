#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio configuration (adjust based on your hardware)
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Simplex I2S mode (separate mic and speaker pins)
#define AUDIO_I2S_METHOD_SIMPLEX

// Microphone I2S pins
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

// Speaker I2S pins
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// Button pins
#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_38
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

// L298N Stepper Motor pins
#define STEPPER_INT1_GPIO       GPIO_NUM_11
#define STEPPER_INT2_GPIO       GPIO_NUM_12
#define STEPPER_INT3_GPIO       GPIO_NUM_13
#define STEPPER_INT4_GPIO       GPIO_NUM_14

// Second OLED 128x32 I2C display pins
#define SECOND_DISPLAY_SDA_PIN  GPIO_NUM_2
#define SECOND_DISPLAY_SCL_PIN  GPIO_NUM_1
#define SECOND_DISPLAY_WIDTH    128
#define SECOND_DISPLAY_HEIGHT   32

// TFT ST7789 SPI pins (6 pins + GND)
#define DISPLAY_CS_PIN        GPIO_NUM_46  // Chip Select
#define DISPLAY_DC_PIN        GPIO_NUM_3   // Data/Command
#define DISPLAY_RST_PIN      GPIO_NUM_8   // Reset
#define DISPLAY_MOSI_PIN      GPIO_NUM_18  // MOSI/SDA
#define DISPLAY_CLK_PIN       GPIO_NUM_17  // SCL/SCLK
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_9   // Backlight control

// ST7789 display configuration
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0

#endif // _BOARD_CONFIG_H_
