"""Read-only ability audit at the pinned Frontier revision.

Run with Python 3.10+. CSV is written to stdout. The item audit's frontier.csv
is the source for unchanged item and move prefilters.
"""
import argparse
from collections import Counter, defaultdict
import csv
from pathlib import Path
import re
import subprocess
import sys

REVISION = "2d1e107ad820a3b0a601af968b49856f74151ee1"
ENUM = "include/constants/abilities.h"
INFO = "src/data/abilities.h"
EXPORTER = "src/frontier_gc_battle.c"
TOKEN = r"\bABILITY_[A-Z0-9_]+\b"
MEMBER = re.compile(r"(?m)^[ \t]*\[(ABILITY_\w+)\]\s*=\s*\{")
SPECIES = re.compile(r"(?m)^[ \t]*\[(SPECIES_\w+)\]\s*=\s*(?:\([^;\n]+\)\s*)?\{")
FUNCTION = re.compile(r"^(?:[a-zA-Z_][\w *]+ )([A-Za-z_]\w*)\([^;]*$")
TAGS = {
    "ABL-POWER": "HUGE_POWER PURE_POWER SOLAR_POWER BLAZE TORRENT OVERGROW SWARM TECHNICIAN DEFEATIST HUSTLE GUTS TOXIC_BOOST FLARE_BOOST SAND_FORCE SHEER_FORCE RECKLESS IRON_FIST TOUGH_CLAWS STRONG_JAW MEGA_LAUNCHER WATER_BUBBLE TRANSISTOR DRAGONS_MAW STEELWORKER STEELY_SPIRIT ROCKY_PAYLOAD FIRE_MANE FLASH_FIRE ADAPTABILITY RIVALRY ANALYTIC BATTERY POWER_SPOT SUPREME_OVERLORD SWORD_OF_RUIN STAKEOUT PLUS MINUS ORICHALCUM_PULSE HADRON_ENGINE TINTED_LENS REFRIGERATE PIXILATE AERILATE GALVANIZE DRAGONIZE DARK_AURA FAIRY_AURA AURA_BREAK NEUROFORCE GORILLA_TACTICS SHARPNESS",
    "ABL-DEFENSE": "THICK_FAT HEATPROOF WATER_BUBBLE HARD_ROCK SOLID_ROCK FILTER PRISM_ARMOR MULTISCALE SHADOW_SHIELD FUR_COAT ICE_SCALES MARVEL_SCALE GRASS_PELT FRIEND_GUARD TERA_SHELL PUNK_ROCK TABLETS_OF_RUIN VESSEL_OF_RUIN BEADS_OF_RUIN FLUFFY BATTLE_ARMOR SHELL_ARMOR UNAWARE HEAVY_METAL LIGHT_METAL",
    "ABL-IMMUNITY": "LEVITATE EELEVATE VOLT_ABSORB WATER_ABSORB FLASH_FIRE SAP_SIPPER LIGHTNING_ROD STORM_DRAIN MOTOR_DRIVE EARTH_EATER WELL_BAKED_BODY WIND_RIDER DRY_SKIN SOUNDPROOF BULLETPROOF WONDER_GUARD MAGIC_GUARD TELEPATHY PURIFYING_SALT GOOD_AS_GOLD ARMOR_TAIL DAZZLING QUEENLY_MAJESTY DAMP LIQUID_OOZE ROCK_HEAD SCRAPPY MAGIC_BOUNCE",
    "ABL-HIT": "STURDY ROUGH_SKIN IRON_BARBS STATIC FLAME_BODY POISON_POINT LONG_REACH AFTERMATH INNARDS_OUT WEAK_ARMOR STAMINA COTTON_DOWN GOOEY TANGLING_HAIR CURSED_BODY EFFECT_SPORE CUTE_CHARM POISON_TOUCH POISON_PUPPETEER SPICY_SPRAY TOXIC_DEBRIS SAND_SPIT PERISH_BODY COLOR_CHANGE MUMMY LINGERING_AROMA WANDERING_SPIRIT BERSERK ANGER_POINT STEAM_ENGINE ELECTROMORPHOSIS WIND_POWER ANGER_SHELL JUSTIFIED RATTLED WATER_COMPACTION THERMAL_EXCHANGE",
    "ABL-STATUS": "LIMBER INSOMNIA VITAL_SPIRIT WATER_VEIL IMMUNITY MAGMA_ARMOR WATER_BUBBLE OBLIVIOUS OWN_TEMPO INNER_FOCUS COMATOSE LEAF_GUARD SWEET_VEIL AROMA_VEIL PASTEL_VEIL FLOWER_VEIL PURIFYING_SALT SHED_SKIN HYDRATION NATURAL_CURE HEALER SYNCHRONIZE TOXIC_CHAIN CORROSION EARLY_BIRD POISON_HEAL QUICK_FEET BAD_DREAMS",
    "ABL-FIELD": "DRIZZLE DROUGHT SAND_STREAM SNOW_WARNING DESOLATE_LAND PRIMORDIAL_SEA DELTA_STREAM CLOUD_NINE AIR_LOCK SWIFT_SWIM CHLOROPHYLL SAND_RUSH SLUSH_RUSH SAND_VEIL SNOW_CLOAK SAND_FORCE OVERCOAT RAIN_DISH ICE_BODY DRY_SKIN SOLAR_POWER ELECTRIC_SURGE GRASSY_SURGE MISTY_SURGE PSYCHIC_SURGE SURGE_SURFER GRASS_PELT HARVEST ORICHALCUM_PULSE HADRON_ENGINE PROTOSYNTHESIS QUARK_DRIVE TERAFORM_ZERO MEGA_SOL FORECAST FLOWER_GIFT MIMICRY SEED_SOWER",
    "ABL-ENTRY": "INTIMIDATE TRACE DOWNLOAD ANTICIPATION FOREWARN FRISK DAUNTLESS_SHIELD INTREPID_SWORD SUPERSWEET_SYRUP COSTAR HOSPITALITY COMMANDER SCREEN_CLEANER TERA_SHIFT UNNERVE AS_ONE_ICE_RIDER AS_ONE_SHADOW_RIDER",
    "ABL-EXIT": "REGENERATOR NATURAL_CURE EMERGENCY_EXIT WIMP_OUT ARENA_TRAP SHADOW_TAG MAGNET_PULL RUN_AWAY",
    "ABL-ORDER": "PRANKSTER GALE_WINGS TRIAGE MYCELIUM_MIGHT QUICK_DRAW STALL QUICK_FEET UNBURDEN SLOW_START SWIFT_SWIM CHLOROPHYLL SAND_RUSH SLUSH_RUSH SURGE_SURFER",
    "ABL-ACCURACY": "NO_GUARD COMPOUND_EYES VICTORY_STAR KEEN_EYE ILLUMINATE MINDS_EYE SAND_VEIL SNOW_CLOAK HUSTLE TANGLED_FEET WONDER_SKIN",
    "ABL-SECONDARY": "SHEER_FORCE SERENE_GRACE SHIELD_DUST STENCH INFILTRATOR INNER_FOCUS",
    "ABL-ITEM": "MAGIC_GUARD SHEER_FORCE KLUTZ UNNERVE GLUTTONY RIPEN CHEEK_POUCH CUD_CHEW HARVEST UNBURDEN SYMBIOSIS PICKUP PICKPOCKET MAGICIAN STICKY_HOLD FRISK BALL_FETCH MULTITYPE RKS_SYSTEM",
    "ABL-CHANGE": "MOLD_BREAKER TURBOBLAZE TERAVOLT MYCELIUM_MIGHT NEUTRALIZING_GAS TRACE RECEIVER POWER_OF_ALCHEMY MUMMY LINGERING_AROMA WANDERING_SPIRIT",
    "ABL-FORM": "SCHOOLING DISGUISE ICE_FACE SHIELDS_DOWN POWER_CONSTRUCT STANCE_CHANGE ZEN_MODE FORECAST FLOWER_GIFT MULTITYPE RKS_SYSTEM BATTLE_BOND GULP_MISSILE HUNGER_SWITCH TERA_SHIFT TERA_SHELL TERAFORM_ZERO ILLUSION IMPOSTER ZERO_TO_HERO EMBODY_ASPECT_TEAL_MASK EMBODY_ASPECT_HEARTHFLAME_MASK EMBODY_ASPECT_WELLSPRING_MASK EMBODY_ASPECT_CORNERSTONE_MASK",
    "ABL-KO": "MOXIE CHILLING_NEIGH GRIM_NEIGH AS_ONE_ICE_RIDER AS_ONE_SHADOW_RIDER BEAST_BOOST EELEVATE BATTLE_BOND AFTERMATH INNARDS_OUT SOUL_HEART RECEIVER POWER_OF_ALCHEMY",
    "ABL-RANK": "CLEAR_BODY WHITE_SMOKE FULL_METAL_BODY HYPER_CUTTER BIG_PECKS MIRROR_ARMOR DEFIANT COMPETITIVE CONTRARY SIMPLE SPEED_BOOST MOODY INTIMIDATE DOWNLOAD CURIOUS_MEDICINE OPPORTUNIST ANGER_POINT STEADFAST GUARD_DOG",
    "ABL-ACTION": "TRUANT SKILL_LINK NORMALIZE PROTEAN REFRIGERATE PIXILATE AERILATE GALVANIZE DRAGONIZE PARENTAL_BOND MERCILESS LIQUID_VOICE DANCER LIBERO PROPELLER_TAIL STALWART GORILLA_TACTICS UNSEEN_FIST PIERCING_DRILL",
    "ABL-POLICY": "RUN_AWAY PICKUP HONEY_GATHER BALL_FETCH ILLUMINATE PRESSURE SUPER_LUCK SNIPER STENCH SAND_VEIL SWARM FLAME_BODY MAGMA_ARMOR COMPOUND_EYES STATIC MAGNET_PULL HUSTLE CUTE_CHARM SYNCHRONIZE VITAL_SPIRIT WHITE_SMOKE QUICK_FEET SUCTION_CUPS HARVEST STORM_DRAIN LIGHTNING_ROD NONE 314 317 AURA_GUARD",
}
PRI = set("MAGIC_GUARD SHEER_FORCE KLUTZ ROUGH_SKIN IRON_BARBS LONG_REACH STURDY UNBURDEN UNNERVE GLUTTONY RIPEN CHEEK_POUCH CUD_CHEW HARVEST".split())
MISMATCH = {"ROUGH_SKIN", "IRON_BARBS"}
TIMES = {
    "ABL-POWER":"power/attack/final damage", "ABL-DEFENSE":"defense/final damage",
    "ABL-IMMUNITY":"target gate/type effectiveness/indirect damage",
    "ABL-HIT":"hit or contact / MoveEnd","ABL-STATUS":"status gate/turn end",
    "ABL-FIELD":"entry/field lookup/turn end","ABL-ENTRY":"entry",
    "ABL-EXIT":"withdrawal/switch eligibility","ABL-ORDER":"action ordering",
    "ABL-ACCURACY":"hit gate/accuracy","ABL-SECONDARY":"secondary effect",
    "ABL-ITEM":"item evaluation/loss/consumption","ABL-CHANGE":"effective lookup/replacement",
    "ABL-FORM":"form check","ABL-KO":"faint or KO","ABL-RANK":"stat stage","ABL-POLICY":"battle/field/reward policy",
    "ABL-ACTION":"action gate/targeting/execute", "ABL-OTHER":"see reader",
}

def git(repo, *args):
    return subprocess.check_output(["git", "--no-optional-locks", "-c", f"safe.directory={repo}",
                                    "-C", repo, *args]).decode("utf-8")

def offset(source, pos):
    return source.count("\n", 0, pos) + 1

def emit(columns, rows):
    writer = csv.writer(sys.stdout, lineterminator="\n")
    writer.writerow(columns.split())
    writer.writerows(rows)

def entries(pattern, source):
    matches = list(pattern.finditer(source))
    for i, m in enumerate(matches):
        stop = matches[i + 1].start() if i + 1 < len(matches) else len(source)
        yield m[1], m.start(), source[m.end():stop]

def values(source):
    ids = {}
    number = 0
    for key, rhs in re.findall(r"(?m)^    ([A-Z][A-Z0-9_]*)(?:[ \t]*=[ \t]*([A-Z0-9_]+))?,", source):
        if rhs:
            number = int(rhs) if rhs.isdecimal() else ids[rhs]
        ids[key] = number
        number += 1
    return {key: n for key, n in ids.items() if key.startswith("ABILITY_")}

def spellings(body, field):
    line = re.search(rf"(?ms)^[ \t]*\.{field}\s*=\s*(.*?)(?=,\s*(?:\n|\}}))", body)
    if not line:
        return []
    return [str_.replace("{JPN}", "").replace("{ENG}", "")
            for str_ in re.findall(r'"([^"]*)"', line[1])]

def source_refs(read, paths):
    evidence = defaultdict(lambda: defaultdict(dict))
    for path in paths:
        if path.startswith("src/battle_") and path.endswith(".c"):
            if re.search(r"_(anim(?:_|\.c)|bg\.c|gfx_|transition|interface)", path):
                continue
            if path.startswith("src/battle_ai_"):
                area = "ai"
            elif path.startswith(("src/battle_message", "src/battle_controller")):
                area = "ui"
            elif path.startswith(("src/battle_frontier", "src/battle_tower", "src/battle_pyramid",
                                  "src/battle_pike", "src/battle_factory", "src/battle_arena",
                                  "src/battle_dome", "src/battle_palace", "src/battle_tent")):
                area = "facility"
            else:
                area = "runtime"
        elif path in ("src/pokemon.c", "src/trainer_util.c", "src/party_menu.c",
                      "src/item_use.c", "src/event_object_movement.c", "src/wild_encounter.c"):
            area = "outside_or_shared"
        elif path == "src/data/pokemon/form_change_tables.h":
            area = "form_data"
        elif path == EXPORTER:
            area = "exporter"
        elif path.startswith("data/battle_scripts") and path.endswith(".s"):
            area = "script"
        elif path.startswith("test/battle/") and path.endswith(".c"):
            area = "test"
        else:
            continue
        function = ""
        in_comment = False
        for line_no, line in enumerate(read(path).splitlines(), 1):
            line = line.split("//", 1)[0]
            if in_comment:
                if "*/" not in line:
                    continue
                line = line.split("*/", 1)[1]
                in_comment = False
            if "/*" in line:
                before, after = line.split("/*", 1)
                in_comment = "*/" not in after
                line = before if in_comment else before + after.split("*/", 1)[1]
            m = FUNCTION.match(line)
            if m:
                function = m[1]
            elif line.startswith("}"):
                function = ""
            for token in set(re.findall(TOKEN, line)) - {"ABILITY_NONE"}:
                ref = f"{path}:{line_no}" + (f":{function}" if function else "")
                evidence[token][area].setdefault((path, function), ref)
    return evidence

def species_slots(read, paths):
    slots = defaultdict(list)
    assigned = defaultdict(set)
    field = re.compile(r"\.abilities\s*=\s*(\{[^}]+\}|\w+)")
    for path in paths:
        if not re.fullmatch(r"src/data/pokemon/species_info/gen_\d+_families\.h", path):
            continue
        data = read(path)
        macros = defaultdict(list)
        definition = r"(?m)^[ \t]*#[ \t]*define[ \t]+(\w+)(?:\([^\n)]*\))?[ \t]+((?:[^\n]*\\\n)*[^\n]*)"
        for m in re.finditer(definition, data):
            macros[m[1]].append(m[2].replace("\\\n", " "))
        def read_slots(body, walked):
            for m in field.finditer(body):
                if m[1].startswith("{"):
                    yield tuple(re.findall(TOKEN, m[1]))
                elif m[1] in macros and m[1] not in walked:
                    for expansion in macros[m[1]]:
                        yield from read_slots(".abilities = "+expansion, walked | {m[1]})
            for call in re.findall(r"\b([A-Z][A-Z_0-9]+)\s*\(", body):
                if call in macros and call not in walked:
                    for expansion in macros[call]:
                        yield from read_slots(expansion, walked | {call})
        for species, start, body in entries(SPECIES, data):
            # Stop at the end of this species when a later macro starts before the next entry.
            body = re.split(r"(?m)^    \},\s*$", body, maxsplit=1)[0]
            ref = f"{path}:{offset(data, start)}"
            for members in set(read_slots(body, set())):
                if not members:
                    continue
                slots[species].append(members)
                for ability in members:
                    assigned[ability].add(ref)
    return slots, assigned

def policy_kind(exporter):
    resolver = exporter.split("static BattleAbility ResolveAbility",1)[1].split("static void ExportAbilityModifiers",1)[0]
    weather = exporter.split("static bool8 ResolveWeatherAbility",1)[1].split("static BattleAbility ResolveAbility",1)[0]
    modifiers = exporter.split("sAbilityModifiers[] =",1)[1].split("};",1)[0]
    flag = set(re.findall(TOKEN, resolver + weather + modifiers))
    # Compare case labels for helpers; Remove symbols only used on a comparison RHS.
    cases = set(re.findall(r"case (ABILITY_\w+):", resolver + weather)) | set(re.findall(TOKEN, modifiers))
    inert = {"ABILITY_"+name for name in ("NONE","RUN_AWAY","PICKUP","HONEY_GATHER","BALL_FETCH")}
    assert cases <= flag and inert <= cases
    return cases - inert, inert

def abilities(read, indexes, assigned, hooks, inert, evidence):
    source = read(INFO)
    meta = {name: (offset(source,start),body) for name,start,body in entries(MEMBER, source)}
    assert set(meta) == set(indexes)
    rows = []
    for ability, id_ in sorted(indexes.items(), key=lambda p:p[1]):
        definition_line, body = meta[ability]
        short = ability.removeprefix("ABILITY_")
        names = spellings(body,"name")
        name = names[0] if names else ""
        ja = name if re.search(r"[ぁ-龯ァ-ヿ]", name) or name=="-------" else ""
        en = name if not ja else ""
        d = spellings(body,"description")
        flags = " ".join(re.findall(r"\.(\w+)\s*=\s*TRUE", body))
        refs = {k: ' '.join(v.values()) for k, v in evidence[ability].items()}
        refs = defaultdict(str, refs)
        present = "DIRECT_RUNTIME_REFERENCE" if refs["runtime"] else "NO_DIRECT_RUNTIME_REFERENCE"
        scope = [k for k,v in TAGS.items() if short in v.split()]
        if not scope:
            scope = ["ABL-OTHER"]
        status = "MISMATCH" if short in MISMATCH else "PARTIAL" if ability in hooks else (
            "NOT_APPLICABLE" if ability in inert else "UNSUPPORTED")
        focus = "ABL-B1C" if short in PRI else ""
        passive = set(scope) & {"ABL-POWER","ABL-DEFENSE","ABL-ACCURACY","ABL-IMMUNITY","ABL-ORDER"}
        event = set(scope) - {"ABL-POWER","ABL-DEFENSE","ABL-ACCURACY","ABL-IMMUNITY","ABL-ORDER","ABL-POLICY"}
        activation = "mixed" if passive and event else "condition-at-query" if passive else "event-family" if event else "policy-or-unused"
        rows.append((id_, ability, ja, en, " | ".join(d), " ".join(scope),
                     " | ".join(TIMES[k] for k in scope), flags, status,
                     "hooks" if ability in hooks else "none" if ability in inert else "unsupported",
                     definition_line, present, len(assigned[ability]), focus,
                     "SOURCE_CONFIRMED:reference" if refs["runtime"] else "UNKNOWN:effect",
                     "see priority spec" if focus else "exact runtime conditions not individually validated",
                     *(refs[k] for k in ("runtime","script","ui","ai","exporter","test","outside_or_shared","facility","form_data")), activation))
    return rows

def roster(path, slots, hooks, inert):
    with path.open(encoding="utf-8",newline="") as f:
        source = list(csv.DictReader(f))
    rows = []
    for row in source:
        wanted = row["ability"]
        versions = slots[row["species"]]
        normal = {a for v in versions for a in v[:2]} - {"ABILITY_NONE"}
        hidden = {a for v in versions for a in v[2:]} - {"ABILITY_NONE"}
        if not versions:
            choices, certainty = set(), "unknown-species-expression"
        elif wanted and wanted != "ABILITY_NONE" and all(wanted in v for v in versions):
            choices, certainty = {wanted}, "requested-in-all-source-variants"
        elif wanted and wanted != "ABILITY_NONE" and any(wanted in v for v in versions):
            choices, certainty = normal | {wanted}, "conditional-fixed-or-normal"
        else:
            choices, certainty = normal, "normal-slot-fallback"
        rows.append((row["set_source_line"],row["set"],row["species"],wanted,
                     " ".join(sorted(normal))," ".join(sorted(hidden))," ".join(sorted(choices)),
                     certainty, " ".join(sorted("hooks:"+a for a in choices & hooks) +
                                         sorted("none:"+a for a in choices & inert) +
                                         sorted("unsupported:"+a for a in choices - hooks - inert)),
                     row["item"],row["v19_static_gate"],row["item_ability_prefilter"],
                     row["move_effect_switch_prefilter"],row["moves"]))
    assert len(rows)==2040 and len({row[1] for row in rows})==2040
    return rows

def default_species_slots(repo, wsl):
    if git(repo, "rev-parse", "HEAD").strip() != REVISION:
        raise ValueError("Default slot projection requires the pinned Frontier checkout")
    if git(repo, "status", "--porcelain", "--untracked-files=no").strip():
        raise ValueError("Default slot projection requires clean tracked Frontier files")
    compiler = ["wsl.exe", "-d", wsl, "--", "gcc"] if wsl else ["gcc"]
    defines = "#define TRUE 1\n#define FALSE 0\n"
    include = ["config/general.h", "config/battle.h", "config/pokemon.h",
               "config/species_enabled.h", "config/overworld.h", "src/data/pokemon/species_info.h"]
    source = defines + "".join(f'#include "{path}"\n' for path in include)
    output = subprocess.check_output(compiler + ["-E", "-P", "-I", "include", "-I", ".", "-x", "c", "-"],
                                     cwd=repo, input=source.encode("ascii")).decode("utf-8")
    slots = {}
    for species, _, body in entries(SPECIES, output):
        selected = re.search(r"\.abilities\s*=\s*\{([^}]+)\}", body)
        if selected:
            slots[species] = [tuple(re.findall(TOKEN, selected[1]))]
    return slots

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("frontier_repo")
    p.add_argument("table", choices=("abilities","frontier","frequency","default","default-frequency","summary"))
    p.add_argument("--wsl", help="Use gcc in this WSL distro for default-slot projection")
    args = p.parse_args()
    repo = Path(args.frontier_repo).resolve().as_posix()
    def read(path):
        return git(repo, "show", f"{REVISION}:{path}")
    sys.stdout.reconfigure(encoding="utf-8",newline="")
    ids = values(read(ENUM))
    paths = git(repo, "ls-tree","-r","--name-only",REVISION,"src","data","test/battle").splitlines()
    hooks,inert = policy_kind(read(EXPORTER))
    slots, assigned = species_slots(read,paths)
    if args.table in ("abilities","summary"):
        evidence = source_refs(read,paths)
        rows = abilities(read,ids,assigned,hooks,inert,evidence)
        if args.table == "abilities":
            emit("ability_id ability ja en description tags timing_family data_flags gc_v19_audit gc_exporter_route data_line symbol_evidence static_species_slot_sites priority_spec confidence unverified runtime script ui ai exporter test outside_or_shared facility form_data activation_model",rows)
        else:
            print("ability_rows",len(rows),"hooks",len(hooks),"none",len(inert))
            print("runtime_reference",sum(bool(row[16]) for row in rows),"static_species_refs",sum(bool(refs) for key, refs in assigned.items() if key != "ABILITY_NONE"))
            print("no_runtime", " ".join(row[1] for row in rows if not row[16]))
            print("no_species", " ".join(row[1] for row in rows if not row[12]))
        return
    item_table = Path(__file__).resolve().parent.parent / "item-mechanics" / "frontier.csv"
    if args.table in ("default","default-frequency"):
        slots = default_species_slots(repo, args.wsl)
    rows = roster(item_table,slots,hooks,inert)
    if args.table in ("frontier","default"):
        emit("set_source_line set species requested normal_slots hidden_slots static_export_candidates resolution gc_exporter_routes item static_item_gate item_ability_gate unsupported_move_effects moves",rows)
        return
    item_gate = read(EXPORTER).split("static bool8 ItemAbility", 1)[1].split("static bool8 ConsumableItemAbility", 1)[0]
    item_related = set(re.findall(TOKEN, item_gate))
    requested = Counter(row[3] for row in rows)
    candidates = Counter()
    unique = Counter()
    strict_cap = Counter()
    blockers = defaultdict(Counter)
    for row in rows:
        choices = row[6].split()
        candidates.update(choices)
        if len(choices)==1:
            unique.update(choices)
        for a in choices:
            if row[12] or row[10].startswith("UNSUPPORTED") or row[11] or a in item_related:
                blockers[a].update(row[12].split())
            else:
                strict_cap[a] += 1
    freq = []
    for ability in ids:
        route = "hooks" if ability in hooks else "none" if ability in inert else "unsupported"
        freq.append((ability,requested[ability],unique[ability],candidates[ability],route,
                     candidates[ability] if route=="unsupported" else 0,
                     strict_cap[ability] if route=="unsupported" else 0,
                     " ".join(m for m,_ in blockers[ability].most_common(4))))
    emit("ability set_requested singleton_static_candidate any_static_candidate gc_v19_exporter_route ability_only_cap strict_cap_with_unchanged_prefilters frequent_move_blockers",
         sorted(freq,key=lambda row:(-row[3],row[0])))

if __name__ == "__main__":
    main()
