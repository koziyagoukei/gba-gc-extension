#!/bin/sh
# Builds and runs the native tests of the GBA connector's generic layer
# against a pokeemerald-expansion checkout (gc-extension branch or a
# content branch that carries it), e.g.:
#   tools/connector-test.sh ../decomps/pokeemerald-frontier-gc
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
target=${1:?usage: connector-test.sh <pokeemerald checkout>}
out=${TMPDIR:-/tmp}/gcx-connector-tests
mkdir -p "$out"
flags="-std=gnu11 -Wall -Wextra -Wno-unused-parameter -g -fsanitize=address,undefined -fno-sanitize-recover=all"
inc="-iquote $root/tests/connector/stub -iquote $target/include"
common="$target/src/gc_ext/wire.c $target/src/gc_ext/message.c"
for test in "$root"/tests/connector/*_test.c; do
    name=$(basename "$test" .c)
    case $name in
    result_test) sources="$target/src/gc_companion_protocol.c" ;;
    *) sources="$target/src/gc_companion_link.c" ;;
    esac
    gcc $flags $inc "$test" $sources $common -o "$out/$name"
    "$out/$name"
done
