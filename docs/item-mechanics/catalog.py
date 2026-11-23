"""Produce the item audit CSVs from the pinned Frontier Git tree.

Python 3.10+. Reads Git objects only. Writes CSV to stdout.
"""
import argparse
from collections import Counter, defaultdict
import csv
import re
import subprocess
import sys

REVISION = "2d1e107ad820a3b0a601af968b49856f74151ee1"
ITEMS = "src/data/items.h"
EFFECTS = "src/data/hold_effects.h"
SETS = "src/data/battle_frontier/battle_frontier_mons.h"
FORMS = "src/data/pokemon/form_change_tables.h"
EXPORTER = "src/frontier_gc_battle.c"
HOLDERS = r"(?m)^[ \t]*\[(HOLD_EFFECT_\w+)\]\s*=\s*\{([^}]*)\}"
INITIALIZERS = r"(?m)^[ \t]*\[(ITEM_\w+)\]\s*=\s*\{"

FAMILIES = {
    "ITM-DAMAGE": "TYPE_POWER PLATE GEMS RESIST_BERRY EXPERT_BELT LIFE_ORB METRONOME MUSCLE_BAND WISE_GLASSES SOUL_DEW ADAMANT_ORB LUSTROUS_ORB GRISEOUS_ORB OGERPON_MASK PUNCHING_GLOVE",
    "ITM-STATS": "CHOICE_BAND CHOICE_SCARF CHOICE_SPECS ASSAULT_VEST EVIOLITE LIGHT_BALL DEEP_SEA_TOOTH DEEP_SEA_SCALE QUICK_POWDER METAL_POWDER THICK_CLUB MACHO_BRACE POWER_ITEM",
    "ITM-ORDER": "CHOICE_BAND CHOICE_SCARF CHOICE_SPECS ASSAULT_VEST QUICK_CLAW LAGGING_TAIL CUSTAP_BERRY SHED_SHELL IRON_BALL QUICK_POWDER MACHO_BRACE POWER_ITEM",
    "ITM-ACCURACY": "WIDE_LENS ZOOM_LENS EVASION_UP MICLE_BERRY BLUNDER_POLICY",
    "ITM-CRITICAL": "CRITICAL_UP SCOPE_LENS LUCKY_PUNCH LEEK",
    "ITM-HP": "RESTORE_HP RESTORE_PCT_HP CONFUSE_FLAVOR ENIGMA_BERRY SHELL_BELL BIG_ROOT",
    "ITM-CURE": "CURE_PAR CURE_SLP CURE_PSN CURE_BRN CURE_FRZ CURE_CONFUSION CURE_STATUS MENTAL_HERB",
    "ITM-ENDURE": "FOCUS_BAND FOCUS_SASH",
    "ITM-HIT": "AIR_BALLOON ROCKY_HELMET WEAKNESS_POLICY SNOWBALL LUMINOUS_MOSS CELL_BATTERY ABSORB_BULB JABOCA_BERRY ROWAP_BERRY STICKY_BARB ENIGMA_BERRY KEE_BERRY MARANGA_BERRY",
    "ITM-MOVE-END": "LIFE_ORB SHELL_BELL FLINCH THROAT_SPRAY BLUNDER_POLICY RESTORE_PP WHITE_HERB MIRROR_HERB RED_CARD EJECT_BUTTON EJECT_PACK",
    "ITM-STAGE": "ATTACK_UP DEFENSE_UP SPEED_UP SP_ATTACK_UP SP_DEFENSE_UP RANDOM_STAT_UP WHITE_HERB MIRROR_HERB CLEAR_AMULET WEAKNESS_POLICY SNOWBALL LUMINOUS_MOSS CELL_BATTERY ABSORB_BULB KEE_BERRY MARANGA_BERRY TERRAIN_SEED ROOM_SERVICE ADRENALINE_ORB BOOSTER_ENERGY BERSERK_GENE THROAT_SPRAY BLUNDER_POLICY",
    "ITM-SWITCH": "RED_CARD EJECT_BUTTON EJECT_PACK SHED_SHELL HEAVY_DUTY_BOOTS AIR_BALLOON",
    "ITM-RESIDUAL": "LEFTOVERS BLACK_SLUDGE FLAME_ORB TOXIC_ORB STICKY_BARB WHITE_HERB MIRROR_HERB BINDING_BAND GRIP_CLAW BIG_ROOT",
    "ITM-FIELD": "DAMP_ROCK HEAT_ROCK ICY_ROCK SMOOTH_ROCK TERRAIN_EXTENDER TERRAIN_SEED LIGHT_CLAY SAFETY_GOGGLES UTILITY_UMBRELLA ROOM_SERVICE AIR_BALLOON IRON_BALL",
    "ITM-SPECIAL": "POWER_HERB FLOAT_STONE RING_TARGET DESTINY_KNOT PROTECTIVE_PADS ABILITY_SHIELD COVERT_CLOAK LOADED_DICE SAFETY_GOGGLES PUNCHING_GLOVE CLEAR_AMULET",
    "ITM-GIMMICK": "MEGA_STONE Z_CRYSTAL PRIMAL_ORB DRIVE MEMORY PLATE GRISEOUS_ORB OGERPON_MASK",
    "ITM-POLICY": "DOUBLE_PRIZE EXP_SHARE LUCKY_EGG FRIENDSHIP_UP PREVENT_EVOLVE REPEL CAN_ALWAYS_RUN MACHO_BRACE POWER_ITEM",
}
PER_USE = set("""RESTORE_HP RESTORE_PCT_HP CURE_PAR CURE_SLP CURE_PSN CURE_BRN CURE_FRZ RESTORE_PP CURE_CONFUSION CURE_STATUS CONFUSE_FLAVOR ATTACK_UP DEFENSE_UP SPEED_UP SP_ATTACK_UP SP_DEFENSE_UP CRITICAL_UP RANDOM_STAT_UP WHITE_HERB MENTAL_HERB FOCUS_SASH POWER_HERB ENIGMA_BERRY RESIST_BERRY MICLE_BERRY CUSTAP_BERRY JABOCA_BERRY ROWAP_BERRY KEE_BERRY MARANGA_BERRY GEMS RED_CARD EJECT_BUTTON ABSORB_BULB CELL_BATTERY LUMINOUS_MOSS SNOWBALL WEAKNESS_POLICY TERRAIN_SEED ADRENALINE_ORB EJECT_PACK ROOM_SERVICE BLUNDER_POLICY THROAT_SPRAY MIRROR_HERB BOOSTER_ENERGY BERSERK_GENE""".split())
V19_PASSIVE = set("TYPE_POWER MUSCLE_BAND WISE_GLASSES DAMP_ROCK HEAT_ROCK ICY_ROCK SMOOTH_ROCK TERRAIN_EXTENDER LEFTOVERS BLACK_SLUDGE FOCUS_SASH".split())
V19_BERRY = set("RESTORE_HP RESTORE_PCT_HP CURE_PAR CURE_SLP CURE_PSN CURE_BRN CURE_FRZ CURE_CONFUSION CURE_STATUS".split())
V19_INERT = set("NONE DOUBLE_PRIZE EXP_SHARE LUCKY_EGG FRIENDSHIP_UP PREVENT_EVOLVE REPEL CAN_ALWAYS_RUN".split())

def git(repo, *args):
    return subprocess.check_output(["git", "--no-optional-locks", "-c",
                                    f"safe.directory={repo}", "-C", repo, *args]).decode("utf-8")

def line(source, offset):
    return source.count("\n", 0, offset) + 1

def fields(block):
    return {key: value.strip().rstrip(",").strip()
            for key, value in re.findall(r"(?m)^\s*\.(\w+)\s*=\s*([^\n]+)", block)}

def output(columns, rows):
    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(columns.split())
    writer.writerows(rows)

def enum_values(source, machines):
    # TM/HM macros introduce aliases only. IDs restart explicitly after them.
    scope = source.split("enum __attribute__((packed)) Item", 1)[1].split("};", 1)[0]
    values = {}
    next_value = 0
    for symbol, rhs in re.findall(r"(?m)^    ([A-Z][A-Z_0-9]+)(?:[ \t]*=[ \t]*([A-Z_0-9]+))?,", scope):
        value = (int(rhs) if rhs.isdecimal() else values[rhs]) if rhs else next_value
        values[symbol] = value
        next_value = value + 1
    for kind in ("TM", "HM"):
        macros = machines.split(f"#define FOREACH_{kind}(F)", 1)[1].split("#", 1)[0]
        for number, move in enumerate(re.findall(r"F\((\w+)\)", macros), 1):
            values[f"ITEM_{kind}_{move}"] = values[f"ITEM_{kind}{number:02}"]
    return values

def selected_item_fields(block):
    # For audited fields with conditional alternatives, use the pinned defaults.
    conditions = {
        "I_SITRUS_BERRY_HEAL >= GEN_4": True,
        "I_LAX_INCENSE_BOOST >= GEN_4": True,
        "I_EXP_SHARE_ITEM >= GEN_6": True,
        "B_X_ITEMS_BUFF >= GEN_7": True,
        "B_USE_FROSTBITE == TRUE": False,
        "I_KEY_ESCAPE_ROPE >= GEN_8": True,
        "I_KEY_FOSSILS >= GEN_4": True,
    }
    lines = block.splitlines(keepends=True)
    for index, text in enumerate(lines):
        directive = text.strip()
        if not directive.startswith("#if "):
            continue
        condition = directive[4:]
        if condition not in conditions:
            continue
        end = index + 1
        active = conditions[condition]
        while lines[end].strip() != "#endif":
            token = lines[end].strip()
            if token == "#else":
                active = not active
            elif token.startswith(("#if", "#elif")):
                raise ValueError(f"Unexpected nested condition: {token}")
            elif not active:
                lines[end] = "\n"
            end += 1
    return fields("".join(lines))

def catalog(read, ids, roster, role):
    source = read(ITEMS)
    forms = read(FORMS)
    form_lines = defaultdict(list)
    for number, text in enumerate(forms.splitlines(), 1):
        if "FORM_CHANGE_" in text:
            for symbol in set(re.findall(r"\bITEM_[A-Z_0-9]+\b", text)) - {"ITEM_NONE"}:
                form_lines[symbol].append(number)
    entries = list(re.finditer(INITIALIZERS, source))
    result = {}
    for index, entry in enumerate(entries):
        name = entry[1]
        block = source[entry.end():entries[index + 1].start() if index + 1 < len(entries) else len(source)]
        obj = selected_item_fields(block)
        effect = obj.get("holdEffect", "HOLD_EFFECT_NONE")
        short = effect.removeprefix("HOLD_EFFECT_")
        raw_name = re.search(r'"([^"]*)"', obj.get("name", ""))
        literal = raw_name[1] if raw_name else ""
        ja = literal.replace("{JPN}", "") if "{JPN}" in literal or re.search(r"[ぁ-龯ァ-ヿ]", literal) else ""
        en = literal.replace("{ENG}", "") if literal and not ja else ""
        pocket = obj.get("pocket", "")
        form = form_lines.get(name, [])
        if len(set(obj) & {"holdEffect", "holdEffectParam"}) and "#if" in block:
            conditional = "1"
        else:
            conditional = ""
        if name == "ITEM_NONE":
            gc = "NOT_APPLICABLE:no-held-item"
        elif name == "ITEM_ENIGMA_BERRY_E_READER":
            gc = "UNSUPPORTED:save-defined"
        elif name in role:
            gc = "PARTIAL:allowlist+species+rule+roster"
        elif form:
            gc = "PARTIAL:species-dependent-form-check"
        elif pocket == "POCKET_BERRIES":
            gc = "PARTIAL:V19-berry" if short in V19_BERRY else "UNSUPPORTED:berry-effect"
        elif short in V19_PASSIVE:
            gc = "PARTIAL:version+rules+roster"
        elif short in V19_INERT:
            gc = "NOT_APPLICABLE:held-effect-only"
        else:
            gc = "UNSUPPORTED"
        tags = [family for family, members in FAMILIES.items() if short in members.split()]
        if form:
            tags.append("ITM-FORM")
        if obj.get("flingPower", "0") != "0":
            tags.append("ITM-FLING")
        if obj.get("battleUsage") not in (None, "EFFECT_ITEM_NONE"):
            tags.append("ITM-BAG")
        if not tags:
            tags.append("ITM-PRESENCE")
        result[name] = (ids[name], name, ja, en, obj.get("name", ""), effect,
                        obj.get("holdEffectParam", "0"), pocket, obj.get("secondaryId", ""),
                        obj.get("flingPower", ""), obj.get("battleUsage", ""),
                        obj.get("battleUseFunc", ""), obj.get("fieldUseFunc", ""),
                        " ".join(tags), gc, len(roster.get(name, [])), line(source, entry.start()),
                        conditional, " ".join(map(str, form)))
    return result

def frontier_sets(source):
    result = defaultdict(list)
    for entry in re.finditer(r"(?ms)^[ \t]*\[(FRONTIER_MON_\w+)\]\s*=\s*\{\s*\n(.*?)^    \},?", source):
        obj = fields(entry[2])
        obj["set"] = entry[1]
        if "species" not in obj:
            continue
        item = obj.get("heldItem", "ITEM_NONE")
        result[item].append((line(source, entry.start()), obj))
    if sum(map(len, result.values())) != 2040:
        raise ValueError("Expected 2,040 literal Frontier set initializers")
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frontier_repo")
    parser.add_argument("table", choices=("items", "effects", "frontier", "frequency", "summary"))
    args = parser.parse_args()
    repo = args.frontier_repo.replace("\\", "/").rstrip("/")
    def read(path):
        return git(repo, "show", f"{REVISION}:{path}")
    sys.stdout.reconfigure(encoding="utf-8", newline="")
    exporter = read(EXPORTER)
    allow = set(re.findall(r"\bITEM_\w+\b", exporter.split("sAuditedGimmickItems[] =", 1)[1].split("};", 1)[0]))
    roster = frontier_sets(read(SETS))
    ids = enum_values(read("include/constants/items.h"), read("include/constants/tms_hms.h"))
    items = catalog(read, ids, roster, allow)
    missing = roster.keys() - items.keys()
    if len({row[0] for row in items.values()}) != len(items):
        raise ValueError("Duplicate item data ID")
    if missing:
        raise ValueError(f"Missing held item data: {sorted(missing)}")
    held = Counter(row[5] for row in items.values() if row[1] != "ITEM_NONE" and row[5] != "HOLD_EFFECT_NONE")
    if args.table == "items":
        output("item_id item ja en name_source hold_effect hold_param_expr pocket secondary_id fling_power battle_usage battle_use field_use tags gc_v19_hold_gate frontier_sets item_line item_block_has_cpp form_table_lines",
               sorted(items.values()))
    elif args.table == "effects":
        definitions = read("include/constants/hold_effects.h")
        effect_source = read(EFFECTS)
        flags = {m[1]: (" ".join(re.findall(r"\.(\w+)\s*=\s*TRUE", m[2])), line(effect_source, m.start()))
                 for m in re.finditer(HOLDERS, effect_source)}
        hold_source = read("src/battle_hold_effects.c")
        cases = {m[1]: line(hold_source, m.start()) for m in re.finditer(r"case (HOLD_EFFECT_\w+):", hold_source)}
        readers = defaultdict(list)
        paths = git(repo, "ls-tree", "-r", "--name-only", REVISION, "src").splitlines()
        for path in paths:
            if not re.fullmatch(r"src/battle(?:_[a-z_]+)?\.c", path):
                continue
            if any(token in path for token in ("anim", "_ai_", "_graphics", "_bg")):
                continue
            seen = set()
            for number, text in enumerate(read(path).splitlines(), 1):
                for symbol in set(re.findall(r"\bHOLD_EFFECT_\w+\b", text.split("//")[0])) - {"HOLD_EFFECT_NONE"}:
                    if symbol not in seen:
                        readers[symbol].append(f"{path}:{number}")
                        seen.add(symbol)
        tests = set(git(repo, "ls-tree", "-r", "--name-only", REVISION, "test/battle/hold_effect").splitlines())
        rows = []
        for index, m in enumerate(re.finditer(r"(?m)^    (HOLD_EFFECT_\w+),", definitions)):
            symbol = m[1]
            short = symbol.removeprefix("HOLD_EFFECT_")
            info, info_line = flags.get(symbol, ("", ""))
            tags = " ".join(tag for tag, members in FAMILIES.items() if short in members.split())
            loss = "used-on-activation" if short in PER_USE else "burst-not-ordinary-consumption" if short == "AIR_BALLOON" else "transfer" if short == "STICKY_BARB" else "kept-on-activation"
            rows.append((index, symbol, held[symbol], tags or "ITM-PRESENCE", info, loss,
                         line(definitions, m.start()), info_line, cases.get(symbol, ""),
                         " ".join(readers[symbol]), f"test/battle/hold_effect/{short.lower()}.c" if f"test/battle/hold_effect/{short.lower()}.c" in tests else ""))
        output("effect_id hold_effect explicit_items tags dispatcher_flags usual_self_loss enum_line flags_line dispatch_case_line primary_read_sites candidate_test", rows)
    elif args.table in ("frontier", "frequency"):
        abilities = set(re.findall(r"\bABILITY_\w+\b",
                                    exporter.split("static bool8 ItemAbility", 1)[1].split("static bool8 ConsumableItemAbility", 1)[0]))
        move_source = read("src/data/moves_info.h")
        move_entries = re.split(r"(?m)^[ \t]*\[(MOVE_\w+)\]\s*=\s*\{", move_source)
        accepted = set(re.findall(r"case (EFFECT_\w+):",
                                  exporter.split("static u8 ResolveEffects", 1)[1].split("static void ResolveMove", 1)[0]))
        definitely_unsupported = set()
        for move, body in zip(move_entries[1::2], move_entries[2::2]):
            # Unknown/conditional or macro-generated move data remains eligible in this upper bound.
            effects = set(re.findall(r"\.effect\s*=\s*(EFFECT_\w+)\s*,", body))
            if effects and not effects & accepted:
                definitely_unsupported.add(move)
        rows = []
        for item, entries in roster.items():
            for number, obj in entries:
                ability = obj.get("ability", "")
                moves = re.findall(r"\bMOVE_\w+\b", obj.get("moves", ""))
                blockers = [move for move in moves if move in definitely_unsupported]
                rows.append((number, obj["set"], item, obj["species"], ability, " ".join(moves), items[item][5], items[item][14],
                             "listed-item-ability" if ability in abilities else "", " ".join(blockers)))
        if args.table == "frontier":
            output("set_source_line set item species ability moves hold_effect v19_static_gate item_ability_prefilter move_effect_switch_prefilter", sorted(rows))
        else:
            by_item = defaultdict(list)
            for row in rows:
                by_item[row[2]].append(row)
            frequency = []
            for item, group in by_item.items():
                gate = items[item][14]
                item_cap = sum(not row[8] for row in group)
                strict_cap = sum(not row[8] and not row[9] for row in group)
                abilities = Counter(row[4] for row in group if row[8])
                moves = Counter(move for row in group for move in row[9].split())
                frequency.append((item, items[item][5], gate, len(group), item_cap, strict_cap,
                                  " ".join(key for key, _ in abilities.most_common(3)),
                                  " ".join(key for key, _ in moves.most_common(3))))
            output("item hold_effect v19_static_gate sets retained_after_item_ability retained_after_both top_item_ability_blockers top_move_effect_blockers",
                   sorted(frequency, key=lambda row: (-row[3], row[0])))
    else:
        print(f"ITEMS_COUNT={ids['ITEMS_COUNT']} explicit={len(items)} explicit_nonzero={len(items)-1}")
        effects = re.findall(r"(?m)^    (HOLD_EFFECT_\w+),", read("include/constants/hold_effects.h"))
        print(f"hold_effect_enum_excluding_NONE={len(effects)-1} assigned_non_NONE={len(held)}")
        print("unassigned:", " ".join(sorted(set(effects) - held.keys() - {"HOLD_EFFECT_NONE"})))
        print(f"items_with_non_NONE_hold_effect={sum(held.values())} frontier_sets={sum(map(len, roster.values()))}")
        print(f"explicit_form_mentions={sum(bool(row[-1]) for row in items.values())}")
        print(f"frontier_distinct_nonempty={len(roster.keys() - {'ITEM_NONE'})}")
        for name, sets in sorted(roster.items(), key=lambda pair: (-len(pair[1]), pair[0])):
            print(f"{len(sets):4} {name:30} {items[name][5]:31} {items[name][14]}")

if __name__ == "__main__":
    main()
