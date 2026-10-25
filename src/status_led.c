/*
 * Copyright (c) 2026 Iacopo Moles
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "status_led.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

#define LED_STORAGE_ID PARTITION_ID(storage_partition)

/* Bump when the layout of struct led_settings changes */
#define LED_SETTINGS_MAGIC 0x324c505aU /* "ZPL2" */

#define LED_COLOR_MAX 0xffffffU

/* Same range as CONFIG_APP_LED_IDLE_TIMEOUT_MS */
#define LED_TIMEOUT_MIN_MS 1U
#define LED_TIMEOUT_MAX_MS 60000U

/*
 * Fixed record at the start of the storage partition, read with a single
 * flash_area_read() at boot. Erased flash reads as 0xff and fails the magic
 * check, a torn write fails the CRC check.
 */
struct led_settings {
	uint32_t magic;
	/* 0xRRGGBB, indexed by enum status_led_state */
	uint32_t colors[STATUS_LED_STATE_COUNT];
	/* Time without USB traffic before falling back from active to configured */
	uint32_t idle_timeout_ms;
	/* CRC32 (IEEE) of all the fields above */
	uint32_t crc;
};

static const struct led_settings led_defaults = {
	.colors = {
		[STATUS_LED_UNCONFIGURED] = CONFIG_APP_LED_COLOR_UNCONFIGURED,
		[STATUS_LED_CONFIGURED] = CONFIG_APP_LED_COLOR_CONFIGURED,
		[STATUS_LED_ACTIVE] = CONFIG_APP_LED_COLOR_ACTIVE,
	},
	.idle_timeout_ms = CONFIG_APP_LED_IDLE_TIMEOUT_MS,
};

static const char *const led_state_names[STATUS_LED_STATE_COUNT] = {
	[STATUS_LED_UNCONFIGURED] = "unconfigured",
	[STATUS_LED_CONFIGURED] = "configured",
	[STATUS_LED_ACTIVE] = "active",
};

static const struct device *const led_strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

/* Updated from main, the USB stack thread, the system work queue and the shell */
static K_MUTEX_DEFINE(led_lock);
static uint32_t led_colors[STATUS_LED_STATE_COUNT];
/* Read without led_lock for every USB transfer, a 32-bit load is atomic */
static uint32_t led_idle_timeout_ms;
static enum status_led_state led_state = STATUS_LED_STATE_COUNT;

/* Must be called with led_lock held */
static void led_show(enum status_led_state state, bool force)
{
	struct led_rgb pixel = {
		.r = (led_colors[state] >> 16) & 0xff,
		.g = (led_colors[state] >> 8) & 0xff,
		.b = led_colors[state] & 0xff,
	};
	int ret;

	/* Skip redundant updates, this is called for every USB transfer */
	if (state == led_state && !force) {
		return;
	}

	ret = led_strip_update_rgb(led_strip, &pixel, 1);
	if (ret) {
		LOG_ERR("Failed to update LED strip, %d", ret);
		return;
	}

	led_state = state;
}

void status_led_set(enum status_led_state state)
{
	k_mutex_lock(&led_lock, K_FOREVER);
	led_show(state, false);
	k_mutex_unlock(&led_lock);
}

static void led_idle_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Fall back from active to configured, unless the device was reset
	 * or unconfigured in the meantime
	 */
	k_mutex_lock(&led_lock, K_FOREVER);
	if (led_state == STATUS_LED_ACTIVE) {
		led_show(STATUS_LED_CONFIGURED, false);
	}
	k_mutex_unlock(&led_lock);
}

static K_WORK_DELAYABLE_DEFINE(led_idle_work, led_idle_handler);

void status_led_activity(void)
{
	status_led_set(STATUS_LED_ACTIVE);
	k_work_reschedule(&led_idle_work, K_MSEC(led_idle_timeout_ms));
}

static uint32_t led_settings_crc(const struct led_settings *settings)
{
	return crc32_ieee((const uint8_t *)settings, offsetof(struct led_settings, crc));
}

/* Returns 0 and fills settings if a valid record is stored */
static int led_settings_load(struct led_settings *settings)
{
	const struct flash_area *fa;
	int ret;

	ret = flash_area_open(LED_STORAGE_ID, &fa);
	if (ret) {
		return ret;
	}

	ret = flash_area_read(fa, 0, settings, sizeof(*settings));
	flash_area_close(fa);
	if (ret) {
		return ret;
	}

	if (settings->magic != LED_SETTINGS_MAGIC ||
	    settings->crc != led_settings_crc(settings)) {
		return -ENOENT;
	}

	for (int i = 0; i < STATUS_LED_STATE_COUNT; i++) {
		if (settings->colors[i] > LED_COLOR_MAX) {
			return -EINVAL;
		}
	}

	if (settings->idle_timeout_ms < LED_TIMEOUT_MIN_MS ||
	    settings->idle_timeout_ms > LED_TIMEOUT_MAX_MS) {
		return -EINVAL;
	}

	return 0;
}

static int led_settings_save(struct led_settings *settings)
{
	const struct flash_area *fa;
	int ret;

	settings->magic = LED_SETTINGS_MAGIC;
	settings->crc = led_settings_crc(settings);

	ret = flash_area_open(LED_STORAGE_ID, &fa);
	if (ret) {
		return ret;
	}

	ret = flash_area_erase(fa, 0, fa->fa_size);
	if (ret == 0) {
		ret = flash_area_write(fa, 0, settings, sizeof(*settings));
	}

	flash_area_close(fa);

	return ret;
}

static int led_settings_erase(void)
{
	const struct flash_area *fa;
	int ret;

	ret = flash_area_open(LED_STORAGE_ID, &fa);
	if (ret) {
		return ret;
	}

	ret = flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);

	return ret;
}

/* Must be called with led_lock held */
static void led_apply_settings(const struct led_settings *settings)
{
	memcpy(led_colors, settings->colors, sizeof(led_colors));
	led_idle_timeout_ms = settings->idle_timeout_ms;
}

int status_led_init(void)
{
	struct led_settings settings;
	int ret;

	if (!device_is_ready(led_strip)) {
		LOG_ERR("LED strip device %s is not ready", led_strip->name);
		return -ENODEV;
	}

	k_mutex_lock(&led_lock, K_FOREVER);

	ret = led_settings_load(&settings);
	if (ret) {
		LOG_DBG("No stored LED settings (%d), using defaults", ret);
		led_apply_settings(&led_defaults);
	} else {
		led_apply_settings(&settings);
	}

	led_show(STATUS_LED_UNCONFIGURED, true);

	k_mutex_unlock(&led_lock);

	return 0;
}

static int cmd_led_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&led_lock, K_FOREVER);
	for (int i = 0; i < STATUS_LED_STATE_COUNT; i++) {
		shell_print(sh, "%-12s 0x%06x%s", led_state_names[i], led_colors[i],
			    i == led_state ? " (current)" : "");
	}
	shell_print(sh, "%-12s %u ms", "timeout", led_idle_timeout_ms);
	k_mutex_unlock(&led_lock);

	return 0;
}

static int cmd_led_set(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long color;
	int state;
	int err = 0;

	ARG_UNUSED(argc);

	for (state = 0; state < STATUS_LED_STATE_COUNT; state++) {
		if (strcmp(argv[1], led_state_names[state]) == 0) {
			break;
		}
	}

	if (state == STATUS_LED_STATE_COUNT) {
		shell_error(sh, "Unknown state %s, use unconfigured, configured or active",
			    argv[1]);
		return -EINVAL;
	}

	color = shell_strtoul(argv[2], 16, &err);
	if (err || color > LED_COLOR_MAX) {
		shell_error(sh, "Invalid color %s, use RRGGBB, e.g. 0x002000", argv[2]);
		return -EINVAL;
	}

	k_mutex_lock(&led_lock, K_FOREVER);
	led_colors[state] = color;
	if (state == led_state) {
		led_show(state, true);
	}
	k_mutex_unlock(&led_lock);

	shell_print(sh, "%s set to 0x%06lx, run 'led save' to keep it after a reboot",
		    led_state_names[state], color);

	return 0;
}

static int cmd_led_timeout(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long timeout;
	int err = 0;

	if (argc < 2) {
		shell_print(sh, "%u ms", led_idle_timeout_ms);
		return 0;
	}

	timeout = shell_strtoul(argv[1], 10, &err);
	if (err || timeout < LED_TIMEOUT_MIN_MS || timeout > LED_TIMEOUT_MAX_MS) {
		shell_error(sh, "Invalid timeout %s, use %u to %u ms", argv[1],
			    LED_TIMEOUT_MIN_MS, LED_TIMEOUT_MAX_MS);
		return -EINVAL;
	}

	k_mutex_lock(&led_lock, K_FOREVER);
	led_idle_timeout_ms = timeout;
	k_mutex_unlock(&led_lock);

	shell_print(sh, "Idle timeout set to %lu ms, run 'led save' to keep it after a reboot",
		    timeout);

	return 0;
}

static int cmd_led_save(const struct shell *sh, size_t argc, char **argv)
{
	struct led_settings settings;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_mutex_lock(&led_lock, K_FOREVER);
	memcpy(settings.colors, led_colors, sizeof(settings.colors));
	settings.idle_timeout_ms = led_idle_timeout_ms;
	k_mutex_unlock(&led_lock);

	ret = led_settings_save(&settings);
	if (ret) {
		shell_error(sh, "Failed to save LED settings, %d", ret);
		return ret;
	}

	shell_print(sh, "LED settings saved");

	return 0;
}

static int cmd_led_reset(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = led_settings_erase();
	if (ret) {
		shell_error(sh, "Failed to erase stored LED settings, %d", ret);
		return ret;
	}

	k_mutex_lock(&led_lock, K_FOREVER);
	led_apply_settings(&led_defaults);
	if (led_state < STATUS_LED_STATE_COUNT) {
		led_show(led_state, true);
	}
	k_mutex_unlock(&led_lock);

	shell_print(sh, "LED settings reset to the defaults");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
	SHELL_CMD(show, NULL, "Show the status LED settings", cmd_led_show),
	SHELL_CMD_ARG(set, NULL,
		      "Set a status LED color, until reboot unless saved\n"
		      "Usage: led set <unconfigured|configured|active> <RRGGBB>",
		      cmd_led_set, 3, 0),
	SHELL_CMD_ARG(timeout, NULL,
		      "Show or set the USB traffic idle timeout, until reboot unless saved\n"
		      "Usage: led timeout [ms]",
		      cmd_led_timeout, 1, 1),
	SHELL_CMD(save, NULL, "Save the status LED settings to flash", cmd_led_save),
	SHELL_CMD(reset, NULL, "Erase the saved settings and use the defaults", cmd_led_reset),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(led, &sub_led, "Status LED settings", NULL);
