// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Congatec Board Controller watchdog driver
 *
 * Copyright (C) 2024 Bootlin
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/mfd/cgbc.h>

#define CGBC_WDT_CMD_TRIGGER	0x27
#define CGBC_WDT_CMD_INIT	0x28
#define	CGBC_WDT_DISABLE	0x00

#define CGBC_WDT_MODE_SINGLE_EVENT 0x02

#define DEFAULT_TIMEOUT		30
#define DEFAULT_PRETIMEOUT	0

enum {
	ACTION_INT = 0,
	ACTION_SMI,
	ACTION_RESET,
	ACTION_BUTTON,
};

static unsigned int timeout = DEFAULT_TIMEOUT;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. (>=0, default="
		 __MODULE_STRING(DEFAULT_TIMEOUT) ")");

static unsigned int pretimeout = DEFAULT_PRETIMEOUT;
module_param(pretimeout, uint, 0);
MODULE_PARM_DESC(pretimeout,
		 "Watchdog pretimeout in seconds. (>=0, default="
		 __MODULE_STRING(DEFAULT_PRETIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct cgbc_wdt_data {
	struct cgbc_device_data	*cgbc;
	struct watchdog_device	wdd;
	unsigned int timeout_action;
	unsigned int pretimeout_action;
};

struct cgbc_wdt_cmd_cfg {
	u8 cmd;
	u8 mode;
	u8 action;
	u8 timeout1[3];
	u8 timeout2[3];
	u8 reserved[3];
	u8 delay[3];
} __packed;

static int cgbc_wdt_start(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	unsigned int timeout1 = (wdd->pretimeout > 0) ?
		(wdd->timeout - wdd->pretimeout) * 1000 : wdd->timeout * 1000;
	unsigned int timeout2 = (wdd->pretimeout > 0) ?
		wdd->pretimeout * 1000 : 0;


	struct cgbc_wdt_cmd_cfg cmd_start = {
		.cmd = CGBC_WDT_CMD_INIT,
		.mode = CGBC_WDT_MODE_SINGLE_EVENT,
		.action = (pretimeout > 0) ?
			2 | (u8)(wdt_data->pretimeout_action << 2) |
			(u8)(wdt_data->timeout_action << 4) :
			1 | (u8)(wdt_data->timeout_action << 2),
		.timeout1[0] = (u8)(timeout1 & 0xFF),
		.timeout1[1] = (u8)((timeout1 & 0xFF00) >> 8),
		.timeout1[2] = (u8)((timeout1 & 0xFF0000) >> 16),
		.timeout2[0] = (u8)(timeout2 & 0xFF),
		.timeout2[1] = (u8)((timeout2 & 0xFF00) >> 8),
		.timeout2[2] = (u8)((timeout2 & 0xFF0000) >> 16),
		.reserved[0] = 0x00,
		.reserved[1] = 0x00,
		.reserved[2] = 0x00,
		.delay[0] = 0x00,
		.delay[1] = 0x00,
		.delay[2] = 0x00,
	};

	return cgbc_command(cgbc, &cmd_start, sizeof(cmd_start), NULL, 0, NULL);
}

static int cgbc_wdt_stop(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	struct cgbc_wdt_cmd_cfg cmd_stop = {
		.cmd = CGBC_WDT_CMD_INIT,
		.mode = CGBC_WDT_DISABLE,
		.action = 0x00,
		.timeout1[0] = 0x00,
		.timeout1[1] = 0x00,
		.timeout1[2] = 0x00,
		.timeout2[0] = 0x00,
		.timeout2[1] = 0x00,
		.timeout2[2] = 0x00,
		.reserved[0] = 0x00,
		.reserved[1] = 0x00,
		.reserved[2] = 0x00,
		.delay[0] = 0x00,
		.delay[1] = 0x00,
		.delay[2] = 0x00,
	};

	return cgbc_command(cgbc, &cmd_stop, sizeof(cmd_stop), NULL, 0, NULL);
}

static int cgbc_wdt_keepalive(struct watchdog_device *wdd)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);
	struct cgbc_device_data *cgbc = wdt_data->cgbc;
	u8 cmd_ping = CGBC_WDT_CMD_TRIGGER;

	return cgbc_command(cgbc, &cmd_ping, sizeof(cmd_ping), NULL, 0, NULL);
}

static int cgbc_wdt_set_pretimeout(struct watchdog_device *wdd,
				   unsigned int pretimeout)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);

	if (pretimeout > wdd->timeout)
		return -EINVAL;

	wdd->pretimeout = pretimeout;
	wdt_data->pretimeout_action = ACTION_SMI;

	if (watchdog_active(wdd))
		return cgbc_wdt_start(wdd);

	return 0;
}

static int cgbc_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	struct cgbc_wdt_data *wdt_data = watchdog_get_drvdata(wdd);

	if (timeout < wdd->timeout) {
		dev_dbg(wdd->parent,
			"pretimeout > timeout. Set pretimeout to zero\n");
		wdd->pretimeout = 0;
	}
	wdd->timeout = timeout;
	wdt_data->timeout_action = ACTION_RESET;

	if (watchdog_active(wdd))
		return cgbc_wdt_start(wdd);

	return 0;
}

static const struct watchdog_info cgbc_wdt_info = {
	.identity	= "CGBC Watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
		WDIOF_MAGICCLOSE | WDIOF_PRETIMEOUT
};

static const struct watchdog_ops cgbc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= cgbc_wdt_start,
	.stop		= cgbc_wdt_stop,
	.ping		= cgbc_wdt_keepalive,
	.set_timeout	= cgbc_wdt_set_timeout,
	.set_pretimeout = cgbc_wdt_set_pretimeout,
};

static int cgbc_wdt_probe(struct platform_device *pdev)
{
	struct cgbc_device_data *cgbc = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cgbc_wdt_data *wdt_data;
	struct watchdog_device *wdd;
	int ret;

	wdt_data = devm_kzalloc(dev, sizeof(*wdt_data), GFP_KERNEL);
	if (!wdt_data)
		return -ENOMEM;

	wdt_data->cgbc = cgbc;
	wdd = &wdt_data->wdd;
	wdd->parent = dev;

	wdd->info = &cgbc_wdt_info;
	wdd->ops = &cgbc_wdt_ops;

	watchdog_set_drvdata(wdd, wdt_data);
	watchdog_set_nowayout(wdd, nowayout);

	cgbc_wdt_set_timeout(wdd, timeout);
	cgbc_wdt_set_pretimeout(wdd, pretimeout);

	platform_set_drvdata(pdev, wdt_data);
	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	dev_info(dev, "Watchdog registered with %ds timeout\n", wdd->timeout);

	return 0;
}

static struct platform_driver cgbc_wdt_driver = {
	.driver		= {
		.name	= "cgbc-wdt",
	},
	.probe		= cgbc_wdt_probe,
};

module_platform_driver(cgbc_wdt_driver);

MODULE_DESCRIPTION("CGBC Watchdog Driver");
MODULE_AUTHOR("Thomas Richard <thomas.richard@bootlin.com>");
MODULE_LICENSE("GPL");
