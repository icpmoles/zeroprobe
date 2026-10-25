/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2026 Iacopo Moles
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usbd_impl.h"
#include "status_led.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/dap/dap_link.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_sample, LOG_LEVEL_INF);

#include "webusb.h"
#include "msosv2.h"

DAP_LINK_CONTEXT_DEFINE(sample_dap_ctx, DEVICE_DT_GET_ONE(zephyr_swdp_gpio));

/*
 * The USB device stack has no traffic notification, so intercept the
 * dispatch of completed non-control endpoint transfers to class instances.
 * Wrapped with -Wl,--wrap=usbd_class_handle_xfer, see CMakeLists.txt.
 */
int __real_usbd_class_handle_xfer(struct usbd_context *const uds_ctx,
				  struct net_buf *const buf,
				  const int err);

int __wrap_usbd_class_handle_xfer(struct usbd_context *const uds_ctx,
				  struct net_buf *const buf,
				  const int err)
{
	if (err == 0) {
		status_led_activity();
	}

	return __real_usbd_class_handle_xfer(uds_ctx, buf, err);
}

static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	// LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	/*
	 * Unconfigured, configured by the host, or active on traffic within the
	 * idle timeout, colors and timeout are set from the 'led' shell command.
	 * Other messages, e.g. CDC ACM line state, leave the LED unchanged.
	 */
	switch (msg->type) {
	case USBD_MSG_RESET:
	case USBD_MSG_VBUS_REMOVED:
		status_led_set(STATUS_LED_UNCONFIGURED);
		break;
	case USBD_MSG_CONFIGURATION:
		/* Status is the configuration value, 0 means unconfigured */
		status_led_set(msg->status ? STATUS_LED_CONFIGURED : STATUS_LED_UNCONFIGURED);
		break;
	default:
		break;
	}

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

	ret = status_led_init();
	if (ret) {
		return ret;
	}

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
