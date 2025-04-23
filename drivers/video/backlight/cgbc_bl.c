// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cgbc_bl - 
 *
 * Copyright (C) 2025 Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/backlight.h>
#include <linux/bitfield.h>
#include <linux/mfd/cgbc.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define CGBC_BL_CMD	0x75

#define CGBC_BL_BRIGHTNESS_MASK		GENMASK(6, 0)

#define CGBC_BL_MAX_BRIGHTNESS	100

struct cgbc_bl {
	struct cgbc_device_data *cgbc;
};

static int cgbc_bl_update_status(struct backlight_device *bl)
{
	struct cgbc_bl *cgbc_bl = bl_get_data(bl);
	u8 cmd[4] = { CGBC_BL_CMD };
	u8 data[3];
	int ret;

	int brightness = backlight_get_brightness(bl);

	ret = cgbc_command(cgbc_bl->cgbc, cmd, sizeof(cmd), data, sizeof(data), NULL);
	if (ret)
		return ret;

	cmd[1] = (data[0] & 0x80) | (FIELD_PREP(CGBC_BL_BRIGHTNESS_MASK, brightness));
	cmd[2] = data[1];
	cmd[3] = data[2];

	return cgbc_command(cgbc_bl->cgbc, cmd, sizeof(cmd), data, sizeof(data), NULL);
}

static int cgbc_bl_get_brightness(struct backlight_device *bl)
{
	struct cgbc_bl *cgbc_bl = bl_get_data(bl);
	u8 cmd[4] = { CGBC_BL_CMD };
	u8 data[3];
	int ret;

	ret = cgbc_command(cgbc_bl->cgbc, cmd, sizeof(cmd), data, sizeof(data), NULL);

	return ret ?: FIELD_GET(CGBC_BL_BRIGHTNESS_MASK, data[0]);
}

static const struct backlight_ops cgbc_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = cgbc_bl_get_brightness,
	.update_status	= cgbc_bl_update_status
};

static int cgbc_bl_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct backlight_device *bl_dev;
	struct backlight_properties props = { };
	struct cgbc_bl *cgbc_bl;

	cgbc_bl = devm_kzalloc(&pdev->dev, sizeof(*cgbc_bl), GFP_KERNEL);
	if (!cgbc_bl)
		return -ENOMEM;

	cgbc_bl->cgbc = cgbc;

	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = CGBC_BL_MAX_BRIGHTNESS;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	bl_dev = devm_backlight_device_register(&pdev->dev, pdev->name, &pdev->dev,
						cgbc_bl, &cgbc_bl_ops, &props);
	if (IS_ERR(bl_dev))
		return PTR_ERR(bl_dev);

	bl_dev->props.brightness = cgbc_bl_get_brightness(bl_dev);

	return 0;
}

static struct platform_driver cgbc_bl_driver = {
	.driver = {
		.name = "cgbc-bl",
	},
	.probe = cgbc_bl_probe,
};

module_platform_driver(cgbc_bl_driver);

MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_DESCRIPTION("");
MODULE_LICENSE("GPL");
