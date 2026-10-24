# Script modules

What the script modules share: the code editor, the keys, presets, and the controls and CV of `csnd`, `chuk`, `fstr` and `scsy`. Each module's README covers its language. [Using](USING.md) covers what every module shares.

| Module | Language | Code view | Key port | Ctrl-S on the built-in writes |
|-|-|-|-|-|
| [`sapf`](../plugins/sapf/README.md) | sapf | yes | 7771 | `BOOT/sapf/live.sapf` |
| [`fstr`](../plugins/faust/README.md) | Faust | yes | 7772 | `BOOT/faust/live.dsp` |
| [`csnd`](../plugins/csound/README.md) | Csound | yes | 7773 | `BOOT/csound/live.csd` |
| [`chuk`](../plugins/chuck/README.md) | ChucK | yes | 7774 | `BOOT/chuck/live.ck` |
| [`alda`](../plugins/alda/README.md) | Alda | yes | 7775 | `BOOT/alda/live.alda` |
| [`scsy`](../plugins/scsynth/README.md) | SuperCollider SynthDef | no: a def is a compiled binary | none | - |

## The Code view

The Code view shows the program as text. Any key but Esc, typed on the pages, opens it and takes effect. Code (button 6) or Esc returns to the pages. The browser ignores keys.

- Encoder 1 moves the cursor by rows, encoder 2 by columns.
- The status line shows the file and row, the state (`running`, `compiling` or `error`), the DSP load, the key source (`usb`, or `tcp :<port>`), and the keys.
- A compile error shows in orange below the status line. A key's result, such as `saved ...`, shows there for 3 s.
- Ctrl-E highlights the rows it sent for 0.3 s.

## Getting keys in

- **USB keyboard**: through a USB 2.0 hub; see [USB devices](USING.md#usb-devices).
- **TCP port**: each module listens on 127.0.0.1 at its key port. Forward it over ssh: `ssh -L 7774:127.0.0.1:7774 root@<ssp-address>`.

Keys go to the module on screen. With no keyboard module on screen, the last one shown keeps them. `ttyp` takes keys the same way, on port 7770.

The port takes 4-byte key records, not text: kind (0 down, 1 up, 2 tap, 3 release all), HID usage (page 0x07), modifier byte (taps only; HID bits, 0x01 left Ctrl, 0x02 left Shift), 0. `tools/ttyp_type.py --port <port> FILE`, in the source tree, types a file as taps. It was written for `ttyp`, so:

- capitals arrive as lower case;
- it sends no Ctrl chords;
- Enter keeps the line's indent, so an indented line gains the previous line's indent.

## Keys

The shared editor gives every module these keys. [Differences](#differences) lists where a module departs from them.

| Key | Does |
|-|-|
| Ctrl-Enter | runs the whole text in place of the program; the file is unchanged. A program that fails leaves the previous one running |
| Ctrl-E | runs the block around the cursor beside the program. A block is the lines up to blank lines outside brackets, strings and comments; on a blank line, the block above |
| Ctrl-. | none: shows `no parts to clear` |
| Ctrl-1 .. Ctrl-9, Ctrl-0, Ctrl-- | none: shows `no parts to remove` |
| Ctrl-R | none: shows `no reset in this module` |
| Ctrl-S | writes the text to the program's file; for the built-in, to the file in the table above |
| Ctrl-Shift-S | Save As; see below |
| Ctrl-O | opens the browser, as Load |
| Ctrl-L | empties the Code view; again on the empty view, brings the text back |
| Esc | returns to the pages |
| Enter | a new line, with the current line's indent |
| Backspace, Delete | delete before and after the cursor |
| Tab | spaces to the next multiple of 4 |
| arrows, Home, End | move; Home and End go to the line's start and end |
| PageUp, PageDown | move 10 rows |
| Ctrl-Home, Ctrl-End | go to the text's start and end |

Save As opens a prompt holding the file's name. Type a name and press Enter. A relative name saves beside the program's file, an absolute path as is, and a missing extension is added. The new file becomes the program's file. An existing file needs a second Enter. Esc cancels.

### Differences

| Key | Module | Does |
|-|-|-|
| Ctrl-Enter | `chuk` | ends every Ctrl-E shred; audio stops while the program compiles |
| Ctrl-Enter | `fstr` | the previous program plays until the new one compiles |
| Ctrl-Enter | `sapf` | stops every sound, then evaluates the text |
| Ctrl-Enter | `alda` | the score replaces the running one when the loop ends, or at once after a stop |
| Ctrl-E | `csnd` | compiles the block into the running orchestra, or sends it as score events; see [its README](../plugins/csound/README.md#keyboard) |
| Ctrl-E | `chuk` | runs the block as a new shred, up to 16 |
| Ctrl-E | `fstr`, `alda` | none: shows `runs whole programs only: Ctrl-Enter` |
| Ctrl-. | `chuk` | removes every shred, the program's too: silence until the next Ctrl-Enter |
| Ctrl-. | `sapf` | interrupts the evaluation and stops every sound |
| Ctrl-. | `alda` | stops: gates close, MIDI notes are released, silence until the next score runs |
| Ctrl-1 .. Ctrl-9, Ctrl-0 | `chuk` | removes Ctrl-E shred 1 to 10, numbered in the order added |
| Ctrl-- | `chuk` | removes the newest Ctrl-E shred |
| Ctrl-R | `sapf`, `alda` | stops, empties the Code view, and forgets the program's file; Ctrl-L brings the text back, Ctrl-S then writes the built-in's file |

## Editor facts

- The code is coloured by the module's tree-sitter grammar.
- A held key repeats after 400 ms, every 50 ms. A Ctrl chord does not repeat: a held Ctrl-Enter runs once.
- The layout is US.
- Tabs in a loaded file become spaces, to the next multiple of 4.
- Relative paths in the code resolve beside the program's file, or beside the built-in's file in the table above.
- Ctrl-Enter keeps every control's position. Load sets a control to the program's default where the program gives one: `fstr` sliders, `sapf`'s mouse, `alda`'s controls but Clock. `@pN` lines give no default.

## Presets

A preset stores:

- the program's file path;
- after Ctrl-Enter, the code last run, which runs in place of the file when the preset loads;
- the text, when it holds an edit not yet run;
- the encoder positions.

It does not store what Ctrl-E ran. Load clears the stored code: the file runs again.

## Controls

The modules have sixteen controls, `p1`..`p16`. Each exists for presets, MIDI learn and `rack`, declared or not.

### `@pN` lines

`csnd`, `chuk` and `scsy`'s sidecar declare controls in comment lines. The comment syntax is in each README. The form is `@pN label [min max [unit]] [log] [cv N]`:

- The label is one word.
- Without a range a control is 0 to 1.
- `log` maps the encoder logarithmically. With a range not wholly above 0 it is ignored.
- `cv N` makes input N (1..8) move the control; see [CV on a control](#cv-on-a-control).
- The pages show only the declared controls, four to a page, in order. A program that declares none shows all sixteen as `P1`..`P16`.
- A preset stores the encoder position. It restores the same position in a program with a different range.

### Encoders

- Turning moves a control 5% of its range a detent. On a `log` range each detent is the same interval.
- Turning while pressing moves it 0.5% a detent.
- Pressing and releasing without turning sets the control to the start of its range, not the program's default.
- A control with named choices, such as `alda`'s Clock, moves one choice a detent.

## CV

In 1..In 8 and Out 1..Out 8 carry audio and CV alike. An SSP signal of 1.0 is 5 V, so CV is 0.2 per volt: 1 V is 0.2, and 1 V/oct pitch is 0.2 per octave. This applies to `csnd`, `chuk`, `fstr` and `scsy`; each README gives the program's own syntax.

A program gets CV in two ways: on a control, or by reading the input itself.

### CV on a control

A control with CV follows input N: `cv N` on its `@pN` line, or `[cv:N]` in an `fstr` label.

| Range | Each volt | -1 V | +1 V | +5 V |
|-|-|-|-|-|
| log | an octave | half | double | 32x |
| linear | a tenth of the range | -10% of the range | +10% | +50% |

- The encoder sets the value the CV moves from. The result stays within the control's range.
- An orange mark on the control's bar shows where the CV has moved it, and the value shown is the moved one. A preset stores the encoder position, not the moved value.
- The input still reaches the program as audio.
- How often the CV is read differs per module; see its README.

### CV in the program

Read the input in the program for audio-rate modulation or a scaling of your own. There is no mark on screen.

### CV out

Write a value or a slow signal to an output, at 0.2 per volt.
