/*
 * Copyright (c) 2026 Iacopo Moles
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEROPROBE_STATUS_LED_H
#define ZEROPROBE_STATUS_LED_H

enum status_led_state {
	/* Not configured by a USB host */
	STATUS_LED_UNCONFIGURED,
	/* Configured by the host, no USB traffic */
	STATUS_LED_CONFIGURED,
	/* USB traffic within the idle timeout, see 'led timeout' */
	STATUS_LED_ACTIVE,
	STATUS_LED_STATE_COUNT,
};

/*
 * Load the colors and idle timeout stored in the storage partition, falling
 * back to the Kconfig defaults, and show the unconfigured color.
 */
int status_led_init(void);

void status_led_set(enum status_led_state state);

/* Show the active color, and fall back to configured once traffic stops */
void status_led_activity(void);

#endif /* ZEROPROBE_STATUS_LED_H */
