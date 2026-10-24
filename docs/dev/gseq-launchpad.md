# gseq: Launchpad Mini MK3

Status: steps 1-4 built 2026-10-23; on the SSP, device steps 27-31 pass (user, 2026-10-23); 32-36 and step 5 pending. Launchpad X (step 6) works on the SSP: pad velocity and the window frame (user, 2026-10-24). Decided: 6 sequencer rows and 2 keyboard rows; side keys mute the visible rows and set the keyboard's octave; capture in each mode ([Capture](#capture)).

A Novation Launchpad Mini MK3 edits and shows `gseq`'s grid, and plays notes into it. State flows both ways: pads edit the pattern, LEDs show the pattern, playheads, cursor and mutes. gseq's own screen and encoders keep working; both views edit the same engine words.

## Device

From the Programmer's Reference Manual (`Launchpad Mini - Programmers Reference Manual.pdf`, pp. 6-14) and [lpminimk3](https://github.com/obeezzy/lpminimk3)'s `components.py`:

- Two USB MIDI interfaces: `LPMiniMK3 DAW` and `LPMiniMK3 MIDI`. gseq uses the MIDI interface.
- Programmer mode: `F0 00 20 29 02 0D 0E 01 F7`. Live mode: `... 0E 00 F7`.
- Pads send notes `10 * row + col`, 11-88, row 1 at the bottom. The top row sends CC 91-98: up, down, left, right, Session, Drums, Keys, User. The side `[>]` keys send CC 89, 79, ... 19, top to bottom. The logo is 99. A press is value 127, a release 0.
- LEDs: SysEx `F0 00 20 29 02 0D 03 <spec>... F7`, up to 81 specs. A spec is a type, an LED index and data: static palette (1 byte), flash (2 palette bytes), pulse (1), RGB (3 bytes, 0-127). A full RGB frame is 413 bytes; JUCE's ALSA output grows its event buffer to fit (`juce_Midi_linux.cpp:134`).
- Flash and pulse follow MIDI clock sent to the device, or 120 bpm without it.

Velocity: the pads are not velocity sensitive. On the SSP, soft and hard presses both sent `90 0B 7F` (2026-10-23), against retail listings that claim otherwise.

Launchpad X, from its Programmer's Reference Manual (`Launchpad X - Programmers Reference Manual.pdf`, pp. 6-15): the same Programmer-mode layout, messages and LED SysEx, with device byte `0C` for the Mini's `0D`. Interfaces `LPX DAW` and `LPX MIDI`; on the SSP, ALSA ports `Launchpad X LPX DAW In` (port 0) and `Launchpad X LPX MIDI In` (port 1), both read-write (2026-10-24). The top row reads up, down, left, right, Session, Note, Custom, Capture MIDI. Its pads send the press's velocity, shaped by the device's velocity curve (Setup menu, or SysEx command `04`). Aftertouch arrives as `A0` or `D0` messages, which the decoder drops. Added 2026-10-24; works on the SSP (user, 2026-10-24).

Connection: through the powered USB hub on the SSP's left port, next to the keyboard. The left port takes controllers only through a powered hub.

## Layout

```
 [^][v][<][>] [Rec][Stp][Fol][ - ]        top row, CC 91-98
 [ seq row r      steps c .. c+7 ] [m]    pad rows 8-3: a 6 x 8 window
 [ seq row r+1                   ] [m]      onto the 8 x 32 grid
 [ ...                           ] [m]
 [ seq row r+5                   ] [m]
 [ .  C# D# .  F# G# A# .        ] [+]    pad row 2: black keys
 [ C  D  E  F  G  A  B  C        ] [-]    pad row 1: white keys
```

| Control | Does |
|-|-|
| up, down | move the window a row: rows 1-6, 2-7 or 3-8 |
| left, right | move the window 8 steps: 4 pages over 32 steps |
| cursor pad | see [Capture](#capture) |
| side key beside a window row | mutes that row |
| key pad | see [Capture](#capture) |
| side keys + and - | the keyboard's octave, C-1 to C8 (its top C stays within 0-127) |
| Session, Drums | Rec and Step, as the SSP buttons |
| Keys | Follow: the window pages to keep the cursor row's playhead in view |
| User | unused |

Terms: a **cursor pad** is a pad in the 6 x 8 window; a **key pad** is a pad in the keyboard rows.

The hardware labels of Session, Drums and Keys do not match these actions. Their LEDs show the state instead: Rec blinks while armed and lights while recording, as on screen.

Piano layout over strict chromatic (2 x 8, 16 semitones): 13 notes, but the pad shapes read as a keyboard. Black-key gaps (pads 21, 24, 28) stay dark and do nothing.

## Capture

Decided 2026-10-23.

| Mode | Cursor pad | Key pad |
|-|-|-|
| neither | moves the cursor | sounds through the cursor row's routing |
| Rec, armed | moves the cursor and re-arms on its row | sounds, as from a MIDI keyboard |
| Rec, recording | moves the cursor; the take stays on its row | records into the take through `GseqEngine::noteIn()`, as from a MIDI keyboard |
| Step | with a key pad held: sets the step on with the held note. With none: switches the step on or off | held for the next cursor pad; sounds |

Step mode:

- A held key always sets the step on. Only a press with no key held switches a step off, so a held note is never discarded.
- Several keys held: the chord spreads to the rows below, lowest note first, wrapping from row 8 to row 1, as recording does.
- The default note is the last key pad pressed, else C4 (60). A step switched on with no key held takes the cursor pad's velocity on the X, 100 on the Mini.
- A step that was on keeps its duration. On the X it takes the key's velocity; on the Mini it keeps its own, as the Mini's 127 carries no force. A new step gets the key's velocity (100 on the Mini) and 1/2 step.
- The cursor does not move on, unlike step entry from a MIDI keyboard. So key pads do not reach `noteIn()` in Step mode: the Launchpad view keeps the held keys and writes steps with `setStep()`. A MIDI keyboard's step entry is unchanged.

Key pads send velocity 100 on the Mini, whose pads are not velocity sensitive, and the press's velocity on the X; encoder 2 edits it afterwards.

The SSP's screen frames the window in dim red, in both modes, while a Launchpad is open.

The window moves the least it must to show the cursor, whether a cursor pad, the SSP arrows or step entry moved it. Follow overrides this while on.

Engine changes: sounding a key outside Rec and Step, through the cursor row (today `keyDown` ignores the note); re-arming Rec on a new row.

## LEDs

| Pad | Colour |
|-|-|
| step on | amber, brighter for higher velocity (RGB), as on screen |
| step off, within the row's length | dim grey |
| past the row's length | dark |
| row with no output | grey, as on screen |
| the row's playing step | white |
| the cursor | flashing |
| side key, muted row | red; unmuted: dim |
| keyboard | white keys dim white, black keys dim blue, C brighter; a held key full white |
| octave keys | dim; dark at the range's end |

## Implementation

**Driver** (`plugins/common/launchpad/`), with no gseq knowledge:

- Finds the in and out ports whose names contain `LPMiniMK3 MI`, else `LPX MIDI`: ALSA cuts port names to 31 characters. With both plugged in, the X wins, for its velocity. Polls for unplug and replug, as `BaseProcessor::checkMidiDevices` does.
- On connect: Programmer mode, then a full frame. On close: Live mode, which hides the Programmer-mode LEDs.
- An 81-LED frame. A flush sends one SysEx with the LEDs that changed since the last flush.
- Pads and keys decode to one position grid, x and y 0-8; the side keys are x 8, the top row y 8.

**gseq:**

- Input runs on JUCE's MIDI input thread and writes engine atomics: `setStep`, `setRow`, `setCursor`, `noteIn`, the Rec and Step requests. View state (window position, octave, Follow) is atomics in the processor.
- `notesIn_` is a single-producer queue. The general panel's MIDI in and the Launchpad's both call `noteIn()`. On Linux JUCE has one ALSA client with one input thread (`juce_Midi_linux.cpp`, `AlsaClient::SequencerThread`), so there is still one producer. A test should fail if that changes.
- Render: the existing 1 ms `midiLoop` thread builds a frame every 16 ms (60 Hz) and flushes it. A 1/32 step at 300 bpm lasts 25 ms, so every playhead step is shown.
- MIDI clock to the Launchpad, 24 per quarter note from gseq's clock, so flashing follows the tempo.
- The cursor: the engine's `cursor_` becomes the only copy. `PluginEditor` reads it each timer tick, not only in step entry, and no longer resets it to row 1, step 1 when it opens.
- One owner: a static flag in `LaunchpadDevice.cpp`; the instance that polls first while the Launchpad is free opens it. Another takes it within a second of the first's removal.
- The Launchpad's port stays in the general panel's MIDI IN list: that list is shared by every module, and other modules may want the Launchpad's notes. The README warns against choosing it for gseq.

## Failure modes

- **`kill -9`.** Synthor is stopped this way, so the destructor does not run. The Launchpad stays in Programmer mode with stale LEDs until the next gseq connects or it is replugged.
- **Shared ALSA client.** `docs/dev/midi-review.md` asks whether a second module's `openDevice` replaces the first one's port. Unchecked; the device test covers it with the Launchpad and a keyboard on one gseq.
- **Fixed velocity.** The Mini's pads send 127 whatever the force, so every keyed note has velocity 100 until edited.

## Alternatives

1. **Generic grid layer.** A monome-style API (key events, 8 x 8 varibright LEDs) with the Launchpad as one backend. ttyp's grid ops are a second client (`plugins/teletype/README.md`, Not connected). Against: one client today. The driver's API stays narrow, so the layer can come later.
2. **The DAW interface.** Session mode on `LPMiniMK3 DAW` keeps the MIDI interface free for the Launchpad's own Custom modes. Against: the DAW interface reserves the Session and Custom buttons, and gseq wants every button.

## Steps

1. Done 2026-10-23, on the powered hub beside a nanoKEY2. Both enumerate (`/proc/asound/cards`). ALSA ports: `Launchpad Mini MK3 LPMiniMK3 DA` (port 0) and `... LPMiniMK3 MI` (port 1), cut to 31 characters. Through `/dev/snd/midiC2D0` (the SSP has no alsa-utils): Programmer mode and its read-back, the LED SysEx (pad 11, pad 88, the logo, up, side key 19 as designed), and input: pads as notes at 127 whatever the force, up as CC 91, the bottom side key as CC 19. The device also has an HID keyboard interface and an "Onboarding" mass-storage drive (off in the bootloader); the HID interface (`EV=100013`) has no LEDs, so `KeyInput`'s `openKeyboard()` skips it.
2. Done 2026-10-23: the protocol in `plugins/common/launchpad/Launchpad.{h,cpp}`, tested by `plugins/common/tests/launchpad_test.cpp` (mapping, decoding, Programmer mode, frame diff and SysEx against the manual's bytes). The JUCE device code (finding the ports, reconnecting) moves to step 4, where gseq links it and the host build compiles it.
3. Done 2026-10-23: the editor reads the engine's cursor before each input and each timer tick; `GseqEngine::playIn()` sounds a key from the cursor's row down in any mode and writes no step; Rec re-arms on the cursor's row while armed, from any cursor move. Tests `testPlay` and `testRecordRearm` in `gseq_test.cpp`, `playIn` in its ThreadSanitizer test. Device: `gseq-device-test.md` steps 2, 20, 24 and the new 26.
4. Done 2026-10-23: `plugins/common/launchpad/LaunchpadDevice.{h,cpp}` (JUCE: finds, opens, reconnects, one instance at a time) and `plugins/gseq/Source/LaunchpadView.{h,cpp}` (JUCE-free: pads to engine calls, the frame), driven from gseq's `midiLoop`. Tests: `plugins/gseq/tests/launchpad_view_test.cpp`, also under ThreadSanitizer. Device test: `gseq-device-test.md` steps 27-36.
5. MIDI clock to the Launchpad; the device test.
6. Built 2026-10-24: the Launchpad X. `Model` in `Launchpad.h` holds each model's port name, device byte and whether its pads send velocity; `Event::velocity` carries it to the view. Tests: `testVelocity` in `launchpad_test.cpp`, `testKeyVelocity` in `launchpad_view_test.cpp`. Device test: `gseq-device-test.md` steps 36b-36c. Also 2026-10-24: roll pads add notes at the pad's velocity (`testRoll`), and the editor frames the Launchpad's window (step 36d).
