/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usbd_impl.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/dap/dap_link.h>
#include <zephyr/drivers/led_strip.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_sample, LOG_LEVEL_INF);

#include "webusb.h"
#include "msosv2.h"

DAP_LINK_CONTEXT_DEFINE(sample_dap_ctx, DEVICE_DT_GET_ONE(zephyr_swdp_gpio));

#define LED_BRIGHTNESS 0x20

static const struct device *const led_strip = DEVICE_DT_GET(DT_ALIAS(led_strip));
static const struct led_rgb led_red = { .r = LED_BRIGHTNESS };
static const struct led_rgb led_green = { .g = LED_BRIGHTNESS };
static const struct led_rgb led_blue = { .b = LED_BRIGHTNESS };

static void led_set_color(const struct led_rgb *color)
{
	/* The driver may use the pixel buffer as scratch space */
	struct led_rgb pixel = *color;
	int ret;

	ret = led_strip_update_rgb(led_strip, &pixel, 1);
	if (ret) {
		LOG_ERR("Failed to update LED strip, %d", ret);
	}
}

static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	led_set_color(&led_blue);

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}
}

int main(void)
{
	struct usbd_context *usbd_device;
	int ret;

	if (!device_is_ready(led_strip)) {
		LOG_ERR("LED strip device %s is not ready", led_strip->name);
		return -ENODEV;
	}

	led_set_color(&led_red);

	ret = dap_link_init(&sample_dap_ctx);
	if (ret) {
		LOG_ERR("Failed to initialize DAP controller, %d", ret);
		return ret;
	}

	ret = dap_link_backend_usb_init(&sample_dap_ctx);
	if (ret) {
		LOG_ERR("Failed to initialize DAP Link USB backend, %d", ret);
		return ret;
	}

	usbd_device = usbd_setup_device(sample_msg_cb);
	if (usbd_device == NULL) {
		LOG_ERR("Failed to setup USB device");
		return -ENODEV;
	}

	ret = usbd_add_descriptor(usbd_device, &bos_vreq_msosv2);
	if (ret) {
		LOG_ERR("Failed to add MSOSv2 capability descriptor");
		return ret;
	}

	ret = usbd_add_descriptor(usbd_device, &bos_vreq_webusb);
	if (ret) {
		LOG_ERR("Failed to add WebUSB capability descriptor");
		return ret;
	}

	ret = usbd_init(usbd_device);
	if (ret) {
		LOG_ERR("Failed to initialize device support");
		return ret;
	}

	if (!usbd_can_detect_vbus(usbd_device)) {
		ret = usbd_enable(usbd_device);
		if (ret) {
			LOG_ERR("Failed to enable device support");
			return ret;
		}
	}

	return 0;
}
