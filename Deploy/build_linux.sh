#!/usr/bin/env bash
# Builds GRAVE PRIX for Linux (Release, GCC 12) and packs dist/GravePrix-<version>-linux-x86_64.tar.gz: the
# program, its data and assets, the player's readme, the credits and the licence. Built on Ubuntu 22.04 it runs on glibc 2.35 and
# newer, SteamOS (the Steam Deck) included.
#
#   bash Deploy/build_linux.sh [BUILD_DIR]      (default ~/build/graveprix-release; nice -n 15, four jobs)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$HOME/build/graveprix-release}"
version="$(sed -n 's/^project( GravePrix VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"

# raylib's source from the Windows build where there is one (no download), Ninja where there is one.
extra=()
[ -d "$root/build/msvc/_deps/raylib-src" ] && extra+=(-DFETCHCONTENT_SOURCE_DIR_RAYLIB="$root/build/msvc/_deps/raylib-src")
command -v ninja > /dev/null && extra+=(-G Ninja)
CC="${CC:-gcc-12}" CXX="${CXX:-g++-12}" cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
nice -n 15 cmake --build "$build" --parallel 4

name="GravePrix-$version-linux-x86_64"
stage="$build/release/$name"
rm -rf "$build/release"
mkdir -p "$stage"
cp "$build/GravePrix" "$stage/GravePrix"
strip "$stage/GravePrix"
cp -r "$root/data" "$stage/data"
cp -r "$root/assets" "$stage/assets"
cp "$root/docs/README-player.txt" "$stage/README.txt"
cp "$root/docs/CREDITS.txt" "$stage/"
cp "$root/LICENSE" "$stage/"
mkdir -p "$root/dist"
tar -C "$build/release" -czf "$root/dist/$name.tar.gz" "$name"
echo "$root/dist/$name.tar.gz"
