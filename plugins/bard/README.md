# bard

`bard` is two spoken-word players. Each deck plays a book (an audiobook, a lecture, a radio play) from a shelf, remembers where it was, and moves through the book by bookmarks written in a text file beside the audio.

Ported from the [`bard` engine](https://github.com/shakfu/sk-engines/tree/main/src/engine/bard) in [sk-engines](https://github.com/shakfu/sk-engines).

## Install

Copy `bard.so` to `BOOT/plugins` (from source: `make install MOD=bard`) and restart the SSP.

Put the library in `BOOT/bard`, or choose another folder with Load. None ships.

## First use

1. Copy a few `.wav` books into `BOOT/bard/<shelf>/`, where `<shelf>` is any folder name.
2. Add `bard`.
3. Patch Out L and Out R to the outputs ([Using](../../docs/USING.md#a-module-runs-only-while-a-jack-is-patched)).
4. Both decks open the first book and play it. Buttons 1 and 3 pause and play decks A and B.

## Library

- **Root**: a folder of shelves. Load (button 5) opens a browser; navigate into the root and press Load again.

- **Shelf**: each subfolder of the root, in name order. A root without subfolders is a single shelf. A card laid out for the engine's Daisy build, with shelves `bard/0` to `bard/15`, works unchanged.

- **Book**: each `.wav` or `.raw` file on a shelf, in name order. `.wav` is 16, 24 or 32-bit PCM or 32-bit float, any channel count, at its own rate. `.raw` is 16-bit mono at the rate in `bard.cfg`.

- **Sidecar**: `NAME.txt` (or `NAME.TXT`) beside `NAME.wav` lists the bookmarks. Without one, the book gets 4 to 32 marks, one at the start and the rest about every 5 minutes, placed the same way each time.

- **bard.cfg** in the root: `resume=off` stops all writes; `rate=HZ` sets the `.raw` rate (default 48000).

- **resume.txt** in the root: the position of the 64 books played most recently, written every 30 s while a deck plays, on a pause and on a book change.

### Sidecar

```text
#!bard order=file loop=off
# a comment
0:00              Prologue
14:32             Chapter 1
1:02:11-1:04:00   a passage
2841              a bare number is seconds
```

- A time is `[[H:]M:]S[.mmm]`. Text after it is a label, for whoever edits the file.

- A mark without an end runs to the next later mark, or to the end of the book.

- `order=file` plays the marks in line order, `order=time` in time order, `order=shuffle` in a shuffled order that is the same each time.

- `loop=segment` loops a segment in `recite`; `loop=book` restarts the book at its end in `read`; `loop=off` holds.

- A line that does not parse is skipped.

## Controls

The pages form three groups: deck A, deck B, and the global `mix` page ([Using](../../docs/USING.md#buttons)). A long `PG+` goes to `mix`; a long `PG-` returns.

| Page | Encoder 1 | Encoder 2 | Encoder 3 | Encoder 4 |
|-|-|-|-|-|
| A book | Book | Mark | Rate | Keep |
| A shelf | Shelf | Position | Volume | Seq |
| A voice | Colour | Colour Mix | Room | Room Mix |
| A more | Character | Seam | Duck | Release |
| A marks | Loop | Reroll | | |
| B pages | as deck A | | | |
| mix | A/B | Route | | |

- Selectors. Each acts once it has rested 180 ms.
  - **Book**: a book on the shelf.
  - **Mark**: a mark, in line order. It acts only when it moves, so it does not undo Next.
  - **Shelf**: a shelf in the root.

- **Rate**: 0.5x at 0, 1x at the centre, 2.5x at 1.

- **Keep**: at 0 the pitch follows the rate; at 1 the pitch is held.

- **Position**: a move jumps to that point of the book, or of the segment in `recite` and `wander`.

- **Seq**: `read` plays through the book, marks are only jump targets; `recite` plays one segment and stops at its end; `wander` plays the segments in the sidecar's order.

- **Loop**: what a segment end does: `file` takes the sidecar's `loop=` (hold if it has none), `hold` stops, `loop` repeats. In wander, `loop` wraps the play order.

- **Reroll**: re-places the automatic marks; the same value gives the same marks.

- **Colour**: drive and a band-limit, from clean through wireless to telephone. **Colour Mix**: 0 is off.

- **Room**: size of a plate, hall or slap room, chosen by **Character**. **Room Mix**: 0 is off.

- **Seam**: a fade-in after each jump, 0 to 500 ms.

- **Duck**: how far this deck's voice lowers the other deck. **Release**: the duck's recovery, 0 slow, 1 fast.

- **Route**: `stereo` centres both decks; `split` puts A left, B right; `random` pans each deck at random.

| Button | |
|-|-|
| 1, 3 | play/pause deck A, B |
| 2, 4 | back 15 s on deck A, B; not before the start of the segment in `recite` and `wander` |
| 6, 8 | next entry in the play order on deck A, B |

A book opens playing, at its resumed position. The screen shows each deck's shelf, book, position, length and mark.

## Jacks

| In | Name | Does |
|-|-|-|
| 1 | `A Book` | added to Book; 5 V spans the shelf |
| 2 | `A Mark` | added to Mark; 5 V spans the marks |
| 3 | `A Volume` | added to Volume |
| 4 | `A Gate` | a rising edge above 2 V is Next |
| 5-8 | `B Book`, `B Mark`, `B Volume`, `B Gate` | as 1-4, for deck B |
| 9 | `A/B` | added to A/B |

| Out | Name | Carries |
|-|-|-|
| 1, 2 | `Out L`, `Out R` | the mix, through a soft limiter |
| 3, 4 | `A Out`, `B Out` | each deck after Volume and the duck, before the crossfade and route |
| 5, 6 | `A Env`, `B Env` | each deck's voice envelope, 0 to 5 V |
| 7, 8 | `A Gate`, `B Gate` | a 5 ms pulse when the playhead enters another bookmark |
