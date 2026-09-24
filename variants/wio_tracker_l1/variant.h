/*
  Seeed Studio Wio Tracker L1 / L1 Pro — nRF52840 + Semtech SX1262 + Quectel
  L76K GNSS, for the Adafruit nRF52 Arduino core.

  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)
  Copyright (C) 2026, Arborisis

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Pin numbers are the nRF52840's own (port * 32 + pin): the digital pin
  map below is the identity, as in the RAK4630 variant of this tree, so
  that Boards.h names the same numbers the schematic does. The assignment
  is the one Meshtastic's seeed_wio_tracker_L1 variant carries, which is
  the vendor's:

      LoRa  SX1262   CS P1.14  RESET P1.07  BUSY P1.10  DIO1 P0.07
                     RXEN ("LORA_SW") P1.08, DIO2 drives the RF switch,
                     DIO3 feeds the TCXO at 1.8 V
            SPI      SCK P0.30  MOSI P0.28  MISO P0.03
      OLED  SSD1306  SDA P0.06  SCL P0.05  (I2C 0x3C, 128x64)
      GNSS  L76K     MCU RX P0.26  MCU TX P0.27  STANDBY P1.09 (low = sleep)
      LED            P1.01 (active high)      Buzzer  P1.00 (PWM)
      Button         P0.08 (to ground)
      Battery        VBAT/2 on P0.31 (AIN7), divider enabled by P0.04 high
      Grove I2C      SDA P0.00  SCL P0.01
      QSPI flash     P25Q16H, 2 MB
*/

#pragma once

#ifndef _VARIANT_WIO_TRACKER_L1_
#define _VARIANT_WIO_TRACKER_L1_

#define WIO_TRACKER_L1

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/
#include <Arduino.h>

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (8)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs — one user LED on P1.01; the core's "second LED" is pointed at
// the same pin so that nothing blinks the buzzer.
#define PIN_LED1 (33)
#define PIN_LED2 (33)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is lit

// Buttons
#define PIN_BUTTON1 (8)

// Buzzer
#define PIN_BUZZER (32)

/*
 * Analog pins
 */
#define PIN_A0 (2)
#define PIN_A1 (3)
#define PIN_A2 (4)
#define PIN_A3 (5)
#define PIN_A4 (28)
#define PIN_A5 (29)
#define PIN_A6 (30)
#define PIN_A7 (31)

	static const uint8_t A0 = PIN_A0;
	static const uint8_t A1 = PIN_A1;
	static const uint8_t A2 = PIN_A2;
	static const uint8_t A3 = PIN_A3;
	static const uint8_t A4 = PIN_A4;
	static const uint8_t A5 = PIN_A5;
	static const uint8_t A6 = PIN_A6;
	static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Battery: half of VBAT on P0.31, once P0.04 has enabled the divider.
#define PIN_VBAT (31)
#define PIN_VBAT_ENABLE (4)

// Other pins
#define PIN_AREF (0xff)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

	static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
// Serial1: the L76K GNSS receiver (9600 baud). RX is what the MCU reads.
#define PIN_SERIAL1_RX (26)
#define PIN_SERIAL1_TX (27)
#define PIN_GNSS_STANDBY (41)

/*
 * SPI Interfaces: the SX1262
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (3)
#define PIN_SPI_MOSI (28)
#define PIN_SPI_SCK (30)

	static const uint8_t SS = 46;
	static const uint8_t MOSI = PIN_SPI_MOSI;
	static const uint8_t MISO = PIN_SPI_MISO;
	static const uint8_t SCK = PIN_SPI_SCK;

/*
 * Wire Interfaces: the OLED, then the Grove connector
 */
#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA (6)
#define PIN_WIRE_SCL (5)

#define PIN_WIRE1_SDA (0)
#define PIN_WIRE1_SCL (1)

// QSPI Pins
#define PIN_QSPI_SCK 21
#define PIN_QSPI_CS 25
#define PIN_QSPI_IO0 20
#define PIN_QSPI_IO1 24
#define PIN_QSPI_IO2 22
#define PIN_QSPI_IO3 23

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
