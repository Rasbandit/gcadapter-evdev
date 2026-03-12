# gcadapter-evdev

A patched and extended version of the Wii U/Switch GameCube adapter evdev bridge,
adding multi-adapter support and fixing several upstream bugs.

Exposes each port on up to two GC adapters as a virtual evdev gamepad via uinput,
using libusb for direct USB access — no kernel module required.

---

## What's different from upstream

This repo bundles a patched [libgcadapter](https://github.com/PMArkive/libgcadapter)
and patches to the daemon itself. All changes are documented below.

### Multi-adapter support (`--adapter N`)

The daemon accepts `--adapter 0` or `--adapter 1` to target a specific physical adapter.
Run two instances simultaneously to expose up to 8 controllers.

On first launch, the Nth matching `057e:0337` USB device is claimed (index-based).
After that, the adapter is tracked by physical USB bus+port so the correct device is
re-claimed after unplug/replug regardless of enumeration order changes.

### Bug: failed `claim_interface` killed other process's adapter

When `libusb_claim_interface` fails (adapter already owned by another instance),
the original code called `gc_adapter_close_usb` which unconditionally called
`libusb_attach_kernel_driver` — reattaching the kernel driver on the device another
process had open, killing that instance's connection. Fixed to only close the raw
handle on failed claim, leaving the kernel driver state untouched.

### Bug: `sizeof` iteration over button map

`sizeof(evdev_map_buttons)` returns the byte count, not the element count — upstream
was iterating 95 times instead of the correct 10. Fixed to
`sizeof(evdev_map_buttons) / sizeof(evdev_map_buttons[0])`.

### Bug: wrong SDL button mapping

Upstream had incorrect A/B/X/Y evdev codes for Ryujinx/SDL. Fixed:

| GC Button | evdev Code | SDL Name |
|---|---|---|
| A | BTN_SOUTH (304) | A |
| B | BTN_EAST (305) | B |
| X | BTN_NORTH (307) | X |
| Y | BTN_WEST (308) | Y |
| Z | BTN_THUMBR (318) | R3 |

### Bug: wrong axis ranges and layout

- `absmin=0` / `absmax=255` (upstream used 32–223; triggers rest near 0 which fell below minimum)
- Added `absfuzz=4`, `absflat=10` for sticks; no flat for triggers
- C-stick → `ABS_RX`/`ABS_RY`, triggers → `ABS_Z`/`ABS_RZ` (upstream had wrong axes)
- Trigger axis order: `triggers.x` (left) → `ABS_Z`, `triggers.y` (right) → `ABS_RZ`
- Left/right shoulder buttons (`BTN_TL`/`BTN_TR`) removed — they fired at end of trigger
  travel and conflicted with analog trigger axes in Ryujinx

### Bug: unstable device name

Upstream included the adapter pointer address in the device name (e.g.
`"GameCube Adapter (0x2f79f320) Port 1"`), which changed on every restart and broke
evmapy config filename matching. Simplified to `"GameCube Controller"`.

### Bug: macro syntax in gctypes.h

`GC_PAD_STATES_EQUAL` and `GC_PAD_AXISES_EQUAL` used `{ ... }` block syntax instead
of `( ... )` expression syntax — wouldn't compile as a condition. Fixed.

---

## Requirements

- Fedora/Linux with `udev`
- `gcc`, `libusb-1.0-devel`
- User in the `input` group (for `/dev/uinput` access)

```bash
sudo dnf install gcc libusb1-devel   # Fedora
sudo apt install gcc libusb-1.0-0-dev  # Debian/Ubuntu
```

---

## Build

```bash
make
```

Binary output: `bin/gcadapter_evdev`

---

## Install

```bash
sudo cp bin/gcadapter_evdev /usr/local/bin/gcadapter_evdev
```

### udev rule (user access to adapter)

`/etc/udev/rules.d/71-gcadapter.rules`:
```
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTRS{idVendor}=="057e", ATTRS{idProduct}=="0337", TAG+="uaccess"
```

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

---

## Usage

```bash
# Single adapter (ports 1-4)
gcadapter_evdev --adapter 0

# Two adapters simultaneously (8 players)
gcadapter_evdev --adapter 0 &
gcadapter_evdev --adapter 1 &
```

### systemd user service (auto-start)

`~/.config/systemd/user/gcadapter-evdev@.service`:
```ini
[Unit]
Description=GameCube Adapter evdev bridge (adapter %i)
After=graphical-session.target

[Service]
ExecStart=/usr/local/bin/gcadapter_evdev --adapter %i
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable --now gcadapter-evdev@0.service
systemctl --user enable --now gcadapter-evdev@1.service  # second adapter
```

---

## Adapter must be in Wii U/Switch mode

The physical switch on the back of the adapter must be set to **Wii U** (not PC).
The USB VID:PID must be `057e:0337`. PC mode (`0079:1846`) is not supported.

---

## Credits

- Original daemon: [gemarcano/gcadapter_oc_kmod](https://github.com/gemarcano/gcadapter_oc_kmod)
- libgcadapter: [PMArkive/libgcadapter](https://github.com/PMArkive/libgcadapter)
