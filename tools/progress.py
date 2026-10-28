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
    0x858CB2: ("CODE_858CB2", "CODE_858CB2"),
    0x858E57: ("CopiesWRAMtoOBJVGRAM", "CopiesWRAMtoOBJVGRAM"),
    0x82E5E7: ("SUB_82E5E7", "SUB_82E5E7"),
    0x82EA80: ("SUB_82EA80", "SUB_82EA80"),
    0x82EAB4: ("SUB_82EAB4", "SUB_82EAB4"),
    0x82E742: ("SUB_82E742", "SUB_82E742"),
    0x8281BF: ("ShippingReturn", "ShippingReturn"),    # no address comment
    0x83F4D7: ("CODE_83F4D7", "CODE_83F4D7"),
    0x8580B9: ("CODE_8580B9", "CODE_8580B9"),
    0x858100: ("CODE_858100", "CODE_858100"),
    0x8581CB: ("CODE_8581CB", "CODE_8581CB"),
    0x8582C7: ("CODE_8582C7", "CODE_8582C7", "GetOBJOrderToDraw"),
    0x858896: ("PresetsWRAMCopyOAMCopy", "PresetsWRAMCopyOAMCopy"),
    0x81CFE6: ("CODE_81CFE6", "CODE_81CFE6"),
    0x81D03D: ("CODE_81D03D", "CODE_81D03D"),
    0x82EB57: ("SUB_82EB57", "SUB_82EB57"),
    0x83F4D8: ("CODE_83F4D8", "CODE_83F4D8"),
    0x818000: ("SUB_818000", "SUB_818000"),
    0x8180B7: ("SUB_8180B7", "SUB_8180B7"),          # its first part, to the RTL
    # the player's actions $10-$57 (actions.c), inside CODE_81D716's block
    0x81D7C5: ("@81D7C5", "PlayerAction_10", "@81D979"),
    0x81D979: ("@81D979", "PlayerAction_11", "@81D9CC"),
    0x81D9CC: ("@81D9CC", "PlayerAction_12", "@81DA82"),
    0x81DA82: ("@81DA82", "PlayerAction_13", "@81DB38"),
    0x81DB38: ("@81DB38", "PlayerAction_14", "@81DBA1"),
    0x81DBA1: ("@81DBA1", "PlayerAction_15", "@81DBCC"),
    0x81DBCC: ("@81DBCC", "PlayerAction_16", "@81DBCD"),
    0x81DBCD: ("@81DBCD", "PlayerAction_17", "@81DBCE"),
    0x81DBCE: ("@81DBCE", "PlayerAction_18", "@81DBCF"),
    0x81DBCF: ("@81DBCF", "PlayerAction_19", "@81DBD0"),
    0x81DBD0: ("@81DBD0", "PlayerAction_1A", "@81DBD1"),
    0x81DBD1: ("@81DBD1", "PlayerAction_1B", "@81DBD2"),
    0x81DBD2: ("@81DBD2", "PlayerAction_1C", "@81DBD3"),
    0x81DBD3: ("@81DBD3", "PlayerAction_1D", "@81DBF0"),
    0x81DBF0: ("@81DBF0", "PlayerAction_1E", "@81DC57"),
    0x81DC57: ("@81DC57", "PlayerAction_1F", "@81DC58"),
    0x81DC58: ("@81DC58", "PlayerAction_20", "@81DC59"),
    0x81DC59: ("@81DC59", "PlayerAction_21", "@81DC5A"),
    0x81DC5A: ("@81DC5A", "PlayerAction_22", "@81DC5B"),
    0x81DC5B: ("@81DC5B", "PlayerAction_23", "@81DC5C"),
    0x81DC5C: ("@81DC5C", "PlayerAction_24", "@81DC5D"),
    0x81DC5D: ("@81DC5D", "PlayerAction_25", "@81DC5E"),
    0x81DC5E: ("@81DC5E", "PlayerAction_26", "@81DCCE"),
    0x81DCCE: ("@81DCCE", "PlayerAction_27", "@81DCF3"),
    0x81DCF3: ("@81DCF3", "PlayerAction_28", "@81DD18"),
    0x81DD18: ("@81DD18", "PlayerAction_29", "@81DD3D"),
    0x81DD3D: ("@81DD3D", "PlayerAction_2A", "@81DD62"),
    0x81DD62: ("@81DD62", "PlayerAction_2B", "@81DD87"),
    0x81DD87: ("@81DD87", "PlayerAction_2C", "@81DDAC"),
    0x81DDAC: ("@81DDAC", "PlayerAction_2D", "@81DE1C"),
    0x81DE1C: ("@81DE1C", "PlayerAction_2E", "@81E245"),
    0x81E245: ("@81E245", "PlayerAction_2F", "@81E26A"),
    0x81E26A: ("@81E26A", "PlayerAction_30", "@81E28F"),
    0x81E28F: ("@81E28F", "PlayerAction_31", "@81E2B4"),
    0x81E2B4: ("@81E2B4", "PlayerAction_32", "@81E2D9"),
    0x81E2D9: ("@81E2D9", "PlayerAction_33", "@81E349"),
    0x81E349: ("@81E349", "PlayerAction_34", "@81E36E"),
    0x81E36E: ("@81E36E", "PlayerAction_35", "@81E394"),
    0x81E394: ("@81E394", "PlayerAction_36", "@81E404"),
    0x81E404: ("@81E404", "PlayerAction_37", "@81E429"),
    0x81E429: ("@81E429", "PlayerAction_38", "@81E44E"),
    0x81E44E: ("@81E44E", "PlayerAction_39", "@81E473"),
    0x81E473: ("@81E473", "PlayerAction_3A", "@81E498"),
    0x81E498: ("@81E498", "PlayerAction_3B", "@81E4BD"),
    0x81E4BD: ("@81E4BD", "PlayerAction_3C", "@81E4E2"),
    0x81E4E2: ("@81E4E2", "PlayerAction_3D", "@81E507"),
    0x81E507: ("@81E507", "PlayerAction_3E", "@81E52C"),
    0x81E52C: ("@81E52C", "PlayerAction_3F", "@81E59C"),
    0x81E59C: ("@81E59C", "PlayerAction_40", "@81E6C3"),
    0x81E6C3: ("@81E6C3", "PlayerAction_41", "@81E6EA"),
    0x81E6EA: ("@81E6EA", "PlayerAction_42", "@81E79E"),
    0x81E79E: ("@81E79E", "PlayerAction_43", "@81EBD5"),
    0x81EBD5: ("@81EBD5", "PlayerAction_44", "@81EFAD"),
    0x81EFAD: ("@81EFAD", "PlayerAction_45", "@81F061"),
    0x81F061: ("@81F061", "PlayerAction_46", "@81F121"),
    0x81F121: ("@81F121", "PlayerAction_47", "@81F2DA"),
    0x81F2DA: ("@81F2DA", "PlayerAction_48", "@81F40E"),
    0x81F40E: ("@81F40E", "PlayerAction_49", "@81F490"),
    0x81F490: ("@81F490", "PlayerAction_4A", "@81F4AD"),
    0x81F4AD: ("@81F4AD", "PlayerAction_4B", "@81F4CA"),
    0x81F4CA: ("@81F4CA", "PlayerAction_4C", "@81F4E6"),
    0x81F4E6: ("@81F4E6", "PlayerAction_4D", "@81F502"),
    0x81F502: ("@81F502", "PlayerAction_4E", "@81F51E"),
    0x81F51E: ("@81F51E", "PlayerAction_4F", "@81F53A"),
    0x81F53A: ("@81F53A", "PlayerAction_50", "@81F5B0"),
    0x81F5B0: ("@81F5B0", "PlayerAction_51", "@81F6D7"),
    0x81F6D7: ("@81F6D7", "PlayerAction_52", "@81F7FF"),
    0x81F7FF: ("@81F7FF", "PlayerAction_53", "@81F86A"),
    0x81F86A: ("@81F86A", "PlayerAction_54", "@81F942"),
    0x81F942: ("@81F942", "PlayerAction_55", "@81FA69"),
    0x81FA69: ("@81FA69", "PlayerAction_56", "@81FA84"),
    0x81FA84: ("@81FA84", "PlayerAction_57", "DATA16_81FAB4"),
    0x81D716: ("CODE_81D716", "CODE_81D716", "@81D7C5"),
    # the player each frame (playerstate.c), inside SUB_81BFB7's block
    0x81C061: ("@81C061", "PlayerState_1C", "@81C0B5"),       # not rewritten: waits frames
    0x81BFDC: ("@81BFDC", "PlayerState_00", "@81C002"),
    0x81C002: ("@81C002", "ResetIdleAnimatioTimer", "DATA16_81C027"),
    0x81C0B5: ("@81C0B5", "PlayerState_1A", "@81C111"),
    0x81C111: ("@81C111", "PlayerState_17", "@81C153"),
    0x81C153: ("@81C153", "PlayerState_18", "@81C1A1"),
    0x81C1A1: ("@81C1A1", "PlayerState_16", "@81C1F0"),
    0x81C1F0: ("@81C1F0", "PlayerState_15", "@81C211"),
    0x81C211: ("@81C211", "PlayerState_14", "@81C232"),
    0x81C232: ("@81C232", "PlayerState_0F", "@81C242"),
    0x81C242: ("@81C242", "PlayerState_10", "@81C263"),
    0x81C263: ("@81C263", "PlayerState_11", "@81C28E"),
    0x81C28E: ("@81C28E", "PlayerState_12", "@81C2AF"),
    0x81C2AF: ("@81C2AF", "PlayerState_13", "@81C333"),
    0x81C333: ("@81C333", "PlayerState_1B", "@81C364"),
    0x81C364: ("@81C364", "PlayerState_0D", "@81C395"),
    0x81C395: ("@81C395", "PlayerState_0C", "@81C4EC"),
    0x81C4EC: ("@81C4EC", "PlayerState_0B", "@81C51A"),
    0x81C51A: ("@81C51A", "PlayerState_0A", "@81C556"),
    0x81C556: ("@81C556", "CODE_81C556", "@81C66E"),
    0x81C66E: ("@81C66E", "CODE_81C66E", "@81C736"),
    0x81C736: ("@81C736", "CODE_81C736", "@81C83B"),
    0x81C83B: ("@81C83B", "CODE_81C83B", "@81C86B"),
    0x81C86B: ("@81C86B", "PlayerState_03", "@81C991"),
    0x81C991: ("@81C991", "PlayerState_04", "@81C9DF"),
    0x81C9DF: ("@81C9DF", "PlayerState_05", "@81CA36"),
    0x81CA36: ("@81CA36", "PlayerState_08", "@81CB14"),
    0x81CB14: ("@81CB14", "CODE_81CB14", "@81CB5E"),
    0x81CB5E: ("@81CB5E", "CODE_81CB5E", "@81CB77"),
    0x81CB77: ("@81CB77", "CODE_81CB77", "CODE_81CFA0"),
    # the item routines and their state machines (items.c)
    0x8180FE: ("@8180FE", "ItemObject_01", "@818102"),
    0x818102: ("@818102", "ItemObject_02", "@818106"),
    0x818106: ("@818106", "ItemObject_03", "@81810A"),
    0x81810A: ("@81810A", "ItemObject_04", "@81810E"),
    0x81810E: ("@81810E", "ItemObject_05", "@818112"),
    0x818112: ("@818112", "ItemObject_06", "@818116"),
    0x818116: ("@818116", "ItemObject_07", "@81811A"),
    0x81811A: ("@81811A", "ItemObject_08", "@81811E"),
    0x81811E: ("@81811E", "ItemObject_09", "@818122"),
    0x818122: ("@818122", "ItemObject_0A", "@818126"),
    0x818126: ("@818126", "ItemObject_0D", "@81812A"),
    0x81812A: ("@81812A", "ItemObject_0E", "@81812E"),
    0x81812E: ("@81812E", "ItemObject_0F", "@818132"),
    0x818132: ("@818132", "ItemObject_10", "@81814A"),
    0x81814A: ("@81814A", "ItemObject_11", "@818162"),
    0x818162: ("@818162", "ItemObject_12", "@81817A"),
    0x81817A: ("@81817A", "ItemObject_13", "@818192"),
    0x818192: ("@818192", "ItemObject_14", "@818196"),
    0x818196: ("@818196", "ItemObject_15", "@81819A"),
    0x81819A: ("@81819A", "ItemObject_16", "@81819E"),
    0x81819E: ("@81819E", "ItemObject_17", "@8181A2"),
    0x8181A2: ("@8181A2", "ItemObject_18", "@8181A6"),
    0x8181A6: ("@8181A6", "ItemObject_19", "@8181AA"),
    0x8181AA: ("@8181AA", "ItemObject_1A", "@8181AE"),
    0x8181AE: ("@8181AE", "ItemObject_1B", "@8181C8"),
    0x8181C8: ("@8181C8", "ItemObject_1C", "@8181E2"),
    0x8181E2: ("@8181E2", "ItemObject_1D", "@8181FC"),
    0x8181FC: ("@8181FC", "ItemObject_1E", "@81823E"),
    0x81823E: ("@81823E", "ItemObject_1F", "@818280"),
    0x818280: ("@818280", "ItemObject_20", "@8182C2"),
    0x8182C2: ("@8182C2", "ItemObject_21", "@8182FE"),
    0x8182FE: ("@8182FE", "ItemObject_22", "@8183B6"),
    0x8183B6: ("@8183B6", "ItemObject_23", "@8183B7"),
    0x8183B7: ("@8183B7", "ItemObject_24", "@8183BB"),
    0x8183BB: ("@8183BB", "ItemObject_25", "@8183BF"),
    0x8183BF: ("@8183BF", "ItemObject_26", "@8183C3"),
    0x8183C3: ("@8183C3", "ItemObject_27", "@8183C4"),
    0x8183C4: ("@8183C4", "ItemObject_28", "@8183C5"),
    0x8183C5: ("@8183C5", "ItemObject_29", "@8183C9"),
    0x818499: ("@818499", "ItemObject_44", "@8184D5"),
    0x8184D5: ("@8184D5", "ItemObject_45", "@818511"),
    0x818511: ("@818511", "ItemObject_46", "@81854D"),
    0x81854D: ("@81854D", "ItemObject_47", "@818589"),
    0x818589: ("@818589", "ItemObject_48", "@8185C5"),
    0x8185C5: ("@8185C5", "ItemObject_49", "@8185DF"),
    0x8185DF: ("@8185DF", "ItemObject_4A", "@8185F6"),
    0x8185F6: ("@8185F6", "ItemObject_4B", "@81860D"),
    0x81860D: ("@81860D", "ItemObject_4C", "@818624"),
    0x818624: ("@818624", "ItemObject_4D", "@81865B"),
    0x81865B: ("@81865B", "ItemObject_4E", "@818692"),
    0x818692: ("@818692", "ItemObject_4F", "@8186C9"),
    0x8186C9: ("@8186C9", "ItemObject_50", "@818700"),
    0x818700: ("@818700", "ItemObject_51", "@818737"),
    0x818737: ("@818737", "ItemObject_52", "@81874E"),
    0x81874E: ("@81874E", "ItemObject_53", "@818765"),
    0x818765: ("@818765", "ItemObject_54", "@818781"),
    0x818781: ("@818781", "ItemObject_55", "@81879D"),
    0x81879D: ("@81879D", "ItemObject_56", "@8187B7"),
    0x8187B7: ("@8187B7", "ItemObject_58", "@8187BB"),
    0x8187BB: ("@8187BB", "ItemObject_59", "@8187BF"),
    0x8187BF: ("@8187BF", "ItemObject_5A", "@8187C3"),
    0x81883A: ("CODE_81883A", "CODE_81883A", "CODE_818855"),
    0x818855: ("CODE_818855", "CODE_818855", "CODE_81889A"),
    0x81889A: ("CODE_81889A", "CODE_81889A", "CODE_8188BC"),
    0x8188BC: ("CODE_8188BC", "CODE_8188BC", "CODE_8188E5"),
    0x8188E5: ("CODE_8188E5", "CODE_8188E5", "CODE_818923"),
    0x818923: ("CODE_818923", "CODE_818923", "CODE_818B6B"),
    0x818B6B: ("CODE_818B6B", "CODE_818B6B", "CODE_818BA5"),
    0x818BA5: ("CODE_818BA5", "CODE_818BA5", "CODE_818C24"),
    0x818C24: ("CODE_818C24", "CODE_818C24", "CODE_818D38"),
    0x818D38: ("CODE_818D38", "CODE_818D38", "CODE_818E16"),
    0x818E16: ("CODE_818E16", "CODE_818E16", "CODE_818E1B"),
    0x818E1B: ("CODE_818E1B", "CODE_818E1B", "CODE_818E98"),
    0x818E98: ("CODE_818E98", "CODE_818E98", "CODE_818F2A"),
    0x818F2A: ("CODE_818F2A", "CODE_818F2A", "CODE_818F5A"),
    0x818F5A: ("CODE_818F5A", "CODE_818F5A", "CODE_819010"),
    0x819010: ("CODE_819010", "CODE_819010", "CODE_819021"),
    0x819021: ("CODE_819021", "CODE_819021", "CODE_819049"),
    0x819049: ("CODE_819049", "CODE_819049", "CODE_819228"),
    0x819228: ("CODE_819228", "CODE_819228", "CODE_819379"),
    0x819379: ("CODE_819379", "CODE_819379", "@81968F"),
    0x81968F: ("CODE_81968F", "CODE_81968F", "CODE_819695"),
    0x819695: ("CODE_819695", "CODE_819695", "DATA8_8196AF"),
    # who appears on entering each map (UNK_TableFunctionSelect's routines)
    0x83CB69: ("@83CB69", "MapEvent_CB69", "@83D2E0"),
    0x83EBA6: ("@83EBA6", "MapEvent_EBA6", "RanchMasteryCalculator"),
    0x83E76D: ("@83E76D", "MapEvent_E76D", "@83EBA6"),
    0x83D2E0: ("@83D2E0", "MapEvent_D2E0", "@83D4D7"),
    0x83DE9B: ("@83DE9B", "MapEvent_DE9B", "@83DED3"),
    0x83E407: ("@83E407", "MapEvent_E407", "@83E4CB"),
    0x83E4CB: ("@83E4CB", "MapEvent_E4CB", "@83E586"),
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
