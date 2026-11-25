#!/bin/sh
set -eu
cd "$(dirname "$0")"
out=$(mktemp -d)
trap 'rm -f "$out/optimized" "$out/sanitized"; rmdir "$out"' EXIT
# Separate optimized and sanitizer runs keep both configurations reproducible.
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic item_hook.c test_item_hook.c -o "$out/optimized"
"$out/optimized"
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    item_hook.c test_item_hook.c -o "$out/sanitized"
"$out/sanitized"
