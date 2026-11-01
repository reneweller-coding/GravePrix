#!/usr/bin/env bash
# Builds GRAVE PRIX for macOS (Release, Apple's clang, the runner's architecture) and packs
# dist/GravePrix-<version>-macos-<arch>.zip: the program (ad hoc signed, so Apple Silicon starts it), its data and
# assets, the player's readme, the credits and the licence. Made on GitHub's macOS runners (release.yml); nobody here
# has a Mac, so it is compiled and checked (--validate) but never played.
#
#   bash Deploy/build_macos.sh [BUILD_DIR]      (default build/macos)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/macos}"
version="$(sed -n 's/^project( GravePrix VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
arch="$(uname -m)"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel
"$build/GravePrix" --validate --data "$root/data" | tail -n 1

name="GravePrix-$version-macos-$arch"
stage="$build/release/$name"
rm -rf "$build/release"
mkdir -p "$stage"
cp "$build/GravePrix" "$stage/GravePrix"
strip "$stage/GravePrix"
codesign --force --sign - "$stage/GravePrix"
cp -R "$root/data" "$stage/data"
cp -R "$root/assets" "$stage/assets"
cp "$root/docs/README-player.txt" "$stage/README.txt"
cp "$root/docs/CREDITS.txt" "$stage/"
cp "$root/LICENSE" "$stage/"
mkdir -p "$root/dist"
( cd "$build/release" && zip -qry "$root/dist/$name.zip" "$name" )
echo "$root/dist/$name.zip"
