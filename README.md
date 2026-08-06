# Open94 — a native decompilation of NHL Hockey '94 (Genesis)

A byte-perfect rebuild and native decompilation of EA's *NHL Hockey '94* for
the Sega Genesis / Mega Drive.

| | |
|---|---|
| ROM rebuild | **byte-perfect** (md5 `8356b3f0d091b9cc441e2ff8721ad063`, checksum patched in-build) |
| Static recompilation | all 24,955 instructions → C; ~98.9% of execution runs native, cycle-exact |
| Lockstep validation | 4.83 billion instructions verified vs interpreter, **0 divergences** |
| Determinism | whole-run RAM hash bit-identical, native vs interpreter, on every coverage script |
| Decompilation | **468 routines / ~41,600 lines of readable C**, each shadow-verified per call |

Game logic is decompiled; the thin hardware layer (ISRs, VBlank pollers, DMA,
the Z80 sound-driver protocol) intentionally stays behind an emulated
boundary, with the embedded interpreter as its bit-exact reference.

## How it works

```
  3. Readable decomp (native/decomp/) — shadow-verified, flag-exact, cycle-annotated
  2. Static recompilation (generated C) — lockstep-validated, exact cycle model
  1. Byte-perfect source rebuild (Source.asm + asm68k)
  0. Reference emulator (Genesis Plus GX, pinned + patched)
```

- **Rebuild** — `Source.asm` assembles with stock `asm68k` under Wine in
  Docker, checksum is patched, and the result is `cmp`'d against your
  original ROM. A build either matches exactly or fails.
- **Recompile** — `native/recomp/emit.py` translates the listing to C, one
  block per instruction, preserving exact 68000 flag semantics and cycle
  costs. The harness embeds a pinned Genesis Plus GX and swaps its CPU core
  for the recompiled code, with per-instruction interpreter fallback.
  Proven two ways: lockstep validation (`--validate` — every register, flag,
  PC and cycle compared continuously) and A/B determinism (`--ramhash` —
  identical work-RAM hash for interpreter-driven vs native runs).
- **Decompile** — each routine is rewritten as readable C, registered in a
  dispatch table, and shadow-verified: on every real call its full
  write-set, registers, flags, and predicted cycles are compared against
  the original code before it ever runs live. Flag-exact ALU helpers
  (`util68k.h`), per-instruction cycle annotations, and atomic commit with
  interpreter fallback keep partial coverage safe and interrupt timing
  identical. Hardware writes are staged and replayed through the real
  emulator handlers, with tiered verification for interrupt-crossed spans.

Routines that synchronize with hardware (pollers spinning on ISR-written
counters, DMA primitives, the Z80 mailbox) are not atomically liftable by
construction and permanently remain interpreter-owned — the same
hardware-abstraction line mature decomp projects draw. All 657 functions in
the ROM are classified: decompiled, runtime core, or its structural shell.

## Building

**Bring your own legally-obtained ROM** — this repository contains none:

```
_0 Temp/NHL Hockey 94 Ori.bin        (md5 8356b3f0d091b9cc441e2ff8721ad063)
```

Everything runs in Docker; no host toolchain required.

```bash
# one-time: build the two images
docker build -t nhl94-wine "_Assembly Tools/linux"
docker build -t nhl94-dev  native

# 1. assemble + verify byte-perfect (also carves the four incbin'd
#    art/palette data files out of your ROM)
./build-linux.sh

# 2. regenerate the recompiled C from the fresh listing
python3 native/recomp/emit.py

# 3. fetch the pinned emulator core and build the harness
native/fetch-deps.sh
native/build.sh

# 4. run the gates
docker run --rm -v "$PWD":/work -w /work nhl94-dev \
  ./native/harness/harness dist/nhl94-build.bin \
  --validate --lift-verify --script native/scripts/start-game.txt
```

Harness flags: `--validate` (lockstep verify) · `--native` (recompiled C
drives execution) · `--lift` / `--lift-verify` (decomp routines live /
shadow-checked) · `--ramhash N` (A/B determinism) · `--script` (scripted
input) · `--shot` (screenshot) · `--profout` (profiling). A macOS SDL2 app
builds via `native/make-mac-bundle.sh` (see `native/README-mac.md`).

## Layout

```
Source.asm, Unknown/, Equates.asm, ...   the 68k source (reassembles byte-perfect)
_Assembly Tools/                         stock asm68k + build utilities (+ Dockerfile)
build-linux.sh                           assemble → checksum → verify → dist/
native/
  recomp/    emit.py (instruction translator), liftscan.py (triage/ranking)
  harness/   headless runner: validator, native dispatcher, lift runtime
  decomp/    THE DECOMP — readable C: game.c, render.c, anim.c, vdp.c,
             overlay.c, controls.c, save.c, math.c · registry.c · util68k.h
  app/       SDL2 windowed app (macOS)
  scripts/   scripted-input coverage runs + RAM-hash reference checkpoints
  vendor-patches/  pinned Genesis-Plus-GX integration patch
```

## Legal

Non-commercial preservation / interoperability research. *NHL Hockey '94*
is © Electronic Arts; NHL and NHLPA marks belong to their owners. This
repository distributes **no ROM image and no original binary assets** —
the build requires your own copy of the game and verifies against it. The
tooling in `native/` (harness, recompiler, decompilation) is this
project's own work.

## Acknowledgments

- **MarkeyJester** — the NHL Hockey '94 disassembly this project builds
  on, via the [nhl94.com](https://nhl94.com) community.
- [Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX) (pinned at
  `8ae4ef7`) — the reference emulator core this project validates against.
- The [nhl94.com](https://nhl94.com) community — decades of ROM maps,
  editing guides, and institutional knowledge.
