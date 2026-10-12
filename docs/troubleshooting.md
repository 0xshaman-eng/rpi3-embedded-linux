# Troubleshooting

Real problems met while setting up the Raspberry Pi 3 B+ with Buildroot, with cause and fix.
Format: **Symptom → Cause → Fix → Lesson**.

---

## Workspace and configuration

### One Buildroot folder, two boards (UDOO and Pi)

- **Symptom:** the Pi settings were made inside the Buildroot source folder, which also held an older build.
- **Cause:** building without `O=` puts `.config` and `output/` inside the source - only one board possible.
- **Fix:**
  1. Kept the downloads: `mv dl ~/Tools/buildroot-dl`
  2. Removed the old, incompatible output: `rm -rf output`
  3. Moved the source: `mv ~/Downloads/buildroot-2026.08 ~/Tools/`
  4. Moved the Pi settings to the Pi folder and initialised it:
     ```bash
     mkdir -p ~/Rpi_Workspace/br-rpi3
     mv ~/Tools/buildroot-2026.08/.config ~/Rpi_Workspace/br-rpi3/.config
     cd ~/Tools/buildroot-2026.08 && make O=~/Rpi_Workspace/br-rpi3 olddefconfig
     ```
- **Lesson:** one shared source + one `O=` output folder per board. Move the source only **before** the first build -
  a built output contains absolute paths.

### Old build output could not be reused

- **Symptom:** `output/images` contained only `rootfs.ext2/ext4/tar` from an earlier build.
- **Cause:** it was built with a different configuration. Changing Target options or the toolchain needs a full rebuild.
- **Fix:** delete `output/`, keep `dl/`.

### Download dir still the default

- **Symptom:** `BR2_DL_DIR="$(TOPDIR)/dl"` after moving the downloads.
- **Cause:** `$(TOPDIR)` is the Buildroot source folder, not the shared cache.
- **Fix:** Build options → Download dir → `/home/shaman/Tools/buildroot-dl`.

### Root filesystem overlay path - folder did not exist

- **Cause:** the overlay folder must be created before Buildroot can use it.
- **Fix:** `mkdir -p ~/Rpi_Workspace/rpi3-embedded-linux/overlay/etc/network`, add `interfaces`,
  then enter the **full path** in menuconfig.
- **Check:** after the build, `cat ~/Rpi_Workspace/br-rpi3/target/etc/network/interfaces`.

### libgpiod not found under Hardware handling

- **Cause:** it is a library: **Target packages → Libraries → Hardware handling**.
- **Fix:** `/` → `LIBGPIOD` → press the number shown. Enable **install tools** below it too.

### "Build cross gdb for the host" cannot be selected

- **Symptom:** search `/` → `HOST_GDB` finds it, but jumping to it shows nothing.
- **Cause:** a "Depends on" condition is not met - with the external Arm toolchain the option is hidden.
- **Not a problem:** the Arm toolchain includes `arm-none-linux-gnueabihf-gdb`. Fallback: `sudo apt install gdb-multiarch`.

### savedefconfig: Error while saving defconfig

```
*** Error while saving defconfig to: .../configs/rpi3_custom_defconfig
```

- **Cause:** the `configs/` folder did not exist.
- **Fix:** `mkdir -p ~/Rpi_Workspace/rpi3-embedded-linux/configs`, run again.

---

## SD card

### Card had other partitions (boot, root, var)

- **Cause:** a previous system image.
- **Fix:** unmount **all** partitions before `dd`: `sudo umount /dev/sda1 /dev/sda2 /dev/sda3`.
  Writing `sdcard.img` replaces everything.

### Only 32 MB + 120 MB partitions on a 7.4 GB card

- **Normal.** The rootfs size is set in the config (`BR2_TARGET_ROOTFS_EXT2_SIZE="120M"`).
  Increase it in **Filesystem images → exact size** if packages no longer fit.

---

## Serial console

### picocom: FATAL: cannot lock /dev/ttyUSB0

- **Cause:** another program (usually another picocom window) has the port open.
- **Fix:** close it (`Ctrl+A`, `Ctrl+X`), or `sudo fuser -v /dev/ttyUSB0` → `sudo kill <PID>`, or re-plug the adapter.
  If `fuser` shows `ModemManager`: `sudo systemctl stop ModemManager`.

### Screen full of garbage characters

Checked in this order:

1. **UART configuration on the card** - was already correct:
   `config.txt` has `dtoverlay=miniuart-bt`, `cmdline.txt` has `console=ttyAMA0,115200`.
   (On a Pi 3 without this, the GPIO pins use the mini UART, whose speed drifts with the GPU clock.)
2. **Wiring / noise** - the real cause. Mostly readable text with garbage bursts, and junk *after* the login
   prompt when the Pi sends nothing, means electrical noise:
   - GND firmly on pin 6 (pin 9 is also GND)
   - RXD on pin 8, TXD on pin 10 - outer row, counted from the SD card end
   - FT232R jumper on **3.3 V**
   - short jumper wires, away from the power cable

Useful tests:

| Test | How | Tells you |
|---|---|---|
| Pi off | Unplug Pi power with picocom open | Garbage continues → noise/wiring |
| Loopback | Adapter TXD → its own RXD, type in picocom | Adapter + picocom OK |
| Other speeds | `Ctrl+A Ctrl+U` / `Ctrl+A Ctrl+D` in picocom | Readable at another speed → UART clock problem |
| Network | `ping 192.168.7.3`, `ssh root@192.168.7.3` | Pi boots fine regardless of serial |

- **Lesson:** the serial port is only a monitor - SSH can be used while the serial link is being fixed.

---

## Login and SSH

### `ls` shows nothing after login

- **Normal.** You start in `/root`, which is empty. Use `ls /`.

### Which username?

- **`root`.** `shaman` in `shaman login:` is the hostname. The password is not echoed while typing.

### ssh: Connection refused

- **Meaning:** network OK, but no SSH server listening on port 22.
- **Fix:** enable **dropbear**, set a **root password**, rebuild, re-flash.
- **Check on the Pi:** `ps | grep dropbear`, `netstat -tln` → `:22 LISTEN`.

### SSH rejects every password

- **Cause:** empty root password - Dropbear refuses it.
- **Fix:** System configuration → Root password.

### WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED

- **Cause:** a new image generates a new host key. Expected after re-flashing.
- **Fix:** `ssh-keygen -R 192.168.7.3`.

---

## Network

### Laptop on the LAN cannot reach the Pi through the switch

```
enp4s0   UP   10.101.4.119/24
```

- **Cause:** the laptop has only its LAN address (`10.101.4.x`); the Pi is on `192.168.7.x`.
- **Fix (per session):** `sudo ip addr add 192.168.7.1/24 dev enp4s0` - two addresses on one port.
- **Lost after:** reboot, cable re-plug, suspend, network reconnect. `File exists` = already set.

### ifup: Cannot find device "eth0" / no IP after boot

- **Cause:** the Pi 3 B+ Ethernet is a USB chip (`lan78xx`) that appears after `S40network` starts.
- **Fix:** the `pre-up` wait loop in `overlay/etc/network/interfaces`.

---

## Eclipse and remote development

Details: [eclipse-setup.md](eclipse-setup.md)

### Workspace created in /home/shaman/home/shaman/...

- **Cause:** the workspace path was typed without the leading `/`, so Eclipse made it relative to the home folder.
- **Fix:** close Eclipse, move the folder to the intended place, remove the empty leftovers, reopen with the full path:
  ```bash
  mv /home/shaman/home/shaman/Rpi_Workspace/eclipse-workspace ~/Rpi_Workspace/eclipse-workspace
  rmdir ~/home/shaman/Rpi_Workspace ~/home/shaman ~/home
  ```
- No `sudo` is needed anywhere in the home folder.

### Run Configurations: "Project does not exist"

- **Cause:** after moving or switching the workspace, the project was no longer registered in it.
- **Fix:** File → Import → General → Existing Projects into Workspace (Copy into workspace: off),
  then pick the project with **Browse...** instead of typing its name.

### Remote connection list shows only "Local"

- **Cause:** remote connections are stored per workspace (`.metadata`).
- **Fix:** create the SSH connection again (Connection → **New... → SSH**).

### Private key set to id_ed25519.pub

- **Cause:** the **public** key was entered in Window → Preferences → Network Connections → SSH2 → General.
- **Fix:** Private keys = `id_ed25519` (no `.pub`). The `.pub` file is what goes to the Pi via `ssh-copy-id`.

### ssh-copy-id: ERROR: No identities found

- **Cause:** no SSH key on the laptop yet.
- **Fix:** `ssh-keygen -t ed25519` (Enter three times), then `ssh-copy-id root@192.168.7.3`.

### ssh-copy-id: No route to host

- **Cause:** the laptop had the board address, but the Pi did not answer (not booted, no `eth0` IP, cable).
- **Check on the Pi:** `ifconfig eth0`, `ps | grep dropbear`.

### scp: /usr/libexec/sftp-server: not found

```
sh: line 0: /usr/libexec/sftp-server: not found
scp: Connection closed
```

- **Cause:** since OpenSSH 9, `scp` uses the SFTP protocol. Dropbear has no SFTP server.
- **Quick fix:** `scp -O` (legacy protocol).
- **Real fix:** Buildroot → Target packages → Networking applications → **gesftpserver**, rebuild, re-flash.
  Eclipse's uploader also needs it.

### Eclipse: "Error during file upload" / "Algorithm negotiation fail"

- **Cause:** Eclipse's built-in SSH library only offers old key exchange methods (`diffie-hellman-group1/14-sha1`,
  `group-exchange-sha1/sha256`). Dropbear in Buildroot 2026.08 offers only modern ones
  (`curve25519-sha256`, `ecdh-sha2-nistp*`, `diffie-hellman-group14-sha256`) and does not implement group exchange.
  Ticking every method in Eclipse does not help; a newer Eclipse showed the same list.
- **Diagnose:** `ssh -vv root@192.168.7.3 exit 2>&1 | grep -A9 "peer server KEXINIT"`
- **Solution used:** deploy with `scp`/`ssh` (`rpi-run`), debug with gdbserver + gdb or Eclipse's
  **GDB (DSF) Manual Remote Debugging Launcher** (plain TCP, no SSH).
- **Alternative:** Dropbear "enable legacy crypto" + an RSA PEM key for Eclipse (weaker algorithms on the Pi).
- **Lesson:** "Algorithm negotiation fail" means both sides are reachable but share no cipher suite -
  compare the two algorithm lists.

### Text file busy

- **Cause:** replacing a program that is still running on the Pi.
- **Fix:** stop it first (`/etc/init.d/S99myapp stop` or `killall myapp`), then copy.

---

## Shutdown

### EXT4-fs: orphan cleanup ... recovery complete

- **Cause:** power was removed without shutting down.
- **Fix:** always `poweroff` and wait for the green LED to stop before unplugging.
