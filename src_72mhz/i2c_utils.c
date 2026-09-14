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
 
/**
 * Delay: 
 * 125kHz at HK32F1  48Mhz 12 nop
 * 111kHz at HK32F1  48Mhz 14 nop
 * 110kHz at STM32F1 72MHz 15 nop 
 * 100kHz at HK32F1  48MHz 16 nop, 422mc for all
 * 107kHz at GD32F1  72MHz 20 nop
 * 102kHz at GD32F1  72Mhz 21 nop
 * 98kHz  at GD32F1  72Mhz 22 nop, 406ms for all
 * 87kHz  at GD32F1  72Mhz 22 nop with strethcing
 * 98kHz  at GD32F1  72Mhz 19 nop with strethcing
 */

#include "i2c_utils.h"

/* 
 * Setting the base timeout for clock stretching.
 * 7,000 iterations of ~5.1 µs each (the duration of a single i2c_delay)
 * provide a total timeout of around 35.7 milliseconds, 
 * which fully complies with the I2C / SMBus specifications for bus hang detection.
 */
#define I2C_STRETCH_TIMEOUT_TICKS  7000

/* 
 * Pin configuration: Open-Drain with internal Pull-up enabled.
 * PB6 — SCL (clock), PB7 — SDA (data)
 */
void i2c_init_pins(void) {
    // 1. Enable Port B clock
    rcc_periph_clock_enable(RCC_GPIOB);

    // 2. Configure PB6 (SCL) and PB7 (SDA) as Open-Drain Output, 50MHz
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ, 
                  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO6 | GPIO7);

    // 3. In STM32F1, writing "1" to the ODR in Open-Drain mode activates the internal pull-up resistor (if configured)
    gpio_set(GPIOB, GPIO6 | GPIO7);
}

/* 
 *  Your original baseline half-period delay.19 NOP iterations 
 *  at a 72 MHz core clock and FLASH_ACR_LATENCY_2WS 
 *  form a physical delay of ~5.1 µs (resulting in a bus frequency of ~98 kHz).
 */
static void i2c_delay(void) {
    for (volatile int i = 0; i < 19; i++) __asm__("nop"); 
}

// Macros for convenient and concise reading/writing of the SDA line
#define SDA_HIGH() gpio_set(GPIOB, GPIO7)
#define SDA_LOW()  gpio_clear(GPIOB, GPIO7)
#define SDA_GET()  gpio_get(GPIOB, GPIO7)

/* 
 * Setting SCL HIGH with a true Clock Stretching check
 * using a do-while loop. In normal operation, the delay is executed exactly once.
 * If the slave holds the line low, the master counts down the timeout in single i2c_delay() steps 
 * until SCL becomes 1.
 */
static bool i2c_scl_high(void) {
    gpio_set(GPIOB, GPIO6); // The master releases the SCL line to HIGH.
    
    volatile int32_t timeout = I2C_STRETCH_TIMEOUT_TICKS; 
    
    do {
        i2c_delay(); // Executed at least once (nominal half-period).
        
        if (--timeout <= 0) {
            return false; // Actual timeout is ~35 ms: the slave is frozen or the line is shorted.
        }
    } while (!gpio_get(GPIOB, GPIO6)); // Loop while the pin is physically low.
    
    return true; 
}

/* 
 * 2. Setting SCL to LOW with a mirrored do-while logic for the i2c_delay() call.
 *     This is required to achieve an ideal 50/50 duty cycle under normal conditions.
 *     By utilizing a volatile variable and an identical loop structure, 
 *     the compiler with -Os optimization will generate the exact same call 
 *     and check overhead as in the i2c_scl_high() function.
 */
static bool i2c_scl_low(void) {
    gpio_clear(GPIOB, GPIO6); // The master pulls SCL low.
    
    volatile int32_t timeout = I2C_STRETCH_TIMEOUT_TICKS; 
    
    do {
        i2c_delay(); // Executed exactly once to ensure a stable 50% duty cycle.
        
        if (--timeout <= 0) {
            return false; 
        }
    } while (gpio_get(GPIOB, GPIO6) == 1); // Using a deliberately false condition to force an immediate exit after the first iteration.
    
    return true; 
}

/* Generate START condition on the I2C bus */
static bool i2c_start(void) {
    SDA_HIGH(); 
    if (!i2c_scl_high()) return false; // SCL = HIGH + internal half-period delay
    
    SDA_LOW();      // SDA transition from high to low while SCL is high indicates a START condition
    i2c_delay();    // Hold SDA low for one full half-period before SCL goes low
    
    if (!i2c_scl_low()) return false;  // SCL = LOW + internal half-period delay
    return true;
}

/* Generate STOP condition on the I2C bus */
static bool i2c_stop(void) {
    SDA_LOW();  
    // Redundant delay removed: the i2c_scl_low() function has already provided the required delay in the previous step.
    
    if (!i2c_scl_high()) return false; // SCL = HIGH + internal half-period delay
    
    SDA_HIGH();     // DA rising while SCL is high (STOP condition)
    i2c_delay();    // Bus free time between a STOP and START condition
    return true;
}

/* Writes 1 byte to the bus. Returns true if the slave responded with ACK (pulled SDA low). */
static bool i2c_write_byte(uint8_t byte, bool *success) {
    for (int i = 0; i < 8; i++) {
        // Drive the MSB of data onto the SDA line
        if (byte & 0x80) SDA_HIGH(); else SDA_LOW();
        
        // Generating the SCL clock pulse
        if (!i2c_scl_high()) { *success = false; return false; }
        if (!i2c_scl_low())  { *success = false; return false; }
        
        byte <<= 1;
    }
    
    // ACK/NACK verification phase
    SDA_HIGH(); // Release SDA to allow the slave to pull it low (ACK) if desired.
    if (!i2c_scl_high()) { *success = false; return false; }
    
    bool ack = !SDA_GET(); // Reading SDA: a logic 0 indicates a successful ACK.
    
    if (!i2c_scl_low()) { *success = false; return false; }
    
    *success = true;
    return ack;
}

/* Reading 1 byte of data from the bus. The ack argument specifies whether to send an ACK to the slave device. */
static uint8_t i2c_read_byte(bool ack, bool *success) {
    uint8_t byte = 0;
    SDA_HIGH(); // Release the SDA line as an input to receive data bits.
    
    for (int i = 0; i < 8; i++) {
        if (!i2c_scl_high()) { *success = false; return 0; }
        if (SDA_GET()) byte |= (0x80 >> i); // Write the bit to the destination byte
        if (!i2c_scl_low())  { *success = false; return 0; }
    }
    
    // Forming ACK (SDA=0) or NACK (SDA=1) response for the slave
    if (ack) SDA_LOW(); else SDA_HIGH();
    
    if (!i2c_scl_high()) { *success = false; return 0; }
    if (!i2c_scl_low())  { *success = false; return 0; }
    
    SDA_HIGH(); // Returning SDA to the default HIGH state
    *success = true;
    return byte;
}

/* 
 * Universal I2C transaction function (argument-compatible with the standard libopencm3 signature).
 * It correctly handles sequential write and read operations using Repeated START.
 */
int stm32_i2c_transfer7_timeout(uint32_t i2c, uint8_t addr, uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    (void)i2c; // Not used, as communication is implemented via software (Bit-bang).
    
    gpio_set(GPIOB, GPIO6 | GPIO7); // Guaranteed initialization of lines to 1 on every function call
    bool success = true;

    // Processing Quick Command (used in i2cdetect -y utility for fast device search)
    if (wn == 0 && rn == 0) {
        if (!i2c_start()) return -1;
        bool ack = i2c_write_byte(addr << 1, &success);
        i2c_stop();
        return (success && ack) ? 0 : -1;
    }

    // 1. Data write stage
    if (wn > 0) {
        if (!i2c_start()) return -1;
        // Sending 7-bit address + write bit (0)
        if (!i2c_write_byte(addr << 1, &success) || !success) { 
            i2c_stop(); return -1; 
        }
        // Sequentially write a data array
        for (size_t i = 0; i < wn; i++) {
            if (!i2c_write_byte(w[i], &success) || !success) { 
                i2c_stop(); return -1; 
            }
        }
    }

    // 2. Data read stage
    if (rn > 0) {
        // Generates a standard START (if wn == 0) or a REPEATED START (if wn > 0) without releasing the bus.
        if (!i2c_start()) return -1; 
        // Sending a 7-bit address + read bit (1)
        if (!i2c_write_byte((addr << 1) | 1, &success) || !success) {
            i2c_stop(); return -1;
        }
        // Sequentially read a data array
        for (size_t i = 0; i < rn; i++) {
            // Send ACK for all bytes except the last one (send NACK for the last byte).
            r[i] = i2c_read_byte(i < (rn - 1), &success); 
            if (!success) { i2c_stop(); return -1; }
        }
    }

    i2c_stop(); // Terminating a successful communication session with a STOP signal
    return 0;   
}
