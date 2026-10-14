# Teletype port

The `ttyp` plugin (`plugins/teletype`). Goal: [monome teletype](https://github.com/monome/teletype) as an SSP module, live-coded from a USB keyboard. A keyboard over the network is the secondary input.

## Approach

Run the unmodified teletype firmware against [monome-rack](https://github.com/Dewb/monome-rack)'s mock hardware (`firmware/mock_hardware`, about 1.9k lines), built as a separate `.so`. The plugin plays the role of monome-rack's `TeletypeModule.cpp`: it drives `hardware_step`, `hardware_triggerInterrupt(0)` (1 ms timer), `setGPIO` (trigger inputs `A00`..), `setADC` (IN, PARAM), `getDAC` and `getGPIO(B08..B11)` (outputs), `hidMessage` (keys) and `setScreenBuffer` (128x64, one byte per pixel, 0-15).

Each instance loads its own copy of the `.so` with `RTLD_LOCAL`, as monome-rack's `FirmwareManager.cpp` does. The core has mutable globals (`chaos_state`, `processing_delays`, the i2c ops' unit and channel state), so a shared copy would mix instances.

Rejected: linking only `src/` behind `teletype_io.h`. It would need a rewrite of the live, edit, pattern and preset modes (`module/`, about 2.2k lines) that the firmware already contains.

## Spike results (2026-10-12)

Firmware: monome-rack's teletype5 pin `d294b94`, and upstream head `e390bac`. Both build and pass.

- The firmware cross-builds with `clang --target=arm-linux-gnueabihf -mcpu=cortex-a17`. It needs `-DARCH_LIN=1` (normally from Rack's `arch.mk`), monome-rack's `teletype4` submodule (for `serializer.h`), and `ragel` for `scanner.c` and `match_token.c`. Stripped size: 508 KB. Its only dependencies are libc symbols.
- A dlopen harness on the SSP loaded two copies and typed into each through `hidMessage`. `CV 1 V 3` gave DAC 19648 (3.00 V) in A; `CV 1 V 7` gave 45872 (7.00 V) and `TR 1 1` gave TR1 high in B. The instances did not interfere. Live mode rendered to the screen buffer.
- Idle cost on the SSP: 10 s of simulated time took 4 ms CPU at one pump per 1 ms tick, and 17-21 ms at 48 pumps per tick (per sample at 48 kHz). That is at most 0.2% of a core.
- `hidMessage` runs the UI and any command synchronously. `L 1 16000: CV 2 I` took 2.5-5.7 ms inside it on the SSP (3 runs). A 128-frame block at 48 kHz lasts 2.7 ms.

## Design consequences

- **Firmware thread.** One thread owns every firmware call and ticks at 1 ms. The audio thread exchanges input states, ADC values, DAC values and TR states with it through atomics. A long script then delays teletype, as it does on hardware, and does not stall Synthor. Cost: up to one block of trigger latency. Calling the firmware in `processBlock`, as VCV does, would drop audio at the loop above.
- **CV range.** `/root/synthor.sh` sets `amixer sset 'PCM' 99%`, which its comment gives as -5.2 V to +5.2 V. Teletype's CV outputs span 0-10 V (DAC 0-16383). Two mappings:
  - volt-accurate, clipped above 5.2 V: `V`, `N` and 1 V/oct pitch stay correct up to about 5 octaves;
  - shifted by -5 V: the full teletype range maps to -5 V to +5 V, but absolute voltages are wrong.
  
  `ttyp` defaults to volt-accurate, with a range setting per CV output and one for IN. Not yet measured with a voltmeter.
- **Inputs.** Teletype has 8 trigger inputs and IN; PARAM can map to an encoder. That is 9 inputs; the repo's plugins declare at most 8. Synthor's limit is unchecked.
- **i2c ops** (Ansible, Just Friends, TELEX, ER-301, W/, crow, Disting) have no bus. The mock layer's i2c queue can stay unconnected. Grid needs FTDI serial, and the SSP kernel has no `ftdi_sio` driver; leave grid out.
- **Scenes.** Replace NVRAM with a file in `/media/BOOT`, through monome-rack's `serializePreset`/`deserializePreset`.

## Keyboard

The SSP kernel (5.13.13) has `usbhid` and `hid-generic`. Synthor holds `/dev/input/event0`-`event5` (keypad, 4 encoders, gpio-keys) and no other input node. The USB-Ethernet adapter is on the DWC2 controller (`ff540000.usb`). JUCE key events do not reach plugins on the SSP ([kbdmouse.md](../misc_dev_notes/kbdmouse.md)).

The other port is on the EHCI controller (`ff500000.usb`) and has no OHCI companion. A keyboard plugged in directly fails with `Cannot enable` (2026-10-12): keyboards are low or full speed, and EHCI handles only high speed. Behind a USB 2.0 hub, whose transaction translator converts the traffic, a Freebird60 enumerated at 12 Mbit/s as `event6`, with its mouse, system and consumer interfaces as `event7`-`event9`. Synthor did not open any of them. Users need a USB 2.0 hub on that port, or the keyboard on the DWC2 port.

Raw reads of `event6` gave press, release and autorepeat (value 2, about every 50 ms after a 250 ms delay). On unplug the read fails with `ENODEV`. A Keychron V1 put its keyboard on interface 2, after its System Control and Consumer Control nodes, so the node number does not identify the keyboard.

- **USB:** a worker opens the first `/dev/input/event*` above `event5` that has `EV_KEY` and `EV_LED`. It maps Linux `KEY_*` codes to HID usages and queues `(key, mod, held, release)` for the firmware thread. evdev value 2 (autorepeat) maps to `held`. On `ENODEV`, close the node and rescan.
- **Network:** a laptop client reads raw key events and sends the same 4-byte records over TCP. A terminal over ssh cannot send them: teletype uses Win-key chords, Ctrl+Alt, Shift+Ctrl and held keys. The plugin listens on 127.0.0.1 only and the user forwards the port with `ssh -L`, so no unauthenticated port is open on the LAN.

## Implementation (2026-10-12)

- **Sources.** `scripts/fetch_teletype.sh` (run by `make deps`) fetches teletype, libavr32 and monome-rack at pinned commits and checks their SHA-256. It builds ragel 6.10 when none is installed. No submodules.
- **Firmware.** `plugins/teletype/firmware` builds `ttyp_firmware.so` as its own CMake project, so the native tests need no JUCE. `firmware_image.S` embeds it in `ttyp.so` with `.incbin`.
- **Loading.** Each instance writes the image to a `memfd` and `dlopen`s `/proc/self/fd/N`. The fd stays open while loaded. Closed after `dlopen`, the next instance reused its number, glibc matched the path to the loaded object, and both instances shared one firmware.
- **Keys.** A USB keyboard's events become HID boot reports through `firmware/ttyp_hid.c`. The firmware's HID timer polls them every 47 ms, so key repeat is the firmware's, and a key released within one poll is missed, as on the hardware. TCP taps go through `hardware_hidMessage`, which handles each key at once.
- **Key routing.** `KeyInput` (`plugins/common/keys`), one per plugin library, sends keys to the instance whose editor last ran `onSSPTimer`. It grabs the keyboard (`EVIOCGRAB`), so the console and SysRq do not see it, and hands the grab to another keyboard plugin on screen; see [sapf-plugin.md](sapf-plugin.md), Keys. `TTYP_KEYBOARD=0` turns the keyboard off; the host tests set it.
- **Triggers.** The audio thread counts rises per input. A rise sets the pin low then high, and the pin stays high for that tick: the firmware's trigger handler reads the pin when its event runs, so a pulse set low in the same tick was dropped.
- **State.** NVRAM (the 32 scenes) and VRAM (the live scene), 358 KB, gzipped into the preset. A state whose sizes differ from the firmware's is refused.
- **Scene files.** `Teletype::syncDisk` repeats teletype's USB stick mode (`module/usb_disk_mode.c`) through monome-rack's `hardware_serializePreset` and `hardware_deserializePreset`: every slot to `ttNNs.txt`, then each `ttNN.txt` into its slot. Files are read and written outside the firmware lock; exports go to a temp file and are renamed. `TTYP_SCENES` overrides the folder for the host test.
- **Licence.** `ttyp` is GPL-3.0: the upstream projects ship the GPL-2 text with no version stated, which GPL-2 section 9 lets a user take as any later version. monome-rack's build compiles Atmel ASF headers whose licence limits redistribution to Atmel microcontrollers; `firmware/include` replaces them with the same values, and a check of every name against the originals found no difference. See `plugins/teletype/NOTICE.md`.
- **Tests.** `plugins/teletype/tests/engine_test.cpp` covers the engine and firmware natively; `test_teletype` in `plugins/common/tests/test_engine_plugins.py` covers the plugin through the SSP API, typing through the TCP port; `tests/run_on_ssp.sh` runs the engine test and the spike harness (`spike/harness.c`) on the SSP. All pass, on the SSP too.

## Status (2026-10-13)

In Synthor on the SSP: listed, typed into from a USB keyboard through a hub, and trigger outputs patched to trigger inputs run scripts. The CV test and the remaining checks are in [plugins/teletype/TODO.md](../../plugins/teletype/TODO.md). User documentation, including the constraints on the SSP: [plugins/teletype/README.md](../../plugins/teletype/README.md).

## Open

See [plugins/teletype/TODO.md](../../plugins/teletype/TODO.md). Also untested: a keyboard on the DWC2 port without a hub.
