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
#include "init.h"
#include "descriptors.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

void board_total_hardware_reset_to_analog(void)
{
    // Temporarily enable the clock to write the configuration to the CRL/CRH registers.
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOD);

    // PORT A: Set all to analog, except SWD debug (PA13, PA14)
    uint16_t safe_pins_a = GPIO_ALL & ~(GPIO13 | GPIO14);
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, safe_pins_a);

    // PORT B: Configure all 16 pins as analog
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO_ALL);

    // PORT C: Configure all 16 pins as analog (including unrouted hidden C0-C12)
    gpio_set_mode(GPIOC, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO_ALL);

    // PORT D: Configure all pins as analog, except for the future crystal oscillator pins (PD0, PD1)
    uint16_t safe_pins_d = GPIO_ALL & ~(GPIO0 | GPIO1);
    gpio_set_mode(GPIOD, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, safe_pins_d);

    // Completely power down the port logic by gating their clocks."
    rcc_periph_clock_disable(RCC_GPIOA);
    rcc_periph_clock_disable(RCC_GPIOB);
    rcc_periph_clock_disable(RCC_GPIOC);
    rcc_periph_clock_disable(RCC_GPIOD);
}

/**
 * Clock configuration (72 MHz for CPU, 48 MHz for USB)
 * Total execution time (success): ~235 ms
 */
static bool system_clock_setup_72mhz(void) {
    volatile uint32_t timeout;

    /* Phase 1: Coarse supply stabilization (~75 ms on HSI)*/
    for (timeout = 0; timeout < 100000; timeout++) __asm__("nop");

    /* Phase 2: Reset and switch to HSI */
    RCC_CR |= RCC_CR_HSION;
    while (!(RCC_CR & RCC_CR_HSIRDY));
    RCC_CFGR = 0;
    RCC_CR &= ~(RCC_CR_HSEON | RCC_CR_PLLON | RCC_CR_HSEBYP);

    /* Phase 3: Flash latency: 2 WS required for 72 MHz */
    FLASH_ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2WS;

    /* Phase 4: Start up HSE (8 MHz) */
    RCC_CR |= RCC_CR_HSEON;
    for (timeout = 0; timeout < 0xFFFF; timeout++) {
        if (RCC_CR & RCC_CR_HSERDY) break;
    }

    /* Phase 5: HSE Stabilization Time / Delay  */
    for (timeout = 0; timeout < 100000; timeout++) __asm__("nop");
    
    if (!(RCC_CR & RCC_CR_HSERDY)) return false;

    /* Phase 6: PLL configuration (HSE x9 = 72MHz, USBPRE=0 (DIV 1.5), APB1 /2) */
    uint32_t cfgr = (0x7 << 18) | (1 << 16) | (0x4 << 8); 
    RCC_CFGR = cfgr;
    
    RCC_CR |= RCC_CR_PLLON;
    for (timeout = 0; timeout < 0xFFFF; timeout++) {
        if (RCC_CR & RCC_CR_PLLRDY) break;
    }
 
    /* Phase 7: PLL guard time */
    for (timeout = 0; timeout < 100000; timeout++) __asm__("nop");

    if (!(RCC_CR & RCC_CR_PLLRDY)) return false;

    /* Phase 8: Switch SYSCLK to PLL */
    RCC_CFGR |= 0x02;
    while ((RCC_CFGR & (0x3 << 2)) != (0x2 << 2));
    
    /* Phase 9: Updating clock frequencies for libopencm3 */
    rcc_ahb_frequency  = 72000000;   
    rcc_apb1_frequency = 36000000;
    rcc_apb2_frequency = 72000000;

    return true;
}

/**
 * Hardware initialization of ports PA0-PA7 as inputs with pull-up resistors.
 * Protects fast GD32 silicon from EMI/noise and intermediate 0.8V levels during startup.
 */
void cp2112_hardware_gpio_init(void) {
    rcc_periph_clock_enable(RCC_GPIOA);
    
    /* GD32 fast core bus synchronization workaround */
    __asm__ volatile("nop");
    __asm__ volatile("nop");

    /* Enable internal pull-up to 3.3V. */
    gpio_set(GPIOA, GPIO0 | GPIO1 | GPIO2 | GPIO3 | GPIO4 | GPIO5 | GPIO6 | GPIO7);
    
    /* Configure strictly as INPUT PULL-UP according to Silicon Labs specification */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 
                  GPIO_CNF_INPUT_PULL_UPDOWN, 
                  GPIO0 | GPIO1 | GPIO2 | GPIO3 | GPIO4 | GPIO5 | GPIO6 | GPIO7);
}

/**
 * Reconfiguring PA0-PA7 pins
 */
void cp2112_hardware_gpio_apply_config(uint8_t direction, uint8_t mode) {
    uint16_t pins_as_input   = 0;
    uint16_t pins_out_od     = 0;
    uint16_t pins_out_pp     = 0;
    
    uint16_t dir_mask  = (uint16_t)direction;
    uint16_t mode_mask = (uint16_t)mode;

    /* Extracting from the shadow register the mask of pins that have already been configured as outputs */
    uint16_t already_outputs = (uint16_t)shadow_gpio_config[0];

    for (uint8_t i = 0; i < 8; i++) {
        uint16_t current_pin = (1U << i);

        if (dir_mask & current_pin) {
            if (mode_mask & current_pin) {
                pins_out_pp |= current_pin;
            } else {
                pins_out_od |= current_pin;
            }
        } else {
            pins_as_input |= current_pin;  
        }
    }

    /* 1. Open-Drain Output Configuration */
    if (pins_out_od != 0) {
        /*
           ORIGINAL BEHAVIOR: Clear ODR (disable input pull-up) 
		   ONLY for pins that were NOT previously configured as outputs. 
		   Former outputs retain their current ODR state!
	    */
        uint16_t new_od_outputs = pins_out_od & ~already_outputs;
        if (new_od_outputs != 0) {
            gpio_clear(GPIOA, new_od_outputs); 
        }

        gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, pins_out_od);
    }

    /* 2. Push-Pull Output Configuration */
    if (pins_out_pp != 0) {
        /* Similarly, reset only the new outputs to 0 */
        uint16_t new_pp_outputs = pins_out_pp & ~already_outputs;
        if (new_pp_outputs != 0) {
            gpio_clear(GPIOA, new_pp_outputs); 
        }

        gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, pins_out_pp);
    }

    /* 3. Set pin mode to input pull-up */
    if (pins_as_input != 0) {
        gpio_set(GPIOA, pins_as_input);
        gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, pins_as_input);
    }

    /* Saving configuration snapshot */
    shadow_gpio_config[0] = direction;
    shadow_gpio_config[1] = mode;
}

/**
 * @brief  Hardware-controlled state changes on physical pins of port A.
 * @note   Works universally with both open-drain and push-pull outputs.
 * 
 * @param  values  Logic levels byte (1 = High/Hi-Z, 0 = Low/GND)
 * @param  mask    Pins to be updated in current transaction (Linux)
 */
void cp2112_hardware_gpio_apply_latch(uint8_t values, uint8_t mask) {
    /* Explicitly cast 8-bit host masks to a 16-bit type to prevent GCC bugs */
    uint16_t values_16 = (uint16_t)values;
    uint16_t mask_16   = (uint16_t)mask;

    uint16_t pins_to_set   = 0;
    uint16_t pins_to_clear = 0;

    /* Bitwise parsing of the incoming configuration for the port's A lower 8 pins" */
    for (uint8_t i = 0; i < 8; i++) {
        uint16_t current_pin = (1U << i);

        /* If the host requires updating the state of this specific pin */
        if (mask_16 & current_pin) { 
            if (values_16 & current_pin) {
                /* logic 1: 
                 * — In open-drain mode: turns off the transistor, placing the pin into Hi-Z (the line is released).
                 * — In push-pull mode, it strongly drives the pin to 3.3V. 
                 */
                pins_to_set |= current_pin;   
            } else {
                /* logic 0: 
                 * — For both modes, it turns on the bottom transistor, pulling the physical layer down to GND (0 V).
                 */
                pins_to_clear |= current_pin; 
            }
        }
    }

    /* 
     * Physical atomic writing to MCU hardware registers via BSRR/BRR
     * completely eliminates race conditions and glitches on adjacent pins of GPIO Port A.
     */
    if (pins_to_set != 0) {
        gpio_set(GPIOA, pins_to_set);
    }
    
    if (pins_to_clear != 0) {
        gpio_clear(GPIOA, pins_to_clear);
    }
    
}


void led_timers_init(void) {
    /* Enable AFIO and GPIOB, remap before timer configuration */
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_GPIOB);
    gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP2);

    /* Enabling TIM3/TIM2 clock and executing a hardware reset */
    rcc_periph_clock_enable(RCC_TIM3);
    rcc_periph_reset_pulse(RST_TIM3);

    rcc_periph_clock_enable(RCC_TIM2);
    rcc_periph_reset_pulse(RST_TIM2);

    /* --- TIM3 (PB1, CH4) --- */
    /* Keep OC output disabled until configured (libopencm3 API) */
    timer_disable_oc_output(TIM3, TIM_OC4);

    timer_set_prescaler(TIM3, TIMER_PRESCALER);
    timer_set_period(TIM3, 0xFFFF);

    timer_set_oc_mode(TIM3, TIM_OC4, TIM_OCM_PWM2);
    timer_set_oc_polarity_low(TIM3, TIM_OC4);
    timer_set_oc_value(TIM3, TIM_OC4, 0xFFFF); /* Safe value */
    timer_disable_oc_preload(TIM3, TIM_OC4);

    timer_set_counter(TIM3, 0);
    timer_generate_event(TIM3, TIM_EGR_UG);

    timer_enable_oc_output(TIM3, TIM_OC4);
    /* Do not call timer_enable_counter if the counter starts automatically */

    /* --- TIM2 (PB10, CH3) --- */
    timer_disable_oc_output(TIM2, TIM_OC3);

    timer_set_prescaler(TIM2, TIMER_PRESCALER);
    timer_set_period(TIM2, 0xFFFF);

    timer_set_oc_mode(TIM2, TIM_OC3, TIM_OCM_PWM2);
    timer_set_oc_polarity_low(TIM2, TIM_OC3);
    timer_set_oc_value(TIM2, TIM_OC3, 0xFFFF); /* Safe value */
    timer_disable_oc_preload(TIM2, TIM_OC3);

    timer_set_counter(TIM2, 0);
    timer_generate_event(TIM2, TIM_EGR_UG);

    timer_enable_oc_output(TIM2, TIM_OC3);
    /* Do not call timer_enable_counter if the counter starts automatically */

    /* Set pins to AF open-drain */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN, GPIO1 | GPIO10);
}

/* Non-blocking toggle for LED1 (PB1) */
void led1_trigger(void) {
    /* Stop the timer */    
    timer_disable_counter(TIM3);
    
    /* Calculating the LED turn-on point for PWM comparison */
    uint32_t trigger_point = 0xFFFFU - LED_PULSE_TICKS;
    
    /* set CCR4 as a constant (the transition point where PWM drops to 0*/    
    timer_set_oc_value(TIM3, TIM_OC4, trigger_point);
    
    /* Initialize the timer with this difference — the count will start right from it!
 	   In PWM2 mode, when CNT >= CCR, the output instantly drops to 0 (the LED turns on).
	*/    
    timer_set_counter(TIM3, trigger_point);    
    
    /* Enable one-shot mode and start */
    timer_one_shot_mode(TIM3);
    timer_enable_counter(TIM3);
}

/* Non-blocking toggle for LED2 (PB10) */
void led2_trigger(void) {
    /* Stop the timer */ 
    timer_disable_counter(TIM2); 
    
    /* Calculating the LED turn-on point for PWM comparison */ 
    uint32_t trigger_point = 0xFFFFU - LED_PULSE_TICKS;
    
    /* Adjust the pulse duration */
    timer_set_oc_value(TIM2, TIM_OC3, trigger_point);
    
    /* Initialize the timer with this difference — the count will start right from it!
       In PWM2 mode, when CNT >= CCR, the output instantly drops to 0 (the LED turns on)
     */ 
    timer_set_counter(TIM2, trigger_point); 
    
    /* Enable one-shot mode and start */
    timer_one_shot_mode(TIM2);
    timer_enable_counter(TIM2);  
}

void board_init (void) {
	board_total_hardware_reset_to_analog();
    /* 1. Instantaneous host blinding */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOC);

    /* Pull down D+ and D- while we are still at 8MHz */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO11 | GPIO12);
    gpio_clear(GPIOA, GPIO11 | GPIO12);

    /* Enable the LED port */
    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

    /* 2. Safe overclocking */
    while (1) {
        gpio_clear(GPIOC, GPIO13); // LED On
        if (system_clock_setup_72mhz()) {
            break; // Success: LED stayes On
        } else {
            gpio_set(GPIOC, GPIO13); // Error
            for (volatile int j = 0; j < 400000; j++) __asm__("nop");
        }
    }

    /* 3. All systems preparation */
    /* Stability delay before enabling peripherals */
    for (volatile int j = 0; j < 2000000; j++) __asm__("nop");

	led_timers_init();
}

