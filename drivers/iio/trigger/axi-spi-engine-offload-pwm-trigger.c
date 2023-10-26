// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for a PWM trigger and DMA buffer connected to a AXI SPI Engine offload.
 * http://analogdevicesinc.github.io/hdl/library/spi_engine/spi_engine_offload.html
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Copyright (C) 2023 BayLibre, SAS
 */

#include <linux/pwm.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/sysfs.h>

struct axi_offload_pwm_trigger {
	struct iio_buffer *buffer;
	struct pwm_device *pwm;
};

static const struct iio_trigger_ops axi_offload_pwm_trigger_ops;

static void axi_spi_engine_offload_pwm_trigger_release(void *data)
{
	struct iio_trigger *trig = data;

	iio_trigger_put(trig);
}

/**
 * devm_axi_spi_engine_offload_pwm_trigger_get_optional - try to get trigger
 *
 * @dev: A SPI peripheral device associated with the offload.
 *
 * Optionally gets a handle to an AXI SPI Engine Offload PWM Trigger.
 *
 * Return: A pointer to the trigger or NULL if not specified or negative error.
 */
struct iio_trigger
*devm_axi_spi_engine_offload_pwm_trigger_get_optional(struct device *dev)
{
	struct fwnode_handle *offload;
	struct device *offload_dev;
	struct iio_trigger *trig;
	int ret;

	/*
	 * This is the optional part, if the SPI peripheral doesn't have the
	 * adi,offloads property, then no trigger was specified.
	 */
	if (!device_property_present(dev, "adi,offloads"))
		return NULL;

	/* Traverse fwnodes and devices to get to the trigger. */

	offload = fwnode_find_reference(dev_fwnode(dev), "adi,offloads", 0);
	if (IS_ERR(offload))
		return ERR_CAST(offload);

	offload_dev = bus_find_device_by_fwnode(&platform_bus_type, offload);
	if (IS_ERR(offload_dev))
		return ERR_CAST(offload_dev);

	if (!offload_dev)
		return ERR_PTR(-EPROBE_DEFER);

	trig = iio_trigger_acquire_by_parent(offload_dev);
	if (!trig)
		return ERR_PTR(-EPROBE_DEFER);

	ret = devm_add_action_or_reset(dev,
			axi_spi_engine_offload_pwm_trigger_release, trig);
	if (ret)
		return ERR_PTR(ret);

	/* Ensure the found trigger was allocated by this driver. */
	if (trig->ops != &axi_offload_pwm_trigger_ops)
		return ERR_PTR(-EINVAL);

	return trig;
}
EXPORT_SYMBOL_NS_GPL(devm_axi_spi_engine_offload_pwm_trigger_get_optional,
		     IIO_SPI_ENGINE_OFFLOAD);

/**
 * axi_spi_engine_offload_pwm_trigger_setup - attaches the trigger and buffer
 *
 * @indio_dev:	The IIO device to attach the trigger and buffer to.
 * @trig:	The trigger to attach.
 *
 * The trigger must be an AXI SPI Engine Offload PWM Trigger, e.g. one acquired
 * by devm_axi_spi_engine_offload_pwm_trigger_get_optional().
 *
 * Return: 0 on success or negative error.
 */
int axi_spi_engine_offload_pwm_trigger_setup(struct iio_dev *indio_dev,
					     struct iio_trigger *trig)
{
	struct axi_offload_pwm_trigger *st = iio_trigger_get_drvdata(trig);

	/* Ensure trigger is of correct type, otherwise st is invalid. */
	if (trig->ops != &axi_offload_pwm_trigger_ops)
		return -EINVAL;

	indio_dev->modes |= INDIO_BUFFER_HARDWARE | INDIO_HARDWARE_TRIGGERED;
	indio_dev->trig = iio_trigger_get(trig);

	return iio_device_attach_buffer(indio_dev, st->buffer);
}
EXPORT_SYMBOL_NS_GPL(axi_spi_engine_offload_pwm_trigger_setup,
		     IIO_SPI_ENGINE_OFFLOAD);

static int axi_offload_pwm_trigger_set_state(struct iio_trigger *trig, bool state)
{
	struct axi_offload_pwm_trigger *st = iio_trigger_get_drvdata(trig);

	if (state)
		return pwm_enable(st->pwm);

	pwm_disable(st->pwm);

	return 0;
}

static int axi_offload_pwm_trigger_validate_device(struct iio_trigger *trig,
						   struct iio_dev *indio_dev)
{
	/* Don't allow assigning trigger via sysfs. */
	return -EINVAL;
}

static const struct iio_trigger_ops axi_offload_pwm_trigger_ops = {
	/*
	 * TODO: this callback is never called since this isn't an event trigger
	 * so it can probably be removed
	 */
	.set_trigger_state = axi_offload_pwm_trigger_set_state,
	.validate_device = axi_offload_pwm_trigger_validate_device,
};

static u32 axi_spi_engine_offload_pwm_trigger_get_rate(struct iio_trigger *trig)
{
	struct axi_offload_pwm_trigger *st = iio_trigger_get_drvdata(trig);
	u64 period_ns = pwm_get_period(st->pwm);

	if (period_ns)
		return DIV_ROUND_CLOSEST_ULL(NSEC_PER_SEC, period_ns);

	return 0;
}

static int
axi_spi_engine_offload_set_samp_freq(struct axi_offload_pwm_trigger *st,
				     u32 requested_hz)
{
	int period_ns;

	if (requested_hz == 0)
		return -EINVAL;

	period_ns = DIV_ROUND_UP(NSEC_PER_SEC, requested_hz);

	// FIXME: We really just need a clock, not a PWM. The current duty cycle
	// value is a hack to work around the edge vs. level offload trigger issue.
	return pwm_config(st->pwm, 10, period_ns);
}

static ssize_t sampling_frequency_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct iio_trigger *trig = to_iio_trigger(dev);

	return sysfs_emit(buf, "%u\n",
			  axi_spi_engine_offload_pwm_trigger_get_rate(trig));
}

static ssize_t sampling_frequency_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	struct axi_offload_pwm_trigger *st = iio_trigger_get_drvdata(trig);
	int ret;
	u32 val;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;

	ret = axi_spi_engine_offload_set_samp_freq(st, val);
	if (ret)
		return ret;

	return len;
}

static DEVICE_ATTR_RW(sampling_frequency);

static struct attribute *axi_offload_pwm_trigger_attrs[] = {
	&dev_attr_sampling_frequency.attr,
	NULL
};

ATTRIBUTE_GROUPS(axi_offload_pwm_trigger);

static void axi_offload_pwm_trigger_pwm_disable(void *data)
{
	struct pwm_device *pwm = data;

	pwm_disable(pwm);
}

static int axi_offload_pwm_trigger_probe(struct platform_device *pdev)
{
	struct axi_offload_pwm_trigger *st;
	struct iio_trigger *trig;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(st->pwm))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->pwm),
				     "failed to get PWM\n");

	st->buffer = devm_iio_dmaengine_buffer_alloc(&pdev->dev, "rx");
	if (IS_ERR(st->buffer))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->buffer),
				     "failed to allocate buffer\n");

	trig = devm_iio_trigger_alloc(&pdev->dev, "%s-%s-pwm-trigger",
			dev_name(pdev->dev.parent), dev_name(&pdev->dev));
	if (!trig)
		return -ENOMEM;

	trig->ops = &axi_offload_pwm_trigger_ops;
	trig->dev.parent = &pdev->dev;
	trig->dev.groups = axi_offload_pwm_trigger_groups;
	iio_trigger_set_drvdata(trig, st);

	ret = axi_spi_engine_offload_set_samp_freq(st, 1000);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to set sampling frequency\n");

	/*
	 * REVISIT: How to properly integrate enable into IIO so that it is
	 * only enabled when buffer is enabled? Technically, it probably doesn't
	 * hurt to leave it on all the time for now.
	 */
	ret = pwm_enable(st->pwm);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable PWM\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				axi_offload_pwm_trigger_pwm_disable, st->pwm);
	if (ret)
		return ret;

	return devm_iio_trigger_register(&pdev->dev, trig);
}

static const struct of_device_id axi_offload_pwm_trigger_match_table[] = {
	{ .compatible = "adi,axi-spi-engine-offload-pwm-trigger-dma-output" },
	{ }
};
MODULE_DEVICE_TABLE(of, axi_offload_pwm_trigger_match_table);

static struct platform_driver axi_offload_pwm_trigger_driver = {
	.probe = axi_offload_pwm_trigger_probe,
	.driver = {
		.name = "axi-spi-engine-offload-pwm-trigger",
		.of_match_table = axi_offload_pwm_trigger_match_table,
	},
};
module_platform_driver(axi_offload_pwm_trigger_driver);

MODULE_AUTHOR("David Lechner <dlechner@baylibre.com>");
MODULE_DESCRIPTION("AXI SPI Engine Offload PWM Trigger");
MODULE_LICENSE("GPL");
