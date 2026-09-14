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
 
#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/gpio.h>

/* Bus hang flags */
extern uint8_t bus_stuck_flags;

/** 
 * Configure pin as open-drain with internal pull-up
 */
void i2c_init_pins(void);

/**
 * Bus clearance algorithm. (Bus Clear).
 */
void i2c_bus_clear(void);

/**
 * Protected transfer with timeouts.
 */
int stm32_i2c_transfer7_timeout(uint32_t i2c, uint8_t addr, uint8_t *w, size_t wn, uint8_t *r, size_t rn);

static void i2c_hard_reset(uint32_t i2c);

#endif
