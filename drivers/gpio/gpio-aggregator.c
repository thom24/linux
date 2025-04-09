// SPDX-License-Identifier: GPL-2.0-only
//
// GPIO Aggregator
//
// Copyright (C) 2019-2020 Glider bv

#define DRV_NAME       "gpio-aggregator"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/forwarder.h>
#include <linux/gpio/machine.h>

#define AGGREGATOR_MAX_GPIOS 512

/*
 * GPIO Aggregator sysfs interface
 */

struct gpio_aggregator {
	struct gpiod_lookup_table *lookups;
	struct platform_device *pdev;
	char args[];
};

static DEFINE_MUTEX(gpio_aggregator_lock);	/* protects idr */
static DEFINE_IDR(gpio_aggregator_idr);

static int aggr_add_gpio(struct gpio_aggregator *aggr, const char *key,
			 int hwnum, unsigned int *n)
{
	struct gpiod_lookup_table *lookups;

	lookups = krealloc(aggr->lookups, struct_size(lookups, table, *n + 2),
			   GFP_KERNEL);
	if (!lookups)
		return -ENOMEM;

	lookups->table[*n] = GPIO_LOOKUP_IDX(key, hwnum, NULL, *n, 0);

	(*n)++;
	memset(&lookups->table[*n], 0, sizeof(lookups->table[*n]));

	aggr->lookups = lookups;
	return 0;
}

static int aggr_parse(struct gpio_aggregator *aggr)
{
	char *args = skip_spaces(aggr->args);
	char *name, *offsets, *p;
	unsigned int i, n = 0;
	int error = 0;

	unsigned long *bitmap __free(bitmap) =
			bitmap_alloc(AGGREGATOR_MAX_GPIOS, GFP_KERNEL);
	if (!bitmap)
		return -ENOMEM;

	args = next_arg(args, &name, &p);
	while (*args) {
		args = next_arg(args, &offsets, &p);

		p = get_options(offsets, 0, &error);
		if (error == 0 || *p) {
			/* Named GPIO line */
			error = aggr_add_gpio(aggr, name, U16_MAX, &n);
			if (error)
				return error;

			name = offsets;
			continue;
		}

		/* GPIO chip + offset(s) */
		error = bitmap_parselist(offsets, bitmap, AGGREGATOR_MAX_GPIOS);
		if (error) {
			pr_err("Cannot parse %s: %d\n", offsets, error);
			return error;
		}

		for_each_set_bit(i, bitmap, AGGREGATOR_MAX_GPIOS) {
			error = aggr_add_gpio(aggr, name, i, &n);
			if (error)
				return error;
		}

		args = next_arg(args, &name, &p);
	}

	if (!n) {
		pr_err("No GPIOs specified\n");
		return -EINVAL;
	}

	return 0;
}

static ssize_t new_device_store(struct device_driver *driver, const char *buf,
				size_t count)
{
	struct gpio_aggregator *aggr;
	struct platform_device *pdev;
	int res, id;

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	/* kernfs guarantees string termination, so count + 1 is safe */
	aggr = kzalloc(sizeof(*aggr) + count + 1, GFP_KERNEL);
	if (!aggr) {
		res = -ENOMEM;
		goto put_module;
	}

	memcpy(aggr->args, buf, count + 1);

	aggr->lookups = kzalloc(struct_size(aggr->lookups, table, 1),
				GFP_KERNEL);
	if (!aggr->lookups) {
		res = -ENOMEM;
		goto free_ga;
	}

	mutex_lock(&gpio_aggregator_lock);
	id = idr_alloc(&gpio_aggregator_idr, aggr, 0, 0, GFP_KERNEL);
	mutex_unlock(&gpio_aggregator_lock);

	if (id < 0) {
		res = id;
		goto free_table;
	}

	aggr->lookups->dev_id = kasprintf(GFP_KERNEL, "%s.%d", DRV_NAME, id);
	if (!aggr->lookups->dev_id) {
		res = -ENOMEM;
		goto remove_idr;
	}

	res = aggr_parse(aggr);
	if (res)
		goto free_dev_id;

	gpiod_add_lookup_table(aggr->lookups);

	pdev = platform_device_register_simple(DRV_NAME, id, NULL, 0);
	if (IS_ERR(pdev)) {
		res = PTR_ERR(pdev);
		goto remove_table;
	}

	aggr->pdev = pdev;
	module_put(THIS_MODULE);
	return count;

remove_table:
	gpiod_remove_lookup_table(aggr->lookups);
free_dev_id:
	kfree(aggr->lookups->dev_id);
remove_idr:
	mutex_lock(&gpio_aggregator_lock);
	idr_remove(&gpio_aggregator_idr, id);
	mutex_unlock(&gpio_aggregator_lock);
free_table:
	kfree(aggr->lookups);
free_ga:
	kfree(aggr);
put_module:
	module_put(THIS_MODULE);
	return res;
}

static DRIVER_ATTR_WO(new_device);

static void gpio_aggregator_free(struct gpio_aggregator *aggr)
{
	platform_device_unregister(aggr->pdev);
	gpiod_remove_lookup_table(aggr->lookups);
	kfree(aggr->lookups->dev_id);
	kfree(aggr->lookups);
	kfree(aggr);
}

static ssize_t delete_device_store(struct device_driver *driver,
				   const char *buf, size_t count)
{
	struct gpio_aggregator *aggr;
	unsigned int id;
	int error;

	if (!str_has_prefix(buf, DRV_NAME "."))
		return -EINVAL;

	error = kstrtouint(buf + strlen(DRV_NAME "."), 10, &id);
	if (error)
		return error;

	if (!try_module_get(THIS_MODULE))
		return -ENOENT;

	mutex_lock(&gpio_aggregator_lock);
	aggr = idr_remove(&gpio_aggregator_idr, id);
	mutex_unlock(&gpio_aggregator_lock);
	if (!aggr) {
		module_put(THIS_MODULE);
		return -ENOENT;
	}

	gpio_aggregator_free(aggr);
	module_put(THIS_MODULE);
	return count;
}
static DRIVER_ATTR_WO(delete_device);

static struct attribute *gpio_aggregator_attrs[] = {
	&driver_attr_new_device.attr,
	&driver_attr_delete_device.attr,
	NULL
};
ATTRIBUTE_GROUPS(gpio_aggregator);

static int __exit gpio_aggregator_idr_remove(int id, void *p, void *data)
{
	gpio_aggregator_free(p);
	return 0;
}

static void __exit gpio_aggregator_remove_all(void)
{
	mutex_lock(&gpio_aggregator_lock);
	idr_for_each(&gpio_aggregator_idr, gpio_aggregator_idr_remove, NULL);
	idr_destroy(&gpio_aggregator_idr);
	mutex_unlock(&gpio_aggregator_lock);
}


/*
 *  GPIO Forwarder
 */

#define fwd_tmp_values(fwd)	&(fwd)->tmp[0]
#define fwd_tmp_descs(fwd)	(void *)&(fwd)->tmp[BITS_TO_LONGS((fwd)->chip.ngpio)]

#define fwd_tmp_size(ngpios)	(BITS_TO_LONGS((ngpios)) + (ngpios))

int gpio_fwd_request(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return fwd->descs[offset] ? 0 : -ENODEV;
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_request, "GPIO_FORWARDER");

int gpio_fwd_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_get_direction(fwd->descs[offset]);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_get_direction, "GPIO_FORWARDER");

int gpio_fwd_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_direction_input(fwd->descs[offset]);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_direction_input, "GPIO_FORWARDER");

int gpio_fwd_direction_output(struct gpio_chip *chip,
			      unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_direction_output(fwd->descs[offset], value);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_direction_output, "GPIO_FORWARDER");

int gpio_fwd_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return chip->can_sleep ? gpiod_get_value_cansleep(fwd->descs[offset])
			       : gpiod_get_value(fwd->descs[offset]);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_get, "GPIO_FORWARDER");

static int gpio_fwd_get_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				 unsigned long *bits)
{
	struct gpio_desc **descs = fwd_tmp_descs(fwd);
	unsigned long *values = fwd_tmp_values(fwd);
	unsigned int i, j = 0;
	int error;

	bitmap_clear(values, 0, fwd->chip.ngpio);
	for_each_set_bit(i, mask, fwd->chip.ngpio)
		descs[j++] = fwd->descs[i];

	if (fwd->chip.can_sleep)
		error = gpiod_get_array_value_cansleep(j, descs, NULL, values);
	else
		error = gpiod_get_array_value(j, descs, NULL, values);
	if (error)
		return error;

	j = 0;
	for_each_set_bit(i, mask, fwd->chip.ngpio)
		__assign_bit(i, bits, test_bit(j++, values));

	return 0;
}

int gpio_fwd_get_multiple_locked(struct gpio_chip *chip, unsigned long *mask,
			  unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long flags;
	int error;

	if (chip->can_sleep) {
		mutex_lock(&fwd->mlock);
		error = gpio_fwd_get_multiple(fwd, mask, bits);
		mutex_unlock(&fwd->mlock);
	} else {
		spin_lock_irqsave(&fwd->slock, flags);
		error = gpio_fwd_get_multiple(fwd, mask, bits);
		spin_unlock_irqrestore(&fwd->slock, flags);
	}

	return error;
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_get_multiple_locked, "GPIO_FORWARDER");

static void gpio_fwd_delay(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	const struct gpiochip_fwd_timing *delay_timings;
	bool is_active_low = gpiod_is_active_low(fwd->descs[offset]);
	u32 delay_us;

	delay_timings = &fwd->delay_timings[offset];
	if ((!is_active_low && value) || (is_active_low && !value))
		delay_us = delay_timings->ramp_up_us;
	else
		delay_us = delay_timings->ramp_down_us;
	if (!delay_us)
		return;

	if (chip->can_sleep)
		fsleep(delay_us);
	else
		udelay(delay_us);
}

void gpio_fwd_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	if (chip->can_sleep)
		gpiod_set_value_cansleep(fwd->descs[offset], value);
	else
		gpiod_set_value(fwd->descs[offset], value);

	if (fwd->delay_timings)
		gpio_fwd_delay(chip, offset, value);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_set, "GPIO_FORWARDER");

static void gpio_fwd_set_multiple(struct gpiochip_fwd *fwd, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpio_desc **descs = fwd_tmp_descs(fwd);
	unsigned long *values = fwd_tmp_values(fwd);
	unsigned int i, j = 0;

	for_each_set_bit(i, mask, fwd->chip.ngpio) {
		__assign_bit(j, values, test_bit(i, bits));
		descs[j++] = fwd->descs[i];
	}

	if (fwd->chip.can_sleep)
		gpiod_set_array_value_cansleep(j, descs, NULL, values);
	else
		gpiod_set_array_value(j, descs, NULL, values);
}

void gpio_fwd_set_multiple_locked(struct gpio_chip *chip, unsigned long *mask,
				  unsigned long *bits)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	unsigned long flags;

	if (chip->can_sleep) {
		mutex_lock(&fwd->mlock);
		gpio_fwd_set_multiple(fwd, mask, bits);
		mutex_unlock(&fwd->mlock);
	} else {
		spin_lock_irqsave(&fwd->slock, flags);
		gpio_fwd_set_multiple(fwd, mask, bits);
		spin_unlock_irqrestore(&fwd->slock, flags);
	}
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_set_multiple_locked, "GPIO_FORWARDER");

int gpio_fwd_set_config(struct gpio_chip *chip, unsigned int offset,
			unsigned long config)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_set_config(fwd->descs[offset], config);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_set_config, "GPIO_FORWARDER");

int gpio_fwd_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);

	return gpiod_to_irq(fwd->descs[offset]);
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_to_irq, "GPIO_FORWARDER");

/*
 * The GPIO delay provides a way to configure platform specific delays
 * for the GPIO ramp-up or ramp-down delays. This can serve the following
 * purposes:
 *   - Open-drain output using an RC filter
 */
#define FWD_FEATURE_DELAY		BIT(0)

#ifdef CONFIG_OF_GPIO
static int gpio_fwd_delay_of_xlate(struct gpio_chip *chip,
				   const struct of_phandle_args *gpiospec,
				   u32 *flags)
{
	struct gpiochip_fwd *fwd = gpiochip_get_data(chip);
	struct gpiochip_fwd_timing *timings;
	u32 line;

	if (gpiospec->args_count != chip->of_gpio_n_cells)
		return -EINVAL;

	line = gpiospec->args[0];
	if (line >= chip->ngpio)
		return -EINVAL;

	timings = &fwd->delay_timings[line];
	timings->ramp_up_us = gpiospec->args[1];
	timings->ramp_down_us = gpiospec->args[2];

	return line;
}

static int gpio_fwd_setup_delay_line(struct gpiochip_fwd *fwd)
{
	struct gpio_chip *chip = &fwd->chip;

	fwd->delay_timings = devm_kcalloc(chip->parent, chip->ngpio,
					  sizeof(*fwd->delay_timings),
					  GFP_KERNEL);
	if (!fwd->delay_timings)
		return -ENOMEM;

	chip->of_xlate = gpio_fwd_delay_of_xlate;
	chip->of_gpio_n_cells = 3;

	return 0;
}
#else
static int gpio_fwd_setup_delay_line(struct gpiochip_fwd *fwd)
{
	return 0;
}
#endif	/* !CONFIG_OF_GPIO */

struct gpiochip_fwd *
devm_gpio_fwd_alloc(struct device *dev, unsigned int ngpios)
{
	const char *label = dev_name(dev);
	struct gpiochip_fwd *fwd;
	struct gpio_chip *chip;

	fwd = devm_kzalloc(dev, struct_size(fwd, tmp, fwd_tmp_size(ngpios)),
			   GFP_KERNEL);
	if (!fwd)
		return ERR_PTR(-ENOMEM);

	fwd->descs = devm_kcalloc(dev, ngpios, sizeof(*fwd->descs), GFP_KERNEL);
	if (!fwd->descs)
		return ERR_PTR(-ENOMEM);

	chip = &fwd->chip;

	chip->label = label;
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->request = gpio_fwd_request;
	chip->get_direction = gpio_fwd_get_direction;
	chip->direction_input = gpio_fwd_direction_input;
	chip->direction_output = gpio_fwd_direction_output;
	chip->get = gpio_fwd_get;
	chip->get_multiple = gpio_fwd_get_multiple_locked;
	chip->set = gpio_fwd_set;
	chip->set_multiple = gpio_fwd_set_multiple_locked;
	chip->to_irq = gpio_fwd_to_irq;
	chip->base = -1;
	chip->ngpio = ngpios;

	return fwd;
}
EXPORT_SYMBOL_NS_GPL(devm_gpio_fwd_alloc, "GPIO_FORWARDER");

int gpio_fwd_add_gpio_desc(struct gpiochip_fwd *fwd, struct gpio_desc *desc,
			   unsigned int offset)
{
	struct gpio_chip *chip = &fwd->chip;

	if (offset > chip->ngpio)
		return -EINVAL;

	if (fwd->descs[offset])
		return -EEXIST;

	/*
	 * If any of the GPIO lines are sleeping, then the entire forwarder
	 * will be sleeping.
	 */
	if (gpiod_cansleep(desc))
		chip->can_sleep = true;

	fwd->descs[offset] = desc;

	dev_dbg(chip->parent, "%u => gpio %d irq %d\n", offset,
		desc_to_gpio(desc), gpiod_to_irq(desc));

	return 0;
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_add_gpio_desc, "GPIO_FORWARDER");

int gpio_fwd_register(struct gpiochip_fwd *fwd)
{
	struct gpio_chip *chip = &fwd->chip;
	unsigned int ndescs = 0, i;
	int error;

	for (i = 0; i < chip->ngpio; i++)
		if (fwd->descs[i])
			ndescs++;

	/*
	 * Some gpio_desc were not registers. They will be registered at runtime
	 * but we have to suppose they can sleep.
	 */
	if (ndescs != chip->ngpio)
		chip->can_sleep = true;

	if (chip->can_sleep)
		mutex_init(&fwd->mlock);
	else
		spin_lock_init(&fwd->slock);

	error = devm_gpiochip_add_data(chip->parent, chip, fwd);

	return error;
}
EXPORT_SYMBOL_NS_GPL(gpio_fwd_register, "GPIO_FORWARDER");

/**
 * gpio_fwd_create() - Create a new GPIO forwarder
 * @dev: Parent device pointer
 * @ngpios: Number of GPIOs in the forwarder.
 * @descs: Array containing the GPIO descriptors to forward to.
 *         This array must contain @ngpios entries, and must not be deallocated
 *         before the forwarder has been destroyed again.
 * @features: Bitwise ORed features as defined with FWD_FEATURE_*.
 *
 * This function creates a new gpiochip, which forwards all GPIO operations to
 * the passed GPIO descriptors.
 *
 * Return: An opaque object pointer, or an ERR_PTR()-encoded negative error
 *         code on failure.
 */
static struct gpiochip_fwd *gpio_fwd_create(struct device *dev,
					    unsigned int ngpios,
					    struct gpio_desc *descs[],
					    unsigned long features)
{
	struct gpiochip_fwd *fwd;
	unsigned int i;
	int error;

	fwd = devm_gpio_fwd_alloc(dev, ngpios);
	if (!fwd)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ngpios; i++) {
		error = gpio_fwd_add_gpio_desc(fwd, descs[i], i);
		if (error)
			return ERR_PTR(error);
	}

	if (features & FWD_FEATURE_DELAY) {
		error = gpio_fwd_setup_delay_line(fwd);
		if (error)
			return ERR_PTR(error);
	}

	error = gpio_fwd_register(fwd);
	if (error)
		return ERR_PTR(error);

	return fwd;
}


/*
 *  GPIO Aggregator platform device
 */

static int gpio_aggregator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_desc **descs;
	struct gpiochip_fwd *fwd;
	unsigned long features;
	int i, n;

	n = gpiod_count(dev, NULL);
	if (n < 0)
		return n;

	descs = devm_kmalloc_array(dev, n, sizeof(*descs), GFP_KERNEL);
	if (!descs)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		descs[i] = devm_gpiod_get_index(dev, NULL, i, GPIOD_ASIS);
		if (IS_ERR(descs[i]))
			return PTR_ERR(descs[i]);
	}

	features = (uintptr_t)device_get_match_data(dev);
	fwd = gpio_fwd_create(dev, n, descs, features);
	if (IS_ERR(fwd))
		return PTR_ERR(fwd);

	platform_set_drvdata(pdev, fwd);
	return 0;
}

static const struct of_device_id gpio_aggregator_dt_ids[] = {
	{
		.compatible = "gpio-delay",
		.data = (void *)FWD_FEATURE_DELAY,
	},
	/*
	 * Add GPIO-operated devices controlled from userspace below,
	 * or use "driver_override" in sysfs.
	 */
	{}
};
MODULE_DEVICE_TABLE(of, gpio_aggregator_dt_ids);

static struct platform_driver gpio_aggregator_driver = {
	.probe = gpio_aggregator_probe,
	.driver = {
		.name = DRV_NAME,
		.groups = gpio_aggregator_groups,
		.of_match_table = gpio_aggregator_dt_ids,
	},
};

static int __init gpio_aggregator_init(void)
{
	return platform_driver_register(&gpio_aggregator_driver);
}
module_init(gpio_aggregator_init);

static void __exit gpio_aggregator_exit(void)
{
	gpio_aggregator_remove_all();
	platform_driver_unregister(&gpio_aggregator_driver);
}
module_exit(gpio_aggregator_exit);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("GPIO Aggregator");
MODULE_LICENSE("GPL v2");
