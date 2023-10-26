/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 BayLibre, SAS */

#ifndef _AXI_SPI_ENGINE_OFFFLOAD_PWM_TRIGGER_H_
#define _AXI_SPI_ENGINE_OFFFLOAD_PWM_TRIGGER_H_

#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

struct iio_trigger
*devm_axi_spi_engine_offload_pwm_trigger_get_optional(struct device *dev);

int axi_spi_engine_offload_pwm_trigger_setup(struct iio_dev *indio_dev,
					     struct iio_trigger *trig);

/**
 * axi_spi_engine_offload_pwm_trigger_get_offload_id - Gets the offload ID.
 *
 * @trig: A trigger that was obtained via
 *        devm_axi_spi_engine_offload_pwm_trigger_get_optional().
 *
 * Return: The ID of the SPI offload associated with this trigger.
 */
static inline u32
axi_spi_engine_offload_pwm_trigger_get_offload_id(struct iio_trigger *trig)
{
	return trig->dev.parent->id;
}

#endif /* _AXI_SPI_ENGINE_OFFFLOAD_PWM_TRIGGER_H_ */
