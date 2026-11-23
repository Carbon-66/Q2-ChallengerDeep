# Challenger Deep

A ship-fighting survival game for the Shanling Q2 under [Q2 Pod](https://github.com/DiamondBond/q2-pod),
as a full-screen **card game** in Library → Games. Sail one of nine ships on one of five seas
and sink wave after wave of pirate and navy ships, steering with the wheel and firing broadsides
with Centre.

The game is new code. Its art, fonts and sound effects are read at run time from the player's
own copy of the 2008 click-wheel iPod game *Pirates of the Caribbean* (the `.ipg` file), put in
the card folder. **No file from that game is in this repository or in the card folder as
built**: don't commit one, and don't attach one to a release.

The player's guide (install, controls, how to play) is [card/README.txt](card/README.txt), copied
into the card folder as `README.txt`.

## Building

```sh
./build.sh        # build/q2-challengerdeep and dist/.challengerdeep (copy .challengerdeep to the card's root)
PS_IPG=/path/to/your.ipg test/run.sh   # host tests: the real main() on a fake Q2 panel
```

A release is a zip of the card folder, `.challengerdeep/` holding `q2-challengerdeep`, `q2app`,
the icons and `README.txt`: Q2 Pod's App Store takes the app's folder from the repository's name
less `q2-`, and installs a release only with that folder and program in it. It treats only a new
tag as an update.

Needs `clang`, `ld.lld` and `python3` (with Pillow for `tools/icon.py` and the tests). Built the
same way as q2-doom and Q2A Miami: no MIPS sysroot. The binary matches the ABI of Q2 Pod's own
helpers: MIPS32r2 little-endian, o32, FR=1, NaN2008, glibc 2.28, interpreter
`/lib/ld-linux-mipsn8.so.1`. `sysroot/include` declares the libc used and `sysroot/stubs/gen.py`
makes stub `libc.so.6`, `libpthread.so.0` and `libasound.so.2` to link against, so the Q2's own
libraries are what run. If you add a libc call, add it to `gen.py` too (the link fails until you
do). `rt/` has the entry point crt1.o would give and libgcc's 64-bit division.

## Layout

| Path | What it is |
| --- | --- |
| `src/q2.c` | The platform: the portrait panel, Q2 Pod's input datagrams, the log, `main()` |
| `src/sound.c` | The mixer, on ALSA as Q2 Pod's q2video plays (`audio=1`) |
| `src/zip.c` | The `.ipg` (a zip) and a raw-deflate decoder after zlib's puff |
| `src/assets.c` | The iPod game's texture banks, sound banks, maps and fonts |
| `src/gfx.c` | Drawing: paletted sprites, tiled water, text, shapes; sine and arctangent |
| `src/game.c` | The game: ships, cannon, waves, pickups, the HUD and the menus |
| `card/` | `q2app`, icons and the player's README |
| `test/` | Host build, fake framebuffer (`fbshim.c`) and ALSA (`alsa_fake.c`), scripted play (`drive.py`), frame viewer |

## The iPod game's files

Worked out for this game; the `.ipg`'s executables are FairPlay-encrypted and aren't read.

- **Texture banks** (`gametb*.dat`, `maintbui.dat`, `plyrship_*.dat`): 64 entries of 110 bytes
  (file name, 30 bytes; atlas name, 60; then the data's size, its OpenGL ES format, type, width
  and height as little-endian words), then each entry's data from byte 7040. The formats are
  `OES_compressed_paletted_texture`'s: `0x8b96` PALETTE8_RGBA8 and `0x8b97` PALETTE8_R5_G6_B5.
- **Ships**: the player's in a 5 × 5 grid of 24 headings, the enemies' 4 × 4 of 16, in reading
  order anticlockwise from east.
- **Sound banks** (`sb*.dat`): 64 entries of 113 bytes (file name; cue name; size; a loop flag;
  bits, channels and rate), then unsigned 8-bit samples from byte 7232, each padded to even.
- **Maps** (`Caribbean.flat` and the others): text; after the `…Tileset` line, the width and
  height in 32 px tiles and the tiles as `N*T` runs.
- **Fonts**: grey TGAs, the glyphs in rows; the sheets have no full stop, comma or hyphen, so
  those are drawn.

## What it doesn't do yet

- **Music.** The iPod game's music is AAC (`.m4a`) and there is no AAC decoder on the Q2 for a
  card game to use, so only the sound effects play.
- The kraken, whirlpools, storms and ports in the banks aren't used.
