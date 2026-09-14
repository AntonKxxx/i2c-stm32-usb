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

#include "descriptors.h"
#include <libopencm3/stm32/desig.h>

volatile xfer_status_t xfer_status = { .report_id = 0x16, .status0 = 0x00 };
volatile bool ready_to_report = false; 

/* 0x21: USB Config (9 bytes data)
   VID: 0x10C4 -> 0xC4, 0x10
   PID: 0xEA90 -> 0x90, 0xEA
*/

/* 0x06: SMBus Config (13 bytes data) */
volatile smbus_conf_t curr_conf = {
    .clock_speed = 0xA0860100, // 0-3: Hz (Big Endian 100000)
    .device_address = 0x02,    // 4: Slave Address
    .auto_send_read = 1,       // 5: Enabled
    .write_timeout = 0x0000,   // 6-7: ms (BE)
    .read_timeout = 0x0000,    // 8-9: ms (BE)
    .scl_low_timeout = 0,      // 10: Disabled
    .retry_time = 0x0000       // 11-12: BE
};

/* 0x21: USB Config (9 bytes data) */
uint8_t usb_conf_data[9] = {
    0xC4, 0x10, // 0-1: vid (Little Endian)
    0x90, 0xEA, // 2-3: pid (Little Endian)
    0x32,       // 4: max_power (0x32 * 2mA = 100mA) <-- Here now
    0x00,       // 5: power_mode (0x00 = bus powered)
    0x01,       // 6: release_major
    0x00,       // 7: release_minor
    0x00        // 8: mask (write flags, 0 on read)
};

/* 0x02: GPIO Config (4 bytes) - default: all inputs, Open-Drain */
uint8_t gpio_conf_data[4] = {0x00, 0x00, 0x00, 0x00};
/* Shadow configuration register report 0x02 (Direction, Mode, Special, ClkDiv) */
uint8_t shadow_gpio_config[4] = {0x00, 0x00, 0x00, 0x00};

/* 0x03: GPIO Latch (2 bytes) - Latch status (Default: High)  */
uint8_t gpio_latch_data[2] = {0xFF, 0x00};

/* 0x22: Manufacturer String (Silicon Laboratories) */
uint8_t manuf_str_data[63] = {
    0x2A, 0x03, 
    'S', 0, 'i', 0, 'l', 0, 'i', 0, 'c', 0, 'o', 0, 'n', 0, ' ', 0,
    'L', 0, 'a', 0, 'b', 0, 'o', 0, 'r', 0, 'a', 0, 't', 0, 'o', 0, 'r', 0, 'i', 0, 'e', 0, 's', 0
};

/* 63 bytes data. With report ID it will be 64 bytes — up limit USB packet */
uint8_t product_str_data[63] = {
    0x3E, 0x03, // Length of descriptor's structure (62 bytes)
    'C', 0, 'P', 0, '2', 0, '1', 0, '1', 0, '2', 0, ' ', 0,
    'H', 0, 'I', 0, 'D', 0, ' ', 0, 'U', 0, 'S', 0, 'B', 0, '-', 0, 't', 0, 'o', 0, '-', 0,
    'S', 0, 'M', 0, 'B', 0, 'u', 0, 's', 0, ' ', 0, 'B', 0, 'r', 0, 'i', 0, 'd', 0, 'g', 0, 'e', 0,
    0 
};

/* 0x24: Serial String (0001) */
uint8_t serial_str_data[63] = {
    0x0A, 0x03,
    '0', 0, '0', 0, '0', 0, '1', 0
};

uint8_t lock_byte_value = 0xFF; // Storage

// Buffer for 24 characters HEX + Null-terminator
char serial_number[25] = "000000000000000000000000";

/* Basic device descriptor */
const struct usb_device_descriptor dev_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0110,          // USB 1.10
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,       // device revision
    .bMaxPacketSize0 = 64,
    .bNumConfigurations = 1,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
};

/**
 * HID Report Descriptor.
 * Size: 7 (header) + 18 (reports) * 15 (each) + 1 (footer) = 278 bytes.
 * added all ID include 0x12 for preventing HID IO Failed error.
 */
const uint8_t hid_report_desc[] = {
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, // Vendor Page 0xFF00 (7 bytes)

    // 0x02: GPIO Config (4 bytes data)
    0x85, 0x02, 0x09, 0x02, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0xb1, 0x02,
    
    // 0x03: GPIO Get Port Latch (1 byte data)
    0x85, 0x03, 0x09, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x02,
    
    // 0x04: GPIO Set Port Latch (2 bytes data)
    0x85, 0x04, 0x09, 0x04, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0xb1, 0x02,
    
    // 0x05: Get Version (2 bytes data)
    0x85, 0x05, 0x09, 0x05, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0xb1, 0x02,
    
    // 0x06: SMBus Config (13 bytes data according Linux structure)
    0x85, 0x06, 0x09, 0x06, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x0d, 0xb1, 0x02,

    // 0x10: Data Read Req (3 bytes data - Output)
    0x85, 0x10, 0x09, 0x10, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x03, 0x91, 0x02,
    
    // 0x11: Write/Read Req (63  bytes data - Output)
    0x85, 0x11, 0x09, 0x11, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x02,
    
    // 0x12: Data Read Force (2  bytes data - Output) - for preventing IO FAILED error
    0x85, 0x12, 0x09, 0x12, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x02, 0x91, 0x02,

    // 0x13: Read Response (63 bytes data - Input)
    0x85, 0x13, 0x09, 0x13, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f, 0x81, 0x02,
    
    // 0x14: Data Write Req (63 bytes data - Output)
    0x85, 0x14, 0x09, 0x14, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f, 0x91, 0x02,
    
    // 0x15: Status Request (1 byte data - Output)
    0x85, 0x15, 0x09, 0x15, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
    
    // 0x16: Status Response (7 bytes data - Input)
    0x85, 0x16, 0x09, 0x16, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x07, 0x81, 0x02,
    
    // 0x17: Cancel Transfer (1 byte data - Output)
    0x85, 0x17, 0x09, 0x17, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,

    // 0x20: Lock Byte (1 byte data - Feature)
    0x85, 0x20, 0x09, 0x20, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x01, 0xb1, 0x02,
    
    // 0x21: USB Config (9 bytes data - Feature)
    0x85, 0x21, 0x09, 0x21, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x09, 0xb1, 0x02,
    
    // 0x22: Manufacturer String (62 bytes - Feature)
    0x85, 0x22, 0x09, 0x22, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3e, 0xb1, 0x02,
    
    // 0x23: Product String (62 bytes - Feature)
    0x85, 0x23, 0x09, 0x23, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3e, 0xb1, 0x02,
    
    // 0x24: Serial String (62 bytes - Feature)
    0x85, 0x24, 0x09, 0x24, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3e, 0xb1, 0x02,

    0xc0 // End Collection (1 byte)
};

static const struct {
    struct usb_hid_descriptor hid;
    struct { uint8_t bType; uint16_t wLen; } __attribute__((packed)) report;
} hid_spec = {
    .hid = {
        .bLength = sizeof(hid_spec),
        .bDescriptorType = USB_DT_HID,
        .bcdHID = 0x0101,
        .bNumDescriptors = 1,
    },
    .report = { .bType = USB_DT_REPORT, .wLen = sizeof(hid_report_desc) }
};

/* Interrupt endpoint description — there are now two of them */
const struct usb_endpoint_descriptor ep[] = {
    {
        /* Endpoint 1: Interrupt IN (for responses 0x13 and 0x16) */
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x81,
        .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize = 64,
        .bInterval = 1
    },
    {
        /* Endpoint 2: Interrupt OUT (for commands 0x10, 0x11, 0x14) */
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x01, // OUT direction
        .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize = 64,
        .bInterval = 1
    }
};

const struct usb_interface_descriptor iface = {
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,          // now two endpoints
    .bInterfaceClass = USB_CLASS_HID,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 0,
    .iInterface = 0,
    .extra = &hid_spec,
    .extralen = sizeof(hid_spec),
    .endpoint = ep
};

const struct usb_interface if_list[] = {{ .num_altsetting = 1, .altsetting = &iface }};

const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    /* 9 (Config) + 9 (Interface) + 9 (HID) + 7 (EP IN) + 7 (EP OUT) = 41 bytes (0x29) */
    .wTotalLength = 41, 
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0x80,
    .bMaxPower = 0x32, // 100 mA
    .interface = if_list
};


const char *usb_strings[] = {
    "Silicon Laboratories", 
    "CP2112 HID USB-to-SMBus Bridge", 
    serial_number,
};

/**
 * Generate unique serial number using chip's UID
 */
void update_serial_number(void) {
    desig_get_unique_id_as_string(serial_number, 25);
}
