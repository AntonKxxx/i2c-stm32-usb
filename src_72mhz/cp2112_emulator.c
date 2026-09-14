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
 
#include <string.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/stm32/usart.h>

#include "descriptors.h"
#include "init.h"
#include "i2c_utils.h"

/* Global variables */
usbd_device *usb_dev;
uint8_t usbd_control_buffer[320];
volatile i2c_bridge_t bridge;
static volatile i2c_job_t pending_job;
static xfer_status_t xfer_global; // public status for responses via 0x15

/* Static send data buffer (only for GET_REPORT / IN) */
static uint8_t hid_tx_buf[64] __attribute__((aligned(4)));

/* USB interrups handler */  
void usb_lp_can_rx0_isr(void) {
    if (usb_dev != NULL) {
        usbd_poll(usb_dev);
    }
}

/**
 * Reliable transmission of a packet via Interrupt IN (0x81).
 * Waits for the buffer to become free if it is busy.
 */
static void usb_write_interrupt(uint8_t *buf, uint16_t len) {
    for (;;) {
        bool sent = false;
        nvic_disable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
        /* Checking if a packet of the required length was successfully written. */
        if (usbd_ep_write_packet(usb_dev, 0x81, buf, len) == len) {
            sent = true;
        }
        nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);
        if (sent) break;
        /* If it hasn't been sent, the interrupt will trigger on the next loop iteration,
  		 * which will call poll and free up space in the PMA. 
		 */
    }
}

/**
 * Preparation of the bridge for a new transaction.
 * Called from an interrupt. Sets the status to BUSY so that the host 
 * waits for the main loop to finish processing.
 */
static void prepare_bridge_transfer(void) {
    bridge.rx_sent_len = 0;   // reset send-counter
    bridge.rx_actual_len = 0; // reset length of received data
    bridge.cancel_req = false;
    
    /* Atomic set host state BUSY */
    xfer_status.status0 = STATUS0_BUSY;
    xfer_status.status1 = 0x00;
    xfer_status.length = 0;
    
    /* Flag for main.c (declared as volatile) */
    bridge.pending = true;
}

/**
 * Unified subfunction for handling CP2112 reports.
 */
static void process_cp2112_report(uint8_t *buf, uint16_t len) {
    if (len < 1) return;
    uint8_t id = buf[0];

    switch (id) {
        /* 0x02: GPIO Configuration - Configuration of GPIO directions and operating modes */
        case CP2112_GPIO_CONFIG: 
            if (len >= 5) {
                /* Copy exactly 4 bytes of payload data, starting at offset 1 (skipping the ID) */
                memcpy(gpio_conf_data, &buf[1], 4);
				bridge.gpio_cmd_pending = CP2112_GPIO_CONFIG; // notice command type       
            }
            break;

        /* 0x04: GPIO Set Port Latch - Setting the levels on the GPIO outputs. */
        case CP2112_GPIO_SET: 
            if (len >= 3) {
                /* Copy exactly 2 bytes of payload data (Value, Mask), skipping the ID. */
                memcpy(gpio_latch_data, &buf[1], 2);
                bridge.gpio_cmd_pending = CP2112_GPIO_SET; // notice command type
            }
            break;

        /* 0x06: SMBus Configuration - speed I2C (Clock), timeout and retries */
        case CP2112_SMBUS_CONFIG: 
            if (len >= 14) {
                /* Explicit cast to (void *), since we are writing to a volatile structure. */
                memcpy((void *)&curr_conf, &buf[1], 13);
            }
            break;

        /* 0x10: Data Read Request from I2C-device */
        case CP2112_DATA_READ_REQ: 
            if (len >= 4) {
                /* If the bridge is busy, simply request that main return a BUSY status via 0x15, without interrupting operation. */
                if (bridge.pending) { 
                    break; 
                }
                bridge.addr = buf[1] >> 1;
                bridge.rx_target_len = (buf[2] << 8) | buf[3];
                bridge.tx_len = 0;
                prepare_bridge_transfer();
                led1_trigger(); 
            }
            break;

        /* 0x11: Data Write/Read Request - Write the register address and read (Repeated Start). */
        case CP2112_DATA_WRITE_READ_REQ: 
            /* Correction: len >= 2 to support SMBus Quick Command (empty request) */
            if (len >= 2) {
                /* Don't touch current task if bridgve is BUSY */
                if (bridge.pending) {
                    break; 
                }

                bridge.addr = buf[1] >> 1;

                /* Initialize transaction parameters */
                if (len >= 6) {
                    bridge.rx_target_len = (uint16_t)((buf[2] << 8) | buf[3]);
                    bridge.tx_len = buf[4];
                    
                    if (bridge.tx_len > 0) {
                        uint8_t sz = (bridge.tx_len > 16) ? 16 : (uint8_t)bridge.tx_len; 
                        memcpy((void *)bridge.tx_buf, &buf[5], sz);
                    }
                } else {
                    /* Support of Quick Command / Scanning */
                    bridge.rx_target_len = 0;
                    bridge.tx_len = 0;
                }

                /* Prepare bridge and activate execution in main.c */
                prepare_bridge_transfer();
                led2_trigger(); led1_trigger(); 
                bridge.pending = true; 
            }
            break;


        /* 0x12: Data Read Force Send - force request to send data to host */
        case CP2112_DATA_READ_FORCE: 
            bridge.force_send_pending = true;
            break;

        /* 0x14: Data Write Request (used for Quick Write / Scanning) */
        case CP2112_DATA_WRITE_REQ: 
            /* Correction: len >= 2 to support SMBus Quick Write (Scanning) */
            if (len >= 2) {
                if (bridge.pending) {
                    break;
                }

                bridge.addr = buf[1] >> 1;
                bridge.rx_target_len = 0;

                if (len >= 3) {
                    bridge.tx_len = buf[2];
                    uint8_t sz = (bridge.tx_len > 61) ? 61 : (uint8_t)bridge.tx_len;
                    memcpy((void *)bridge.tx_buf, &buf[3], sz);
                } else {
                    /* Quick Write for scanning */
                    bridge.tx_len = 0;
                }

                /* Prepare bridge and activate execution in main.c */
                prepare_bridge_transfer();
                led2_trigger(); 
                bridge.pending = true;
            }
            break;


        /* 0x15: Transfer Status Request - current transaction status request */
		case CP2112_TRANSFER_STATUS_REQ: // 0x15
    		/* Notice, that host requested status (response will be in Report 0x16 in main) */
    		bridge.ready_to_report_0x16 = true; 
    		break;


        /* 0x17: Cancel Transfer - command to cancel current transaction */
        case CP2112_CANCEL_TRANSFER: 
            if (len >= 1) bridge.cancel_req = true;
            break;

        /* 0x20: Lock Byte - byte to lock settings in NVRAM */
        case CP2112_LOCK_BYTE: 
            if (len >= 2) lock_byte_value = buf[1];
            break;   

        /* 0x21: USB Config - USB parameter settings USB (VID/PID/Power) */
        case CP2112_USB_CONFIG: 
            if (len >= 10) memcpy(usb_conf_data, &buf[1], 9);
            break;

        /* 0x22: Manufacturer String - write manufacturer's string */
        case CP2112_MANUFACTURER_STR:
            if (len >= 63) memcpy(manuf_str_data, &buf[1], 62);
            break;

        /* 0x23: Product String - write product string data */
        case CP2112_PRODUCT_STR:
            if (len >= 63) memcpy(product_str_data, &buf[1], 62);
            break;

        /* 0x24: Serial String - write serial number */
        case CP2112_SERIAL_STR:
            if (len >= 63) memcpy(serial_str_data, &buf[1], 62);
            break;

        default:
            break;
    }
}

/* For Interrupt OUT endpoint EP1 */
static void hid_out_cb(usbd_device *dev, uint8_t ep) {
    uint8_t tmp_buf[64];
    /* Read packet from endpoint */
    int len = usbd_ep_read_packet(dev, ep, tmp_buf, sizeof(tmp_buf));
    
    /* Process only if something received */
    if (len > 0) {
        process_cp2112_report(tmp_buf, (uint16_t)len);
    }
}

/**
 * Will bi called from library, when data SET_REPORT (OUT) completely placed in buffer.
 */
static void hid_set_report_complete(usbd_device *dev, struct usb_setup_data *req) {
    (void)dev;
    uint16_t len = req->wLength; // For gpioset length equals 3
    
    if (len > 0) {
        process_cp2112_report(usbd_control_buffer, len);
    }
}

/**
 * Dispatcher for control requests HID.
 */
static enum usbd_request_return_codes hid_control_dispatch(usbd_device *dev, 
    struct usb_setup_data *req, uint8_t **buf, uint16_t *len, 
    usbd_control_complete_callback *complete) 
{
    /* 1. Report descriptor (mandatory) */
    if (req->bRequest == USB_REQ_GET_DESCRIPTOR && req->wValue == (USB_HID_DT_REPORT << 8)) {
        *buf = (uint8_t *)hid_report_desc;
        *len = sizeof(hid_report_desc);
        return USBD_REQ_HANDLED;
    }

    /* 2. "Right" GET_REPORT */
    if (req->bRequest == USB_HID_REQ_TYPE_GET_REPORT) {
        uint8_t id = req->wValue & 0xFF;
        
        /* Use static buffer */
        uint8_t *data = hid_tx_buf;
        data[0] = id; 

        switch (id) {
            case CP2112_GET_VERSION_INFO: // 0x05
                data[1] = 0x0C; // Part Number (mandatory 0x0C for CP2112)
                data[2] = 0x02; // Device Version
                *len = 3; break;
            
            case CP2112_SMBUS_CONFIG: // 0x06
                 /* Cast to (void *), to avoid "volatile" warning */
                memcpy(&data[1], (void *)&curr_conf, 13);
                *len = 14; break;

            case CP2112_LOCK_BYTE: // 0x20
                data[1] = lock_byte_value;
                *len = 2; break;

            case CP2112_USB_CONFIG: // 0x21
                /* Return stored USB settings (9 bytes data + ID) */
                memcpy(&data[1], usb_conf_data, 9);
                *len = 10; break;

            case CP2112_GPIO_CONFIG: // 0x02
                /* Return stored GPIO settings(4 bytes data + ID) */
                /* Return ID (0x02) and 4 bytes settings (Dir, Mode, Special, clk_div) */
                memcpy(&data[1], shadow_gpio_config, 4);
                *len = 5; break;
            
            case CP2112_GPIO_GET: // 0x03
            {
				data[1] = (uint8_t)(GPIO_IDR(GPIOA) & 0xFF);
                *len = 2; break; 
            }          
        
            case CP2112_MANUFACTURER_STR: // 0x22
                memcpy(&data[1], manuf_str_data, 62);
                *len = 63; break;

            case CP2112_PRODUCT_STR: // 0x23
                memcpy(&data[1], product_str_data, 62);
                *len = 63; break;

            case CP2112_SERIAL_STR: // 0x24
                memcpy(&data[1], serial_str_data, 62);
                *len = 63; break;

            case CP2112_TRANSFER_STATUS_RES: // 0x16
                /* Correction: Simply copy current state from xfer_status, 
                   prepared in main. Don't touch flags here! */
                data[1] = xfer_status.status0;
                data[2] = xfer_status.status1;
                data[3] = (uint8_t)(xfer_status.retries >> 8);
                data[4] = (uint8_t)(xfer_status.retries & 0xFF);
                data[5] = (uint8_t)(bridge.rx_actual_len >> 8);
                data[6] = (uint8_t)(bridge.rx_actual_len & 0xFF);
                *len = 7;
                break;

            default:
                return USBD_REQ_NEXT_CALLBACK;
        }

        /* Point library onto filled buffer */
        *buf = data;
        return USBD_REQ_HANDLED;
    }

    /* 3. SET_REPORT as it was(via complete) */
    if (req->bRequest == USB_HID_REQ_TYPE_SET_REPORT) {
        /* Correction: If host sends 0x15 (Status Request) via Control Pipe, 
           only turn on flag. Response 0x16 will be processed in main in interrupt */
        uint8_t id = req->wValue & 0xFF;
        /* Status request 0x15 to be processed immediately*/
        if (id == CP2112_TRANSFER_STATUS_REQ) { // 0x15
             bridge.ready_to_report_0x16 = true;
             /* Short SET_REPORT on EP0 usually not requires data, 
                but library needs acknowledgement */
             return USBD_REQ_HANDLED;
        }

        /* 
         * Pipeline protection: if bridge processes I2C or previous GPIO,
         * return USBD_REQ_NOTSUPP. 
         * This forces the USB controller to automatically reply with NA.
         * Host Linux automatically waits 1 msec!
         */
        if (xfer_status.status0 != STATUS0_IDLE) {
            return USBD_REQ_NOTSUPP; 
        }
        
        /* If bridge is free — put data into buffer */
        if (req->wLength > 0) {
            *complete = hid_set_report_complete;
            return USBD_REQ_HANDLED; 
        }
    }

    return USBD_REQ_NEXT_CALLBACK;
}

/**
 * USB device enumeration.
 */
static void usb_set_config(usbd_device *dev, uint16_t wV) {
    (void)wV;
    /* Activate IN endpoint */
    usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 64, NULL);
    
    /* Activate OUT endpoint and set callback */
    usbd_ep_setup(dev, 0x01, USB_ENDPOINT_ATTR_INTERRUPT, 64, hid_out_cb);
    usbd_register_control_callback(dev, 0, 0, hid_control_dispatch);
}

/**
 * Send data to USB (Data Read Reply).
 * Will be called by receiving 0x12 (Force Send) from host.
 * According descripto packet should be 64 bytes long.
 */
void send_chunk(void) {
    uint8_t buf[64] = {0};
    buf[0] = CP2112_DATA_READ_REPLY; // 0x13
    buf[1] = STATUS0_COMPLETE;       // 0x02
    
    uint16_t rem = bridge.rx_actual_len - bridge.rx_sent_len;
    uint8_t chunk = (rem > 61) ? 61 : (uint8_t)rem;
    buf[2] = chunk;
    
    if (chunk > 0) {
        memcpy(&buf[3], (void *)&bridge.rx_buf[bridge.rx_sent_len], chunk);
    }

    usb_write_interrupt(buf, 64);
    bridge.rx_sent_len += chunk;

    /** 
	 * READ COMPLETION TRANSITION:
	 * As soon as the last byte of data has been transmitted over USB, 
	 * the bridge officially enters the IDLE state.
     */	
    if (bridge.rx_sent_len >= bridge.rx_actual_len) {
        xfer_status.status0 = STATUS0_IDLE;
        bridge.ready_to_report = false;
        bridge.rx_actual_len = 0;
    }
}

int main(void) {
    board_init(); 
    /* Hardware initialization of PA0-PA7 as outputs */
    cp2112_hardware_gpio_init();
    i2c_init_pins();
    
    update_serial_number();
    rcc_periph_clock_enable(RCC_USB);

    usb_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_descr, &config, 
                        usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
    usbd_register_set_config_callback(usb_dev, usb_set_config);
    
    nvic_set_priority(NVIC_USB_LP_CAN_RX0_IRQ, 0);
    nvic_enable_irq(NVIC_USB_LP_CAN_RX0_IRQ);

    /* Initial state */
    xfer_status.status0 = STATUS0_IDLE;

    while (1) {
        /* 1. Processing Cancel-command (0x17) */
        if (bridge.cancel_req) {
            xfer_status.status0 = STATUS0_IDLE;
            xfer_status.status1 = 0x00;
            bridge.pending = false;
            bridge.ready_to_report = false;
            bridge.ready_to_report_0x16 = false;
            bridge.force_send_pending = false;
            bridge.rx_actual_len = 0;
            bridge.cancel_req = false;
            /* You can add i2c_reset_low_level() if the bus hangs. */
        }

        /* 2. Execute I2C (only physics and fixing of result) */
        if (bridge.pending) {
            xfer_status.status0 = STATUS0_BUSY;

            int r = stm32_i2c_transfer7_timeout(I2C1, bridge.addr, 
                        (uint8_t *)bridge.tx_buf, bridge.tx_len, 
                        (uint8_t *)bridge.rx_buf, bridge.rx_target_len);
			
            if (r == 0) {
                bridge.rx_actual_len = bridge.rx_target_len;
                xfer_status.status0 = STATUS0_COMPLETE;
                xfer_status.status1 = 0x00;
            } else {
                bridge.rx_actual_len = 0;
                xfer_status.status0 = STATUS0_ERROR;
                xfer_status.status1 = 0x01; // NACK
            }

            bridge.rx_sent_len = 0;
            /* If read succeeded - wait for Force Send (0x12) */
            bridge.ready_to_report = (r == 0 && bridge.rx_actual_len > 0);
            
            bridge.pending = false; 
        }

        /* 3. Status-driven response (0x15) */
        if (bridge.ready_to_report_0x16) {
        	bridge.ready_to_report_0x16 = false;
            uint8_t r16[] = {0x16, xfer_status.status0, xfer_status.status1, 0, 0, 
                             (uint8_t)(bridge.rx_actual_len >> 8), (uint8_t)(bridge.rx_actual_len & 0xFF)};
            
            usb_write_interrupt(r16, 7);
  
            /* Transition following host notification: */
            if (xfer_status.status0 == STATUS0_ERROR) {
                /* error reported - bridge is free */
                xfer_status.status0 = STATUS0_IDLE;
            } 
            else if (xfer_status.status0 == STATUS0_COMPLETE && !bridge.ready_to_report) {
                /* Write succeeded - bridge is free */
                xfer_status.status0 = STATUS0_IDLE;
            }
            /* If read (ready_to_report == true), status stays COMPLETE, 
               till data not read via 0x13. */
        }

        /* 4. Data output (0x12) */
        if (bridge.ready_to_report && bridge.force_send_pending) {
        	bridge.force_send_pending = false;
            send_chunk(); 
        }

        /* Orphan data request flushing */
        if (bridge.force_send_pending && !bridge.ready_to_report) {
            bridge.force_send_pending = false;
        }
        
        /* 5. Lock-free autonomous GPIO command processing independent of I2C blocking */
        if (bridge.gpio_cmd_pending == CP2112_GPIO_CONFIG) {
            bridge.gpio_cmd_pending = 0; 
	        /* Passing two isolated data bytes by value */
            cp2112_hardware_gpio_apply_config(gpio_conf_data[0], gpio_conf_data[1]);
        }
        else if (bridge.gpio_cmd_pending == CP2112_GPIO_SET) {
            bridge.gpio_cmd_pending = 0; 
            cp2112_hardware_gpio_apply_latch(gpio_latch_data[0], gpio_latch_data[1]);
        }

    }
}

