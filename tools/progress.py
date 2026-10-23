#!/usr/bin/env python3
"""How much of the game is decompiled.

    python3 tools/progress.py --decomp ../HM-Decomp [--update-readme]

Measures against the HM-Decomp disassembly (no ROM needed, so anyone can
recompute it):

  * Routines: every top-level label in HM-Decomp's code banks whose body has
    instructions: the ones with an address comment (`GetRNG: ;838138`) and
    the ones without (`CODE_81A500:`). Data tables (`db`/`dw`/`dl` only)
    are not counted.
  * Instructions: the 65816 instructions inside them (one per line of the
    disassembly, labelled or not). This is the headline percentage: a
    4096-tile loop weighs more than a three-line helper.

A routine counts as decompiled when it is registered in src/hm_decomp.c
(written in C and verified against the original).

With --update-readme the block between <!-- progress --> and
<!-- /progress --> in README.md (English) and README.es.md (Spanish) is
rewritten.
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):\s*;\s*([0-9A-Fa-f]{6})\b")
ANY_TOP_LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
# An instruction: a mnemonic (3 letters, optional .B/.W/.L) or a %Set8bit /
# %Set16bit macro (one REP/SEP each), maybe after a label on the same line.
INSN = re.compile(r"^\s*(?:[-+]\s+)?(?:\.?[A-Za-z0-9_]+:\s*)?"
                  r"(?:([A-Z]{3})(?:\.[BWL])?\b|%Set(?:8|16)bit)")
DATA = {"DB", "DW", "DL", "DD"}

# Routines HM-Decomp only has as a label inside a bigger block (a local
# `.label`, an indented `CODE_xxxxxx:`, or a top-level label with no
# address comment). Each counts as a routine of its own, from its label to
# the first RTL/RTS, and those instructions are taken out of the block it is
# written in, so nothing is counted twice. Same list as MANUAL in
# tools/import_decomp_names.py.
LOCAL = {
    0x84A5D4: (".AddCowHappiness", "AddCowHappiness"),
    0x81D1A4: ("CODE_81D1A4", "TileAtOffsetFromPlayer"),
    0x81A801: ("SUB_81A801", "SUB_81A801"),
    0x81A83A: ("SUB_81A83A", "SUB_81A83A"),
    0x81A688: ("CODE_81A688", "ChangeMapTile"),
    0x81A6C1: ("CODE_81A6C1", "QueueTileGraphicsDMA"),
    0x81A500: ("CODE_81A500", "SpawnMapObject"),
    0x858000: ("CODE_858000", "CreateGameObject"),
    0x858C30: ("CODE_858C30", "LoadSpriteComponent"),
    0x81A5E1: ("CODE_81A5E1", "StartMapAnimation"),
    0x83F26D: ("RanchMasteryCalculator", "RanchMasteryCalculator"),
    0x83C296: ("CODE_83C296", "PlaceAnimalsOnMap"),
    0x848A0D: ("Event_TimeStopped", "Event_TimeStopped"),     # ";808A0D" in HM-Decomp
    # the same mistake (bank $80 in the comment); Event_UNKCFD1 was being
    # counted with CallIndexedSubrutines, the real $80:9553
    0x849079: ("Event_UNKCCchanges5", "Event_UNKCCchanges5", "Event_UNKJumpif018F"),  # two RTS
    0x849553: ("Event_UNKCFD1", "Event_UNKCFD1"),
    0x8495E8: ("Event_AudioRelated3", "Event_AudioRelated3"),
    0x849633: ("Event_UNKCCchanges9", "Event_UNKCCchanges9", "Event_UNKCCchanges10"),
    0x8491D2: ("Event_UNKTextrelated", "Event_UNKTextrelated"),
    0x849513: ("Event_MapScrolling", "Event_MapScrolling"),
    0x84A90F: ("Event_StoreValue", "Event_StoreValue"),
    0x84B4DC: ("Event_ChangeItemonHand", "Event_ChangeItemonHand"),
    0x84BA51: ("Event_UNKCCchanges16", "Event_UNKCCchanges16"),
    0x84BB31: ("Event_UNKReset5C", "Event_UNKReset5C"),
    0x8393F9: ("CODE_8393F9", "CloseTextBox"),
    0x8394D7: ("CODE_8394D7", "DrawTextBoxArrow"),
    0x839495: ("CODE_839495", "BlinkTextBoxArrow"),
    0x839447: ("CODE_839447", "TextBoxArrow"),
    # several routines with many RTLs under one label: up to the next one
    0x84C2FE: ("CODE_84C2FE", "SaveSlotMenuInputs", "CODE_84C380"),
    0x84C380: ("CODE_84C380", "LoadSlotMenuInputs", "CODE_84C428"),
    0x84C428: ("CODE_84C428", "StartMenuInputs", "CODE_84C458"),
    0x84C458: ("CODE_84C458", "TitleMenuInputs", "CODE_84C540"),
    0x84C540: ("CODE_84C540", "ListInputs", "PlayerMovementInputs"),
    0x84CF05: ("ImmobileInputs", "ImmobileInputs", "<end of file>"),
    0x8093A4: ("CustomiseSpritePalette", "CustomiseSpritePalette"),  # HM-Decomp says 8093A3
    0x83AD91: ("CBBBB", "CBBBB"),
    0x83AEC3: ("CDDDD", "CDDDD"),
    0x83AF37: ("CEEEE", "CEEEE"),
    0x83B0F6: ("CFFFF", "CFFFF"),
    0x84C6B7: ("PlayerMovementInputs", "PlayerMovementInputs", "CODE_84CED2"),  # the whole block, to mode 6
    0x84CED2: ("CODE_84CED2", "CODE_84CED2"),
    0x84C2B4: ("CODE_84C2B4", "CODE_84C2B4"),
    0x84C2D8: ("CODE_84C2D8", "CODE_84C2D8"),
    0x82EA60: ("Unk_OutOfMenu", "Unk_OutOfMenu"),
    0x8493C9: ("Event_ResetFlagDD", "Event_ResetFlag"),  # three labels on the same code
    0x84BA10: ("Event_EditTileonMap", "Event_EditTileonMap"),
    0x83C9E5: (".barncheck", "UnusedBarnCheck"),      # after UnusedBarnCoopCheck's RTL
    0x81AC80: ("ReplaceTilesMoutain", "ReplaceTilesMoutain", "DATA8_00ACCB"),  # HM-Decomp says 81AC7F
    0x81A7CE: ("SUB_81A7CE", "SUB_81A7CE"),
    0x81B2BB: ("SUB_81B2BB", "SUB_81B2BB"),
    0x81B26D: ("@81B26D", "ReplaceTilesMap37", "@81B293"),  # an indented label in the barn's block
    0x81AC71: ("ReplaceTilesTown", "ReplaceTilesTown", "ReplaceTilesFlowerFestival"),  # 7 RTS, 6 never reached
    0x81AC78: ("ReplaceTilesFlowerFestival", "ReplaceTilesFlowerFestival"),
    0x81AC79: ("ReplaceTilesHarvestFestival", "ReplaceTilesHarvestFestival"),
    0x81AC7A: ("DATA8_00AC7A", "DATA8_00AC7A"),
    0x81AC7B: ("DATA8_00AC7B", "DATA8_00AC7B"),
    0x81AC7C: ("ReplaceTilesForkSpring", "ReplaceTilesForkSpring"),
    0x81AC7D: ("ReplaceTilesForkSummer", "ReplaceTilesForkSummer"),
    0x81AC7E: ("ReplaceTilesForkFall", "ReplaceTilesForkFall"),
    0x81AC7F: ("ReplaceTilesForkWinter", "ReplaceTilesForkWinter"),
    0x81ACCB: ("DATA8_00ACCB", "DATA8_00ACCB"),
    0x81ADD1: ("ReplaceTilesMayorHouse", "ReplaceTilesMayorHouse"),
    0x81ADD2: ("ReplaceTilesMayorHouseHall", "ReplaceTilesMayorHouseHall"),
    0x81ADD3: ("ReplaceTilesMariasRoom", "ReplaceTilesMariasRoom"),
    0x81ADD4: ("ReplaceTilesChurch", "ReplaceTilesChurch"),
    0x81AE6A: ("ReplaceTilesFlowerShopRooms", "ReplaceTilesFlowerShopRooms"),
    0x81AE6B: ("ReplaceTilesBar", "ReplaceTilesBar"),
    0x81AE6C: ("ReplaceTilesBarRooms", "ReplaceTilesBarRooms"),
    0x81AE6D: ("ReplaceTilesRestaurant", "ReplaceTilesRestaurant"),
    0x81AE6E: ("ReplaceTilesRestaurantRooms", "ReplaceTilesRestaurantRooms"),
    0x81AFBF: ("ReplaceTilesGeneralStoreRooms", "ReplaceTilesGeneralStoreRooms"),
    0x81B063: ("ReplaceTilesWitchHouse", "ReplaceTilesWitchHouse"),
    0x838D39: ("UNK_Audio14", "UNK_Audio14", "UNK_Audio6"),  # HM-Decomp says 838D38
    0x858B7B: ("CODE_858B7B", "CODE_858B7B"),
    0x8580B9: ("CODE_8580B9", "CODE_8580B9"),
    0x858100: ("CODE_858100", "CODE_858100"),
    0x8581CB: ("CODE_8581CB", "CODE_8581CB"),
    0x8582C7: ("CODE_8582C7", "CODE_8582C7", "GetOBJOrderToDraw"),
    0x858896: ("PresetsWRAMCopyOAMCopy", "PresetsWRAMCopyOAMCopy"),
    0x81CFE6: ("CODE_81CFE6", "CODE_81CFE6"),
    0x81D03D: ("CODE_81D03D", "CODE_81D03D"),
    0x82EB57: ("SUB_82EB57", "SUB_82EB57"),
    0x83F4D8: ("CODE_83F4D8", "CODE_83F4D8"),
    # who appears on entering each map (UNK_TableFunctionSelect's routines)
    0x83D844: ("@83D844", "MapEvent_D844", "@83D929"),
    0x83D929: ("@83D929", "MapEvent_D929", "@83D955"),
    0x83D955: ("@83D955", "MapEvent_D955", "@83DA1F"),
    0x83DA1F: ("@83DA1F", "MapEvent_DA1F", "@83DA4B"),
    0x83DA4B: ("@83DA4B", "MapEvent_DA4B", "@83DAED"),
    0x83DAED: ("@83DAED", "MapEvent_DAED", "@83DB8F"),
    0x83DB8F: ("@83DB8F", "MapEvent_DB8F", "@83DC1E"),
    0x83DC1E: ("@83DC1E", "MapEvent_DC1E", "@83DC4A"),
    0x83DC4A: ("@83DC4A", "MapEvent_DC4A", "@83DD95"),
    0x83DD95: ("@83DD95", "MapEvent_DD95", "@83DE64"),
    0x83DE64: ("@83DE64", "MapEvent_DE64", "@83DE9B"),
    0x83D4D7: ("@83D4D7", "MapEvent_D4D7", "@83D5E7"),
    0x83D5E7: ("@83D5E7", "MapEvent_D5E7", "@83D613"),
    0x83D613: ("@83D613", "MapEvent_D613", "@83D74E"),
    0x83D74E: ("@83D74E", "MapEvent_D74E", "@83D818"),
    0x83D818: ("@83D818", "MapEvent_D818", "@83D844"),
    0x83E586: ("@83E586", "MapEvent_E586", "@83E5A2"),
    0x83E5A2: ("@83E5A2", "MapEvent_E5A2", "@83E5B4"),
    0x83E5B4: ("@83E5B4", "MapEvent_E5B4", "@83E5CD"),
    0x83E5CD: ("@83E5CD", "MapEvent_E5CD", "@83E5DF"),
    0x83E5DF: ("@83E5DF", "MapEvent_E5DF", "@83E5F1"),
    0x83E5F1: ("@83E5F1", "MapEvent_E5F1", "@83E603"),
    0x83E603: ("@83E603", "MapEvent_E603", "@83E632"),
    0x83E632: ("@83E632", "MapEvent_E632", "@83E661"),
    0x83E661: ("@83E661", "MapEvent_E661", "@83E690"),
    0x83E690: ("@83E690", "MapEvent_E690", "@83E6B2"),
    0x83E6B2: ("@83E6B2", "MapEvent_E6B2", "@83E6C4"),
    0x83E6C4: ("@83E6C4", "MapEvent_E6C4", "@83E74C"),
    0x83E74C: ("@83E74C", "MapEvent_E74C", "@83E76D"),
    0x858BB0: ("InvertIfFlipedX", "InvertIfFlipedX", "InvertIfFlipedY"),
    0x858BBC: ("InvertIfFlipedY", "InvertIfFlipedY", "Table_HighOAMXbit"),
    0x81A4F1: ("CODE_81A4F1", "CODE_81A4F1"),
    0x80C3D9: ("SUB_80C3D9", "SUB_80C3D9"),
    0x80C446: ("SUB_80C446", "SUB_80C446"),
    0x80C4B3: ("SUB_80C4B3", "SUB_80C4B3"),
    0x80C520: ("SUB_80C520", "SUB_80C520"),
    0x80C58D: ("SUB_80C58D", "SUB_80C58D"),
    0x80C5FA: ("SUB_80C5FA", "SUB_80C5FA"),
    0x80C667: ("SUB_80C667", "SUB_80C667"),
    0x80C6D4: ("SUB_80C6D4", "SUB_80C6D4"),
    0x80C792: ("SUB_80C792", "SUB_80C792"),
    0x80C7FF: ("SUB_80C7FF", "SUB_80C7FF"),
    0x80C86C: ("SUB_80C86C", "SUB_80C86C"),
    0x80C945: ("SUB_80C945", "SUB_80C945"),
    0x80C9B2: ("SUB_80C9B2", "SUB_80C9B2"),
    0x80CA1F: ("SUB_80CA1F", "SUB_80CA1F"),
    0x80CA8C: ("SUB_80CA8C", "SUB_80CA8C"),
    0x80CADE: ("SUB_80CADE", "SUB_80CADE"),
    0x80CB30: ("SUB_80CB30", "SUB_80CB30"),
    0x80CB82: ("SUB_80CB82", "SUB_80CB82"),
    0x80CBD4: ("SUB_80CBD4", "SUB_80CBD4"),
    0x80CC92: ("SUB_80CC92", "SUB_80CC92"),
    0x80CCE4: ("SUB_80CCE4", "SUB_80CCE4"),
    0x80CD36: ("SUB_80CD36", "SUB_80CD36"),
    0x80CD88: ("SUB_80CD88", "SUB_80CD88"),
    0x80CE46: ("SUB_80CE46", "SUB_80CE46"),
    0x80CE98: ("SUB_80CE98", "SUB_80CE98"),
    0x80CEEA: ("SUB_80CEEA", "SUB_80CEEA"),
    0x80CF3C: ("SUB_80CF3C", "SUB_80CF3C"),
    0x80CFFA: ("SUB_80CFFA", "SUB_80CFFA"),
    0x80D067: ("SUB_80D067", "SUB_80D067"),
    0x80D125: ("SUB_80D125", "SUB_80D125"),
    0x80D1E3: ("SUB_80D1E3", "SUB_80D1E3"),
    0x80D2A1: ("SUB_80D2A1", "SUB_80D2A1"),
    0x80D344: ("SUB_80D344", "SUB_80D344"),
    0x80D402: ("SUB_80D402", "SUB_80D402"),
    0x80D4C0: ("SUB_80D4C0", "SUB_80D4C0"),
    0x80D563: ("SUB_80D563", "SUB_80D563"),
    0x80D606: ("SUB_80D606", "SUB_80D606"),
    0x80D6FA: ("SUB_80D6FA", "SUB_80D6FA"),
    0x80D7EE: ("SUB_80D7EE", "SUB_80D7EE"),
    0x80D891: ("SUB_80D891", "SUB_80D891"),
    0x80D934: ("SUB_80D934", "SUB_80D934"),
    0x80D96B: ("SUB_80D96B", "SUB_80D96B"),
    0x80D9A2: ("SUB_80D9A2", "SUB_80D9A2"),
    0x80D9D9: ("SUB_80D9D9", "SUB_80D9D9"),
    0x80DA10: ("SUB_80DA10", "SUB_80DA10"),
    0x80DA47: ("SUB_80DA47", "SUB_80DA47"),
    0x80DAB4: ("SUB_80DAB4", "SUB_80DAB4"),
    0x80DB06: ("SUB_80DB06", "SUB_80DB06"),
    0x80DC30: ("SUB_80DC30", "SUB_80DC30"),
    0x80DD09: ("SUB_80DD09", "SUB_80DD09"),
    0x84BC9C: ("@84BC9C", "Event_Cmd51", "@84BD44"),
    0x84BD44: ("@84BD44", "Event_Cmd52", "@84BE0D"),
    0x84BE0D: ("@84BE0D", "Event_Cmd53", "@84BED6"),
    0x84BED6: ("@84BED6", "Event_Cmd54", "@84BF20"),
    0x84BF20: ("@84BF20", "Event_Cmd55", "@84BF81"),
    0x84BF81: ("@84BF81", "Event_Cmd56", "@84BFA0"),
    0x84BFA0: ("@84BFA0", "Event_Cmd57", "@84BFBF"),
    0x84BFBF: ("@84BFBF", "Event_Cmd58", "@84C000"),
    0x84C000: ("@84C000", "Event_Cmd59", "@84C014"),
    0x829999: ("ToolUsedCornSeeds", "ToolUsedCornSeeds"),   # ";82A9999" in HM-Decomp
}


def is_insn(code):
    m = INSN.match(code)
    return bool(m) and (m.group(1) or "").upper() not in DATA


def parse(decomp_dir):
    """(routines, lines): routines = {key: [name, instructions]}, key = the
    pc for labels with an address, ("noaddr", name) otherwise; lines = for
    every file, a list of (owner key, code) per line."""
    routines, lines = {}, {}
    for path in sorted(glob.glob(os.path.join(decomp_dir, "src", "code_banks", "*.asm"))):
        current, out = None, []
        for line in open(path, encoding="utf-8", errors="replace").read().split("\n"):
            a = ADDR.search(line)
            if a:
                ADDRESSES.setdefault((path, a.group(1).upper()), len(out))
            m = LABEL.match(line)
            code = line.split(";", 1)[0]
            if m:
                current = int(m.group(2), 16)
                routines.setdefault(current, [m.group(1), 0])
                code = m.group(1) + ":"     # kept so LOCAL can find the label
            else:
                t = ANY_TOP_LABEL.match(code)
                if t:
                    current = ("noaddr", t.group(1))
                    routines.setdefault(current, [t.group(1), 0])
            if current is not None and is_insn(code):
                routines[current][1] += 1
            out.append((current, code))
        lines[path] = out
    return routines, lines


# where each address comment is (";84BC9C;"): LOCAL entries may start or
# end at an address ("@84BC9C") for routines HM-Decomp gives no label
ADDR = re.compile(r";\s*([0-9A-Fa-f]{6})\s*;")
ADDRESSES = {}


def find_start(path, rows, label):
    if label.startswith("@"):
        return ADDRESSES.get((path, label[1:].upper()))
    return next((i for i, (_, c) in enumerate(rows)
                 if c.strip() == label + ":" or c.strip().startswith(label + ":")), None)


def split_locals(routines, lines, carved=None):
    carved = carved or {}
    for pc, entry in LOCAL.items():
        label, name = entry[0], entry[1]
        end = entry[2] if len(entry) > 2 else None     # up to this label, not the first RTL
        for path, rows in lines.items():
            start = find_start(path, rows, label)
            if start is None:
                continue
            stop = ADDRESSES.get((path, end[1:].upper())) if end and end.startswith("@") else None
            # each instruction is taken out of the block that owns its line
            # (the routine may end in another top-level label: `return85: RTL`)
            n = 0
            for j, (owner, code) in enumerate(rows[start:]):
                if stop is not None and start + j >= stop:
                    break
                if end and stop is None and j and code.strip().startswith(end + ":"):
                    break
                if is_insn(code):
                    # a line split_called already carved out (a routine
                    # called with JSL from elsewhere) stays in that routine
                    if (path, start + j) in carved:
                        continue
                    n += 1
                    routines[owner][1] -= 1
                    if not end and re.search(r"\bRT[LS]\b", code):
                        break
            routines[pc] = [name, n]
            break
    return routines


CALL = re.compile(r"\bJS[LR](?:\.[LW])?\s+([A-Za-z_][A-Za-z0-9_]*)\b")
CODE_NAME = re.compile(r"^(?:CODE|SUB)_([0-9A-Fa-f]{6})$")


def split_called(routines, lines):
    """A label inside a block that is called (JSL / JSR) from another block
    is a routine of its own: HM-Decomp often writes several routines under
    one top-level label. Each is carved out, from its label to its first
    RTL / RTS, into a routine keyed by its address (CODE_xxxxxx) or name."""
    local_labels = {entry[0] for entry in LOCAL.values()}
    carved = {}
    callers = {}
    for rows in lines.values():
        for owner, code in rows:
            for target in CALL.findall(code):
                callers.setdefault(target, set()).add(owner)
    for path, rows in lines.items():
        for i, (owner, code) in enumerate(rows):
            m = re.match(r"^\s+([A-Za-z_][A-Za-z0-9_]*):", code)
            if not m or m.group(1) in local_labels:
                continue
            name = m.group(1)
            if not (callers.get(name, set()) - {owner}):
                continue                      # only called from its own block
            k = CODE_NAME.match(name)
            key = int(k.group(1), 16) if k else ("split", name)
            if key in routines:
                continue
            n = 0
            for j, (own2, code2) in enumerate(rows[i:]):
                if j and ANY_TOP_LABEL.match(code2):
                    break
                if is_insn(code2):
                    n += 1
                    routines[own2][1] -= 1
                    carved[(path, i + j)] = key
                    if re.search(r"\bRT[LS]\b", code2):
                        break
            routines[key] = [name, n]
    return routines, carved


def routines_of(decomp_dir):
    found, lines = parse(decomp_dir)
    found, carved = split_called(found, lines)
    found = split_locals(found, lines, carved)
    return {k: (name, n) for k, (name, n) in found.items() if n > 0}


routines = routines_of


# Routines HM-Decomp shows as data (`dw` lines): rewritten and verified, but
# not counted, since the measure is HM-Decomp's own code.
NOT_IN_DECOMP = {0x81B121, 0x81B1CF, 0x81B21E}   # ReplaceTilesCoop, MountainCave, ElfTunnel


def registered(path):
    text = open(path, encoding="utf-8").read()
    names = re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)",\s*hm_\w+,[^{]*?0x([0-9A-Fa-f]{6})\b', text)
    return {int(pc, 16): name for name, pc in names if int(pc, 16) not in NOT_IN_DECOMP}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default=os.path.join(ROOT, "..", "HM-Decomp"))
    ap.add_argument("--update-readme", action="store_true")
    a = ap.parse_args()

    all_r = routines(a.decomp)
    done = registered(os.path.join(ROOT, "src", "hm_decomp.c"))
    missing = [n for pc, n in done.items() if pc not in all_r]
    if missing:
        print("progress: not found in HM-Decomp: " + ", ".join(missing), file=sys.stderr)
        return 1

    total_insn = sum(n for _, n in all_r.values())
    done_insn = sum(all_r[pc][1] for pc in done)
    pct = 100.0 * done_insn / total_insn
    pct_r = 100.0 * len(done) / len(all_r)
    summary = (f"{pct:.2f}% of the code ({done_insn} of {total_insn} instructions), "
               f"{len(done)} of {len(all_r)} routines ({pct_r:.1f}%)")
    print(summary)

    if a.update_readme:
        blocks = {
            "README.md": (
                f"![Decompiled: {pct:.2f}%](https://img.shields.io/badge/decompiled-{pct:.2f}%25-2f855a)\n\n"
                f"**{pct:.2f}%** of the game's code decompiled and verified: "
                f"{done_insn} of {total_insn} instructions, "
                f"{len(done)} of {len(all_r)} routines ({pct_r:.1f}%).\n"
                f"Computed with `python3 tools/progress.py --decomp ../HM-Decomp` "
                f"over the HM-Decomp disassembly."),
            "README.es.md": (
                f"![Descompilado: {pct:.2f}%](https://img.shields.io/badge/descompilado-{pct:.2f}%25-2f855a)\n\n"
                f"**{pct:.2f}%** del código del juego descompilado y verificado: "
                f"{done_insn} de {total_insn} instrucciones, "
                f"{len(done)} de {len(all_r)} rutinas ({pct_r:.1f}%).\n"
                f"Se calcula con `python3 tools/progress.py --decomp ../HM-Decomp` "
                f"sobre el desensamblado de HM-Decomp."),
        }
        for fname, body in blocks.items():
            path = os.path.join(ROOT, fname)
            text = open(path, encoding="utf-8").read()
            block = "<!-- progress -->\n" + body + "\n<!-- /progress -->"
            new, n = re.subn(r"<!-- progress -->.*?<!-- /progress -->", lambda m: block, text, flags=re.S)
            if not n:
                print(f"progress: {fname} has no <!-- progress --> block", file=sys.stderr)
                return 1
            open(path, "w", encoding="utf-8").write(new)
    return 0


if __name__ == "__main__":
    sys.exit(main())
