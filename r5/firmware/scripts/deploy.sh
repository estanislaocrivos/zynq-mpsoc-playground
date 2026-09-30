#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

if [ "$#" -eq 2 ]; then
    ELF="$1"
    TARGET="$2"
else
    echo "Usage: $0 [<file.elf>] <user@host>"
    echo "  e.g: $0 petalinux@192.168.0.155"
    echo "  e.g: $0 build/my.elf petalinux@192.168.0.155"
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "ELF not found: $ELF"
    echo "Run scripts/build.sh first"
    exit 1
fi

echo "Deploying $(basename "$ELF") to $TARGET:/tmp/ ..."
scp "$ELF" "$TARGET:/tmp/"
echo "Done."
