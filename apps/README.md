# apps/

C applications for the Raspberry Pi 3 B+, cross-compiled in Eclipse (Cross GCC, `arm-none-linux-gnueabihf-`)
with the toolchain from the Buildroot image, and deployed over SSH.

| App | What it does | Concepts |
|---|---|---|
| [hello_rpi](hello_rpi/) | Prints a message on the Pi | Cross-compiling, deploy with `scp`/`ssh`, debugging with gdbserver |
| [led_blink](led_blink/) | Blinks an external LED on GPIO17 | GPIO sysfs (`export`, `direction`, `value`), controller base 512, clean shutdown on Ctrl+C |

Planned: `user_led` (onboard ACT/PWR LEDs via the LED class `/sys/class/leds`), libgpiod blinker, button input,
QR code scanning station.

## Build and run any app

```bash
# build: Eclipse → select the project → Ctrl+B
cd ~/Rpi_Workspace/rpi3-embedded-linux/apps/<app>/Debug
file <app>                                   # ELF 32-bit ... ARM, EABI5
scp <app> root@192.168.7.3:/root/
ssh -t root@192.168.7.3 /root/<app>
```

Build outputs (`Debug/`, `Release/`) are not committed - see `.gitignore`.

Setup details: [docs/eclipse-setup.md](../docs/eclipse-setup.md)
