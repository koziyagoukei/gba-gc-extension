#!/bin/bash
# The request V20 cases of battle_test.c (Life Orb, docs/items.md 13.7), which
# the default build leaves out. Before the core implements V20 they fail at
# the start (the request is refused) and this script exits 1.
set -e
cd "$(dirname "$0")/.."
B="protocol/wire.c protocol/message.c protocol/pkmn_protocol.c protocol/battle_protocol.c gamecube/battle/battle_core.c gamecube/battle/battle_effects.c gamecube/battle/battle_abilities.c gamecube/battle/battle_modifiers.c gamecube/battle/battle_field.c gamecube/battle/battle_items.c"
out=${TMPDIR:-/tmp}/battle_test_v20
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -DGC_TEST_V20 -Iprotocol -Igamecube/battle -Igamecube/presentation $B gamecube/battle/battle_test.c -o "$out"
"$out"
