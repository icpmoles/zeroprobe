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

# Credits

- [Zephyr RTOS](https://www.zephyrproject.org/)

- [DAPLink](https://github.com/armmbed/daplink)

- Images created with [Fritzing](https://fritzing.org/)

- Boards footprint created by Vanepp: [RP2040-Zero](https://forum.fritzing.org/t/part-request-waveshare-rp2040-zero/16705/2) & [RPi Pico](https://forum.fritzing.org/t/looking-for-raspberry-pi-pico-part/11915/19) 

- Favicon from "Noto Emoji Font"

# Footnotes