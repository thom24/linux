// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cgbc-hwmon - Congatec Board Controller hardware monitoring driver 
 *
 * Copyright (C) 2024 Thomas Richard <thomas.richard@bootlin.com>
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>

#include <linux/mfd/cgbc.h>

#define CGBC_HWMON_MAX_SENSORS	48

#define CGBC_HWMON_TYPE_TEMP	1
#define CGBC_HWMON_TYPE_IN	2
#define CGBC_HWMON_TYPE_FAN	3

#define CGBC_HWMON_CMD_SENSOR	0x77

struct cgbc_hwmon_sensor {
	enum hwmon_sensor_types type;
	bool active;
	unsigned int index;
	const unsigned char *label;
};

struct cgbc_hwmon_data {
	struct device *hwmon_dev;
	struct cgbc_device_data *cgbc;
	unsigned char nb_sensors;
	struct cgbc_hwmon_sensor sensors[CGBC_HWMON_MAX_SENSORS];
};

static const unsigned char * cgbc_hwmon_labels_temp[] = {"CPU", "BOX", "AMBIENT", "BOARD", "CARRIER", "CHIPSET", "VIDEO", "OTHER", "TOPDIM", "BOTTOMDIM", "UNKNOWN" };
static const unsigned char * cgbc_hwmon_labels_in[] = { "CPU", "RUNTIME", "STDBY", "CMOS_BAT", "POWER_BAT", "AC", "OTHER", "5V", "5V_STDBY", "3V3", "3V3_STDBY", "COREA", "COREB", "12V" };
static const unsigned char * cgbc_hwmon_labels_curr[] = { "DC", "5V", "12V", "UNKNOWN" };
static const unsigned char * cgbc_hwmon_labels_fan[] = { "FAN_CPU", "FAN_BOX", "FAN_AMBIENT", "FAN_CHIPSET", "FAN_VIDEO", "FAN_OTHER", "UNKNOWN" };

static int cgbc_hwmon_probe_sensors(struct cgbc_hwmon_data *hwmon)
{
	struct cgbc_device_data *cgbc = hwmon->cgbc;
	struct cgbc_hwmon_sensor *sensor = &hwmon->sensors[0];
	unsigned char cmd[2] = { CGBC_HWMON_CMD_SENSOR };
	unsigned char data[5], type, id;
	int ret, i;

	ret = cgbc_command(cgbc, &cmd, sizeof(cmd), &data, sizeof(data), NULL);
	if (ret)
		return ret;

	hwmon->nb_sensors = data[0];

	for (i = 0; i < hwmon->nb_sensors; i++) {
		cmd[1] = i;

		ret = cgbc_command(cgbc, &cmd, sizeof(cmd), &data, sizeof(data), NULL);
		if (ret)
			return ret;

		type = (data[1] & 0x60) >> 5;
		id = (data[1] & 0x1F) - 1;

		if (type == CGBC_HWMON_TYPE_TEMP) {
			sensor->type = hwmon_temp;
			sensor->label = cgbc_hwmon_labels_temp[id];
		} else if (type == CGBC_HWMON_TYPE_IN) {
			/* the board controller don't do differences between
			 * current and voltage sensors
			 */
			if (id > ARRAY_SIZE(cgbc_hwmon_labels_in)) {
				sensor->type = hwmon_curr;
				sensor->label = cgbc_hwmon_labels_curr[id - ARRAY_SIZE(cgbc_hwmon_labels_in)];
			 } else {
				sensor->type = hwmon_in;
				sensor->label = cgbc_hwmon_labels_in[id];
			 }
		} else if (type == CGBC_HWMON_TYPE_FAN) {
			sensor->type = hwmon_fan;
			sensor->label = cgbc_hwmon_labels_fan[id];
		} else
			continue;

		sensor->active = (data[1] & 0x80) >> 7;
		sensor->index = i;
		sensor++;
	}

	return 0;
}

static struct cgbc_hwmon_sensor * cgbc_hwmon_find_sensor(struct cgbc_hwmon_data *hwmon, enum hwmon_sensor_types type,
							 int channel)
{
	struct cgbc_hwmon_sensor *sensor = NULL;
	int i, cnt = 0;

	for (i = 0; i < hwmon->nb_sensors; i++) {
		if (hwmon->sensors[i].type == type &&
		    cnt++ == channel) {
			sensor = &hwmon->sensors[i];
			break;
		}
	}

	return sensor;
}

static int cgbc_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct cgbc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct cgbc_hwmon_sensor *sensor = cgbc_hwmon_find_sensor(hwmon, type, channel);
	struct cgbc_device_data *cgbc = hwmon->cgbc;
	unsigned char cmd[2] = { CGBC_HWMON_CMD_SENSOR };
	unsigned char data[5];
	int ret;

	cmd[1] = sensor->index;

	ret = cgbc_command(cgbc, &cmd, sizeof(cmd), &data, sizeof(data), NULL);
	if (ret)
		return ret;

	*val = (data[3] << 8 | data[2]);

	if (sensor->type == hwmon_temp)
		*val *= 100;

	return 0;
}

static umode_t cgbc_hwmon_is_visible(const void *_data,
				     enum hwmon_sensor_types type, u32 attr,
				     int channel)
{
	struct cgbc_hwmon_data *data = (struct cgbc_hwmon_data *) _data;
	struct cgbc_hwmon_sensor *sensor;

	sensor = cgbc_hwmon_find_sensor(data, type, channel);
	if (IS_ERR_OR_NULL(sensor))
		return 0;

	return sensor->active ? 0444 : 0;
}

static int cgbc_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	struct cgbc_hwmon_data *hwmon = dev_get_drvdata(dev);
	struct cgbc_hwmon_sensor *sensor = cgbc_hwmon_find_sensor(hwmon, type, channel);

	*str = sensor->label;

	return 0;
}

static const struct hwmon_channel_info * const cgbc_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL, HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL, HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_ops cgbc_hwmon_ops = {
	.is_visible = cgbc_hwmon_is_visible,
	.read = cgbc_hwmon_read,
	.read_string = cgbc_hwmon_read_string,
};

static const struct hwmon_chip_info cgbc_chip_info = {
	.ops = &cgbc_hwmon_ops,
	.info = cgbc_hwmon_info,
};

static int cgbc_hwmon_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cgbc_hwmon_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->cgbc = cgbc;

	platform_set_drvdata(pdev, data);

	ret = cgbc_hwmon_probe_sensors(data);
	if (ret)
		return ret;

	data->hwmon_dev = devm_hwmon_device_register_with_info(dev, "cgbc_hwmon",
							       data, &cgbc_chip_info, NULL);
	return PTR_ERR_OR_ZERO(data->hwmon_dev);
}

static struct platform_driver cgbc_hwmon_driver = {
	.driver = {
		.name = "cgbc-hwmon",
	},
	.probe = cgbc_hwmon_probe,
};

module_platform_driver(cgbc_hwmon_driver);

MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_DESCRIPTION("Congatec Board Controller Hardware Monitoring Driver");
MODULE_LICENSE("GPL");
