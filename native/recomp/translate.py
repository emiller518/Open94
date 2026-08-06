#!/usr/bin/env python3
"""Stage D translator — pass 1: parse the full assembly source (Source.asm +
Unknown includes) into a line-level IR and produce an instruction census.

Outputs:
  native/recomp/ir.jsonl   one record per source line (label/instr/data)
  stdout                   census: mnemonics, addressing modes, oddities
"""
import re, os, json, sys
from collections import Counter

PROJ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IR_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ir.jsonl")

DIRECTIVES = {"dc", "dcb", "ds", "align", "even", "include", "end", "macro",
              "endm", "if", "endif", "org", "equ", "rept", "endr"}
MACROS = {"cmpnb", "cmpnw", "cmpnl"}

def split_operands(s):
    """Split on commas, respecting ' and \" quotes and () nesting."""
    out, cur, depth, q = [], "", 0, None
    for ch in s:
        if q:
            cur += ch
            if ch == q: q = None
        elif ch in "'\"":
            cur += ch; q = ch
        elif ch == "(":
            depth += 1; cur += ch
        elif ch == ")":
            depth -= 1; cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def strip_comment(s):
    q = None
    for i, ch in enumerate(s):
        if q:
            if ch == q: q = None
        elif ch in "'\"": q = ch
        elif ch == ";": return s[:i]
    return s

MODE_PATTERNS = [
    (re.compile(r"^#"), "imm"),
    (re.compile(r"^d[0-7]$"), "dreg"),
    (re.compile(r"^a[0-7]$|^sp$"), "areg"),
    (re.compile(r"^\(a[0-7]\)\+$"), "postinc"),
    (re.compile(r"^-\(a[0-7]\)$"), "predec"),
    (re.compile(r"^\(a[0-7]\)$"), "indirect"),
    (re.compile(r"^-?[^(,]*\(a[0-7]\)$"), "disp_an"),
    (re.compile(r"^-?[^(,]*\(a[0-7],"), "index_an"),
    (re.compile(r"^-?[^(,]*\(pc\)$"), "disp_pc"),
    (re.compile(r"^-?[^(,]*\(pc,"), "index_pc"),
    (re.compile(r"^\(.*\)\.w$"), "abs_w"),
    (re.compile(r"^\(.*\)\.l$"), "abs_l"),
    (re.compile(r"^(sr|ccr|usp)$"), "special"),
    (re.compile(r"^[da][0-7][-/]"), "reglist"),
]

def classify_op(op):
    o = op.lower()
    for pat, name in MODE_PATTERNS:
        if pat.match(o): return name
    if re.match(r"^[a-z_@.][\w.]*$", o) or re.match(r"^\$?[0-9a-f]+$", o):
        return "abs_sym"
    return "other"

def main():
    src_root = PROJ
    main_file = os.path.join(src_root, "Source.asm")
    mnem_census = Counter()
    mode_census = Counter()
    macro_census = Counter()
    unparsed = []
    counts = Counter()
    ir = open(IR_OUT, "w")

    def parse_file(path, depth=0):
        for lineno, raw in enumerate(open(path, encoding="latin-1"), 1):
            line = raw.rstrip("\r\n")
            body = strip_comment(line).rstrip()
            if not body.strip():
                continue
            label = None
            m = re.match(r"^([A-Za-z_.@][\w.]*):?\s*(.*)$", body) if not body[0].isspace() else None
            if m:
                label, rest = m.group(1), m.group(2)
            else:
                rest = body.strip()
            rec = {"f": os.path.basename(path), "n": lineno}
            if label: rec["l"] = label
            if rest:
                parts = rest.split(None, 1)
                mnem = parts[0].lower()
                ops = split_operands(parts[1]) if len(parts) > 1 else []
                base = mnem.split(".")[0]
                if base == "include":
                    inc = ops[0].strip('"').replace("\\", "/") if ops else parts[1].strip().strip('"').replace("\\", "/")
                    rec.update(k="inc", t=inc)
                    ir.write(json.dumps(rec) + "\n")
                    if "Unknown" in inc:
                        parse_file(os.path.join(src_root, inc), depth + 1)
                    continue
                if base in MACROS:
                    macro_census[base] += 1
                    counts["macro"] += 1
                    rec.update(k="macro", m=mnem, o=ops)
                elif base in DIRECTIVES or base.startswith("dc"):
                    counts["data"] += 1
                    rec.update(k="data", m=mnem)
                elif re.match(r"^[a-z]+$", base):
                    counts["instr"] += 1
                    mnem_census[mnem] += 1
                    for op in ops:
                        mode_census[classify_op(op)] += 1
                    rec.update(k="i", m=mnem, o=ops)
                else:
                    counts["unparsed"] += 1
                    if len(unparsed) < 20: unparsed.append(f"{path}:{lineno}: {body.strip()[:80]}")
                    rec.update(k="?", t=body.strip()[:120])
            else:
                rec["k"] = "label"
                counts["label_only"] += 1
            ir.write(json.dumps(rec) + "\n")

    parse_file(main_file)
    ir.close()

    print(f"=== counts ===\n{dict(counts)}")
    print(f"\n=== {len(mnem_census)} unique mnemonics (top 40) ===")
    for m, c in mnem_census.most_common(40):
        print(f"{m:12} {c}")
    print(f"\n=== base mnemonics ===")
    bases = Counter()
    for m, c in mnem_census.items(): bases[m.split('.')[0]] += c
    print(", ".join(f"{m}({c})" for m, c in bases.most_common()))
    print(f"\n=== addressing modes ===")
    for m, c in mode_census.most_common(): print(f"{m:10} {c}")
    print(f"\n=== macros === {dict(macro_census)}")
    if unparsed:
        print("\n=== unparsed samples ===")
        print("\n".join(unparsed))

if __name__ == "__main__":
    main()
