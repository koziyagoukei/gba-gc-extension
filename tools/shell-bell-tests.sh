#!/bin/bash
# Only the Shell Bell cases of battle_test.c (request V21, docs/shell-bell.md
# section 8), written failing first in M4-B1c-1a; the default build runs them
# too since M4-B1c-1b. Exits 0 when every case starts, reaches its effect
# checks and passes, and every refusal holds.
set -e
cd "$(dirname "$0")/.."
B="protocol/wire.c protocol/message.c protocol/pkmn_protocol.c protocol/battle_protocol.c gamecube/battle/battle_core.c gamecube/battle/battle_effects.c gamecube/battle/battle_abilities.c gamecube/battle/battle_modifiers.c gamecube/battle/battle_field.c gamecube/battle/battle_items.c"
out=${TMPDIR:-/tmp}/battle_test_shell_bell
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -DGC_TEST_SHELL_BELL -Iprotocol -Igamecube/battle -Igamecube/presentation $B gamecube/battle/battle_test.c -o "$out"
"$out"
