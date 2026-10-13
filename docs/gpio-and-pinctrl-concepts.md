# GPIO, pinctrl and the Device Tree on the Raspberry Pi 3 B+ - Concepts Q&A

Questions I worked through while doing the course's GPIO and LED sections on the Raspberry Pi 3 B+,
with the answers and the commands that prove them on the board.

---

## The big picture

```
        CONFIGURATION (at boot)                     VIEWING / USING (at runtime)

  .dts source file  (+ overlays in config.txt)
        │ compiled by dtc
        ▼
  .dtb  ──► firmware ──► kernel reads it ──────────►  /proc/device-tree
                              │                        "what the device tree SAYS"
                              ▼
                    pinctrl subsystem + driver ─────►  /sys/kernel/debug/pinctrl
                    (pinctrl-bcm2835)                  "what pinctrl actually DID"
                              │ writes
                              ▼
                    GPFSEL / pull registers
                    in the BCM2837 hardware
                              │
                              ▼
                       physical pins  ◄─────────────  /sys/class/gpio, libgpiod
                                                       "USE a pin as GPIO"
```

---

## Q1. Where does the processor live?

Inside the **SoC** - the **BCM2837B0**, the square chip under the metal lid in the centre of the board.

```
            BCM2837B0 SoC
   ┌────────────────────────────────────────────────────────┐
   │  ARM Cortex-A53 x 4 (CPU)       VideoCore IV GPU       │
   │  GPIO block (54 pins)   UARTs   I2C   SPI   PWM        │
   │  USB controller   SD/MMC controller   timers           │
   │  interrupt controller   memory controller   SRAM       │
   └────────────────────────────────────────────────────────┘
```

| Chip on the board | Location | Job |
|---|---|---|
| BCM2837B0 SoC | Centre, under the metal lid | CPU, GPU, GPIO, UART, I2C, SPI... |
| 1 GB LPDDR2 DRAM | Underside of the board | Main memory |
| LAN7515 | Near USB/Ethernet | USB hub + Ethernet (why `eth0` appears late at boot) |
| Wi-Fi/Bluetooth module | Under a small shield | Wireless |
| Power management IC | Near the power input | Board voltages |

```
# cat /proc/cpuinfo                                  # 4 cores, CPU part 0xd03 = Cortex-A53
# nproc                                              # 4
# cat /proc/device-tree/cpus/cpu@0/compatible; echo  # arm,cortex-a53
```

The image is 32-bit (`armv7l`), so the 64-bit-capable Cortex-A53 runs in 32-bit mode.
(The UDOO showed `CPU part 0xc09` = Cortex-A9.)

---

## Q2. SoC + eMMC + memory = SoM?

Yes - with one correction: a **SoM** combines the SoC with **DRAM** (not SRAM), eMMC storage and power
management on a small module that plugs into a **carrier board**.

| Level | Contains | Example |
|---|---|---|
| **SoC** | One chip: CPU, GPU, GPIO, UART..., small internal **SRAM** | BCM2837, i.MX6Q, Zynq-7000 |
| **SoM** | SoC + **DRAM** + **eMMC** + PMIC on a module | Raspberry Pi Compute Module 3, i.MX SoMs, Xilinx Kria K26 |
| **Carrier board** | The product's connectors and circuits; the SoM plugs in | Custom product board |
| **SBC** | Everything soldered on one board | Raspberry Pi 3 B+, UDOO Quad, BeagleBone |

| | SRAM | DRAM |
|---|---|---|
| Where | Inside the SoC | Separate chip(s) |
| Size | Kilobytes | 1 GB on the Pi and the UDOO |
| Usable at power-on | Yes | No - must be initialised first |
| Used for | Early boot code, caches | Linux and programs |

Seen in practice on the UDOO: **SPL runs in on-chip SRAM (OCRAM)** because DDR is not usable until SPL initialises it.

Companies use SoMs because the SoC + DDR + power design is the hardest part of a board; the product team then designs a
simpler carrier board. My Pi build even contains a SoM device tree: `bcm2710-rpi-cm3.dtb` (Compute Module 3).

---

## Q3. Does the SoC handle the GPIOs?

Yes. The **GPIO controller is a peripheral inside the SoC**, like the UART or I2C controllers. The CPU controls it
through **memory-mapped registers** - addresses that look like memory but drive hardware.

```
 C program: gpiod_line_set_value(line, 1)
   → kernel driver pinctrl-bcm2835 (runs on the CPU)
     → writes bit 17 of GPSET0 in the GPIO block
       → GPIO17 pin goes to 3.3 V
         → header pin 11 → 330 Ω → LED lights
```

| Function | Registers |
|---|---|
| Pin mode (input / output / ALT0-5) | GPFSEL0-5 (3 bits per pin, 10 pins per register) |
| Output high / low | GPSET0-1 / GPCLR0-1 |
| Read input | GPLEV0-1 |
| Edge detection / interrupts | GPEDS, GPREN, GPFEN... |
| Pull-up / pull-down | GPPUD, GPPUDCLK |

---

## Q4. Do all GPIOs connect to the SoC?

All **54 GPIOs (0-53) are pins of the BCM2837**. The board decides where each one goes:

| GPIOs | Routed to |
|---|---|
| **2-27** | **40-pin header** - for projects |
| 0-1 | Header pins 27/28 - reserved for the HAT ID EEPROM |
| 28-53 | Internal: Wi-Fi/Bluetooth, SD card, onboard LEDs |

Header = 26 GPIO (2-27) + 2 GPIO (0-1) + 2 × 3.3 V + 2 × 5 V + 8 × GND = 40 pins.

Every SoC board works this way - which is why each **board** needs its own **device tree** describing how it wires
the SoC's pins.

---

## Q5. How many GPIO groups does the Pi 3 have?

Linux shows **one** controller for all 54 SoC GPIOs, plus a small firmware expander:

```
# ls /sys/class/gpio/
export       gpiochip512  gpiochip566  unexport
# cat /sys/class/gpio/gpiochip512/label     → pinctrl-bcm2835   (ngpio 54)
# cat /sys/class/gpio/gpiochip566/label     → raspberrypi-exp-gpio   (ngpio 8)
```

Inside the hardware the 54 GPIOs form **two banks** (registers are 32 bits): bank 0 = GPIO 0-31 (`GPSET0`...),
bank 1 = GPIO 32-53 (`GPSET1`...). The driver presents both as one `gpiochip`.

| | BeagleBone Black (course) | Raspberry Pi 3 B+ |
|---|---|---|
| GPIO controllers in Linux | 4 (`gpiochip0/32/64/96`), 32 lines each | 1 (`gpiochip512`), 54 lines |
| Pin numbering | `GPIO1_28` → 1 × 32 + 28 = **60** | `GPIO17` → 512 + 17 = **529** |

Good free header pins for LEDs and buttons: **GPIO 17, 22, 23, 24, 25, 27**.
Keep free when needed: GPIO 2/3 (I2C), 14/15 (serial console), 7-11 (SPI).

---

## Q6. Why is the GPIO block called "bcm2835" on a BCM2837?

Broadcom kept the **same GPIO hardware block** across BCM2835 (Pi 1), BCM2836 (Pi 2) and BCM2837 (Pi 3).
Linux drivers are named after the first chip that had the hardware, so one driver - `pinctrl-bcm2835` - serves them all.
(Same on the UDOO: the i.MX6 serial driver is `imx`/`mxc`, hence `ttymxc1`.)

```
# cat /proc/device-tree/soc/gpio@7e200000/compatible; echo
brcm,bcm2835-gpio
```

| View | GPIO block address |
|---|---|
| Bus address (device tree, Broadcom datasheet) | `0x7E200000` |
| ARM physical address on the Pi 3 | `0x3F200000` |

---

## Q7. What is the raspberrypi-exp-gpio block?

An **8-line GPIO expander controlled by the GPU firmware** - not part of the SoC's GPIO block, not on the header.
Linux cannot reach it directly; the driver asks the firmware through the **mailbox**:

```
Linux driver ──mailbox──► VideoCore firmware ──► expander chip ──► board signals (power LED, radios, camera...)
```

```
# gpioinfo gpiochip1          # names and users of the 8 lines
```

| | `gpiochip512` | `gpiochip566` |
|---|---|---|
| Hardware | GPIO block inside the BCM2837 | Expander via firmware |
| Access | Direct registers (fast) | Mailbox messages (slow) |
| On the header | Yes | No - leave alone |

---

## Q8. Can I see each GPIO separately, like `gpio17`?

A GPIO gets its own folder only after **export**, and the folder uses the **sysfs number** (base + GPIO):

```
# cd /sys/class/gpio
# echo 529 > export                  # GPIO17
# ls                                  # gpio529 appears
# echo out > gpio529/direction
# echo 1 > gpio529/value
# echo 0 > gpio529/value
# echo 529 > unexport
```

| File | Values |
|---|---|
| `direction` | `in` / `out` |
| `value` | `0` / `1` |
| `active_low` | `1` inverts value |
| `edge` | `none`, `rising`, `falling`, `both` (inputs) |

Do not export all 54 - pins used by drivers return `Device or resource busy`.

See **all** GPIOs individually without exporting:

```
# gpioinfo gpiochip0                   # name, user, direction of each of the 54 lines
# cat /sys/kernel/debug/gpio           # current values, e.g. gpio-529 (GPIO17 |sysfs) out lo
```

The GPIO sysfs interface is **deprecated**; the modern interface is the character device `/dev/gpiochip0` used through
**libgpiod** (`gpioset gpiochip0 17=1`), which uses plain GPIO numbers.

---

## Q9. What is `/sys/kernel/debug`, and what does "mount" mean?

**debugfs** is a virtual filesystem for kernel developers. Like `/proc` and `/sys`, its files are generated by the
kernel when read - nothing is stored on the SD card.

| Filesystem | Mounted at | Purpose | Stable for programs? |
|---|---|---|---|
| procfs | `/proc` | Processes, system info | Yes |
| sysfs | `/sys` | Devices and drivers as files | Yes |
| **debugfs** | `/sys/kernel/debug` | Kernel internals for debugging | **No** - may change between kernels |

**Mounting** attaches a filesystem to a folder so its contents appear there:

```
# mount -t debugfs none /sys/kernel/debug
```

| Part | Meaning |
|---|---|
| `-t debugfs` | Filesystem type |
| `none` | No real device behind it (virtual) |
| `/sys/kernel/debug` | Where it appears |

Lasts until reboot. Permanent: add to `/etc/fstab` (or the Buildroot overlay):

```
debugfs  /sys/kernel/debug  debugfs  defaults  0  0
```

---

## Q10. What is pinctrl, and what does `/sys/kernel/debug/pinctrl` show?

**pinctrl** (pin control) is the kernel subsystem for **pin multiplexing and electrical settings** - the modern,
organised handling of the course's "mode configuration registers":

1. The **device tree** describes which pins each device needs and how (function, pull).
2. A driver (e.g. UART) starting up asks pinctrl for its pins.
3. The chip's **pinctrl driver** (`pinctrl-bcm2835`) writes GPFSEL and pull registers.
4. pinctrl **prevents conflicts** - two drivers cannot claim the same pin.

```
# cd /sys/kernel/debug/pinctrl/3f200000.gpio-pinctrl-bcm2835
# cat pinmux-pins | head -30
pin 14 (gpio14): 3f201000.serial (GPIO UNCLAIMED) function alt0 group gpio14
pin 15 (gpio15): 3f201000.serial (GPIO UNCLAIMED) function alt0 group gpio15
pin 17 (gpio17): (MUX UNCLAIMED) (GPIO UNCLAIMED)
```

| File | Shows |
|---|---|
| `pins` | All 54 pins |
| `pinmux-functions` | Available functions: `gpio_in`, `gpio_out`, `alt0`-`alt5` |
| **`pinmux-pins`** | **Current function of each pin and its owner** |
| `pinconf-pins` | Pull-up / pull-down settings |
| `gpio-ranges` | Pin numbers ↔ GPIO numbers (pin 17 → gpio 529) |

`pin 14 ... 3f201000.serial ... alt0` = GPIO14 in ALT0 mode, owned by the serial driver (my console, via the
`miniuart-bt` overlay). `pin 17 ... UNCLAIMED` = free.

---

## Q11. See the pin mode register directly (the course's "mode configuration registers")

GPFSEL registers on the Pi 3 (ARM physical addresses):

| Register | Address | Pins |
|---|---|---|
| GPFSEL0 | `0x3F200000` | GPIO 0-9 |
| **GPFSEL1** | **`0x3F200004`** | **GPIO 10-19** |
| GPFSEL2 | `0x3F200008` | GPIO 20-29 |

GPIO17 → GPFSEL1, bits 21-23 ((17 − 10) × 3 = 21). Values: `000` input, `001` output, `100` ALT0...

```
# devmem 0x3F200004              # before
# gpioset gpiochip0 17=1
# devmem 0x3F200004              # after: bits 21-23 become 001 (output)
```

Read only - writing wrong values can crash the board. If `/dev/mem` access is blocked, use `pinmux-pins` instead.

---

## Q12. If I change something in debugfs, does the device tree change? How do I change pins at device tree level?

**No.** Information flows one way: device tree → kernel/pinctrl → registers → debugfs reports the result.
`/proc/device-tree` and debugfs pinctrl are views. `/sys/class/gpio` and libgpiod change a pin **for GPIO use only,
until reboot**.

The **device tree is read at boot**, so permanent pin changes go into the device tree:

**a) `config.txt`** (no compiling):

```
dtparam=i2c_arm=on                        # I2C on GPIO 2/3
dtoverlay=gpio-led,gpio=17,label=myled    # GPIO17 becomes /sys/class/leds/myled
gpio=17=op,dh                             # firmware: GPIO17 output, high at boot
```

**b) Own overlay** - GPIO17 as output with pull-down:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2835";

    fragment@0 {
        target = <&gpio>;
        __overlay__ {
            myled_pins: myled_pins {
                brcm,pins     = <17>;   /* GPIO17 */
                brcm,function = <1>;    /* 0 = input, 1 = output, 4 = ALT0 ... */
                brcm,pull     = <1>;    /* 0 = none, 1 = pull-down, 2 = pull-up */
            };
        };
    };
};
```

`brcm,function = <1>` is the value that ends up in GPIO17's 3 bits of GPFSEL1 (`001`) - the device tree is the
readable layer above the register.

```bash
dtc -@ -I dts -O dtb -o myled.dtbo myled.dts
# copy myled.dtbo to the SD card's overlays/ folder, add "dtoverlay=myled" to config.txt
```

**c) Edit the board `.dts`** and rebuild with Buildroot (most permanent; on the Pi, overlays are preferred).

| You want to... | Use | Lasts |
|---|---|---|
| See what the device tree says | `/proc/device-tree` | - |
| See each pin's mode and owner | `/sys/kernel/debug/pinctrl/.../pinmux-pins` | - |
| Use a free pin as GPIO now | `/sys/class/gpio`, `gpioset`, libgpiod | Until reboot |
| Change function / pull permanently | Device tree: `config.txt` → overlay → `.dts` | Permanent |

---

## Q13. Is there a device tree on the Pi, and how is it different from the UDOO?

Yes - `bcm2710-rpi-3-b-plus.dtb`, loaded by the **GPU firmware** (`start.elf`), not by U-Boot.

| Stage | What happens |
|---|---|
| Source | `arch/arm/boot/dts/broadcom/bcm2710-rpi-3-b-plus.dts` (Raspberry Pi kernel) |
| Layers | `bcm2710-rpi-3-b-plus.dts` → `bcm2710.dtsi` (BCM2837) → `bcm283x.dtsi` (whole family) |
| Boot | Firmware picks the `.dtb` for the board revision, applies `config.txt` overlays, fills in memory and MAC |
| Runtime | `/proc/device-tree` |

```bash
# laptop: source and compiled tree
cd ~/Rpi_Workspace/br-rpi3/build/linux-*/arch/arm/boot/dts/broadcom/
dtc -I dtb -O dts ~/Rpi_Workspace/br-rpi3/images/bcm2710-rpi-3-b-plus.dtb -o ~/rpi3-b-plus.dts

# the tree the kernel really received (after firmware changes), compared with the file
scp root@192.168.7.3:/sys/firmware/fdt ~/rpi3-live.dtb
dtc -I dtb -O dts ~/rpi3-live.dtb -o ~/rpi3-live.dts
```

```
# Pi: live tree
# cat /proc/device-tree/model; echo        # Raspberry Pi 3 Model B Plus Rev 1.3
# ls /proc/device-tree/soc/
```

| | UDOO | Raspberry Pi 3 B+ |
|---|---|---|
| Device tree | `imx6q-udoo.dtb` | `bcm2710-rpi-3-b-plus.dtb` |
| Loaded by | U-Boot (`tftp` + `bootz`) | GPU firmware (`start.elf`) |
| Customising | Edit `.dts`, recompile | Overlays + `dtparam` in `config.txt` |
| Live view | `/proc/device-tree` | `/proc/device-tree` |
