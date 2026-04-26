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

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap_sample, LOG_LEVEL_INF);

#include "webusb.h"
#include "msosv2.h"

DAP_LINK_CONTEXT_DEFINE(sample_dap_ctx, DEVICE_DT_GET_ONE(zephyr_swdp_gpio));

int main(void)
{
	struct usbd_context *usbd_device;
	int ret;

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

	usbd_device = usbd_setup_device(NULL);
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

	ret = usbd_enable(usbd_device);
	if (ret) {
		LOG_ERR("Failed to enable device support");
		return ret;
	}

	return 0;
}
