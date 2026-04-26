#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/i2s_std.h"
#include "driver/spi_common.h"

/*
 * P4-SoulDesk GPIO Map v0.2
 * Status: FROZEN_FOR_PROTOTYPE
 *
 * This file separates:
 *   1) fixed on-board resources from the Waveshare ESP32-P4-WIFI6 board
 *   2) frozen external expansion pins for prototype bring-up
 *
 * Hard boundaries for this freeze:
 *   - External sensors must use GPIO20/21, not the board silk-screened SCL/SDA.
 *   - Motor control assumes 2-input H-bridge drivers.
 *   - GPIO47/48 are only frozen while using the board TF card in default 4-bit SDMMC mode.
 *   - External I2C pull-ups must be verified on the assembled prototype.
 *
 *   Source: https://www.waveshare.com/wiki/ESP32-P4-WIFI6
 *           and Waveshare official demos 06_sdmmc / 07_I2SCodec
 */

/* ---------- Freeze metadata ---------- */
#define BSP_GPIO_MAP_NAME            "P4-SoulDesk GPIO Map v0.2"
#define BSP_GPIO_MAP_STATUS          "FROZEN_FOR_PROTOTYPE"

/* ---------- SDMMC Slot 0 (4-line, fixed pins, 10k external pullups on board) ---------- */
#define BSP_SD_CLK      GPIO_NUM_43
#define BSP_SD_CMD      GPIO_NUM_44
#define BSP_SD_D0       GPIO_NUM_39
#define BSP_SD_D1       GPIO_NUM_40
#define BSP_SD_D2       GPIO_NUM_41
#define BSP_SD_D3       GPIO_NUM_42

/* ---------- Board shared I2C (ES8311 + touch controller) ---------- */
#define BSP_I2C_PORT    I2C_NUM_0
#define BSP_I2C_SCL     GPIO_NUM_8
#define BSP_I2C_SDA     GPIO_NUM_7
#define BSP_I2C_FREQ_HZ 100000

/* ---------- ES8311 + NS4150B audio ---------- */
#define BSP_I2S_PORT    I2S_NUM_0
#define BSP_I2S_MCLK    GPIO_NUM_13
#define BSP_I2S_BCLK    GPIO_NUM_12
#define BSP_I2S_WS      GPIO_NUM_10
#define BSP_I2S_DOUT    GPIO_NUM_9    /* ESP TX  → codec DSDIN (playback) */
#define BSP_I2S_DIN     GPIO_NUM_11   /* codec ASDOUT → ESP RX (unused here) */

#define BSP_PA_CTRL     GPIO_NUM_53   /* NS4150B enable, active HIGH */

/* ---------- MIPI-DSI display (WLK2802MIPI-15P V2, ST7701S 2.8" 480x640) ---------- */
#define BSP_LCD_H_RES             480
#define BSP_LCD_V_RES             640
#define BSP_LCD_DSI_LANES         1     /* WLK2802 is a 1-lane module per Taobao spec */
#define BSP_LCD_DSI_LANE_MBPS     700   /* need ~530 Mbps for 480x640 RGB888@60Hz; keep headroom */
#define BSP_LCD_DPI_CLK_MHZ       25
#define BSP_LCD_RST_GPIO          GPIO_NUM_NC   /* RC reset on FPC */
#define BSP_LCD_BL_GPIO           GPIO_NUM_NC   /* backlight auto-on via on-board NPN */
#define BSP_MIPI_DPHY_LDO_CHAN    3
#define BSP_MIPI_DPHY_LDO_MV      2500

/* ---------- MIPI-CSI camera (OV5647, SCCB shares board I2C GPIO7/8) ---------- */
#define BSP_CAMERA_SCCB_ADDR      0x36
#define BSP_CAMERA_RESET_GPIO     GPIO_NUM_NC
#define BSP_CAMERA_PWDN_GPIO      GPIO_NUM_NC
#define BSP_MIPI_CSI_LDO_CHAN     3
#define BSP_MIPI_CSI_LDO_MV       2500

/* Capacitive touch (FT6x36 @ 0x38) — shares ES8311 I2C bus (SCL=8/SDA=7) */
#define BSP_TOUCH_RST_GPIO        GPIO_NUM_NC   /* RC reset on FPC */
#define BSP_TOUCH_INT_GPIO        GPIO_NUM_NC   /* not routed on this board */
#define BSP_TOUCH_SWAP_XY         0
#define BSP_TOUCH_MIRROR_X        1
#define BSP_TOUCH_MIRROR_Y        1

/* ---------- External sensor I2C bus (do not share the on-board GPIO7/8 bus) ---------- */
#define BSP_I2C_EXT_PORT          I2C_NUM_1
#define BSP_I2C_EXT_SCL           GPIO_NUM_20
#define BSP_I2C_EXT_SDA           GPIO_NUM_21
#define BSP_I2C_EXT_FREQ_HZ       400000

/* ---------- VL6180X ToF sensors ---------- */
#define BSP_VL6180X_L_XSHUT       GPIO_NUM_22
#define BSP_VL6180X_C_XSHUT       GPIO_NUM_23
#define BSP_VL6180X_R_XSHUT       GPIO_NUM_26
#define BSP_TOF_IRQ_RESERVED      GPIO_NUM_27

#define BSP_VL6180X_ADDR_DEFAULT  0x29
#define BSP_VL6180X_ADDR_L        0x30
#define BSP_VL6180X_ADDR_C        0x31
#define BSP_VL6180X_ADDR_R        0x32

/* ---------- BH1750 / GY-302 ---------- */
#define BSP_BH1750_ADDR           0x23

/* ---------- ICM-42688-P IMU (SPI host routed by GPIO matrix) ---------- */
#define BSP_IMU_SPI_HOST          SPI3_HOST
#define BSP_IMU_SPI_SCLK          GPIO_NUM_28
#define BSP_IMU_SPI_MOSI          GPIO_NUM_29
#define BSP_IMU_SPI_MISO          GPIO_NUM_30
#define BSP_IMU_SPI_CS            GPIO_NUM_31
#define BSP_IMU_INT1              GPIO_NUM_46

/* ---------- 3x N20 motor drivers (2-input H-bridge assumption) ---------- */
#define BSP_MOTOR1_IN1            GPIO_NUM_49
#define BSP_MOTOR1_IN2            GPIO_NUM_50
#define BSP_MOTOR2_IN1            GPIO_NUM_51
#define BSP_MOTOR2_IN2            GPIO_NUM_52
#define BSP_MOTOR3_IN1            GPIO_NUM_47
#define BSP_MOTOR3_IN2            GPIO_NUM_48

/* ---------- 3x quadrature encoders ---------- */
#define BSP_ENC1_A                GPIO_NUM_2
#define BSP_ENC1_B                GPIO_NUM_3
#define BSP_ENC2_A                GPIO_NUM_4
#define BSP_ENC2_B                GPIO_NUM_5
#define BSP_ENC3_A                GPIO_NUM_32
#define BSP_ENC3_B                GPIO_NUM_33

/*
 * Reserved / do-not-repurpose GPIO summary:
 *   GPIO7/8    : board I2C for ES8311 + touch
 *   GPIO9-13   : board audio I2S
 *   GPIO24/25  : USB D-/D+
 *   GPIO35-38  : boot strapping
 *   GPIO39-44  : board TF card 4-bit SDMMC
 *   GPIO53     : on-board PA enable
 *
 * On-chip LDO usage:
 *   LDO_VO3 : shared MIPI DPHY rail for DSI + CSI, 2.5V
 *   LDO_VO4 : SDMMC IO power
 */
