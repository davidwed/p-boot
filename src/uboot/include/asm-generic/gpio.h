/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011 The Chromium OS Authors.
 * Copyright (c) 2011, NVIDIA Corp. All rights reserved.
 */

#ifndef _ASM_GENERIC_GPIO_H_
#define _ASM_GENERIC_GPIO_H_

/*
 * Generic GPIO API for U-Boot
 *
 * --
 * NB: This is deprecated. Please use the driver model functions instead:
 *
 *    - gpio_request_by_name()
 *    - dm_gpio_get_value() etc.
 *
 * For now we need a dm_ prefix on some functions to avoid name collision.
 * --
 *
 * GPIOs are numbered from 0 to GPIO_COUNT-1 which value is defined
 * by the SOC/architecture.
 *
 * Each GPIO can be an input or output. If an input then its value can
 * be read as 0 or 1. If an output then its value can be set to 0 or 1.
 * If you try to write an input then the value is undefined. If you try
 * to read an output, barring something very unusual,  you will get
 * back the value of the output that you previously set.
 *
 * In some cases the operation may fail, for example if the GPIO number
 * is out of range, or the GPIO is not available because its pin is
 * being used by another function. In that case, functions may return
 * an error value of -1.
 */

/**
 * @deprecated	Please use driver model instead
 * Request a GPIO. This should be called before any of the other functions
 * are used on this GPIO.
 *
 * Note: With driver model, the label is allocated so there is no need for
 * the caller to preserve it.
 *
 * @param gpio	GPIO number
 * @param label	User label for this GPIO
 * @return 0 if ok, -1 on error
 */
int gpio_request(unsigned gpio, const char *label);

/**
 * @deprecated	Please use driver model instead
 * Stop using the GPIO.  This function should not alter pin configuration.
 *
 * @param gpio	GPIO number
 * @return 0 if ok, -1 on error
 */
int gpio_free(unsigned gpio);

/**
 * @deprecated	Please use driver model instead
 * Make a GPIO an input.
 *
 * @param gpio	GPIO number
 * @return 0 if ok, -1 on error
 */
int gpio_direction_input(unsigned gpio);

/**
 * @deprecated	Please use driver model instead
 * Make a GPIO an output, and set its value.
 *
 * @param gpio	GPIO number
 * @param value	GPIO value (0 for low or 1 for high)
 * @return 0 if ok, -1 on error
 */
int gpio_direction_output(unsigned gpio, int value);

/**
 * @deprecated	Please use driver model instead
 * Get a GPIO's value. This will work whether the GPIO is an input
 * or an output.
 *
 * @param gpio	GPIO number
 * @return 0 if low, 1 if high, -1 on error
 */
int gpio_get_value(unsigned gpio);

/**
 * @deprecated	Please use driver model instead
 * Set an output GPIO's value. The GPIO must already be an output or
 * this function may have no effect.
 *
 * @param gpio	GPIO number
 * @param value	GPIO value (0 for low or 1 for high)
 * @return 0 if ok, -1 on error
 */
int gpio_set_value(unsigned gpio, int value);

#endif	/* _ASM_GENERIC_GPIO_H_ */
