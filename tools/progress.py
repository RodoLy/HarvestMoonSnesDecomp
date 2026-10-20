#!/usr/bin/env python3
"""How much of the game is decompiled.

    python3 tools/progress.py --decomp ../HM-Decomp [--update-readme]

Measures against the HM-Decomp disassembly (no ROM needed, so anyone can
recompute it):

  * Routines: every top-level label with an address comment in HM-Decomp's
    code banks (`GetRNG: ;838138`) whose body has instructions. Data tables
    (`db`/`dw`/`dl` only) are not counted.
  * Instructions: the 65816 instructions inside those routines (one per
    line of the disassembly). This is the headline percentage: a 4096-tile
    loop weighs more than a three-line helper.

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
ANY_TOP_LABEL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*:")
# An instruction: a mnemonic (3 letters, optional .B/.W/.L) or a %Set8bit /
# %Set16bit macro (one REP/SEP each). Labels and data directives are not.
INSN = re.compile(r"^\s*(?:[-+]\s+)?(?:\.[A-Za-z0-9_]+:\s*)?"
                  r"(?:([A-Z]{3})(?:\.[BWL])?\b|%Set(?:8|16)bit)")
DATA = {"DB", "DW", "DL", "DD"}


def routines(decomp_dir):
    found = {}
    for path in sorted(glob.glob(os.path.join(decomp_dir, "src", "code_banks", "*.asm"))):
        current = None
        for line in open(path, encoding="utf-8", errors="replace"):
            code = line.split(";", 1)[0] if not LABEL.match(line) else line
            m = LABEL.match(line)
            if m:
                current = int(m.group(2), 16)
                found.setdefault(current, [m.group(1), 0])
                continue
            if ANY_TOP_LABEL.match(code):
                current = None              # a label without address: not ours
                continue
            if current is None:
                continue
            i = INSN.match(code)
            if i and (i.group(1) or "").upper() not in DATA:
                found[current][1] += 1
    return {pc: (name, n) for pc, (name, n) in found.items() if n > 0}


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
