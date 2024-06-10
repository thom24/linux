// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Congatec Board Controller MFD core driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/dmi.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/cgbc.h>
#include <linux/sysfs.h>

/* Generic macros */
#define CGBC_MASK_STATUS        (BIT(6) | BIT(7))
#define CGBC_MASK_DATA_COUNT	0x1F
#define CGBC_MASK_ERROR_CODE	0x1F

#define CGBC_STATUS_DATA_READY	0x00
#define CGBC_STATUS_CMD_READY	BIT(6)
#define CGBC_STATUS_ERROR	(BIT(6) | BIT(7))

#define CGBC_CMD_GET_FW_REV	0x21

#define CGBC_VERSION_LEN 10

/* Macros specific to gen5 Board Controller */
#define CGBC_GEN5_SESSION_IO_BASE	0x0E20
#define CGBC_GEN5_SESSION_IO_SIZE	0x0010

#define CGBC_GEN5_CMD_IO_BASE		0x0E00
#define CGBC_GEN5_CMD_IO_SIZE		0x0010

#define CGBC_GEN5_SESSION_CMD		0x00
#define CGBC_GEN5_SESSION_CMD_IDLE	0x00
#define CGBC_GEN5_SESSION_CMD_REQUEST	0x01
#define CGBC_GEN5_SESSION_DATA		0x01
#define CGBC_GEN5_SESSION_STATUS	0x02
#define	CGBC_GEN5_SESSION_STATUS_FREE	0x03
#define CGBC_GEN5_SESSION_ACCESS	0x04
#define	CGBC_GEN5_SESSION_ACCESS_GAINED	0x00

#define CGBC_GEN5_CMD_STROBE	0x00
#define CGBC_GEN5_CMD_INDEX	0x02
#define	CGBC_GEN5_CMD_INDEX_CBM_MAN8	0x00
#define	CGBC_GEN5_CMD_INDEX_CBM_AUTO32	0x03
#define CGBC_GEN5_CMD_DATA	0x04
#define CGBC_GEN5_CMD_ACCESS	0x0C

static struct platform_device *cgbc_pdev;

static int cgbc_gen5_detect_device(struct cgbc_device_data *cgbc)
{
	u16 status;
	int ret;

	ret = read_poll_timeout(ioread16, status,
				status == CGBC_GEN5_SESSION_STATUS_FREE,
				0, 500000, false,
				cgbc->io_session + CGBC_GEN5_SESSION_STATUS);

	if (ret || ioread32(cgbc->io_session + CGBC_GEN5_SESSION_ACCESS))
		ret = -ENODEV;

	return ret;
}

static u8 cgbc_gen5_session_command(struct cgbc_device_data *cgbc, u8 cmd)
{
	u8 ret;

	while (ioread8(cgbc->io_session + CGBC_GEN5_SESSION_CMD) != CGBC_GEN5_SESSION_CMD_IDLE)
		;

	iowrite8(cmd, cgbc->io_session + CGBC_GEN5_SESSION_CMD);

	while (ioread8(cgbc->io_session + CGBC_GEN5_SESSION_CMD) != CGBC_GEN5_SESSION_CMD_IDLE)
		;

	ret = ioread8(cgbc->io_session + CGBC_GEN5_SESSION_DATA);

	iowrite8(CGBC_GEN5_SESSION_STATUS_FREE, cgbc->io_session + CGBC_GEN5_SESSION_STATUS);

	return ret;
}

static int cgbc_gen5_session_request(struct cgbc_device_data *cgbc)
{
	unsigned int ret = cgbc_gen5_detect_device(cgbc);

	if (ret)
		return dev_err_probe(cgbc->dev, ret, "device not found\n");

	cgbc->session = cgbc_gen5_session_command(cgbc, CGBC_GEN5_SESSION_CMD_REQUEST);

	/* the Board Controller sent us a wrong session handle, we cannot communicate
	 * with it
	 */
	if ((cgbc->session < 0x02) || (cgbc->session > 0xFE)) {
		cgbc->session = 0;
		return dev_err_probe(cgbc->dev, -ENODEV, "failed to get a valid session handle\n");
	}

	return 0;
}

static void cgbc_gen5_session_release(struct cgbc_device_data *cgbc)
{
	cgbc_gen5_detect_device(cgbc);

	if (cgbc_gen5_session_command(cgbc, cgbc->session) != cgbc->session)
		dev_err(cgbc->dev, "failed to release session\n");
}

static bool cgbc_gen5_command_lock(struct cgbc_device_data *cgbc)
{
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_GEN5_CMD_ACCESS);

	if (ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_ACCESS) != cgbc->session)
		return false;
	else
		return true;
}

static void cgbc_gen5_command_unlock(struct cgbc_device_data *cgbc)
{
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_GEN5_CMD_ACCESS);
}

static int cgbc_gen5_command(struct cgbc_device_data *cgbc,
			     u8 *cmd, u8 cmd_size, u8 *data, u8 data_size, u8 *status)
{
	u8 checksum = 0, data_checksum = 0, val, istatus;
	int mode_change = -1;
	bool lock;
	int ret, i;

	mutex_lock(&cgbc->lock);

	/* request access */
	ret = read_poll_timeout(cgbc_gen5_command_lock, lock, lock, 0, 100000, false, cgbc);
	if (ret)
		goto out;

	/* wait board controller is ready */
	ret = read_poll_timeout(ioread8, val, val == CGBC_GEN5_CMD_STROBE, 0, 100000, false,
				cgbc->io_cmd + CGBC_GEN5_CMD_STROBE);
	if (ret)
		goto release;

	/* write command packet */
	if (cmd_size <= 2) {
		iowrite8(CGBC_GEN5_CMD_INDEX_CBM_MAN8, cgbc->io_cmd + CGBC_GEN5_CMD_INDEX);
	} else {
		iowrite8(CGBC_GEN5_CMD_INDEX_CBM_AUTO32, cgbc->io_cmd + CGBC_GEN5_CMD_INDEX);
		if ((cmd_size % 4) != 0x03)
			mode_change = (cmd_size & 0xFFFC) - 1;
	}

	for (i = 0; i < cmd_size; i++) {
		iowrite8(cmd[i], cgbc->io_cmd + CGBC_GEN5_CMD_DATA + (i % 4));
		checksum ^= cmd[i];
		if (mode_change == i)
			iowrite8((i + 1) | CGBC_GEN5_CMD_INDEX_CBM_MAN8,
				 cgbc->io_cmd + CGBC_GEN5_CMD_INDEX);
	}

	/* append checksum byte */
	iowrite8(checksum, cgbc->io_cmd + CGBC_GEN5_CMD_DATA + (i % 4));

	/* perform command strobe */
	iowrite8(cgbc->session, cgbc->io_cmd + CGBC_GEN5_CMD_STROBE);

	/* rewind cmd buffer index */
	iowrite8(CGBC_GEN5_CMD_INDEX_CBM_AUTO32, cgbc->io_cmd + CGBC_GEN5_CMD_INDEX);

	/* wait command completion */
	ret = read_poll_timeout(ioread8, val, val == CGBC_GEN5_CMD_STROBE, 0, 100000, false,
				cgbc->io_cmd + CGBC_GEN5_CMD_STROBE);
	if (ret)
		goto release;

	/* check command status */
	checksum = istatus = ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_DATA);
	switch (istatus & CGBC_MASK_STATUS) {
	case CGBC_STATUS_DATA_READY:
		if (istatus > data_size)
			istatus = data_size;
		for (i = 0; i < istatus; i++) {
			data[i] = ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_DATA + ((i + 1) % 4));
			checksum ^= data[i];
		}
		data_checksum = ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_DATA + ((i + 1) % 4));
		istatus &= CGBC_MASK_DATA_COUNT;
		break;
	case CGBC_STATUS_ERROR:
	case CGBC_STATUS_CMD_READY:
		data_checksum = ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_DATA + 1);
		if ((istatus & CGBC_MASK_STATUS) == CGBC_STATUS_ERROR)
			ret = -EIO;
		istatus = istatus & CGBC_MASK_ERROR_CODE;
		break;
	default:
		data_checksum = ioread8(cgbc->io_cmd + CGBC_GEN5_CMD_DATA + 1);
		istatus &= CGBC_MASK_ERROR_CODE;
		ret = -EIO;
		break;
	}

	/* checksum verification */
	if (ret == 0 && data_checksum != checksum)
		ret = -EIO;

release:
	cgbc_gen5_command_unlock(cgbc);

out:
	mutex_unlock(&cgbc->lock);

	if (status)
		*status = istatus;

	return ret;
}

static struct mfd_cell cgbc_gen5_devs[] = {
	{ .name = "cgbc-wdt"	},
	{ .name = "cgbc-gpio"	},
	{ .name = "cgbc-i2c", .id = 1 },
	{ .name = "cgbc-i2c", .id = 2 },
};

static int cgbc_gen5_register_cells(struct cgbc_device_data *cgbc)
{
	return mfd_add_devices(cgbc->dev, -1, cgbc_gen5_devs,
			       ARRAY_SIZE(cgbc_gen5_devs), NULL, 0, NULL);
}

static int cgbc_gen5_map(struct cgbc_device_data *cgbc)
{
	struct device *dev = cgbc->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *ioport;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!ioport)
		return -EINVAL;

	cgbc->io_session = devm_ioport_map(dev, ioport->start,
					resource_size(ioport));
	if (!cgbc->io_session)
		return -ENOMEM;

	ioport = platform_get_resource(pdev, IORESOURCE_IO, 1);
	if (!ioport)
		return -EINVAL;

	cgbc->io_cmd = devm_ioport_map(dev, ioport->start,
				       resource_size(ioport));
	if (!cgbc->io_cmd)
		return -ENOMEM;

	return 0;
}

static struct resource cgbc_gen5_ioresource[] = {
	{
		.start  = CGBC_GEN5_SESSION_IO_BASE,
		.end    = CGBC_GEN5_SESSION_IO_BASE + CGBC_GEN5_SESSION_IO_SIZE,
		.flags  = IORESOURCE_IO,
	},
	{
		.start  = CGBC_GEN5_CMD_IO_BASE,
		.end    = CGBC_GEN5_CMD_IO_BASE + CGBC_GEN5_CMD_IO_SIZE,
		.flags  = IORESOURCE_IO,
	},
};

static const struct cgbc_platform_data cgbc_gen5_platform_data = {
	.ioresource = &cgbc_gen5_ioresource[0],
	.num_ioresource = ARRAY_SIZE(cgbc_gen5_ioresource),
	.command = cgbc_gen5_command,
	.register_cells = cgbc_gen5_register_cells,
	.map = cgbc_gen5_map,
	.init = cgbc_gen5_session_request,
	.close = cgbc_gen5_session_release,
};

static int cgbc_create_platform_device(const struct dmi_system_id *id)
{
	const struct cgbc_platform_data *pdata = id->driver_data;
	const struct platform_device_info pdevinfo = {
		.name = "cgbc",
		.id = PLATFORM_DEVID_NONE,
		.res = pdata->ioresource,
		.num_res = pdata->num_ioresource,
		.data = pdata,
		.size_data = sizeof(*pdata),
	};

	cgbc_pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(cgbc_pdev))
		return PTR_ERR(cgbc_pdev);

	return 0;
}

static ssize_t cgbc_version_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", cgbc->info.version);
}

static DEVICE_ATTR_RO(cgbc_version);

static struct attribute *cgbc_attrs[] = {
	&dev_attr_cgbc_version.attr,
	NULL
};

ATTRIBUTE_GROUPS(cgbc);

static int cgbc_get_info(struct cgbc_device_data *cgbc)
{
	u8 cmd = CGBC_CGBC_CMD_GET_FW_REV;
	u8 data[4];
	int ret;

	ret = cgbc_command(cgbc, &cmd, 1, &data[0], sizeof(data), NULL);
	if (ret)
		return ret;

	cgbc->info.feature = data[0];
	cgbc->info.major = data[1];
	cgbc->info.minor = data[2];
	ret = snprintf(cgbc->info.version, sizeof(cgbc->info.version), "CGBCP%c%c%c",
		       cgbc->info.feature, cgbc->info.major,
		       cgbc->info.minor);

	if (ret < 0)
		return ret;

	return 0;
}

static int cgbc_register_cells(struct cgbc_device_data *pld)
{
	const struct cgbc_platform_data *pdata = dev_get_platdata(pld->dev);

	return pdata->register_cells(pld);
}

static int cgbc_detect_device(struct cgbc_device_data *cgbc)
{
	const struct cgbc_platform_data *pdata = dev_get_platdata(cgbc->dev);
	int ret;

	if (pdata->init) {
		ret = pdata->init(cgbc);
		if (ret)
			return ret;
	}

	ret = cgbc_get_info(cgbc);
	if (ret)
		return ret;

	dev_info(cgbc->dev, "Found Congatec Board Controller - %s\n",
		 cgbc->info.version);

	return cgbc_register_cells(cgbc);
}

int cgbc_command(struct cgbc_device_data *cgbc,
		 u8 *cmd, u8 cmd_size, u8 *data, u8 data_size, u8 *status)
{
	const struct cgbc_platform_data *pdata = dev_get_platdata(cgbc->dev);

	return pdata->command(cgbc, cmd, cmd_size, data, data_size, status);
}
EXPORT_SYMBOL_GPL(cgbc_command);

static int cgbc_probe(struct platform_device *pdev)
{
	const struct cgbc_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct cgbc_device_data *cgbc;
	int ret;

	pdata = dev_get_platdata(dev);

	cgbc = devm_kzalloc(dev, sizeof(*cgbc), GFP_KERNEL);
	if (!cgbc)
		return -ENOMEM;

	cgbc->dev = dev;

	ret = pdata->map(cgbc);
	if (ret)
		return ret;

	mutex_init(&cgbc->lock);

	platform_set_drvdata(pdev, cgbc);

	return cgbc_detect_device(cgbc);
}

static void cgbc_remove(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = platform_get_drvdata(pdev);
	const struct cgbc_platform_data *pdata = dev_get_platdata(cgbc->dev);

	if (pdata->close)
		pdata->close(cgbc);

	mfd_remove_devices(&pdev->dev);
}

static struct platform_driver cgbc_driver = {
	.driver		= {
		.name		= "cgbc",
		.dev_groups	= cgbc_groups,
	},
	.probe		= cgbc_probe,
	.remove_new	= cgbc_remove,
};

static const struct dmi_system_id cgbc_dmi_table[] __initconst = {
	{
		.ident = "SA7",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "congatec"),
			DMI_MATCH(DMI_BOARD_NAME, "conga-SA7"),
		},
		.driver_data	= (void *)&cgbc_gen5_platform_data,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, cgbc_dmi_table);

static int __init cgbc_init(void)
{
	const struct dmi_system_id *id;
	int ret = -ENODEV;

	id = dmi_first_match(cgbc_dmi_table);
	if (IS_ERR_OR_NULL(id))
		return ret;

	ret = cgbc_create_platform_device(id);
	if (ret)
		return ret;

	return platform_driver_register(&cgbc_driver);
}

static void __exit cgbc_exit(void)
{
	platform_device_unregister(cgbc_pdev);
	platform_driver_unregister(&cgbc_driver);
}

module_init(cgbc_init);
module_exit(cgbc_exit);

MODULE_DESCRIPTION("CGBC Core Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cgbc-core");
