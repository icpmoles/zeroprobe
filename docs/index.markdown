---
title: Zeroprobe
# Feel free to add content and custom Front Matter to this file.
# To modify the layout, see https://jekyllrb.com/docs/themes/#overriding-theme-defaults

# layout: home
---

## Pinout

Check the [RPi Pico Pinout Labels](https://pico.pinout.xyz/)


| Zero Pin | Debugger Function |<->| Pico Pin | Target Function | Notes |
|----------|-------------------|---|----------|-----------------|-------|
| 9        | N/A               |   |          |                 |       |
| 10       | SWCLK             |   | CLK      | SWCLK           |       |
| 11       | SWDIO             |   | DIO      | SWDIO           |       |
| 12       | UART0 TX          |   | 1        | UART RX         | [^1]  |
| 13       | UART0 RX          |   | 0        | UART TX         | [^1]  |

![](assets/images/view_bb.png)

[^1]: Target pins depends on the setup of your target board

# How to use

- Download the UF2 firmware from the [Release](https://github.com/icpmoles/zeroprobe/releases) page.

- Flash it on the RP2040-Zero:

    - With the USB cable disconnected

    - Keep pressed the BOOT button

    - Connect the USB cable

    - An external memory device will appear

    - Drag and drop the `zephyr.uf2` file downloaded before

- Connect the two boards as shown above

- **[OPTIONAL]** Test with `pyOCD`

    - Install with `sudo apt install python3-pyocd`

    - Check the device with: `pyocd list`

    ```bash
    $ pyocd list               
    #   Probe/Board                          Unique ID          Target  
    ----------------------------------------------------------------------
    0   Zephyr Project Zeroprobe CMSIS-DAP   xxxxxxxxxxxxxxxx   n/a    
    ```

## SWD/DAP

This should be automatically detected by your SDK if it uses openOCD or pyOCD.

## TTY

On Linux this will expose two serial ports: one reporting the logs of the debugger, and one forwarding the target UART.

Assuming you don't have anything else connected and that you have [tio](https://github.com/tio/tio) installed:


```bash
$ ls /dev
# this will show all the devices connected, we are interested in:
# ttyACM0 = UART bridge
# ttyACM1 = zeroprobe logs
$ tio /dev/ttyACM0
```

# Fixes

In case of device not detected:

- Check with lsusb:
    ```bash
    $ lsusb -d 2fe3:                                                 
    Bus 005 Device 005: ID 2fe3:2ef0 NordicSemiconductor Zeroprobe CMSIS-DAP
    ```
- Add the udev rules for OpenOCD

    - `sudo nano /etc/dev/rules.d/60-openocd.rules`

    - Copy the content of [this file](https://github.com/openocd-org/openocd/blob/master/contrib/60-openocd.rules)

    - `sudo udevadm control --reload`


## TODO

- Use the addressable RGB LED to display information

# Build

Requisites:

- Zephyr SDK 1.0.1

- Zephyr 4.4

- Python 3.12 or newer, git, CMake, Ninja and the other
  [Zephyr host dependencies](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies)

## Set up the workspace

Zeroprobe is a [west](https://docs.zephyrproject.org/latest/develop/west/index.html)
manifest repository: `west.yml` pins Zephyr and the modules it needs
(`cmsis_6` and `hal_rpi_pico`), so a single `west init` creates the whole
workspace.

```bash
$ python3 -m venv zeroprobe-ws/.venv
$ source zeroprobe-ws/.venv/bin/activate
$ pip install west

$ west init -m https://github.com/icpmoles/zeroprobe zeroprobe-ws
$ cd zeroprobe-ws
$ west update
$ west packages pip --install
$ west sdk install -t arm-zephyr-eabi
```

This creates the following layout, the same one used by the CI:

```
zeroprobe-ws/
├── .venv/        # Python virtual environment with west
├── .west/        # workspace config, created by west init
├── external/     # Zephyr and modules, fetched by west update
└── zeroprobe/    # this repository
```

If you already cloned the repository, run `west init -l zeroprobe` from its
parent directory instead of `west init -m ...`, then continue with
`west update`.

To build a branch other than `main`, add `--mr <branch>` to `west init -m`.

## Compile

The board (`rp2040_zero/rp2040`) is already selected in `CMakeLists.txt`:

```bash
$ cd zeroprobe
$ west build
```

The firmware is written to `build/zephyr/zephyr.uf2`, flash it as described in
[How to use](#how-to-use), or run `west flash --runner=uf2` while the board is in BOOT mode.

Use `west build -p` for a pristine build, e.g. after changing `west.yml` or
switching branch.

## Configure

Options can be changed in `prj.conf`, in `boards/rp2040_zero.conf`, or
interactively with `west build -t menuconfig`.

The status LED is set in `boards/rp2040_zero.conf`:

| Option                              | Default    | Meaning                                    |
|-------------------------------------|------------|--------------------------------------------|
| `CONFIG_APP_LED_COLOR_UNCONFIGURED` | `0x200000` | Not configured by a USB host (red)         |
| `CONFIG_APP_LED_COLOR_CONFIGURED`   | `0x000020` | Configured by the host, no traffic (blue)  |
| `CONFIG_APP_LED_COLOR_ACTIVE`       | `0x002000` | USB traffic (green)                        |
| `CONFIG_APP_LED_IDLE_TIMEOUT_MS`    | `500`      | Idle time before going back to configured  |

Colors are `0xRRGGBB`, keep the values low: the WS2812 is very bright.

# Credits

- [Zephyr RTOS](https://www.zephyrproject.org/)

- [DAPLink](https://github.com/armmbed/daplink)

- Images created with [Fritzing](https://fritzing.org/)

- Boards footprint created by Vanepp: [RP2040-Zero](https://forum.fritzing.org/t/part-request-waveshare-rp2040-zero/16705/2) & [RPi Pico](https://forum.fritzing.org/t/looking-for-raspberry-pi-pico-part/11915/19) 

- Favicon from "Noto Emoji Font"

# Footnotes