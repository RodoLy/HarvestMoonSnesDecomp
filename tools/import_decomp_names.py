#!/usr/bin/env python3
"""Name the recompiled functions after the HM-Decomp disassembly.

    python3 tools/import_decomp_names.py --decomp ../HM-Decomp --gen build/gen

Reads every routine label with an address comment in HM-Decomp's code banks
(`GetRNG: ;838138`) and, for each one the recompiler compiles ahead of time
(a `bank_BB_AAAA_MmXx` body exists in the generated code), writes
`BBAAAA<TAB>Name` to data/decomp_names.tsv. tools/decomp_overrides.py then
renames those functions in the decompilation's copy of the generated C, so
it reads `GetRNG_M1X0` instead of `bank_83_8138_M1X0`.

This is a rename of C identifiers and nothing else. Declaring the names to
the analyzer instead (symbols.toml func roots) was tried and rejected: the
extra function boundaries changed how 31 bodies were split and moved work
between frames. The rename leaves the program byte-for-byte the same.

HM-Decomp addresses are in the FastROM mirror ($80-$87); the game also runs
code from the $00-$07 mirror, which the recompiler compiles separately. Those
copies get the same name with a `_lo` suffix.
"""
import argparse
import glob
import os
import re
import sys

LABEL = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):\s*;\s*([0-9A-Fa-f]{6})\b", re.M)
# Every compiled body announces itself; works before and after the rename.
BODY = re.compile(r'cpu_trace_func_entry\(cpu, 0x([0-9a-fA-F]{6}), "[A-Za-z_0-9]*_M([01])X([01])"\)')


# Routines HM-Decomp only has as a local label inside another routine (so
# LABEL does not see them) but that the recompiler compiles on their own.
MANUAL = {
    0x84A5D4: "AddCowHappiness",   # .AddCowHappiness in Event_UNKCowRelated
    0x81D1A4: "TileAtOffsetFromPlayer",  # CODE_81D1A4, after CalculateTileinFront
    0x81A801: "SUB_81A801",        # labels with no address comment
    0x81A83A: "SUB_81A83A",
    0x81A688: "ChangeMapTile",     # CODE_81A688
    0x81A6C1: "QueueTileGraphicsDMA",  # CODE_81A6C1
    0x81A500: "SpawnMapObject",    # CODE_81A500
    0x858000: "CreateGameObject",  # CODE_858000
    0x858AE5: "LoadGameObjectComponents",  # CODE_858AE5
    0x858C30: "LoadSpriteComponent",       # CODE_858C30
    0x81A5E1: "StartMapAnimation",  # CODE_81A5E1
    0x83C296: "PlaceAnimalsOnMap",  # CODE_83C296
    0x848A0D: "Event_TimeStopped",  # HM-Decomp's address comment says 808A0D
    0x8491D2: "Event_UNKTextrelated",  # HM-Decomp says 8091D2
    0x849513: "Event_MapScrolling",  # HM-Decomp says 809513
    0x84A90F: "Event_StoreValue",  # no address in HM-Decomp
    0x84B4DC: "Event_ChangeItemonHand",  # HM-Decomp says 84B4DE
    0x84BA51: "Event_UNKCCchanges16",  # no address in HM-Decomp
    0x84BB31: "Event_UNKReset5C",  # no address in HM-Decomp
    0x8393F9: "CloseTextBox",      # CODE_8393F9
    0x8394D7: "DrawTextBoxArrow",  # CODE_8394D7
    0x839495: "BlinkTextBoxArrow", # CODE_839495
    0x839447: "TextBoxArrow",      # CODE_839447
    0x839862: "FindLetterGraphics",  # CODE_839862
    0x839823: "DrawLetter",        # CODE_839823
    0x839838: "AdvanceLetterCursor",  # CODE_839838
    0x83975F: "BlinkTextCursor",   # CODE_83975F
    0x8397A6: "PrintDigit",        # CODE_8397A6
    0x83951C: "PrintTextFrame",    # ADDDDFFFF
    0x84C2FE: "SaveSlotMenuInputs",  # CODE_84C2FE
    0x84C380: "LoadSlotMenuInputs",  # CODE_84C380
    0x84C428: "StartMenuInputs",   # CODE_84C428
    0x84C458: "TitleMenuInputs",   # CODE_84C458
    0x84C540: "ListInputs",        # CODE_84C540
    0x84CF05: "ImmobileInputs",    # no address in HM-Decomp
    0x8093A4: "CustomiseSpritePalette",  # HM-Decomp says 8093A3 (the RTL before it)
    0x83AD91: "CBBBB",             # indented labels in HM-Decomp
    0x83AEC3: "CDDDD",
    0x83AF37: "CEEEE",
    0x83B0F6: "CFFFF",
    0x84C6B7: "PlayerMovementInputs",  # no address in HM-Decomp
    0x84C7D6: "CODE_84C7D6",       # a JSL target inside PlayerMovementInputs
    0x82EA60: "Unk_OutOfMenu",     # no address in HM-Decomp
    0x84BC9C: "Event_Cmd51",
    0x84BD44: "Event_Cmd52",
    0x84BE0D: "Event_Cmd53",
    0x84BED6: "Event_Cmd54",
    0x84BF20: "Event_Cmd55",
    0x84BF81: "Event_Cmd56",
    0x84BFA0: "Event_Cmd57",
    0x84BFBF: "Event_Cmd58",
    0x84C000: "Event_Cmd59",
    0x8493C9: "Event_ResetFlag",   # HM-Decomp says 8093C9
    0x84BA10: "Event_EditTileonMap",  # no address in HM-Decomp
    0x83C9E5: "UnusedBarnCheck",  # after UnusedBarnCoopCheck's RTL, no label
    0x81A7CE: "SUB_81A7CE",  # maptiles.c
    0x81B2BB: "SUB_81B2BB",  # maptiles.c
    0x81AC80: "ReplaceTilesMoutain",  # maptiles.c
    0x81B121: "ReplaceTilesCoop",  # maptiles.c
    0x81B1CF: "ReplaceTilesMountainCave",  # maptiles.c
    0x81B21E: "ReplaceTilesElfTunnel",  # maptiles.c
    0x81B26D: "ReplaceTilesMap37",  # maptiles.c
    0x81AC71: "ReplaceTilesTown",  # maptiles.c, a lone RTS
    0x81AC78: "ReplaceTilesFlowerFestival",  # maptiles.c, a lone RTS
    0x81AC79: "ReplaceTilesHarvestFestival",  # maptiles.c, a lone RTS
    0x81AC7A: "DATA8_00AC7A",  # maptiles.c, a lone RTS
    0x81AC7B: "DATA8_00AC7B",  # maptiles.c, a lone RTS
    0x81AC7C: "ReplaceTilesForkSpring",  # maptiles.c, a lone RTS
    0x81AC7D: "ReplaceTilesForkSummer",  # maptiles.c, a lone RTS
    0x81AC7E: "ReplaceTilesForkFall",  # maptiles.c, a lone RTS
    0x81AC7F: "ReplaceTilesForkWinter",  # maptiles.c, a lone RTS
    0x81ACCB: "DATA8_00ACCB",  # maptiles.c, a lone RTS
    0x81ADD1: "ReplaceTilesMayorHouse",  # maptiles.c, a lone RTS
    0x81ADD2: "ReplaceTilesMayorHouseHall",  # maptiles.c, a lone RTS
    0x81ADD3: "ReplaceTilesMariasRoom",  # maptiles.c, a lone RTS
    0x81ADD4: "ReplaceTilesChurch",  # maptiles.c, a lone RTS
    0x81AE6A: "ReplaceTilesFlowerShopRooms",  # maptiles.c, a lone RTS
    0x81AE6B: "ReplaceTilesBar",  # maptiles.c, a lone RTS
    0x81AE6C: "ReplaceTilesBarRooms",  # maptiles.c, a lone RTS
    0x81AE6D: "ReplaceTilesRestaurant",  # maptiles.c, a lone RTS
    0x81AE6E: "ReplaceTilesRestaurantRooms",  # maptiles.c, a lone RTS
    0x81AFBF: "ReplaceTilesGeneralStoreRooms",  # maptiles.c, a lone RTS
    0x81B063: "ReplaceTilesWitchHouse",  # maptiles.c, a lone RTS
    0x838D39: "UNK_Audio14",  # HM-Decomp says 838D38 (the RTL of UNK_Audio13)
    0x81D6E2: "CODE_81D6E2",  # interact.c (CODE_ labels are not imported)
    0x81D6F8: "CODE_81D6F8",
    0x81D1C5: "CODE_81D1C5",  # people.c
    0x8581A2: "CODE_8581A2",  # objects.c: destroying a game object
    0x858B7B: "CODE_858B7B",
    0x858B41: "CODE_858B41",
    0x858C9F: "CODE_858C9F",
    0x8580B9: "CODE_8580B9",  # objects.c: moving a game object
    0x858100: "CODE_858100",  # changing its sprite
    0x8581CB: "CODE_8581CB",  # reading a game object back
    0x858BB0: "InvertIfFlipedX",  # no address in HM-Decomp
    0x858BBC: "InvertIfFlipedY",
    0x81A58F: "CODE_81A58F",  # mapedit.c: footprints in the snow
    0x81A600: "CODE_81A600",  # the map animation player
    0x81A4F1: "CODE_81A4F1",
    0x829999: "ToolUsedCornSeeds",  # HM-Decomp's address comment has a typo (82A9999)
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--gen", default="build/gen")
    ap.add_argument("--out", default="data/decomp_names.tsv")
    ap.add_argument("--skip-prefix", default="CODE_",
                    help="comma list of label prefixes that are not routines")
    a = ap.parse_args()

    labels = {}
    for path in sorted(glob.glob(os.path.join(a.decomp, "src", "code_banks", "*.asm"))):
        for m in LABEL.finditer(open(path, encoding="utf-8", errors="replace").read()):
            name, pc = m.group(1), int(m.group(2), 16)
            if any(name.startswith(p) for p in a.skip_prefix.split(",") if p):
                continue
            labels.setdefault(pc, name)

    for pc, name in MANUAL.items():
        labels[pc] = name           # our name wins (HM-Decomp may have a placeholder)

    variants = {}          # pc24 -> sorted list of (m, x)
    for path in glob.glob(os.path.join(a.gen, "bank*_v2.c")):
        for m in BODY.finditer(open(path, encoding="utf-8").read()):
            variants.setdefault(int(m.group(1), 16), set()).add((int(m.group(2)), int(m.group(3))))

    taken = set()
    rows = []
    for pc, name in sorted(labels.items()):
        hi = pc | 0x800000
        for bank_pc, cname in ((hi, name), (hi & 0x7FFFFF, name + "_lo")):
            if bank_pc not in variants or cname in taken:
                continue
            taken.add(cname)
            rows.append(f"{bank_pc:06X}\t{cname}")
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# pc24<TAB>name -- generated by tools/import_decomp_names.py from HM-Decomp labels\n")
        f.write("\n".join(rows) + "\n")
    named = len(rows)
    print(f"import_decomp_names: {len(labels)} decomp labels, {named} compiled routines named")
    return 0


if __name__ == "__main__":
    sys.exit(main())
