#!/usr/bin/env python3
"""Turn the recompilation's generated C into the decompilation's test build.

    python3 tools/decomp_overrides.py --gen-dir build/gen \
        --names data/decomp_names.tsv --decomp src/hm_decomp.c

Runs on a COPY of the generated code (build/gen), never on the
recompilation project's own src/gen.

1. Names. Every compiled routine that HM-Decomp names is renamed from
   `bank_BB_AAAA_MmXx` to `Name_MmXx` (data/decomp_names.tsv, written by
   tools/import_decomp_names.py). Identifiers only: the program is unchanged.

2. Checks. Every routine registered in src/hm_decomp.c gets a wrapper: its
   compiled body is renamed `Name_MmXx__rom` and a new `Name_MmXx` calls it,
   and with HM_DECOMP_CHECK=1 (or during HM_DECOMP_FUZZ) also runs the
   hand-written rewrite on a snapshot and compares (src/hm_decomp.h).
"""
import argparse
import glob
import os
import re
import sys

FUNC_ID = re.compile(r"\bbank_([0-9a-f]{2})_([0-9A-F]{4})_(M[01]X[01])\b")


def load_names(path):
    names = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#"):
                pc, name = line.split("\t")
                names[int(pc, 16)] = name
    return names


def rename(gen_dir, names):
    if not names:
        return 0
    def sub(m):
        name = names.get(int(m.group(1), 16) << 16 | int(m.group(2), 16))
        return f"{name}_{m.group(3)}" if name else m.group(0)
    count = 0
    for path in sorted(glob.glob(os.path.join(gen_dir, "*.c")) + glob.glob(os.path.join(gen_dir, "*.h"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        new = FUNC_ID.sub(sub, text)
        if new != text:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
            count += 1
    return count


WRAP_DECL = """
/* hm decomp wrappers (tools/apply_overrides.py) */
extern int g_hm_decomp_check;
void *hm_decomp_check_begin(const struct CpuState *cpu);
void hm_decomp_check_end(const char *name, void *snapshot, const struct CpuState *cpu, int completed);
"""
WRAPPER = """
RecompReturn {fn}(CpuState *cpu) {{
  if (!g_hm_decomp_check) return {fn}__rom(cpu);
  void *_snap = hm_decomp_check_begin(cpu);
  RecompReturn _r = {fn}__rom(cpu);
  hm_decomp_check_end("{name}", _snap, cpu, _r == RECOMP_RETURN_NORMAL);
  return _r;
}}
"""


def decomp_registry(path):
    if not path or not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8").read()
    # Entries flagged HM_INTERP have no compiled body at their address (the
    # fuzzer runs them in the interpreter; see src/hm_decomp.h), but the game
    # may still run a compiled copy from the bank $00-$3F mirror (Name_lo):
    # those are wrapped too, so live calls are checked.
    return [(name, "HM_INTERP" in flags) for name, flags in
            re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)",\s*hm_\w+,\s*([^,]*),', text)]


def wrap(gen_dir, routines):
    wrapped = 0
    for path in sorted(glob.glob(os.path.join(gen_dir, "bank*_v2.c"))):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if "__rom(CpuState *cpu)" in text:
            continue                                    # already wrapped
        tail = []
        for name, _ in routines:
            pat = re.compile(r"^RecompReturn ((?:%s|%s_lo)_M[01]X[01])\(CpuState \*cpu\) \{" % (name, name), re.M)
            for fn in sorted(set(pat.findall(text))):
                text = text.replace("RecompReturn %s(CpuState *cpu) {" % fn,
                                    "RecompReturn %s__rom(CpuState *cpu) {" % fn, 1)
                text = text.replace('#include "funcs.h"\n',
                                    '#include "funcs.h"\nRecompReturn %s__rom(CpuState *cpu);\n' % fn, 1)
                tail.append(WRAPPER.format(fn=fn, name=name))
                wrapped += 1
        if tail:
            text = text.replace('#include "funcs.h"\n', '#include "funcs.h"\n' + WRAP_DECL, 1)
            text += "".join(tail)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    return wrapped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen-dir", required=True)
    ap.add_argument("--names", required=True, help="data/decomp_names.tsv")
    ap.add_argument("--decomp", required=True, help="src/hm_decomp.c (registry)")
    args = ap.parse_args()
    names = load_names(args.names)
    n = rename(args.gen_dir, names)
    print("decomp_overrides: %d names applied in %d files" % (len(names), n))
    routines = decomp_registry(args.decomp)
    w = wrap(args.gen_dir, routines)
    print("decomp_overrides: %d rewritten routines, %d compiled variants wrapped" % (len(routines), w))
    missing = [r for r, interp in routines if not interp and r not in names.values()]
    if missing:
        print("decomp_overrides: rewritten routines with no compiled body: %s" % ", ".join(missing),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
