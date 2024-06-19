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
#include <linux/iopoll.h>
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

#define CGBC_I2C_FREQ_DEFAULT	100     /* 100 kHz */
#define CGBC_I2C_FREQ_MAX	6100    /* 6.1 MHz */

#define CGBC_I2C_FREQ_UNIT_1KHZ		0x40
#define CGBC_I2C_FREQ_UNIT_10KHZ	0x80
#define CGBC_I2C_FREQ_UNIT_100KHZ	0xC0

#define CGBC_I2C_READ_MAX_LEN	31
#define CGBC_I2C_WRITE_MAX_LEN	32

enum {
	STATE_DONE = 0,
	STATE_INIT,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_ERROR,
};

struct i2c_algo_cgbc_data {
	u8	bus_id;
};

struct cgbc_i2c_data {
	struct device		*dev;
	struct cgbc_device_data *cgbc;
	struct i2c_adapter      adap;
	struct i2c_msg		*msg;
	int			nmsgs;
	int			pos;
	int			state;
};

struct cgbc_i2c_transfer {
	u8 bus_cmd;
	u8 write;
	u8 read;
	u8 addr;
#define CGBC_I2C_TRANSFER_HEADER_SIZE  4
	u8 data[32];
};

static unsigned int bus_frequency = CGBC_I2C_FREQ_DEFAULT;
module_param(bus_frequency, uint, 0);
MODULE_PARM_DESC(bus_frequency, "Set I2C bus frequency in kHz (default="
				__MODULE_STRING(CGBC_I2C_FREQ_DEFAULT)")");

static int cgbc_i2c_get_status(struct i2c_adapter *adap)
{
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	struct cgbc_i2c_data *i2c = i2c_get_adapdata(adap);
	struct cgbc_device_data *cgbc = i2c->cgbc;
	u8 cmd = CGBC_I2C_CMD_STAT | algo_data->bus_id;
	u8 status;
	int ret;

	ret = cgbc_command(cgbc, &cmd, sizeof(cmd), NULL, 0, &status);
	if (ret)
		return ret;

	return status;
}

static int cgbc_i2c_set_frequency(struct i2c_adapter *adap, unsigned int bus_frequency)
{
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	struct cgbc_i2c_data *i2c = i2c_get_adapdata(adap);
	struct cgbc_device_data *cgbc = i2c->cgbc;
	u8 cmd[2], data;
	int ret;

	if (bus_frequency > CGBC_I2C_FREQ_MAX)
		bus_frequency = CGBC_I2C_FREQ_MAX;

	cmd[0] = CGBC_I2C_CMD_SPEED | algo_data->bus_id;

	if (bus_frequency >= 100)
		cmd[1] = CGBC_I2C_FREQ_UNIT_100KHZ | (bus_frequency / 100);
	else if (bus_frequency >= 10)
		cmd[1] = CGBC_I2C_FREQ_UNIT_10KHZ | (bus_frequency / 10);
	else
		cmd[1] = CGBC_I2C_FREQ_UNIT_1KHZ | bus_frequency;

	ret = cgbc_command(cgbc, &cmd, sizeof(cmd), &data, 1, NULL);
	if (ret)
		return dev_err_probe(i2c->dev, ret, "Failed to initialize I2C bus %s",
				     adap->name);

	dev_info(i2c->dev, "%s initialized at %dkHz\n", adap->name, 100);

	return 0;
}

static int cgbc_i2c_xfer_msg(struct i2c_adapter *adap)
{
	struct i2c_algo_cgbc_data *algo_data = adap->algo_data;
	struct cgbc_i2c_data *i2c = i2c_get_adapdata(adap);
	struct cgbc_device_data *cgbc = i2c->cgbc;
	struct i2c_msg *msg = i2c->msg;
	int ret, max_len, len, i;
	u8 cmd_data;

	struct cgbc_i2c_transfer xfer = {
		.bus_cmd = CGBC_I2C_CMD_START | algo_data->bus_id,
		.addr = i2c_8bit_addr_from_msg(msg),
	};

	if (i2c->state == STATE_DONE)
		return ret;

	ret = cgbc_i2c_get_status(adap);

	if (ret == CGBC_I2C_STAT_BUSY)
		return -EBUSY;
	else if (ret < 0)
		goto err;

	if (i2c->state == STATE_INIT || i2c->state == STATE_WRITE) {
		xfer.write = CGBC_I2C_START;
		i2c->state = (msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;
	} else
		xfer.write = 0x00;

	max_len = (i2c->state == STATE_READ) ? CGBC_I2C_READ_MAX_LEN : CGBC_I2C_WRITE_MAX_LEN;
	if (msg->len - i2c->pos > max_len)
		len = max_len;
	else {
		len = msg->len - i2c->pos;

		if (i2c->nmsgs == 1)
			xfer.write |= CGBC_I2C_STOP;
	}

	if (i2c->state == STATE_WRITE) {
		xfer.write |= (1 + len);
		xfer.read = 0x00;
		for (i = 0; i < len; i++)
			xfer.data[i] = msg->buf[i2c->pos + i];

		ret =  cgbc_command(cgbc, &xfer, CGBC_I2C_TRANSFER_HEADER_SIZE + len, NULL, 0, NULL);
		if (ret)
			goto err;
	} else if (i2c->state == STATE_READ) {
		xfer.write |= 1;

		xfer.read = len;
		if (i2c->nmsgs > 1 || msg->len - i2c->pos > max_len)
			xfer.read |= CGBC_I2C_LAST_ACK;

		ret = cgbc_command(cgbc, &xfer, CGBC_I2C_TRANSFER_HEADER_SIZE, NULL, 0, NULL);
		if (ret)
			goto err;

		read_poll_timeout(cgbc_i2c_get_status, ret, ret != CGBC_I2C_STAT_BUSY,
				  0, 1000000, 0, adap);
		if (ret < 0)
			goto err;

		cmd_data = CGBC_I2C_CMD_DATA | algo_data->bus_id;
		ret = cgbc_command(cgbc, &cmd_data, sizeof(cmd_data), msg->buf + i2c->pos, len, NULL);
		if (ret)
			goto err;
	}

	if (!ret && (i2c->state == STATE_WRITE || i2c->state == STATE_READ)) {
		if (len == (msg->len - i2c->pos)) {
			i2c->msg++;
			i2c->nmsgs--;
			i2c->pos = 0;
		} else
			i2c->pos += len;
	}

	if (i2c->nmsgs == 0)
		i2c->state = STATE_DONE;

	return 0;

err:
	i2c->state = STATE_ERROR;
	return ret;
}


static int cgbc_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct cgbc_i2c_data *i2c = i2c_get_adapdata(adap);
	unsigned long timeout = jiffies + HZ;
	int ret;

	i2c->state = STATE_INIT;
	i2c->msg = msgs;
	i2c->nmsgs = num;
	i2c->pos = 0;

	while (time_before(jiffies, timeout)) {
		ret = cgbc_i2c_xfer_msg(adap);

		if (i2c->state == STATE_DONE || i2c->state == STATE_ERROR)
			return (i2c->state == STATE_DONE) ? num : ret;

		if (ret == 0)
			timeout = jiffies + HZ;
	}

	i2c->state = STATE_ERROR;

	return -ETIMEDOUT;
}

static u32 cgbc_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm cgbc_i2c_algorithm = {
	.master_xfer	= cgbc_i2c_xfer,
	.functionality	= cgbc_i2c_func,
};

static struct i2c_algo_cgbc_data cgbc_i2c_algo_data[] = {
	{ .bus_id = CGBC_I2C_PRIMARY_BUS_ID },
	{ .bus_id = CGBC_I2C_PM_BUS_ID },
};

static const struct i2c_adapter cgbc_i2c_adapter[] = {
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
		.name		= "Congatec Power Management I2C adapter",
		.class		= I2C_CLASS_DEPRECATED,
		.algo		= &cgbc_i2c_algorithm,
		.algo_data	= &cgbc_i2c_algo_data[1],
		.nr		= -1,
	},
};

static int cgbc_i2c_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct cgbc_i2c_data *i2c;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->cgbc = cgbc;
	i2c->dev = &pdev->dev;
	i2c->adap = cgbc_i2c_adapter[pdev->id];
	i2c->adap.dev.parent = i2c->dev;
	i2c_set_adapdata(&i2c->adap, i2c);
	platform_set_drvdata(pdev, i2c);

	ret = cgbc_i2c_set_frequency(&i2c->adap, bus_frequency);
	if (ret)
		return ret;

	return i2c_add_numbered_adapter(&i2c->adap);
}

static void cgbc_i2c_remove(struct platform_device *pdev)
{
	 struct cgbc_i2c_data *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
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
