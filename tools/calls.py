#!/usr/bin/env python3
"""List the JSR / JSL call sites of a routine, with their return addresses.

    python3 tools/calls.py --rom rom.sfc 82:9328 [82:9460 ...] [--m 1 --x 0]

A linear disassembly from the entry (LoROM, no copier header or with one),
following REP / SEP to know the width of immediates, until the first
RTS / RTL / RTI that is not followed by more code of the routine (pass
--end to give the end address explicitly). The return address a JSR / JSL
pushes is the address of its own last byte, which is what the rewrites
pass to hm_jsl / push for the frame (never computed by hand).
"""
import argparse
import sys

def length(op, m, x):
    """Total length in bytes of the instruction `op` (m / x = 1: 8-bit)."""
    hi, lo = op >> 4, op & 0xF
    if lo == 0:
        if hi & 1:
            return 2                                   # branches
        return {0x0: 2, 0x2: 3, 0x4: 1, 0x6: 1, 0x8: 2}.get(hi, 2 if x else 3)
    if lo == 2:
        return {0x2: 4, 0x6: 3, 0x8: 3}.get(hi, 2 if hi != 0xA else (2 if x else 3))
    if lo in (1, 3, 5, 6, 7):
        return 2
    if lo == 4:
        return 3 if op in (0x44, 0x54, 0xF4) else 2
    if lo in (8, 0xA, 0xB):
        return 1
    if lo == 9:
        return 3 if hi & 1 else (2 if m else 3)
    if lo == 0xC:
        return 4 if op == 0x5C else 3
    if lo in (0xD, 0xE):
        return 3
    return 4                                           # lo == F: long


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--m", type=int, default=1)
    ap.add_argument("--x", type=int, default=0)
    ap.add_argument("--end", default=None, help="bank:addr where to stop")
    ap.add_argument("entries", nargs="+")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    if len(rom) % 0x8000 == 512:
        rom = rom[512:]

    def rd(addr):
        return rom[((addr >> 16) & 0x3F) << 15 | (addr & 0x7FFF)]

    def parse(s):
        b, ad = s.split(":")
        return int(b, 16) << 16 | int(ad, 16)

    for e in a.entries:
        pc = parse(e)
        end = parse(a.end) if a.end else None
        m, x = a.m, a.x
        print(f"{e}:")
        while True:
            op = rd(pc)
            n = length(op, m, x)
            if op == 0xC2:      # REP
                v = rd(pc + 1); m = 0 if v & 0x20 else m; x = 0 if v & 0x10 else x
            elif op == 0xE2:    # SEP
                v = rd(pc + 1); m = 1 if v & 0x20 else m; x = 1 if v & 0x10 else x
            if op in (0x20, 0xFC):   # JSR abs / JSR (abs,X)
                t = rd(pc + 1) | rd(pc + 2) << 8
                kind = "JSR" if op == 0x20 else "JSR (,X)"
                print(f"  {pc >> 16:02X}:{pc & 0xFFFF:04X} {kind} {pc >> 16:02X}:{t:04X}  ret {(pc + 2) & 0xFFFF:04X}")
            elif op == 0x22:
                t = rd(pc + 1) | rd(pc + 2) << 8 | rd(pc + 3) << 16
                print(f"  {pc >> 16:02X}:{pc & 0xFFFF:04X} JSL {t >> 16:02X}:{t & 0xFFFF:04X}  ret {(pc + 3) & 0xFFFF:04X}")
            pc += n
            if end is not None:
                if pc >= end:
                    break
            elif op in (0x60, 0x6B, 0x40):
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
