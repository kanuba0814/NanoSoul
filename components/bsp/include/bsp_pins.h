#pragma once
/*
 * NanoSoul board-fixed pin map — Waveshare ESP32-P4-WIFI6.
 *
 * These are the *on-board* resources of the dev board itself (TF card, board
 * I2C0, audio codec, MIPI-DSI display, MIPI-CSI camera). They are hard-wired on
 * the module and cannot be moved. Values verified against the working
 * /home/gxxl/testP4 bring-up and Waveshare wiki.
 *
 * The *external* (no-PCB prototype) pins for motors / encoders / INA219 / IMU /
 * BH1750 live in the drv_* modules and CLAUDE.md「固件」节 — do not duplicate
 * them here.
 */

#include "driver/gpio.h"

/* ---------- TF card: fixed pins, on-board 10k pullups, mounted as SDSPI ---------- */
#define BSP_SD_CLK      GPIO_NUM_43
#define BSP_SD_CMD      GPIO_NUM_44   /* = SPI MOSI */
#define BSP_SD_D0       GPIO_NUM_39   /* = SPI MISO */
#define BSP_SD_D1       GPIO_NUM_40
#define BSP_SD_D2       GPIO_NUM_41
#define BSP_SD_D3       GPIO_NUM_42   /* = SPI CS   */
#define BSP_SD_LDO_CHAN 4             /* LDO_VO4 powers the SD IO rail */

/* ---------- Board I2C0 (ES8311 + FT6x36 touch + OV5647 SCCB) ---------- */
#define BSP_I2C0_PORT   0
#define BSP_I2C0_SCL    GPIO_NUM_8
#define BSP_I2C0_SDA    GPIO_NUM_7
#define BSP_I2C0_FREQ   100000

/* ---------- ES8311 codec + NS4150B amp (I2S0) ---------- */
#define BSP_I2S_PORT    0
#define BSP_I2S_MCLK    GPIO_NUM_13
#define BSP_I2S_BCLK    GPIO_NUM_12
#define BSP_I2S_WS      GPIO_NUM_10
#define BSP_I2S_DOUT    GPIO_NUM_9    /* ESP TX -> codec DSDIN (playback) */
#define BSP_I2S_DIN     GPIO_NUM_11   /* codec ASDOUT -> ESP RX (record)  */
#define BSP_PA_CTRL     GPIO_NUM_53   /* NS4150B enable, active HIGH       */
#define BSP_ES8311_ADDR 0x18

/* ---------- MIPI-DSI display (WLK2802, ST7701S 2.8" 480x640 portrait) ---------- */
#define BSP_LCD_H_RES           480
#define BSP_LCD_V_RES           640
#define BSP_LCD_DSI_LANES       1
#define BSP_LCD_DSI_LANE_MBPS   700
#define BSP_LCD_DPI_CLK_MHZ     25

/* ---------- MIPI-CSI camera (OV5647, SCCB shares board I2C0) ---------- */
#define BSP_CAMERA_SCCB_ADDR    0x36

/* ---------- Shared MIPI DPHY LDO rail (DSI + CSI both use VO3 @2.5V) ---------- */
#define BSP_MIPI_DPHY_LDO_CHAN  3
#define BSP_MIPI_DPHY_LDO_MV    2500

/* ---------- Touch (FT6x36 on board I2C0) ---------- */
#define BSP_TOUCH_ADDR          0x38
