#!/usr/bin/env python3
"""Rank lift candidates: parse function boundaries from the listing, build the
call graph, classify hardware-purity, and weight by a --profout profile.

Usage: liftscan.py PROFILE [--top N]
"""
import re, os, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
LISTING = os.path.join(HERE, "Listings.asm")

LIFTED = {"SaveBuf_VerifyChecksum", "SRAM_ReadBytes", "SRAM_WriteBytes",
          "SRAM_RecalcChecksum", "SRAM_FormatSave"}

HW_RE = re.compile(r"\$00?(A1|C0)[0-9A-F]{4}|\(\$(A1|C0)[0-9A-F]{4}\)", re.I)

def parse():
    funcs = {}          # name -> dict(start, end, insns[(addr,mnem,ops)], hw, calls)
    cur = None
    expect_func = False
    for raw in open(LISTING, encoding="latin-1"):
        line = raw.rstrip("\r\n")
        if len(line) < 9 or not re.match(r"^[0-9A-F]{8}", line):
            continue
        addr = int(line[:8], 16)
        src = (line[36:] if len(line) > 36 else "").strip()
        if re.match(r"^; =+ S U B", src):
            expect_func = True
            continue
        m = re.match(r"^; End of function (\w+)", src)
        if m:
            if cur:
                funcs[cur]["end"] = addr
            cur = None
            continue
        m = re.match(r"^(\w+):", src)
        if m and expect_func:
            expect_func = False
            cur = m.group(1)
            funcs[cur] = {"start": addr, "end": None, "insns": [],
                          "hw": False, "calls": set(), "jumps_out": False}
        if cur is None:
            continue
        body = src.split(";")[0].strip()
        if not body or body.endswith(":"):
            continue
        parts = body.split(None, 1)
        mnem = parts[0].lower()
        ops = parts[1] if len(parts) > 1 else ""
        if mnem.startswith(("dc", "include", "align", "even")):
            continue
        funcs[cur]["insns"].append((addr, mnem, ops))
        if HW_RE.search(ops):
            funcs[cur]["hw"] = True
        if mnem.startswith(("jsr", "bsr")):
            t = ops.strip()
            if re.match(r"^\w+$", t):
                funcs[cur]["calls"].add(t)
            else:
                funcs[cur]["hw"] = True        # computed call: opaque
        if mnem.startswith("jmp"):
            funcs[cur]["jumps_out"] = True
    return {k: v for k, v in funcs.items() if v["end"]}

def main():
    prof_path = sys.argv[1]
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 20
    prof = {}
    for ln in open(prof_path):
        a, n = ln.split()
        prof[int(a, 16)] = int(n)

    funcs = parse()
    print(f"functions with closed boundaries: {len(funcs)}")

    # transitive purity: pure = no hw, no jumps out, all calls to lifted or pure
    pure = {}
    def is_pure(name, seen=()):
        if name in pure: return pure[name]
        if name in LIFTED: return True
        f = funcs.get(name)
        if not f or f["hw"] or f["jumps_out"] or name in seen: return False
        ok = all(is_pure(c, seen + (name,)) for c in f["calls"])
        pure[name] = ok
        return ok

    rows = []
    for name, f in funcs.items():
        if name in LIFTED: continue
        weight = sum(prof.get(a, 0) for a, _, _ in f["insns"])
        if not weight: continue
        rows.append((weight, name, len(f["insns"]), is_pure(name),
                     ",".join(sorted(f["calls"])) or "-"))
    rows.sort(reverse=True)

    print(f"\n{'dyn-instrs':>12}  {'pure':>4}  {'size':>5}  name / calls")
    shown = 0
    for w, name, size, p, calls in rows:
        if shown >= top: break
        print(f"{w:>12}  {'YES' if p else 'no':>4}  {size:>5}  {name}  [{calls}]")
        shown += 1

    print("\n=== top PURE candidates ===")
    shown = 0
    for w, name, size, p, calls in rows:
        if not p or shown >= top: continue
        print(f"{w:>12}  size={size:<4} {name}  calls=[{calls}]")
        shown += 1

if __name__ == "__main__":
    main()
