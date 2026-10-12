# Eclipse and Remote Development for the Raspberry Pi 3 B+

How applications are written, cross-compiled, deployed and debugged for the Buildroot Pi image.

**Workflow used in this project:**

```
Eclipse (Cross GCC) ──build──► ARM binary ──scp──► Pi /root ──ssh──► runs on the Pi
                                                       │
                          gdb on laptop ◄──TCP 2345──► gdbserver on the Pi
```

---

## 1. Folder layout

```
~/Embedded_Linux/
├── Tools/
│   ├── buildroot-2026.08/        shared Buildroot source
│   ├── buildroot-dl/             shared download cache
│   └── eclipse/                  Eclipse IDE
├── Udoo_Workspace/
└── Rpi_Workspace/
    ├── br-rpi3/                  Buildroot output (toolchain in host/bin)
    ├── eclipse-workspace/        Eclipse settings only (.metadata) - not in Git
    └── rpi3-embedded-linux/      this repo
        └── apps/
            └── hello_rpi/        Eclipse project (source in Git)
```

`~/Tools`, `~/Udoo_Workspace` and `~/Rpi_Workspace` are symbolic links to the folders above, so older absolute
paths (Buildroot output, Eclipse settings) keep working.

---

## 2. Toolchain

No separate download: Buildroot installed the **Arm GNU Toolchain 15.2.Rel1** in its output folder.

```bash
ls ~/Rpi_Workspace/br-rpi3/host/bin/arm-none-linux-gnueabihf-*
~/Rpi_Workspace/br-rpi3/host/bin/arm-none-linux-gnueabihf-gcc --version
# arm-none-linux-gnueabihf-gcc (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203
```

| Tool | Use |
|---|---|
| `arm-none-linux-gnueabihf-gcc` / `g++` | Cross compiler (Eclipse) |
| `arm-none-linux-gnueabihf-gdb` | Cross debugger (no Python dependency - use this one) |
| `arm-none-linux-gnueabihf-gdb-py` | Same, with Python scripting (needs a matching host Python) |
| `objdump`, `readelf`, `nm`, `size`, `strip` | Inspect binaries |

**Why Buildroot's copy:** its compiler uses the same C library and headers (sysroot) as the Pi's root filesystem,
so programs never fail with "GLIBC not found".

| Toolchain | Suitable? |
|---|---|
| `arm-none-linux-gnueabihf` (Buildroot `host/bin`) | ✅ |
| `arm-none-eabi` / xPack GNU Arm Embedded | ❌ bare-metal, no Linux C library |
| `aarch64-none-linux-gnu` | ❌ 64-bit; the image is 32-bit |
| Ubuntu `gcc-arm-linux-gnueabihf` | ⚠️ works for simple code, but different glibc than the image |

---

## 3. Install Eclipse

Download **Eclipse IDE for C/C++ Developers**, Linux x86_64, from <https://www.eclipse.org/downloads/packages/>.

```bash
cd ~/Tools
tar xf ~/Downloads/eclipse-cpp-*-linux-gtk-x86_64.tar.gz
~/Tools/eclipse/eclipse &
```

- Eclipse is a Java program. The package **bundles its own Java runtime** (`plugins/*justj*`), so the system Java
  version does not matter. Check under Help → About → Installation Details → Configuration → `java.version`.
- Workspace: `/home/shaman/Rpi_Workspace/eclipse-workspace` - **type the leading `/`**. Without it Eclipse creates
  the workspace relative to the home folder (`/home/shaman/home/shaman/...`).

### Dock launcher

```bash
cat > ~/.local/share/applications/eclipse-cpp.desktop << 'EOF'
[Desktop Entry]
Name=Eclipse C/C++
Exec=/home/shaman/Tools/eclipse/eclipse
Icon=/home/shaman/Tools/eclipse/icon.xpm
Type=Application
Categories=Development;IDE;
StartupWMClass=Eclipse
EOF
```

Super key → type `Eclipse` → right-click → **Pin to Dash**.

---

## 4. Create a cross-compiled project

**File → New → C/C++ Project → C Managed Build → Next**

| Field | Value |
|---|---|
| Project name | `hello_rpi` |
| Location | untick default → `/home/shaman/Rpi_Workspace/rpi3-embedded-linux/apps/hello_rpi` |
| Project type | Executable → Hello World ANSI C Project |
| Toolchains | **Cross GCC** (untick "Show project types... supported on the platform" if hidden) |

**Cross GCC Command** page:

| Field | Value |
|---|---|
| Cross compiler prefix | `arm-none-linux-gnueabihf-` (trailing `-` required) |
| Cross compiler path | `/home/shaman/Rpi_Workspace/br-rpi3/host/bin` |

Change later in: Project → Properties → C/C++ Build → Settings → **Cross Settings**.

### Build and check

**Ctrl+B**, then:

```bash
file ~/Rpi_Workspace/rpi3-embedded-linux/apps/hello_rpi/Debug/hello_rpi
```

```
ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (SYSV), dynamically linked,
interpreter /lib/ld-linux-armhf.so.3, for GNU/Linux 3.2.0, with debug_info, not stripped
```

| Part | Meaning |
|---|---|
| `ARM, EABI5` | Built for the Pi, not the laptop |
| `dynamically linked ... ld-linux-armhf.so.3` | Uses the Pi's glibc (hard-float) |
| `with debug_info` | Debug build - gdb can show source lines and variables |

### Existing project into the workspace

Projects live in the repo; the Eclipse workspace only holds settings.
**File → Import → General → Existing Projects into Workspace** → root
`/home/shaman/Rpi_Workspace/rpi3-embedded-linux/apps` → tick the project → **Copy projects into workspace: off**.

---

## 5. Deploy and run (terminal)

Requirements: board address on the laptop and passwordless SSH.

```bash
sudo ip addr add 192.168.7.1/24 dev enp4s0      # or: boardnet (when on the LAN switch)
ssh root@192.168.7.3 uname -m                    # armv7l, no password prompt
```

```bash
cd ~/Rpi_Workspace/rpi3-embedded-linux/apps/hello_rpi/Debug
scp hello_rpi root@192.168.7.3:/root/
ssh root@192.168.7.3 /root/hello_rpi
```

```
hello_rpi            100%   45KB   3.9MB/s   00:00
!!!Hello World!!!
```

| Step | What happens |
|---|---|
| `scp` | Copies the file over SSH (SFTP subsystem - needs `gesftpserver` on the Pi) |
| `ssh ... /root/hello_rpi` | Logs in, runs only that command, returns its output, logs out |
| On the Pi | The loader `/lib/ld-linux-armhf.so.3` loads `libc.so.6` from the rootfs, `main()` runs |

### One-command helper: `rpi-run`

```bash
mkdir -p ~/.local/bin
cat > ~/.local/bin/rpi-run << 'EOF'
#!/bin/bash
# Copy a program to the Raspberry Pi and run it there.
# Usage: rpi-run <program> [arguments...]
PI=root@192.168.7.3
BIN="$1"; shift
[ -f "$BIN" ] || { echo "No such file: $BIN"; exit 1; }
scp -q "$BIN" "$PI:/root/" && ssh -t "$PI" "/root/$(basename "$BIN")" "$@"
EOF
chmod +x ~/.local/bin/rpi-run
```

```bash
cd ~/Rpi_Workspace/rpi3-embedded-linux/apps/hello_rpi/Debug
rpi-run hello_rpi
```

`ssh -t` gives the program a terminal, so **Ctrl+C** stops endless programs on the Pi.

### Shortcut for the board address

```bash
echo "alias boardnet='sudo ip addr add 192.168.7.1/24 dev enp4s0; ip -br addr show enp4s0'" >> ~/.bashrc
source ~/.bashrc
```

---

## 6. Debug with gdbserver

**Terminal 1** - start the program under gdbserver on the Pi:

```bash
ssh root@192.168.7.3 gdbserver :2345 /root/hello_rpi
# Listening on port 2345
```

**Terminal 2** - cross gdb on the laptop:

```bash
cd ~/Rpi_Workspace/rpi3-embedded-linux/apps/hello_rpi/Debug
~/Rpi_Workspace/br-rpi3/host/bin/arm-none-linux-gnueabihf-gdb hello_rpi
```

```
(gdb) set sysroot /home/shaman/Rpi_Workspace/br-rpi3/staging
(gdb) target remote 192.168.7.3:2345
(gdb) break main
(gdb) continue
(gdb) next
(gdb) print i
(gdb) bt
(gdb) quit
```

| Command | Does |
|---|---|
| `set sysroot` | Where the Pi's libraries are on the laptop (symbols for libc) |
| `target remote IP:PORT` | Connect to gdbserver |
| `break` / `continue` / `next` / `step` | Breakpoint, run, step over, step into |
| `print` / `bt` | Show a variable / call stack |

### Graphical debugging in Eclipse (no Eclipse SSH needed)

Start gdbserver as in Terminal 1, then **Run → Debug Configurations → C/C++ Remote Application → New**,
bottom of the dialog: **Select other... → GDB (DSF) Manual Remote Debugging Launcher**.

| Tab | Field | Value |
|---|---|---|
| Main | C/C++ Application | `Debug/hello_rpi` |
| Debugger → Main | GDB debugger | `/home/shaman/Rpi_Workspace/br-rpi3/host/bin/arm-none-linux-gnueabihf-gdb` |
| Debugger → Connection | Type / Host / Port | TCP / `192.168.7.3` / `2345` |

Keys: F6 step over, F5 step into, F8 resume, Ctrl+F2 terminate.

---

## 7. Why not Eclipse's one-click "C/C++ Remote Application" launch?

Eclipse's automatic remote launch uploads and starts the program through **its own SSH library**. With this image
it fails:

```
Error during file upload.
Algorithm negotiation fail
```

Eclipse's SSH2 preferences offer only four key exchange methods:

```
diffie-hellman-group1-sha1
diffie-hellman-group14-sha1
diffie-hellman-group-exchange-sha1
diffie-hellman-group-exchange-sha256
```

Dropbear (Buildroot 2026.08) offers only modern methods (`curve25519-sha256`, `ecdh-sha2-nistp*`,
`diffie-hellman-group14-sha256`) and never implements "group exchange". **No common algorithm → no connection.**
A newer Eclipse release showed the same list. The terminal `ssh` works because Ubuntu's OpenSSH supports the modern methods.

Check what the Pi offers:

```bash
ssh -vv root@192.168.7.3 exit 2>&1 | grep -A9 "peer server KEXINIT"
```

| Option | Trade-off |
|---|---|
| **Terminal deploy + gdbserver (used)** | Modern crypto only, no image change |
| Dropbear "enable legacy crypto" + RSA PEM key for Eclipse | One-click Eclipse launch, weaker SHA-1 algorithms on the Pi |

---

## 8. Running an application permanently

| Level | How | Survives |
|---|---|---|
| Background test | `ssh root@192.168.7.3 'nohup /root/myapp > /var/log/myapp.log 2>&1 &'` | SSH logout |
| Start at boot | Copy to `/usr/bin`, add `/etc/init.d/S99myapp` (start-stop-daemon) | Reboot |
| Auto-restart | `::respawn:/usr/bin/myapp` in `/etc/inittab` | Crashes |
| In every image | Binary + init script in `overlay/`, or a Buildroot package (BR2_EXTERNAL) | Re-flashing |

Stop a running service before replacing its binary (`Text file busy` otherwise).
