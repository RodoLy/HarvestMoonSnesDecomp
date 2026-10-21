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
            m = LABEL.match(line)
            code = line.split(";", 1)[0]
            if m:
                current = int(m.group(2), 16)
                routines.setdefault(current, [m.group(1), 0])
                code = ""
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


def split_locals(routines, lines):
    for pc, (label, name) in LOCAL.items():
        for path, rows in lines.items():
            start = next((i for i, (_, c) in enumerate(rows)
                          if c.strip() == label + ":" or c.strip().startswith(label + ":")), None)
            if start is None:
                continue
            # each instruction is taken out of the block that owns its line
            # (the routine may end in another top-level label: `return85: RTL`)
            n = 0
            for owner, code in rows[start:]:
                if is_insn(code):
                    n += 1
                    routines[owner][1] -= 1
                    if re.search(r"\bRT[LS]\b", code):
                        break
            routines[pc] = [name, n]
            break
    return routines


def routines_of(decomp_dir):
    found, lines = parse(decomp_dir)
    found = split_locals(found, lines)
    return {k: (name, n) for k, (name, n) in found.items() if n > 0}


routines = routines_of


def registered(path):
    text = open(path, encoding="utf-8").read()
    names = re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)",\s*hm_\w+,[^{]*?0x([0-9A-Fa-f]{6})\b', text)
    return {int(pc, 16): name for name, pc in names}


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
