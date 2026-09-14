/*
 * This file is part of the i2c-stm32-usb project.
 * 
 * Copyright (C) 2026 Anton Kalachev <akalachev@hotmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 */
 
#ifndef DESCRIPTORS_H
#define DESCRIPTORS_H

#include <stdint.h>
#include <stdbool.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/stm32/desig.h>

/* Silicon Labs identificators */
#define USB_VID 0x10C4
#define USB_PID 0xEA90

/**
 * Report IDs strictly per the HID-to-SMBus specification and the Linux driver.
 */
enum {
    CP2112_GPIO_CONFIG         = 0x02, // GPIO configuration (Feature)
    CP2112_GPIO_GET		       = 0x03, // Read Latch GPIO (Feature)
    CP2112_GPIO_SET            = 0x04, // Write Latch GPIO (Feature)
    CP2112_GET_VERSION_INFO    = 0x05, // Chip's version(Feature)
    CP2112_SMBUS_CONFIG        = 0x06, // Speed and timeouts (Feature)
    CP2112_DATA_READ_REQ       = 0x10, // Read request (Output)
    CP2112_DATA_WRITE_READ_REQ = 0x11, // Repeated Start (Output)
    CP2112_DATA_READ_FORCE     = 0x12, // Forced data reading(Output)
    CP2112_DATA_READ_REPLY     = 0x13, // Reply (Input)
    CP2112_DATA_WRITE_REQ      = 0x14, // Write request (Output)
    CP2112_TRANSFER_STATUS_REQ = 0x15, // Status request (Output)
    CP2112_TRANSFER_STATUS_RES = 0x16, // Transfer status response (Input)
    CP2112_CANCEL_TRANSFER     = 0x17, // Cancel transaction (Output)
    CP2112_LOCK_BYTE           = 0x20, // Lock configuration (Feature)
    CP2112_USB_CONFIG          = 0x21, // Parameters USB (Feature)
    CP2112_MANUFACTURER_STR    = 0x22, // Manufacture string (Feature)
    CP2112_PRODUCT_STR         = 0x23, // Product string (Feature)
    CP2112_SERIAL_STR          = 0x24, // Serial number (Feature)
};

/* State's codes of I2C-processor */
typedef enum {
    STATUS0_IDLE = 0,
    STATUS0_BUSY,
    STATUS0_COMPLETE,
    STATUS0_ERROR,
} bridge_state_t;

/**
 * Semapfore states of GPIO commands for asynchronious pipeline.
 */
typedef enum {
    GPIO_CMD_IDLE   = 0,
    GPIO_CMD_CONFIG = CP2112_GPIO_CONFIG, // Automatically binds to 0x02
    GPIO_CMD_SET    = CP2112_GPIO_SET     // Automatically binds to0x04
} gpio_cmd_t;

typedef struct {
    volatile uint8_t addr;
    volatile uint8_t tx_buf[64];
    volatile uint16_t tx_len;
    volatile uint8_t rx_buf[512];
    volatile uint16_t rx_target_len;
    volatile uint16_t rx_actual_len;
    volatile uint16_t rx_sent_len;
    volatile uint8_t state;     
    volatile bool pending;     
    volatile bool cancel_req;
    volatile bool force_send_pending; // Label: host faiting for data comming immediately
    volatile bool ready_to_report; // flag: I2C completed, time to send response in USB
    volatile bool ready_to_report_0x16;
    volatile gpio_cmd_t gpio_cmd_pending; 
} i2c_bridge_t;

/* Atomic Task Structure(Producer-Consumer buffer) */
typedef struct {
    uint8_t report_id;
    uint8_t addr;
    uint16_t tx_len;
    uint16_t rx_len;
    uint8_t tx_data[64];
    volatile bool updated; // New task flag
} i2c_job_t;

typedef struct {
    uint8_t report_id;   // 0x16
    uint8_t status0;     // Idle, Busy, Complete, Error
    uint8_t status1;     // Detailed error code (NACK и etc.)
    uint16_t retries;    // BE
    uint16_t length;     // BE (Bytes actually read)
} __attribute__((packed)) xfer_status_t;

/* SMBus Config structure according to Linux kernel version */
typedef struct {
    uint32_t clock_speed;      // 0-3: Hz (Big Endian)
    uint8_t  device_address;   // 4: Master's Slave Address (0x02)
    uint8_t  auto_send_read;   // 5: 1 = enabled, 0 = disabled
    uint16_t write_timeout;    // 6-7: ms (Big Endian)
    uint16_t read_timeout;     // 8-9: ms (Big Endian)
    uint8_t  scl_low_timeout;  // 10: 1 = enabled
    uint16_t retry_time;       // 11-12: # of retries (Big Endian)
} __attribute__((packed)) smbus_conf_t; // total 13 bytes data

/* External declarations of configuration arrays */
extern uint8_t gpio_conf_data[4];  // 4 bytes data from report 0x02
extern uint8_t gpio_latch_data[2]; // 2 bytes data from report 0x04
extern uint8_t usb_conf_data[9];
extern uint8_t lock_byte_value;
extern uint8_t shadow_gpio_config[4];

extern volatile smbus_conf_t curr_conf;

extern volatile xfer_status_t xfer_status;

/* string descriptors */
extern uint8_t manuf_str_data[63];
extern uint8_t product_str_data[63];
extern uint8_t serial_str_data[63];

/* externlal structures */
extern const struct usb_device_descriptor dev_descr;
extern const struct usb_config_descriptor config;
extern const uint8_t hid_report_desc[278]; 

extern const char *usb_strings[];
extern char serial_number[25]; 

void update_serial_number(void);

#endif /* DESCRIPTORS_H */
