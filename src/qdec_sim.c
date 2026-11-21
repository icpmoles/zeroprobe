/*
 * Copyright (c) 2026 Iacopo Moles
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Quadrature encoder simulator, drives the A and B phases of an incremental
 * encoder on two GPIOs. Every step moves one state along the Gray sequence
 * 00 -> 10 -> 11 -> 01 (forward, A leads B) or backwards, so a decoder in
 * x4 mode counts one per step and four steps make one full cycle.
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#define QDEC_NODE DT_PATH(zephyr_user)

#if !DT_NODE_HAS_PROP(QDEC_NODE, qdec_gpios)
#error "qdec_sim needs qdec-gpios = <A>, <B> in the zephyr,user node"
#endif

static const struct gpio_dt_spec qdec_a = GPIO_DT_SPEC_GET_BY_IDX(QDEC_NODE, qdec_gpios, 0);
static const struct gpio_dt_spec qdec_b = GPIO_DT_SPEC_GET_BY_IDX(QDEC_NODE, qdec_gpios, 1);

/* A and B levels for each state, consecutive states differ by one bit */
static const uint8_t qdec_seq[4][2] = {
	{0, 0},
	{1, 0},
	{1, 1},
	{0, 1},
};

/* The timer runs on kernel ticks, one step per tick at most */
#define QDEC_RATE_MAX CONFIG_SYS_CLOCK_TICKS_PER_SEC

/* Shared between the shell thread and the timer ISR */
static struct k_spinlock qdec_lock;
static bool qdec_ready;
static uint8_t qdec_state;
static int64_t qdec_position;
static int8_t qdec_dir;
/* Steps left before stopping, 0 runs until 'qdec_sim stop' */
static uint32_t qdec_remaining;
static uint32_t qdec_period_ticks;

/* Must be called with qdec_lock held */
static void qdec_step(int8_t dir)
{
	qdec_state = (qdec_state + dir) & 0x3;
	qdec_position += dir;
	gpio_pin_set_dt(&qdec_a, qdec_seq[qdec_state][0]);
	gpio_pin_set_dt(&qdec_b, qdec_seq[qdec_state][1]);
}

static void qdec_timer_handler(struct k_timer *timer)
{
	k_spinlock_key_t key = k_spin_lock(&qdec_lock);

	qdec_step(qdec_dir);

	if (qdec_remaining > 0 && --qdec_remaining == 0) {
		k_timer_stop(timer);
		qdec_dir = 0;
	}

	k_spin_unlock(&qdec_lock, key);
}

static K_TIMER_DEFINE(qdec_timer, qdec_timer_handler, NULL);

/* The pins are left alone until the first command, so they stay inputs at boot */
static int qdec_init(const struct shell *sh)
{
	int ret;

	if (qdec_ready) {
		return 0;
	}

	if (!gpio_is_ready_dt(&qdec_a) || !gpio_is_ready_dt(&qdec_b)) {
		shell_error(sh, "GPIO controller not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&qdec_a, GPIO_OUTPUT_INACTIVE);
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&qdec_b, GPIO_OUTPUT_INACTIVE);
	}
	if (ret) {
		shell_error(sh, "Failed to configure the qdec pins, %d", ret);
		return ret;
	}

	qdec_state = 0;
	qdec_ready = true;

	return 0;
}

static void qdec_halt(void)
{
	k_spinlock_key_t key;

	k_timer_stop(&qdec_timer);

	key = k_spin_lock(&qdec_lock);
	qdec_dir = 0;
	qdec_remaining = 0;
	k_spin_unlock(&qdec_lock, key);
}

static int parse_long(const struct shell *sh, const char *arg, const char *what, long *out)
{
	char *end;

	*out = strtol(arg, &end, 0);
	if (end == arg || *end != '\0') {
		shell_error(sh, "Invalid %s %s", what, arg);
		return -EINVAL;
	}

	return 0;
}

static int cmd_qdec_start(const struct shell *sh, size_t argc, char **argv)
{
	k_spinlock_key_t key;
	long rate;
	long steps = 0;
	uint32_t period;
	int ret;

	if (parse_long(sh, argv[1], "rate", &rate)) {
		return -EINVAL;
	}

	if (rate == 0 || labs(rate) > QDEC_RATE_MAX) {
		shell_error(sh, "Invalid rate %ld, use 1 to %d steps/s, negative to go backwards",
			    rate, QDEC_RATE_MAX);
		return -EINVAL;
	}

	if (argc > 2) {
		if (parse_long(sh, argv[2], "step count", &steps)) {
			return -EINVAL;
		}
		if (steps < 0) {
			shell_error(sh, "Invalid step count %ld, use 0 to run until stopped",
				    steps);
			return -EINVAL;
		}
	}

	ret = qdec_init(sh);
	if (ret) {
		return ret;
	}

	qdec_halt();

	/* Rounded to whole kernel ticks, the real rate is printed below */
	period = MAX(1U, (CONFIG_SYS_CLOCK_TICKS_PER_SEC + labs(rate) / 2) / labs(rate));

	key = k_spin_lock(&qdec_lock);
	qdec_dir = rate > 0 ? 1 : -1;
	qdec_remaining = steps;
	qdec_period_ticks = period;
	k_spin_unlock(&qdec_lock, key);

	k_timer_start(&qdec_timer, K_TICKS(period), K_TICKS(period));

	shell_print(sh, "Running %s at %u steps/s (%u cycles/s)%s",
		    rate > 0 ? "forward" : "backward",
		    CONFIG_SYS_CLOCK_TICKS_PER_SEC / period,
		    CONFIG_SYS_CLOCK_TICKS_PER_SEC / period / 4,
		    steps ? ", stops after the given steps" : ", until 'qdec_sim stop'");

	return 0;
}

static int cmd_qdec_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	qdec_halt();
	shell_print(sh, "Stopped at position %lld", qdec_position);

	return 0;
}

static int cmd_qdec_step(const struct shell *sh, size_t argc, char **argv)
{
	k_spinlock_key_t key;
	long steps = 1;
	int ret;

	if (argc > 1 && parse_long(sh, argv[1], "step count", &steps)) {
		return -EINVAL;
	}

	ret = qdec_init(sh);
	if (ret) {
		return ret;
	}

	qdec_halt();

	/* One step at a time, so a decoder never sees two edges at once */
	for (long i = 0; i < labs(steps); i++) {
		key = k_spin_lock(&qdec_lock);
		qdec_step(steps > 0 ? 1 : -1);
		k_spin_unlock(&qdec_lock, key);
		k_busy_wait(10);
	}

	shell_print(sh, "Position %lld", qdec_position);

	return 0;
}

static int cmd_qdec_status(const struct shell *sh, size_t argc, char **argv)
{
	k_spinlock_key_t key;
	int64_t position;
	int8_t dir;
	uint32_t remaining;
	uint32_t period;
	uint8_t state;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	key = k_spin_lock(&qdec_lock);
	position = qdec_position;
	dir = qdec_dir;
	remaining = qdec_remaining;
	period = qdec_period_ticks;
	state = qdec_state;
	k_spin_unlock(&qdec_lock, key);

	shell_print(sh, "Pins     A: GPIO %u, B: GPIO %u%s", qdec_a.pin, qdec_b.pin,
		    qdec_ready ? "" : " (not driven yet)");
	shell_print(sh, "Phases   A=%u B=%u", qdec_seq[state][0], qdec_seq[state][1]);
	shell_print(sh, "Position %lld", position);

	if (dir == 0) {
		shell_print(sh, "State    stopped");
	} else {
		shell_print(sh, "State    running %s at %u steps/s",
			    dir > 0 ? "forward" : "backward",
			    CONFIG_SYS_CLOCK_TICKS_PER_SEC / period);
		if (remaining) {
			shell_print(sh, "Left     %u steps", remaining);
		}
	}

	return 0;
}

static int cmd_qdec_reset(const struct shell *sh, size_t argc, char **argv)
{
	k_spinlock_key_t key;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	key = k_spin_lock(&qdec_lock);
	qdec_position = 0;
	k_spin_unlock(&qdec_lock, key);

	shell_print(sh, "Position reset to 0");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_qdec_sim,
	SHELL_CMD_ARG(start, NULL,
		      "Generate steps continuously, 4 steps per cycle\n"
		      "Usage: qdec_sim start <steps/s> [steps]\n"
		      "A negative rate goes backwards (B leads A), steps 0 or omitted\n"
		      "runs until 'qdec_sim stop'",
		      cmd_qdec_start, 2, 1),
	SHELL_CMD(stop, NULL, "Stop generating steps, the pins keep their level",
		  cmd_qdec_stop),
	SHELL_CMD_ARG(step, NULL,
		      "Move by a number of steps right away, negative goes backwards\n"
		      "Usage: qdec_sim step [steps]",
		      cmd_qdec_step, 1, 1),
	SHELL_CMD(status, NULL, "Show the pins, phases, position and rate", cmd_qdec_status),
	SHELL_CMD(reset, NULL, "Set the position counter to 0, the pins are unchanged",
		  cmd_qdec_reset),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(qdec_sim, &sub_qdec_sim,
		   "Simulate a quadrature encoder, A on GPIO 6 and B on GPIO 7", NULL);
