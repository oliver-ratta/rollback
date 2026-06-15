#!/usr/bin/env bash
set -euo pipefail

# Compile main.c with libgit2 and run the executable (Debian)
# Usage: ./run.sh [compiler args...] -- [program args...]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/main.c"
OUT="$SCRIPT_DIR/main"

# Check for gcc
if ! command -v gcc >/dev/null 2>&1; then
    echo "gcc not found. Install build-essential (sudo apt install build-essential)" >&2
    exit 1
fi

# Check for pkg-config
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config not found. Please install it (sudo apt install pkg-config)" >&2
    exit 1
fi

# Check if libgit2 development package is available to pkg-config
if ! pkg-config --exists libgit2; then
    echo "libgit2 metadata not found. Please install libgit2-dev (sudo apt install libgit2-dev)" >&2
    exit 1
fi

# Split args: options for compiler before '--', program args after '--'
COMPILER_ARGS=()
PROG_ARGS=()
SEEN_DASHDASH=0

for a in "$@"; do
    if [ "$SEEN_DASHDASH" -eq 1 ]; then
        PROG_ARGS+=("$a")
    else
        if [ "$a" = "--" ]; then
            SEEN_DASHDASH=1
        else
            COMPILER_ARGS+=("$a")
        fi
    fi
done

if [ ! -f "$SRC" ]; then
    echo "Source file main.c not found in $SCRIPT_DIR" >&2
    exit 2
fi

# Fetch libgit2 flags
GIT2_CFLAGS=$(pkg-config --cflags libgit2)
GIT2_LIBS=$(pkg-config --libs libgit2)

echo "Compiling $SRC -> $OUT"
# Note: ${GIT2_LIBS} is intentionally left unquoted to expand correctly into separate arguments for gcc
gcc -std=c11 -Wall -Wextra -O2 ${GIT2_CFLAGS} "${COMPILER_ARGS[@]}" -o "$OUT" "$SRC" ${GIT2_LIBS}



