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
    0x849079: "Event_UNKCCchanges5",  # ";809079" in HM-Decomp
    0x849553: "Event_UNKCFD1",        # ";809553"
    0x849633: "Event_UNKCCchanges9",  # ";809633"
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
    0x8582C7: "CODE_8582C7",  # animating the game objects
    0x858896: "PresetsWRAMCopyOAMCopy",  # no address in HM-Decomp
    0x81CFA0: "CODE_81CFA0",  # creating the player's object
    0x81CFE6: "CODE_81CFE6",  # the player's object each frame
    0x81D03D: "CODE_81D03D",  # reading it back
    0x82EB57: "SUB_82EB57",   # the name screen's cursor
    0x83F4D8: "CODE_83F4D8",  # the start of the ending
    0x818000: "SUB_818000",   # putting an item down
    0x8180B7: "SUB_8180B7",   # an item's sprite
    # items.c: the items' own routines (DATA8_8197C0), unlabelled in HM-Decomp
    0x8180FE: "ItemObject_01",
    0x818102: "ItemObject_02",
    0x818106: "ItemObject_03",
    0x81810A: "ItemObject_04",
    0x81810E: "ItemObject_05",
    0x818112: "ItemObject_06",
    0x818116: "ItemObject_07",
    0x81811A: "ItemObject_08",
    0x81811E: "ItemObject_09",
    0x818122: "ItemObject_0A",
    0x818126: "ItemObject_0D",
    0x81812A: "ItemObject_0E",
    0x81812E: "ItemObject_0F",
    0x818132: "ItemObject_10",
    0x81814A: "ItemObject_11",
    0x818162: "ItemObject_12",
    0x81817A: "ItemObject_13",
    0x818192: "ItemObject_14",
    0x818196: "ItemObject_15",
    0x81819A: "ItemObject_16",
    0x81819E: "ItemObject_17",
    0x8181A2: "ItemObject_18",
    0x8181A6: "ItemObject_19",
    0x8181AA: "ItemObject_1A",
    0x8181AE: "ItemObject_1B",
    0x8181C8: "ItemObject_1C",
    0x8181E2: "ItemObject_1D",
    0x8181FC: "ItemObject_1E",
    0x81823E: "ItemObject_1F",
    0x818280: "ItemObject_20",
    0x8182C2: "ItemObject_21",
    0x8182FE: "ItemObject_22",
    0x8183B6: "ItemObject_23",
    0x8183B7: "ItemObject_24",
    0x8183BB: "ItemObject_25",
    0x8183BF: "ItemObject_26",
    0x8183C3: "ItemObject_27",
    0x8183C4: "ItemObject_28",
    0x8183C5: "ItemObject_29",
    0x818499: "ItemObject_44",
    0x8184D5: "ItemObject_45",
    0x818511: "ItemObject_46",
    0x81854D: "ItemObject_47",
    0x818589: "ItemObject_48",
    0x8185C5: "ItemObject_49",
    0x8185DF: "ItemObject_4A",
    0x8185F6: "ItemObject_4B",
    0x81860D: "ItemObject_4C",
    0x818624: "ItemObject_4D",
    0x81865B: "ItemObject_4E",
    0x818692: "ItemObject_4F",
    0x8186C9: "ItemObject_50",
    0x818700: "ItemObject_51",
    0x818737: "ItemObject_52",
    0x81874E: "ItemObject_53",
    0x818765: "ItemObject_54",
    0x818781: "ItemObject_55",
    0x81879D: "ItemObject_56",
    0x8187B7: "ItemObject_58",
    0x8187BB: "ItemObject_59",
    0x8187BF: "ItemObject_5A",
    # UNK_TableFunctionSelect's routines: who appears on entering each map
    0x83CB69: "MapEvent_CB69",
    0x83EBA6: "MapEvent_EBA6",
    0x83E76D: "MapEvent_E76D",
    0x83D2E0: "MapEvent_D2E0",
    0x83DE9B: "MapEvent_DE9B",
    0x83E407: "MapEvent_E407",
    0x83E4CB: "MapEvent_E4CB",
    0x83D844: "MapEvent_D844",
    0x83D929: "MapEvent_D929",
    0x83D955: "MapEvent_D955",
    0x83DA1F: "MapEvent_DA1F",
    0x83DA4B: "MapEvent_DA4B",
    0x83DAED: "MapEvent_DAED",
    0x83DB8F: "MapEvent_DB8F",
    0x83DC1E: "MapEvent_DC1E",
    0x83DC4A: "MapEvent_DC4A",
    0x83DD95: "MapEvent_DD95",
    0x83DE64: "MapEvent_DE64",
    0x83D4D7: "MapEvent_D4D7",
    0x83D5E7: "MapEvent_D5E7",
    0x83D613: "MapEvent_D613",
    0x83D74E: "MapEvent_D74E",
    0x83D818: "MapEvent_D818",
    0x83E586: "MapEvent_E586",
    0x83E5A2: "MapEvent_E5A2",
    0x83E5B4: "MapEvent_E5B4",
    0x83E5CD: "MapEvent_E5CD",
    0x83E5DF: "MapEvent_E5DF",
    0x83E5F1: "MapEvent_E5F1",
    0x83E603: "MapEvent_E603",
    0x83E632: "MapEvent_E632",
    0x83E661: "MapEvent_E661",
    0x83E690: "MapEvent_E690",
    0x83E6B2: "MapEvent_E6B2",
    0x83E6C4: "MapEvent_E6C4",
    0x83E74C: "MapEvent_E74C",
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
