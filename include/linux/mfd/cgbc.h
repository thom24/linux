// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller driver definitions
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#define CGBC_CGBC_CMD_GET_FW_REV	0x21

#define CGBC_VERSION_LEN 10

struct cgbc_info {
	unsigned char feature;
	unsigned char major;
	unsigned char minor;
	char version[CGBC_VERSION_LEN];
};

struct cgbc_device_data {
	void __iomem		*io_session;
	void __iomem		*io_cmd;
	u8			session;
	struct device		*dev;
	struct cgbc_info	info;
	struct mutex		lock;
};

struct cgbc_platform_data {
	const struct resource	*ioresource;
	unsigned int num_ioresource;
	int	(*command)(struct cgbc_device_data *, u8 *, u8, u8 *, u8, u8 *);
	int	(*register_cells)(struct cgbc_device_data *);
	int	(*map)(struct cgbc_device_data *);
	int	(*init)(struct cgbc_device_data *);
	void	(*close)(struct cgbc_device_data *);
};

int cgbc_command(struct cgbc_device_data *cgbc, u8 *cmd, u8 cmd_size, u8 *data,
		 u8 data_size, u8 *status);
