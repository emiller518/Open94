#!/usr/bin/env python3
"""Deep static triage of profiled routines for QUEUE.md refills.

Supersedes liftscan.py for refills. Improvements:
- resolves ALL labels (not just function starts) so far conditional
  branches out of a routine are detected (the sub_1A64C blind spot)
- hw regex catches no-leading-zero immediates (move.l #$C00000,a1 —
  the sub_11544 blind spot)
- flags rte, jmp-out, computed jsr/bsr; mul/div is a NOTE, not a
  blocker (the data-dependent cycle model landed 2026-07-31: charge via
  lift_charge_mulu/muls/divu/divs, results via alu_mulu/muls/divu/divs;
  div helpers decline on a zero divisor)
- hazard heuristics: spin-wait self-loops (pollers), last-insn-not-rts
  (IDA boundary suspects / fall-through tails)
- excludes QUEUE.md Done + Skip entries BY ADDRESS, parsed live from
  native/decomp/QUEUE.md — no hardcoded lists to go stale
- transitive callee closure (calls + branch-to-function-start tails)
  classified as lifted / batch-with / blocked, with the blocking path

Remaining blind spots (confirm at lift time): pointer-mediated hw where
the pointer comes from RAM or a table, RAM-flag pollers that don't match
the 2-insn spin shape, IDA function-boundary errors.

Usage: triage.py [PROFILE] [--cold]   (default native/out/profile-game.txt)

--cold also reports routines with ZERO profile weight (never executed
under the given script). These are triaged identically, but a lift of
one gets NO dynamic verification from that script — verify passes
vacuously. Per QUEUE.md's goal note, reach them with a coverage script
first, or mark the lift `done-unverified`. Cold rows print with a
COLD marker in place of the entry count.
"""
import re, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LISTING = os.path.join(HERE, "Listings.asm")
QUEUE = os.path.join(HERE, "..", "decomp", "QUEUE.md")
argv = [a for a in sys.argv[1:] if not a.startswith("--")]
COLD = "--cold" in sys.argv
PROFILE = argv[0] if argv else \
    os.path.join(HERE, "..", "out", "profile-game.txt")

HW_RE = re.compile(r"\$0{0,2}(A1|C0)[0-9A-F]{4}", re.I)
MULDIV = ("muls", "mulu", "divs", "divu")
SHARED_RTS = 0x15464

def registry_addrs():
    """ground truth: addresses with a live {0x...} entry in registry.c"""
    reg = set()
    path = os.path.join(HERE, "..", "decomp", "registry.c")
    in_comment = False
    for line in open(path, encoding="utf-8"):
        # a commented-out entry (DISABLED rows) is NOT done — track /* */
        s = line
        if in_comment:
            if "*/" in s: in_comment = False
            continue
        if "/*" in s and "*/" not in s: in_comment = True
        s = re.sub(r"/\*.*?\*/", "", s)
        m = re.match(r"\s*\{0x0*([0-9A-Fa-f]+),", s)
        if m:
            reg.add(int(m.group(1), 16))
    return reg

def queue_done_skip():
    """addresses from QUEUE.md's Done and Skip sections. The Done regex
    harvests bare hex from prose too (8 phantom addresses as of
    2026-08-02), so the done-set is intersected with registry.c ground
    truth — a token only counts as Done if a live registry entry exists.
    Registry entries missing from the Done prose are ADDED (a lift that
    shipped is done whether or not the prose mentions it)."""
    done, skip = set(), set()
    section = None
    for line in open(QUEUE, encoding="utf-8"):
        if line.startswith("## "):
            h = line.lower()
            section = ("skip" if "skip" in h else "done" if "done" in h
                       else "todo" if "todo" in h else None)
            continue
        if section == "todo" and line.startswith("|"):
            # non-todo statuses parked in the Todo table (skip / needs-fable /
            # done-unverified) are settled — exclude them like Skip rows
            cells = [c.strip() for c in line.split("|")]
            if len(cells) > 4 and cells[4] in ("skip", "needs-fable",
                                               "done", "done-unverified"):
                for m in re.finditer(r"\b([0-9A-F]{4,6})\b", cells[1]):
                    skip.add(int(m.group(1), 16))
            continue
        if section == "done":
            # old format: "Name 1A14E ·" — new format: "- **Name** `$1A14E` — ..."
            for m in re.finditer(r"[A-Za-z_]\w* ([0-9A-F]{4,6})\b|`\$([0-9A-F]{4,6})`", line):
                done.add(int(m.group(1) or m.group(2), 16))
        elif section == "skip" and line.startswith("|"):
            cell = line.split("|")[1]
            for m in re.finditer(r"\b([0-9A-F]{4,6})\b", cell):
                skip.add(int(m.group(1), 16))
    reg = registry_addrs()
    phantoms = done - reg
    if phantoms:
        print("triage: note: %d Done-section hex token(s) have no registry "
              "entry and are ignored: %s" %
              (len(phantoms), " ".join("%X" % a for a in sorted(phantoms))),
              file=sys.stderr)
    return (done & reg) | reg, skip

labels = {}
funcs = {}

def parse_listing():
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
        if m:
            labels[m.group(1)] = addr
            if expect_func:
                expect_func = False
                cur = m.group(1)
                funcs[cur] = {"start": addr, "end": None, "insns": [],
                              "hw": False, "calls": set(), "jmp": False,
                              "muldiv": False, "rte": False, "computed": False,
                              "branches": []}
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
        f = funcs[cur]
        f["insns"].append((addr, mnem, ops))
        if HW_RE.search(ops):
            f["hw"] = True
        if mnem.startswith(MULDIV):
            f["muldiv"] = True
        if mnem == "rte":
            f["rte"] = True
        if mnem.startswith(("jsr", "bsr")):
            t = ops.strip()
            if re.match(r"^\w+$", t):
                f["calls"].add(t)
            else:
                f["computed"] = True
        if mnem.startswith("jmp"):
            f["jmp"] = True
        if re.match(r"^b(ra|cc|cs|eq|ne|ge|gt|le|lt|hi|ls|mi|pl|vc|vs)(\.[swb])?$", mnem):
            t = ops.strip()
            if re.match(r"^\w+$", t):
                f["branches"].append(t)

def hazards(f):
    out = []
    insns = f["insns"]
    idx = {a: i for i, (a, _, _) in enumerate(insns)}
    for i, (a, m, o) in enumerate(insns):
        if not re.match(r"^b(cc|cs|eq|ne|ge|gt|le|lt|hi|ls|mi|pl|vc|vs)(\.[swb])?$", m):
            continue
        ta = labels.get(o.strip())
        if ta is None or ta not in idx:
            continue
        j = idx[ta]
        if j < i and i - j <= 2 and all(
                bm.split(".")[0] in ("cmp", "cmpn", "tst", "btst", "move")
                for _, bm, _ in insns[j:i]):
            out.append(f"spin-wait loop at ${a:X}")
    if insns:
        lm = insns[-1][1]
        if not (lm in ("rts", "rte") or lm.startswith(("jmp", "bra"))):
            out.append(f"last insn '{lm}' not rts — falls past end marker")
    return out

def far_branches(f):
    return [(t, labels[t]) for t in f["branches"]
            if t in labels and not (f["start"] <= labels[t] < f["end"])]

def own_blockers(name):
    f = funcs[name]
    return [b for b, on in (("hw", f["hw"]),
                            ("rte", f["rte"]), ("jmp-out", f["jmp"]),
                            ("computed-call", f["computed"])) if on]

def main():
    DONE, SKIP = queue_done_skip()
    parse_listing()
    global funcs
    funcs = {k: v for k, v in funcs.items() if v["end"]}
    addr2name = {v["start"]: k for k, v in funcs.items()}

    prof = {}
    for ln in open(PROFILE):
        a, n = ln.split()
        prof[int(a, 16)] = int(n)

    def closure(name, seen=None):
        if seen is None:
            seen = set()
        if name in seen:
            return set(), [], []
        seen.add(name)
        f = funcs[name]
        batch, blocked, notes = set(), [], []
        targets = set(f["calls"])
        for t, ta in far_branches(f):
            if ta == SHARED_RTS:
                notes.append(f"{name}: far shared rts $15464")
            elif t.startswith("locret_"):
                notes.append(f"{name}: far branch to bare rts {t}")
            elif ta in addr2name:
                targets.add(addr2name[ta])
            else:
                blocked.append(f"{name} far-branches into mid-routine {t} (${ta:X})")
        for t in targets:
            ta = labels.get(t)
            if ta is None:
                blocked.append(f"{name} calls unknown target {t}")
            elif ta in DONE:
                pass
            elif ta in SKIP:
                blocked.append(f"{name} calls skip-listed {t}")
            elif ta not in addr2name:
                blocked.append(f"{name} calls {t} (no closed boundary)")
            elif own_blockers(addr2name[ta]):
                blocked.append(f"{name} calls {addr2name[ta]} which has "
                               f"{'/'.join(own_blockers(addr2name[ta]))}")
            else:
                cn = addr2name[ta]
                batch.add(cn)
                b2, bl2, n2 = closure(cn, seen)
                batch |= b2
                blocked += bl2
                notes += n2
        return batch, blocked, notes

    rows = []
    for name, f in funcs.items():
        a = f["start"]
        if a in DONE or a in SKIP:
            continue
        entries = prof.get(a, 0)
        weight = sum(prof.get(ad, 0) for ad, _, _ in f["insns"])
        if weight == 0 and not COLD:
            continue
        ob = own_blockers(name)
        batch, blocked, notes = closure(name)
        batch.discard(name)
        haz = hazards(f)
        if f["muldiv"]:
            haz.append("uses mul/div (lift_charge_mul*/div* + alu_mul*/div*)")
        for bn in sorted(batch):
            haz += [f"batchmate {bn}: {h}" for h in hazards(funcs[bn])]
            if funcs[bn]["muldiv"]:
                haz.append(f"batchmate {bn}: uses mul/div")
        status = "BLOCKED" if (ob or blocked) else ("BATCH" if batch else "PURE")
        rows.append((entries, weight, name, a, len(f["insns"]), status, ob,
                     sorted(batch), blocked[:4], sorted(set(notes)) + haz,
                     sorted(f["calls"])))

    rows.sort(key=lambda r: (-r[0], -r[1]))
    print(f"profiled, not done/skip: {len(rows)}")
    for st in ("PURE", "BATCH", "BLOCKED"):
        print(f"  {st}: {sum(1 for r in rows if r[5] == st)}")
    print()
    for entries, weight, name, a, size, status, ob, batch, blocked, notes, calls in rows:
        wpe = weight // entries if entries else 0
        extra = ""
        if ob:
            extra += " own:" + ",".join(ob)
        if calls:
            extra += " directcalls:" + ",".join(calls)
        if batch:
            extra += " batch:" + ",".join(batch)
        if blocked:
            extra += " blocked:" + "; ".join(blocked)
        if notes:
            extra += " notes:" + "; ".join(notes)
        ecol = "   COLD" if (entries == 0 and weight == 0) else f"{entries:>7}"
        print(f"{ecol} {weight:>9} w/e={wpe:<6} {status:<7} "
              f"{name:<16} ${a:X} {size:>4}i{extra}")

if __name__ == "__main__":
    main()
