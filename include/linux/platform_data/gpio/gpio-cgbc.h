/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gpio-cgbc interface to platform code
 *
 * Copyright (C) 2026 congatec GmbH
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#ifndef _LINUX_GPIO_CGBC_H
#define _LINUX_GPIO_CGBC_H

/**
 * struct cgbc_gpio_platform_data - Platform data of the CGBC GPIO driver
 * @ngpio: Number of GPIOs
 */
struct cgbc_gpio_platform_data {
	int ngpio;
};

#endif /* _LINUX_GPIO_CGBC_H */
