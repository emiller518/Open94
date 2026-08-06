#!/bin/bash
# Linux build for the NHL Hockey '94 disassembly.
# Replicates the game's original Windows build inside the nhl94-wine docker image
# (see "_Assembly Tools/linux/Dockerfile"): the stock Asm68k.exe runs
# under wine; CheckFix/ListEqu/PatchRom/CompRom are compiled natively
# from their C sources. Verifies the ROM against the original at the end.
set -e
cd "$(dirname "$0")"

ROM="_0 Temp/NHL Hockey 94 Ori.bin"
if [ ! -f "$ROM" ]; then
    echo "error: put your legally-obtained ROM at: $ROM" >&2
    exit 1
fi

# Carve the incbin'd art/palette data files from the user's ROM (this repo
# ships no original game bytes; Unknown/44F54.asm + Unknown/4FE04.asm
# include these four files at build time).
extract() { # path offset length
    if [ ! -f "$1" ]; then
        mkdir -p "$(dirname "$1")"
        dd if="$ROM" of="$1" iflag=skip_bytes,count_bytes skip=$(($2)) count=$3 status=none
        echo "extracted: $1 ($3 bytes @ $2)"
    fi
}
extract "Screen Title/Data/Art 'NHL' Logo.bin"   0x52DB4 1280
extract "Screen Title/Data/Pal 'NHL' Logo.bin"   0x532B4  128
extract "Screen Menu/Data/Art 'AT' Bar.bin"      0x4DF18  896
extract "Screen Menu/Data/Palette.bin"           0x4E298  128

docker run --rm -v "$PWD":/work -w /work nhl94-wine bash -ec '
    wine "_Assembly Tools/Asm68k.exe" /q /p Source.asm, "NHL Hockey 94.bin", , Listings.asm
    CheckFix "NHL Hockey 94.bin" "Common Library/Checksum/CheckValue.asm"
    ListEqu asm68k 68k Listings.asm asm68k 68k "Common Library/Checksum/EquMain.asm"
    mkdir -p native/recomp && mv Listings.asm native/recomp/Listings.asm
    cd "Common Library/Checksum"
    wine "../../_Assembly Tools/Asm68k.exe" /q /p Source.asm, Checksum.bin, , ChkListings.asm
    mv ChkListings.asm ../../native/recomp/ChecksumListings.asm
    cd ../..
    PatchRom "Common Library/Checksum/Checksum.bin" "NHL Hockey 94.bin" "Common Library/Checksum/EquMain.asm" CalcChecksum
    CompRom 200 "_0 Temp/NHL Hockey 94 Ori.bin" "NHL Hockey 94.bin"
    chown -R '"$(id -u):$(id -g)"' "NHL Hockey 94.bin" "Common Library/Checksum" 2>/dev/null || true
'

echo "--- full-file verification ---"
if cmp "_0 Temp/NHL Hockey 94 Ori.bin" "NHL Hockey 94.bin"; then
    echo "BYTE-PERFECT: build matches original ROM exactly."
else
    echo "MISMATCH: build differs from original ROM."
    exit 1
fi

# Publish to dist/ for LAN download (served by the nhl94-rom systemd unit, port 8016)
mkdir -p dist
cp "NHL Hockey 94.bin" "dist/nhl94-build.bin"
{
    echo "built:  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "md5:    $(md5sum 'NHL Hockey 94.bin' | cut -d' ' -f1)"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo 'n/a')"
} > dist/build-info.txt
echo "Published to dist/nhl94-build.bin"
