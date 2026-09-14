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
 
#ifndef INIT_H
#define INIT_H

#include <stdint.h>
#include <stddef.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/timer.h>
#include "i2c_utils.h"

/* --- System configuration --- */
#define CPU_FREQ_HZ         72000000U // Processor frequency (72 MHz)
#define TIMER_TICK_FREQ_HZ  10000U    // Desired timer tick frequency (10 kHz = 0.1 ms per tick)
#define LED_PULSE_MS        20U       // LED glow duration in milliseconds

/* Prescaler. Subtract 1 because the PSC hardware register adds 1 to the value */
#define TIMER_PRESCALER     ((CPU_FREQ_HZ / TIMER_TICK_FREQ_HZ) - 1U)

/* Tick count to achieve the target pulse duration */
#define LED_PULSE_TICKS     ((LED_PULSE_MS * TIMER_TICK_FREQ_HZ) / 1000U)

/* Clock configuration (72 MHz for CPU, 48 MHz for USB) */
static bool system_clock_setup_72mhz(void);

/* CPU environment setup */
void board_init (void);

/* Non-blocking trigger for LED1 (PB1) */
void led1_trigger(void);

/* Non-blocking trigger for LED2 (PB10) */
void led2_trigger(void);

/* Hardware initialization of PA0-PA7 for output. */
void cp2112_hardware_gpio_init(void);

/**
 * Hardware application of configuration (Input/Output)
 * Invoked reactively from the main execution loop.
 */
void cp2112_hardware_gpio_apply_config(uint8_t direction, uint8_t mode);

/**
 * Hardware-driven level toggling on physical pins (PA0-PA7).
 * Invoked reactively from the main execution loop.
 */
void cp2112_hardware_gpio_apply_latch(uint8_t values, uint8_t mask);

#endif
