# `picowota` - Raspberry Pi Pico W OTA bootloader

> `picowota`, kinda sounds like you're speaking [Belter](https://expanse.fandom.com/wiki/Belter)

This project implements a bootloader for the Raspberry Pi Pico W for the OpenTrickler Project

credits to: @usedbytes / https://github.com/usedbytes/picowota

OpenTrickler: https://github.com/dirtbit/OpenTrickler-RP2040-Controller, developed by @earms:
https://github.com/eamars/OpenTrickler-RP2040-Controller


## Uploading code via `picowota`

Once you've got the `picowota` bootloader installed on your Pico, you can use
the https://github.com/usedbytes/serial-flash tool to upload code to it.

As long as the Pico is "in" the `picowota` bootloader (i.e. because there's no
valid app code uploaded yet, or your app called `picowota_reboot(true);`), you
can upload an app `.elf` file which was built by `picowota_build_standalone()`:

If using the AP mode, the Pico's IP address will be (at the time of writing)
192.168.4.1/24, and the connected device's something in the same subnet.
Otherwise it depends on your network settings.

(Assuming your Pico's IP address is 192.168.1.123):
```
serial-flash tcp:192.168.1.123:4242 my_executable_name.elf
```

After uploading the code, if successful, the Pico will jump to the newly
uploaded app.
