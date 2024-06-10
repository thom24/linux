// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller I2C busses driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mfd/cgbc.h>

#define CGBC_I2C_PRIMARY_BUS_ID	0
#define CGBC_I2C_PM_BUS_ID	4

#define CGBC_I2C_CMD_START	0x40
#define CGBC_I2C_CMD_STAT	0x48
#define CGBC_I2C_CMD_DATA	0x50
#define CGBC_I2C_CMD_SPEED	0x58

#define CGBC_I2C_STAT_IDL	0x00
#define CGBC_I2C_STAT_DAT	0x01
#define CGBC_I2C_STAT_BUSY	0x02

#define CGBC_I2C_START	0x80
#define CGBC_I2C_STOP	0x40

#define CGBC_I2C_LAST_ACK  0x80    /* send ACK on last read byte */

#define CGBC_I2C_FREQ_UNIT_100KHZ	0xC0

enum {
	STATE_DONE = 0,
	STATE_INIT,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_ERROR,
};

struct i2c_algo_cgbc_data {
	int			bus_id;
	struct i2c_msg		*msg;
	int			nmsgs;
	int			pos;
	int			state;
};

static int cgbc_i2c_get_status(struct i2c_adapter *adap)
{
	struct cgbc_device_data *cgbc = i2c_get_adapdata(adap);
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	u8 cmd = CGBC_I2C_CMD_STAT | algo_data->bus_id;
	u8 status;
	int ret;

	ret = cgbc_command(cgbc, &cmd, 1, NULL, 0, &status);
	if (ret)
		return ret;

	return status;
}

static int cgbc_i2c_xfer_msg(struct i2c_adapter *adap)
{
	struct cgbc_device_data *cgbc = i2c_get_adapdata(adap);
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	struct i2c_msg *msg = algo_data->msg;
	u8 cmd[4 + 32], status;
	int ret = 0, len, i;

	if (algo_data->state == STATE_DONE)
		return ret;

	cmd[0] = CGBC_I2C_CMD_START | algo_data->bus_id;

	if (algo_data->state == STATE_INIT || algo_data->state == STATE_WRITE) {
		cmd[1] = CGBC_I2C_START;
		algo_data->state = (msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;
	} else
		cmd[1] = 0x00;

	cmd[3] = i2c_8bit_addr_from_msg(msg);

	if (msg->len - algo_data->pos > 32)
		len = 32;
	else {
		len = msg->len - algo_data->pos;

		if (algo_data->nmsgs == 1)
			cmd[1] |= CGBC_I2C_STOP;
	}

	if (algo_data->state == STATE_WRITE) {
		cmd[1] |= (1 + len);
		cmd[2] = 0x00;
		cmd[4] = msg->buf[0];
		for (i = 0; i < len; i++)
			cmd[4 + i] = msg->buf[algo_data->pos + i];

		while (cgbc_i2c_get_status(adap) == CGBC_I2C_STAT_BUSY)
			;

		ret =  cgbc_command(cgbc, &cmd[0], 4 + len, NULL, 0, &status);
	} else if (algo_data->state == STATE_READ) {
		cmd[1] |= 1;

		if (algo_data->nmsgs == 1 && msg->len - algo_data->pos <= 32)
			cmd[2] = len;
		else
			cmd[2] = len | CGBC_I2C_LAST_ACK;

		while (cgbc_i2c_get_status(adap) == CGBC_I2C_STAT_BUSY)
			;

		ret = cgbc_command(cgbc, &cmd[0], 4, NULL, 0, &status);
		if (ret)
			goto end;

		cmd[0] = CGBC_I2C_CMD_DATA | algo_data->bus_id;
		while (cgbc_i2c_get_status(adap) == CGBC_I2C_STAT_BUSY)
			;

		ret = cgbc_command(cgbc, &cmd[0], 1, msg->buf + algo_data->pos, len, &status);
	}

	if (!ret && (algo_data->state == STATE_WRITE || algo_data->state == STATE_READ)) {
		if (len == (msg->len - algo_data->pos)) {
			algo_data->msg++;
			algo_data->nmsgs--;
			algo_data->pos = 0;
		} else
			algo_data->pos += len;
	}

	if (algo_data->nmsgs == 0)
		algo_data->state = STATE_DONE;
end:
	return ret;
}


static int cgbc_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	unsigned long timeout = jiffies + HZ;
	int ret;

	algo_data->state = STATE_INIT;
	algo_data->msg = msgs;
	algo_data->nmsgs = num;
	algo_data->pos = 0;

	while (time_before(jiffies, timeout)) {
		ret = cgbc_i2c_xfer_msg(adap);

		if (algo_data->state == STATE_DONE || algo_data->state == STATE_ERROR)
			return (algo_data->state == STATE_DONE) ? num : ret;

		if (ret == 0)
			timeout = jiffies + HZ;
	}

	algo_data->state = STATE_ERROR;

	return -ETIMEDOUT;
}

static u32 cgbc_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm cgbc_i2c_algorithm = {
	.master_xfer	= cgbc_i2c_xfer,
	.functionality	= cgbc_i2c_func,
};

static struct i2c_algo_cgbc_data cgbc_i2c_algo_data[2] = {
	{ .bus_id = CGBC_I2C_PRIMARY_BUS_ID },
	{ .bus_id = CGBC_I2C_PM_BUS_ID },
};

static struct i2c_adapter cgbc_i2c_adapter[2] = {
	{
		.owner		= THIS_MODULE,
		.name		= "Congatec Primary I2C adapter",
		.class		= I2C_CLASS_DEPRECATED,
		.algo		= &cgbc_i2c_algorithm,
		.algo_data	= &cgbc_i2c_algo_data[0],
		.nr		= -1,
	},
	{
		.owner		= THIS_MODULE,
		.name		= "Congatec Power Mangement I2C adapter",
		.class		= I2C_CLASS_DEPRECATED,
		.algo		= &cgbc_i2c_algorithm,
		.algo_data	= &cgbc_i2c_algo_data[1],
		.nr		= -1,
	},
};

static int cgbc_i2c_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adap = &cgbc_i2c_adapter[pdev->id];
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	struct device *dev = &pdev->dev;
	struct cgbc_device_data *cgbc = dev_get_drvdata(dev->parent);
	u8 cmd[2], data, status;
	int ret;

	adap->dev.parent = dev;

	i2c_set_adapdata(adap, cgbc);

	cmd[0] = CGBC_I2C_CMD_SPEED | 0;
	cmd[1] = CGBC_I2C_FREQ_UNIT_100KHZ | algo_data->bus_id;

	ret = cgbc_command(cgbc, &cmd[0], 2, &data, 1, &status);
	if (ret)
		return dev_err_probe(&adap->dev, ret, "Failed to initialize I2C bus %s",
				     adap->name);

	dev_info(dev, "%s initialized at %dkHz\n", adap->name, 100);
	return i2c_add_numbered_adapter(adap);
}

static void cgbc_i2c_remove(struct platform_device *pdev)
{
	i2c_del_adapter(&cgbc_i2c_adapter[pdev->id]);
}

static struct platform_driver cgbc_i2c_driver = {
	.driver = {
		.name = "cgbc-i2c",
	},
	.probe		= cgbc_i2c_probe,
	.remove_new	= cgbc_i2c_remove,
};

module_platform_driver(cgbc_i2c_driver);

MODULE_DESCRIPTION("CGBC I2C Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cgbc_i2c");
