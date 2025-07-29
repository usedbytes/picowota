# `picowota` - Raspberry Pi Pico W OTA bootloader

> `picowota`, kinda sounds like you're speaking [Belter](https://expanse.fandom.com/wiki/Belter)

This project implements a bootloader for the Raspberry Pi Pico W which allows
upload of program code over WiFi ("Over The Air").

The easiest way to use it is to include this repository as a submodule in the
application which you want to be able to update over WiFi.

There's an example project using picowota at https://github.com/usedbytes/picowota_blink

The simplest way to build `picowota` by itself is:

```
mkdir build
cd build
export PICOWOTA_WIFI_SSID=picowota
export PICOWOTA_WIFI_PASS=password
export PICOWOTA_WIFI_AP=1
cmake -DPICO_BOARD=pico_w -DPICO_SDK_PATH=/your/path/to/pico-sdk ../
make
```

## Using in your project

First add `picowota` as a submodule to your project:
```
git submodule add https://github.com/usedbytes/picowota
git submodule update --init picowota
git commit -m "Add picowota submodule"
```

Then modifiy your project's CMakeLists.txt to include the `picowota` directory:

```
add_subdirectory(picowota)
```

`picowota` either connects to an existing WiFi network (by default) or
creates one, in both cases with the given SSID and password.

You can either provide the following as environment variables, or set them
as CMake variables:

```
PICOWOTA_WIFI_SSID # The WiFi network SSID
PICOWOTA_WIFI_PASS # The WiFi network password
PICOWOTA_WIFI_AP # Optional; 0 = connect to the network, 1 = create it
PICOWOTA_ENTRY_PIN # Optional; default use pin 15, you can change it
```

Then, you can either build just your standalone app binary (suitable for
updating via `picowota` when it's already on the Pico), or a combined binary
which contains the bootloader and the app (suitable for flashing the first
time):

```
picowota_build_standalone(my_executable_name)
picowota_build_combined(my_executable_name)
```

Note: The combined target will also build the standalone binary.

To be able to update your app, you must provide a way to return to the
bootloader. By default, if GPIO15 is pulled low at boot time, then `picowota`
will stay in bootloader mode, ready to receive new app code.

You can also return to the bootloader from your app code - for example when a
button is pressed, or in response to some network request. The
`picowota_reboot` library provides a `picowota_reboot(bool to_bootloader)`
function, which your app can call to get back in to the bootloader.

```
CMakeLists.txt:

target_link_libraries(my_executable_name picowota_reboot)

your_c_code.c:

#include "picowota/reboot.h"

...

{

	...

	if (should_reboot_to_bootloader) {
		picowota_reboot(true);
	}

	...

}
```

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

## How it works

This is derived from my Pico non-W bootloader, https://github.com/usedbytes/rp2040-serial-bootloader, which I wrote about in a blog post: https://blog.usedbytes.com/2021/12/pico-serial-bootloader/

The bootloader code attempts to avoid "bricking" by storing a CRC of the app
code which gets uploaded. If the CRC doesn't match, then the app won't get run
and the Pico will stay in `picowota` bootloader mode. This should make it fairly
robust against errors in transfers etc.

## Known issues

### Bootloader/app size and `cyw43` firmware

The WiFi chip on a Pico W needs firmware, which gets built in to any program
you build with the Pico SDK. This is relatively large - 300-400 kB, which is why
this bootloader is so large.

This gets duplicated in the `picowota` bootloader binary and _also_ the app
binary, which obviously uses up a significant chunk of the Pico's 2 MB flash.

It would be nice to be able to avoid this duplication, but the Pico SDK
libraries don't give a mechanism to do so.

I've raised https://github.com/raspberrypi/pico-sdk/issues/928 for consideration.
