# led_blink - Blink an External LED with GPIO sysfs

A C application that blinks an LED on **GPIO17** of the Raspberry Pi 3 B+ by writing to the Linux
**GPIO sysfs** interface (`/sys/class/gpio`). Cross-compiled in Eclipse with the Arm GNU Toolchain 15.2.Rel1
from the Buildroot image, deployed over SSH.

```
GPIO17 -> sysfs gpio529 (base 512)
Blinking - press Ctrl+C to stop
^C
LED off, GPIO released
```

---

## What it shows

| Concept | Where in the code |
|---|---|
| Hardware control through **sysfs files** | `write_str()` writes `export`, `direction`, `value` |
| GPIO **controller base** on newer kernels (GPIO17 = 512 + 17 = **529**) | `find_gpio_base()` reads `gpiochip*/label` and `base` instead of hard-coding 529 |
| **Clean shutdown** of hardware | `SIGINT`/`SIGTERM` handler: LED off, pin unexported |
| Error handling on sysfs writes | `fclose()` is checked - sysfs reports errors when the write completes |
| Cross-compilation for ARM | Eclipse Cross GCC, `arm-none-linux-gnueabihf-` |

---

## Hardware

| Part | Value |
|---|---|
| LED | Any colour - long leg = anode (+), short leg = cathode (−) |
| Resistor | 330 Ω (220 Ω - 1 kΩ is fine) - limits the current |
| Wires | 2 female-to-male jumpers |

### Wiring

```
Pi pin 11 (GPIO17) ──── 330 Ω ──── LED long leg (+)
                                   LED short leg (−) ──── Pi pin 9 (GND)
```

```
SD card end of the board
   ┌──────────────────────────────────────
   │ Pin 2   4   6   8  10  12 ...   outer row (board edge)
   │ Pin 1   3   5   7   9  11 ...   inner row
   └──────────────────────────────────────
                       ▲   ▲
                     GND   GPIO17
```

- GPIO pins are **3.3 V** - never connect 5 V to them, always use the resistor.
- The serial console adapter uses pins 6, 8, 10 (outer row) - no conflict.
- LED does not light → turn it around.

---

## How it works

```
led_blink (user space)
   │  fopen / fputs / fclose
   ▼
/sys/class/gpio/export                  "529"  → kernel creates /sys/class/gpio/gpio529/
/sys/class/gpio/gpio529/direction       "out"
/sys/class/gpio/gpio529/value           "1" / "0"
/sys/class/gpio/unexport                "529"  → folder removed
   │
   ▼
GPIO sysfs (kernel)  →  pinctrl-bcm2835 driver  →  GPSET0 / GPCLR0 registers  →  pin 11  →  LED
```

### Why 529 and not 17?

`/sys/class/gpio` uses global numbers. The Pi 3's GPIO controller registers at **base 512** on recent kernels:

```
# cat /sys/class/gpio/gpiochip512/label
pinctrl-bcm2835
# cat /sys/class/gpio/gpiochip512/base
512
```

So BCM GPIO17 is sysfs **529**. The program finds the base by itself, so it keeps working if the base changes.

### Program flow

| Step | Code | Same as shell command |
|---|---|---|
| 1. Find base | `find_gpio_base()` | `cat /sys/class/gpio/gpiochip*/label` |
| 2. Export | write `529` to `export` | `echo 529 > /sys/class/gpio/export` |
| 3. Output | write `out` to `direction` | `echo out > gpio529/direction` |
| 4. Blink | write `1` / `0` to `value` | `echo 1 > gpio529/value` |
| 5. Clean up | LED off, write `529` to `unexport` | `echo 529 > /sys/class/gpio/unexport` |

The signal handler only sets a flag (`running = 0`); the cleanup runs in `main()`. This is the safe pattern - only
async-signal-safe work inside a signal handler.

---

## Build (Eclipse)

| Setting | Value |
|---|---|
| Project | File → New → Other → C/C++ → **C Project** → Executable → Empty Project |
| Location | `~/Rpi_Workspace/rpi3-embedded-linux/apps/led_blink` |
| Toolchain | **Cross GCC** |
| Cross compiler prefix | `arm-none-linux-gnueabihf-` |
| Cross compiler path | `~/Rpi_Workspace/br-rpi3/host/bin` |

**Ctrl+B**, then check:

```bash
file ~/Rpi_Workspace/rpi3-embedded-linux/apps/led_blink/Debug/led_blink
# ELF 32-bit LSB pie executable, ARM, EABI5 ... dynamically linked, interpreter /lib/ld-linux-armhf.so.3
```

### Change the blink speed

```c
#define ON_MS      100
#define OFF_MS     900
```

---

## Run on the Pi

```bash
cd ~/Rpi_Workspace/rpi3-embedded-linux/apps/led_blink/Debug
scp led_blink root@192.168.7.3:/root/
ssh -t root@192.168.7.3 /root/led_blink
```

`ssh -t` gives the program a terminal, so **Ctrl+C** reaches it on the Pi and the cleanup runs.

One command with the `rpi-run` helper (see [docs/eclipse-setup.md](../../docs/eclipse-setup.md)):

```bash
rpi-run led_blink
```

### Run in the background

```bash
ssh root@192.168.7.3 'nohup /root/led_blink > /var/log/led_blink.log 2>&1 &'
ssh root@192.168.7.3 killall led_blink        # SIGTERM → LED off, pin released
```

---

## Test without the program

Wiring check from the shell:

```bash
ssh root@192.168.7.3 'echo 529 > /sys/class/gpio/export; echo out > /sys/class/gpio/gpio529/direction; echo 1 > /sys/class/gpio/gpio529/value'
ssh root@192.168.7.3 'echo 0 > /sys/class/gpio/gpio529/value; echo 529 > /sys/class/gpio/unexport'
```

No LED connected? Read the pin while the program runs:

```bash
ssh root@192.168.7.3 cat /sys/class/gpio/gpio529/value      # alternates between 1 and 0
```

---

## Troubleshooting

| Message | Cause | Fix |
|---|---|---|
| `/sys/class/gpio/export: Device or resource busy` | Pin still exported (earlier test, or a crash) | `ssh root@192.168.7.3 'echo 529 > /sys/class/gpio/unexport'` |
| `.../direction: No such file or directory` | Kernel had not created `gpio529/` yet | Increase `usleep(100000)` after export |
| `BCM2835 GPIO controller not found` | Kernel without GPIO sysfs (`CONFIG_GPIO_SYSFS`) | Enable it, or use libgpiod |
| Runs, LED dark | Wiring | Pin 11 / pin 9, resistor, LED direction |
| `scp: ... sftp-server: not found` | No SFTP server on the Pi | `scp -O`, or add `gesftpserver` / OpenSSH to the image |

---

## sysfs vs libgpiod

The GPIO sysfs interface is what the course teaches - simple and transparent, but **deprecated** in the kernel.

| | GPIO sysfs (this app) | libgpiod (`/dev/gpiochip0`) |
|---|---|---|
| Interface | Text files | C library + character device |
| Pin numbers | Global (529) | Chip + line (`gpiochip0`, line 17) |
| Program crashes | Pin stays exported | Released automatically |
| Inputs / buttons | Polling or `poll()` on `value` | Edge events with kernel timestamps |
| Status | Deprecated | Current standard |

Next step: the same blinker with libgpiod, then a button on another GPIO.

Background: [docs/gpio-and-pinctrl-concepts.md](../../docs/gpio-and-pinctrl-concepts.md)
