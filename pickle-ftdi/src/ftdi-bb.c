/*
 * Copyright (C) 2005-2016 Darron Broad
 * Copyright (C) 2016 Gerhard Bertelsmann
 * All rights reserved.
 *
 * This file is part of Pickle Microchip PIC ICSP.
 *
 * Pickle Microchip PIC ICSP is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * Pickle Microchip PIC ICSP is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Pickle Microchip PIC ICSP. If not, see http://www.gnu.org/licenses/
 */

#include "pickle.h"

/*
 * Shadow output
 */
static uint8_t pin_latch = 0, pin_mask = 0xFF;

/*
 * FTDI handle
 */
static struct ftdi_context handle = {0};

/*
 * I/O buffer
 */
static uint8_t buffer[FTDI_BB_MAX_BITS_TRANSFER * 2];

/*
 * Configuration
 */
static uint8_t clock_pin, data_pin_input, data_pin_output, clock_falling, msb_first;

int
ftdi_bb_open(const char *usb_serial)
{
#ifdef __linux
	/* Initialize and find device */
	if (ftdi_init(&handle) < 0) {
		printf("%s: ftdi_init failed [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if (*usb_serial) {
		if ((ftdi_usb_open_desc(&handle, 0x0403, 0x6015, NULL, usb_serial) < 0) &&
			(ftdi_usb_open_desc(&handle, 0x0403, 0x6001, NULL, usb_serial) < 0)) {
			printf("%s: can't open FT232R/FT230X device [%s] with serial ID %s\n",
				__func__, ftdi_get_error_string(&handle), usb_serial);
			return -1;
		}
	} else {
		if ((ftdi_usb_open(&handle, 0x0403, 0x6015) < 0) &&
			(ftdi_usb_open(&handle, 0x0403, 0x6001) < 0)) {
			printf("%s: can't open FT230X device [%s]\n", __func__,
				ftdi_get_error_string(&handle));
			return -1;
		}
	}

	/* All input */
	pin_mask = 0;

	if (ftdi_set_bitmode(&handle, pin_mask, BITMODE_SYNCBB) < 0) {
		printf("%s: can't enable bitbang mode [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if (ftdi_set_baudrate(&handle, 65536) < 0) {
		printf("%s: can't set baudrate [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	return 1;
#else
	return -1;
#endif
}

void
ftdi_bb_close(void)
{
#ifdef __linux
	ftdi_disable_bitbang(&handle);
	ftdi_usb_reset(&handle);
	ftdi_usb_close(&handle);
#endif
}

int
ftdi_bb_configure(struct ftdi_bb_config *config)
{
#ifdef __linux
	clock_pin = config->clock_pin;
	clock_falling = config->clock_falling;
	data_pin_input = config->data_pin_input;
	data_pin_output = config->data_pin_output;
	msb_first = config->msb_first;

	return 1;
#else
	return -1
#endif
}

int
ftdi_bb_io(struct ftdi_bb_io *io)
{
#ifdef __linux
	uint8_t old_mask = pin_mask, pin_port;

	if (io->dir) {	/* In */
		pin_mask &= ~(1 << io->pin);
	} else {	/* Out */
		pin_mask |= (1 << io->pin);
		if (io->bit)	/* Set */
			pin_latch |= (1 << io->pin);
		else		/* Reset */
			pin_latch &= ~(1 << io->pin);
	}

	if (pin_mask != old_mask) {
		if (ftdi_set_bitmode(&handle, pin_mask, BITMODE_SYNCBB) < 0) {
			printf("%s: ftdi_set_bimode failed [%s]\n", __func__,
				ftdi_get_error_string(&handle));
			return -1;
		}
	}

	if (ftdi_write_data(&handle, &pin_latch, 1) < 0) {
		printf("%s: ftdi_write_error [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if (ftdi_read_data(&handle, &pin_port, 1) < 0) {
		printf("%s: ftdi_read_error [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if (io->dir) /* In */
		io->bit = (pin_port & (1 << io->pin)) != 0;

	return 1;
#endif
	return -1;
}

int
ftdi_bb_shift(struct ftdi_bb_shift *shift)
{
#ifdef __linux
	uint32_t index = 0;
	uint8_t old_mask, offset;
	uint64_t value, value_mask;

	if (data_pin_input == data_pin_output) {
		old_mask = pin_mask;
		if (shift->dir) /* In */
			pin_mask &= ~(1 << data_pin_input);
		else		/* Out */
			pin_mask |= (1 << data_pin_input);
		if (pin_mask != old_mask) {
			if (ftdi_set_bitmode(&handle, pin_mask, BITMODE_SYNCBB) < 0) {
				printf("%s: ftdi_set_bimode failed [%s]\n", __func__,
					ftdi_get_error_string(&handle));
				return -1;
			}
		}
	}

	bzero(buffer, FTDI_BB_MAX_BITS_TRANSFER * 2);
	value = shift->bits;

	value_mask = (msb_first) ? (1U << (shift->nbits - 1)) : (1 << 0);
	for (int i = 0; i < shift->nbits; ++i) {
		if (!shift->dir) {	/* Out */
			if (value & value_mask)	/* Set */
				pin_latch |= (1 << data_pin_output);
			else			/* Reset */
				pin_latch &= ~(1 << data_pin_output);
		}
		pin_latch |= (1 << clock_pin);
		buffer[index++] = pin_latch;
		pin_latch &= ~(1 << clock_pin);
		buffer[index++] = pin_latch;
		value_mask = (msb_first) ? (value_mask >> 1) : (value_mask << 1);
	}

	/* set data pin to low */
	pin_latch &= ~(1 << data_pin_output);
	buffer[index++] = pin_latch;
	buffer[index++] = pin_latch;

	if ((ftdi_write_data(&handle, buffer, index)) < 0) {
		printf("%s: ftdi_write_error [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if ((ftdi_read_data(&handle, buffer, index)) < 0) {
		printf("%s: ftdi_read_error [%s]\n", __func__,
			ftdi_get_error_string(&handle));
		return -1;
	}

	if (shift->dir) { /* In */
		value = 0;
		/* sometimes there is an offset of one scan cycle */
		/* this is true when the clock pin is read as one */
		offset = (buffer[1] & (1 << clock_pin)) ? 1 : 2;
		value_mask = (msb_first) ? (1U << (shift->nbits - 1)) : (1 << 0);
		for (int i = 0; i < shift->nbits; ++i) {
			if (buffer[i * 2 + offset] & (1 << data_pin_input))
				value |= value_mask;
			value_mask = (msb_first) ? (value_mask >> 1) : (value_mask << 1);
		}
		shift->bits = value;
	}
	return 1;
#else
	return -1;
#endif
}
