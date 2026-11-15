# SH7305 probe (gint / Hollyhock-3)

A ten-page diagnostic for the Casio ClassPad II (fxCP), built with gint
and loaded by the Hollyhock-3/YAL launcher.

## Build

Inside the repository's fxSDK devcontainer:

```sh
fxsdk build-cp
```

The build generates **two separate files** under `build-cp/`:

- `SH7305Probe.hh3` — the small executable add-in.
- `romprobe.bin` — the 1,572,864-byte (0x180000) external payload.

Copy both files to the calculator's storage root and launch the HH3
with Hollyhock-3/YAL. Press a key to advance through the ten pages.

To regenerate the binary independently:

```sh
python3 tools/make_romprobe.py romprobe.bin
```

## Saved results

On each launch the add-in writes a plain-text report to the calculator's
storage root: \`\\fls0\\results.txt\` (gint path \`/results.txt\`). It overwrites
the previous run and records CPU, clock, MMU, code mapping, and full ROM
file-verification results even when the ROM check fails. The final screen
shows \`SAVE OK\` or \`SAVE FAILED\` with an error code. File operations
use gint's native fxCP Fugue filesystem directly (no world switch).

## Data layout

The binary is zero-filled except for:

| Offset | Byte |
| --- | --- |
| `0x000000` | `0x5a` |
| `0x080000` | `0xa5` |
| `0x17ffff` | `0x3c` |

The add-in opens `/romprobe.bin`, verifies its exact file length, and
streams all bytes through a 4 KiB buffer. It reports marker matches and
full-file integrity without linking 1.5 MiB into the HH3 image.

## Interpretation

The SH7305 **does** support MMU translation: see
[NTOS_SH4/src/mmu.hpp](https://github.com/TheRainbowPhoenix/NTOS_SH4/blob/main/src/mmu.hpp)
and [tlb_helper.S](https://github.com/TheRainbowPhoenix/NTOS_SH4/blob/main/src/tlb_helper.S).
The diagnostic reads the live MMUCR. When address translation (AT) is on,
it queries gint's UTLB translation for P0 addresses; P1/P2 use their
direct physical aliases. No UTLB entries are changed by this add-in.

The external file check is **file integrity**, not a physical ROM mapping
test: the file is held in storage, not the add-in's `.rodata` segment.
Its physical mapping or contiguous ROM residency cannot be inferred from
the streaming result. To test 1.5 MiB of UTLB-backed mapped memory would
require an additional, explicitly mapped resident backing buffer or a
file-backed pager such as the experimental NTOS loader uses.

## CI

GitHub Actions uses `ghcr.io/therainbowphoenix/gint-docker:main` to build
with `fxsdk build-cp`, verify the external payload and HH3 size, and
upload both files together as `SH7305Probe-fxCP`.
