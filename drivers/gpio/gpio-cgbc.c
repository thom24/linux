// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller GPIO driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/gpio/driver.h>
#include <linux/mfd/cgbc.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#define CGBC_GPIO_NGPIO	14

#define CGBC_GPIO_CMD_GET	0x64
#define CGBC_GPIO_CMD_SET	0x65
#define CGBC_GPIO_CMD_DIR_GET	0x66
#define CGBC_GPIO_CMD_DIR_SET	0x67

struct cgbc_gpio_data {
	struct gpio_chip	chip;
	struct cgbc_device_data	*cgbc;
	struct mutex lock;
};

static int cgbc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	u8 cmd[3], val;
	int ret;

	cmd[0] = CGBC_GPIO_CMD_GET;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	if (ret)
		return ret;

	if (offset > 7) {
		cmd[1] = 0x01;
		ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
		if (ret)
			return ret;
	}

	return (int)((val & (u8)BIT(offset % 8)) >> (offset % 8));
}

static void cgbc_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	u16 gpio_values = 0;
	u8 cmd[3], val;
	int ret;

	mutex_lock(&gpio->lock);

	cmd[0] = CGBC_GPIO_CMD_GET;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	if (ret)
		goto end;

	gpio_values = val;

	if (offset > 7) {
		cmd[1] = 0x01;
		ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
		if (ret)
			goto end;

		gpio_values |= (val << 8);
	}

	if (value)
		gpio_values |= BIT(offset);
	else
		gpio_values &= ~((u16) BIT(offset));

	cmd[0] = CGBC_GPIO_CMD_SET;
	cmd[1] = 0x00;
	cmd[2] = (u8) (gpio_values & 0xFF);
	cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);

	if (offset > 7) {
		cmd[1] = 0x01;
		cmd[2] = (u8) ((gpio_values >> 8) & 0xFF);
		cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	}

end:
	mutex_unlock(&gpio->lock);
}

static int cgbc_gpio_direction_set(struct gpio_chip *chip, unsigned offset, int direction)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	u16 gpio_values = 0;
	u8 cmd[3], val;
	int ret;

	mutex_lock(&gpio->lock);

	cmd[0] = CGBC_GPIO_CMD_DIR_GET;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	if (ret)
		goto end;

	gpio_values = val;

	if (offset > 7) {
		cmd[1] = 0x01;
		ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
		if (ret)
			goto end;

		gpio_values |= (val << 8);
	}

	if (direction == GPIO_LINE_DIRECTION_IN)
		gpio_values &= ~(BIT(offset));
	else
		gpio_values |= BIT(offset);

	cmd[0] = CGBC_GPIO_CMD_DIR_SET;
	cmd[1] = 0x00;
	cmd[2] = (u8) (gpio_values & 0xFF);

	ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	if (ret)
		goto end;

	if (offset > 7) {
		cmd[1] = 0x01;
		cmd[2] = (u8) ((gpio_values >> 8) & 0xFF);

		cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	}

end:
	mutex_unlock(&gpio->lock);

	return ret;
}

static int cgbc_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return cgbc_gpio_direction_set(chip, offset, GPIO_LINE_DIRECTION_IN);
}

static int cgbc_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	int ret;

	ret = cgbc_gpio_direction_set(chip, offset, GPIO_LINE_DIRECTION_OUT);
	if (!ret)
		cgbc_gpio_set(chip, offset, value);

	return ret;
}

static int cgbc_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct cgbc_gpio_data *gpio = gpiochip_get_data(chip);
	struct cgbc_device_data *cgbc = gpio->cgbc;
	u8 cmd[3], val;
	int ret;

	cmd[0] = CGBC_GPIO_CMD_DIR_GET;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
	if (ret)
		return ret;

	if (offset > 7) {
		cmd[1] = 0x01;
		ret = cgbc_command(cgbc, &cmd[0], sizeof(cmd), &val, 1, NULL);
		if (ret)
			return ret;
	}

	if ((val & BIT(offset % 8)) >> (offset % 8))
		return GPIO_LINE_DIRECTION_OUT;
	else
		return GPIO_LINE_DIRECTION_IN;
}

static int cgbc_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cgbc_device_data *cgbc = dev_get_drvdata(dev->parent);
	struct cgbc_gpio_data *gpio;
	struct gpio_chip *chip;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->cgbc = cgbc;

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = "gpio-cgbc";
	chip->owner = THIS_MODULE;
	chip->parent = dev;
	chip->base = -1;
	chip->direction_input = cgbc_gpio_direction_input;
	chip->direction_output = cgbc_gpio_direction_output;
	chip->get_direction = cgbc_gpio_get_direction;
	chip->get = cgbc_gpio_get;
	chip->set = cgbc_gpio_set;
	chip->ngpio = CGBC_GPIO_NGPIO;

	mutex_init(&gpio->lock);

	ret = devm_gpiochip_add_data(dev, chip, gpio);
	if (ret)
		return dev_err_probe(dev, ret, "Could not register GPIO chip\n");

	dev_info(dev, "GPIO functionality initialized with %d pins\n",
		 chip->ngpio);

	return 0;
}

static struct platform_driver cgbc_gpio_driver = {
	.driver = {
		.name = "cgbc-gpio",
	},
	.probe	= cgbc_gpio_probe,
};

module_platform_driver(cgbc_gpio_driver);

MODULE_DESCRIPTION("CGBC GPIO Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cgbc-gpio");
