# SH7305 probe (gint / Hollyhock-3)

A nine-page diagnostic for the Casio ClassPad II (fxCP), built with gint
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
with Hollyhock-3/YAL. Press a key to advance through the nine pages.

To regenerate the binary independently:

```sh
python3 tools/make_romprobe.py romprobe.bin
```

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

This is a **file integrity test**, not a physical ROM mapping test.
On ClassPad/Hollyhock-3 gint executes in directly addressed RAM without
an active MMU translation. The external file is held in storage, not in
the add-in's `.rodata` segment. Its physical mapping or contiguous ROM
residency therefore **cannot be inferred** from these results.

The CPU and CPG pages show hardware register values. The code map shows
the direct P1 alias and checks its first instruction word if the code is
located in P1/P2. The original user-RAM and UTLB probes are displayed as
not applicable to fxCP.
