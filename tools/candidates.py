#!/usr/bin/env python3
"""Which routines to decompile next.

    python3 tools/candidates.py --decomp ../HM-Decomp [-n 40]

For every routine of HM-Decomp not yet registered in src/hm_decomp.c, lists
its size (instructions, as tools/progress.py counts them), the routines it
calls (JSL / JSR to a label) that are still to do, and whether it touches
hardware registers ($21xx PPU / APU, $42xx, $43xx DMA) or waits for the
NMI, which the checker cannot compare. The "ready" ones (every callee
done, no hardware) come first, biggest first.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import progress  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALL = re.compile(r"\bJS[LR](?:\.[LW])?\s+([A-Za-z_][A-Za-z0-9_.]*)")
HW = re.compile(r"\$(?:21[0-9A-Fa-f]{2}|42[0-9A-Fa-f]{2}|43[0-9A-Fa-f]{2})\b|!APUIO|!(?:INI|NMI|HDMA|MDMA|VMAIN|VMADD|CGADD|OAMADD|INIDISP)|WaitForNMI")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", default=os.path.join(ROOT, "..", "HM-Decomp"))
    ap.add_argument("-n", type=int, default=40)
    a = ap.parse_args()

    found, lines = progress.parse(a.decomp)
    all_r = progress.routines(a.decomp)
    done = progress.registered(os.path.join(ROOT, "src", "hm_decomp.c"))
    done_names = set(done.values())
    # names of everything (HM-Decomp label -> key) and the code of each block
    name_of = {k: n for k, (n, _) in found.items()}
    label_key = {n: k for k, n in name_of.items()}
    for pc, (label, name, *_) in progress.LOCAL.items():
        label_key[label.lstrip(".")] = pc
        label_key[name] = pc
    code = {}
    for rows in lines.values():
        for owner, c in rows:
            if owner is not None:
                code.setdefault(owner, []).append(c)

    def is_done(callee):
        k = label_key.get(callee)
        return callee in done_names or (k is not None and k in done)

    rows = []
    for k, (name, n) in all_r.items():
        if k in done:
            continue
        text = "\n".join(code.get(k, []))
        callees = sorted({c for c in CALL.findall(text) if not c.startswith(".")})
        todo = [c for c in callees if not is_done(c) and c != name]
        hw = bool(HW.search(text))
        rows.append((not todo and not hw, n, name, k, todo, hw))
    rows.sort(key=lambda r: (not r[0], -r[1]))
    for ready, n, name, k, todo, hw in rows[: a.n]:
        where = f"{k:06X}" if isinstance(k, int) else "------"
        flags = ("ready" if ready else "") + (" HW" if hw else "")
        print(f"{where} {n:5d} {name:32s} {flags:6s} {' '.join(todo[:6])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
