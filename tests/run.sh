#!/bin/sh
set -eu
set -f

pixter_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pixter_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/pixter-tests.XXXXXX")
trap 'rm -rf "$pixter_test_dir"' EXIT HUP INT TERM
pixter_cc=${CC:-cc}
pixter_cflags=${CFLAGS:--O2}
if [ "${SDL_CFLAGS+x}" != x ]; then
	SDL_CFLAGS="$(pkg-config --cflags sdl2) -I$(pkg-config --variable=includedir sdl2)"
fi
if [ "${SDL_LIBS+x}" != x ]; then
	SDL_LIBS=$(pkg-config --libs sdl2)
fi

# Compiler and linker flag variables intentionally undergo word splitting.
"$pixter_cc" $pixter_cflags -std=gnu11 $SDL_CFLAGS "$pixter_root/tests/melody.c" \
	-o "$pixter_test_dir/melody-classic" $SDL_LIBS
"$pixter_cc" $pixter_cflags -std=gnu11 -DCHECK_COLOR $SDL_CFLAGS "$pixter_root/tests/melody.c" \
	-o "$pixter_test_dir/melody-color" $SDL_LIBS
"$pixter_test_dir/melody-classic"
"$pixter_test_dir/melody-color"
