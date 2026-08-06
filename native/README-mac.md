# NHL Hockey '94 — native app, macOS build

One-time setup (needs [Homebrew](https://brew.sh)):

```bash
brew install sdl2
```

Build and run:

```bash
make -C app
./app/nhl94 nhl94-build.bin
```

## Controls

| Genesis | Keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad / left stick |
| A / B / C | Z / X / C | A / B / X |
| Start | Enter | Start |
| Quit | Esc | — |

Battery save is written to `nhl94.srm` in the working directory on quit
(pass `--sram FILE` to change).

## Getting fresh builds

The Linux box serves the latest verified ROM at
`http://192.168.0.72:8016/nhl94-build.bin` — re-download it and re-run;
the app itself only needs rebuilding when the native shell changes.

## Recording play sessions (Stage D test coverage)

Run with `--record`:

```bash
./app/nhl94 nhl94-build.bin --record session1.rec
```

Play normally, quit with Esc. Send `session1.rec` (and the auto-created
`session1.rec.srm`) back to the Linux box — every recorded game becomes
a deterministic replay used to verify the recompiled code.
