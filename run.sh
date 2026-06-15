
#!/usr/bin/env bash
set -euo pipefail

# Compile main.c and run the executable (Debian)
# Usage: ./run.sh [compiler args...] -- [program args...]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/main.c"
OUT="$SCRIPT_DIR/main"

if ! command -v gcc >/dev/null 2>&1; then
	echo "gcc not found. Install build-essential (sudo apt install build-essential)" >&2
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

echo "Compiling $SRC -> $OUT"
gcc -std=c11 -Wall -Wextra -O2 "${COMPILER_ARGS[@]}" -o "$OUT" "$SRC"


