#!/usr/bin/env python3
"""Stage D pass 2 — emit per-instruction C semantic functions from the
assembler listing (native/recomp/Listings.asm). Each function predicts the
full CPU state after its instruction; the harness validator checks every
prediction against the interpreter during real runs.

Outputs native/harness/gen_insns_{0..3}.c + gen_insns.h
"""
import re, os, sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
LISTING = os.path.join(HERE, "Listings.asm")
OUTDIR = os.path.normpath(os.path.join(HERE, "..", "harness"))
NCHUNK = 4

# ---------------------------------------------------------------- listing parse

sym = {}        # label -> address ; also equates (var_3E etc.)
insns = []      # dicts: addr, len, mnem, ops(list of str), text

def parse_listing(path=None, offset=0):
    equ_re = re.compile(r"^([A-Za-z_.@][\w.]*)\s*=\s*(.+?)\s*$")
    lab_re = re.compile(r"^([A-Za-z_.@][\w.]*):\s*(.*)$")
    prev = None
    for raw in open(path or LISTING, encoding="latin-1"):
        line = raw.rstrip("\r\n")
        if len(line) < 9 or not re.match(r"^[0-9A-F]{8}", line):
            continue
        addr = int(line[:8], 16) + offset
        bytecol = line[9:36] if len(line) > 36 else line[9:]
        src = line[36:] if len(line) > 36 else ""
        nbytes = len(re.sub(r"[^0-9A-F]", "", bytecol)) // 2
        body = src
        # strip comment (quote-aware)
        q = None; cut = len(body)
        for i, ch in enumerate(body):
            if q:
                if ch == q: q = None
            elif ch in "'\"": q = ch
            elif ch == ";": cut = i; break
        body = body[:cut].rstrip()
        s = body.strip()
        if not s: continue
        m = equ_re.match(s)
        if m and "=" in s:
            try: sym[m.group(1)] = evalexpr(m.group(2))
            except Exception: pass
            continue
        label = None
        m = lab_re.match(s)
        if m and not s.startswith(("dc.", "dcb.")):
            label, s = m.group(1), m.group(2).strip()
        if label and s.startswith("="):
            try: sym[label] = evalexpr(s[1:])
            except Exception: pass
            continue
        if label and label not in sym: sym[label] = addr
        if not s: continue
        parts = s.split(None, 1)
        mnem = parts[0].lower()
        base = mnem.split(".")[0]
        if base in ("dc", "dcb", "include", "align", "even", "end", "incbin",
                    "macro", "endm", "ds", "org"):
            continue
        if not re.match(r"^[a-z]+(\.[bwsl])?$", mnem):
            continue
        ops = split_ops(parts[1]) if len(parts) > 1 else []
        insns.append({"addr": addr, "len": nbytes, "mnem": mnem, "ops": ops, "text": s})

def split_ops(s):
    out, cur, depth, q = [], "", 0, None
    for ch in s:
        if q:
            cur += ch
            if ch == q: q = None
        elif ch in "'\"": cur += ch; q = ch
        elif ch == "(": depth += 1; cur += ch
        elif ch == ")": depth -= 1; cur += ch
        elif ch == "," and depth == 0: out.append(cur.strip()); cur = ""
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

# ---------------------------------------------------------------- expressions

def evalexpr(t):
    t = t.strip()
    # char literals
    def chrep(m):
        v = 0
        for c in m.group(1): v = (v << 8) | ord(c)
        return str(v)
    t = re.sub(r"'([^']{1,4})'", chrep, t)
    t = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), t)
    t = re.sub(r"%([01]+)", lambda m: str(int(m.group(1), 2)), t)
    # symbols
    def symrep(m):
        name = m.group(0)
        if name in sym: return f"({sym[name]})"
        mm = re.match(r"^var_([0-9A-F]+)$", name)
        if mm: return f"(-{int(mm.group(1),16)})"
        raise KeyError(name)
    t = re.sub(r"[A-Za-z_.@][\w.]*", symrep, t)
    v = eval(t, {"__builtins__": {}}, {})
    return int(v)

# ---------------------------------------------------------------- EA codegen

SZ = {"b": 1, "w": 2, "l": 4}

class EA:
    """kind: dreg areg imm ind postinc predec disp idx abs
       Produces C fragments; side effects handled by caller via pre/post."""
    def __init__(self, kind, **kw):
        self.kind = kind; self.__dict__.update(kw)

def parse_ea(op, insn_addr):
    o = re.sub(r"\bsp\b", "a7", op.strip(), flags=re.I)
    if "*" in o:
        o = re.sub(r"(?<![\w)])\*", str(insn_addr), o)
    ol = o.lower()
    m = re.match(r"^d([0-7])$", ol)
    if m: return EA("dreg", n=int(m.group(1)))
    m = re.match(r"^(a([0-7])|sp)$", ol)
    if m: return EA("areg", n=7 if m.group(1) == "sp" else int(m.group(2)))
    if ol.startswith("#"):
        return EA("imm", val=evalexpr(o[1:]) & 0xFFFFFFFF)
    m = re.match(r"^\(a([0-7])\)\+$", ol)
    if m: return EA("postinc", n=int(m.group(1)))
    m = re.match(r"^-\(a([0-7])\)$", ol)
    if m: return EA("predec", n=int(m.group(1)))
    m = re.match(r"^\((a([0-7])|sp)\)$", ol)
    if m: return EA("ind", n=7 if m.group(1) == "sp" else int(m.group(2)))
    m = re.match(r"^(.*)\(a([0-7]),\s*([da])([0-7])(\.[wl])?\)$", o, re.I)
    if m:
        disp = evalexpr(m.group(1)) if m.group(1).strip() else 0
        return EA("idx", n=int(m.group(2)), xa=(m.group(3).lower() == "a"),
                  xn=int(m.group(4)), xw=(m.group(5) or ".w").lower() == ".w",
                  disp=disp & 0xFFFFFFFF)
    m = re.match(r"^(.*)\((a([0-7])|sp)\)$", o, re.I)
    if m:
        n = 7 if m.group(2).lower() == "sp" else int(m.group(3))
        return EA("disp", n=n, disp=evalexpr(m.group(1)) & 0xFFFFFFFF)
    m = re.match(r"^(.*)\(pc,\s*([da])([0-7])(\.[wl])?\)$", o, re.I)
    if m:
        base = evalexpr(m.group(1)) if m.group(1).strip() else 0
        return EA("idx_pc", base=base & 0xFFFFFFFF, xa=(m.group(2).lower() == "a"),
                  xn=int(m.group(3)), xw=(m.group(4) or ".w").lower() == ".w")
    m = re.match(r"^(.*)\(pc\)$", o, re.I)
    if m: return EA("abs", val=evalexpr(m.group(1)) & 0xFFFFFFFF)
    m = re.match(r"^\((.*)\)\.([wl])$", o, re.I)
    if m:
        v = evalexpr(m.group(1)) & 0xFFFFFFFF
        if m.group(2).lower() == "w":
            v &= 0xFFFF
            if v & 0x8000: v |= 0xFFFF0000
        return EA("abs", val=v)
    # bare symbol / number = absolute
    return EA("abs", val=evalexpr(o) & 0xFFFFFFFF)

def ea_addr(ea, sz, pre):
    """Return C expr for effective address; append side-effect stmts to pre."""
    if ea.kind == "ind": return f"c->a[{ea.n}]"
    if ea.kind == "disp": return f"(c->a[{ea.n}]+{c32(ea.disp)})"
    if ea.kind == "idx":
        idx = f"c->{'a' if ea.xa else 'd'}[{ea.xn}]"
        idx = f"SEXT16({idx})" if ea.xw else f"(int32_t){idx}"
        return f"(c->a[{ea.n}]+{c32(ea.disp)}+(uint32_t)({idx}))"
    if ea.kind == "idx_pc":
        idx = f"c->{'a' if ea.xa else 'd'}[{ea.xn}]"
        idx = f"SEXT16({idx})" if ea.xw else f"(int32_t){idx}"
        return f"({ea.base}u+(uint32_t)({idx}))"
    if ea.kind == "abs": return f"{ea.val}u"
    if ea.kind == "postinc":
        step = 2 if (sz == 1 and ea.n == 7) else sz
        global _eactr
        _eactr += 1
        pre.append(f"uint32_t ea{_eactr} = c->a[{ea.n}]; c->a[{ea.n}] += {step};")
        return f"ea{_eactr}"
    if ea.kind == "predec":
        step = 2 if (sz == 1 and ea.n == 7) else sz
        pre.append(f"c->a[{ea.n}] -= {step};")
        return f"c->a[{ea.n}]"
    raise ValueError(ea.kind)

_eactr = 0

def c32(v):
    v &= 0xFFFFFFFF
    return f"{v}u" if v < 0x80000000 else f"0x{v:X}u"

def ea_read(ea, sz, pre):
    """Read a source operand, materializing it into a temp appended to `pre`.

    The temp is not cosmetic: on the 68000 the source operand is fetched
    BEFORE the destination's effective address is calculated, so a source
    that names the same address register the destination post-increments
    or pre-decrements must observe the register's pre-update value. Callers
    splice `pre` into the body and then use the returned expression, so
    returning a bare expression here would let the destination's side effect
    (appended to `pre` later, by ea_write/ea_rmw) execute first and be seen
    by the source read. `move.l -$20(a0),(a0)+` at $17410 is the live case.
    Immediates need no temp — they cannot be invalidated."""
    global _eactr
    if ea.kind == "imm":
        return c32(ea.val & ((1 << (sz * 8)) - 1) if sz < 4 else ea.val)
    if ea.kind == "dreg": expr = f"GETR(c->d[{ea.n}],{sz})"
    elif ea.kind == "areg": expr = f"GETR(c->a[{ea.n}],{sz})"
    else: expr = f"rc_read{sz*8}(c,{ea_addr(ea, sz, pre)})"
    _eactr += 1
    pre.append(f"uint32_t sv{_eactr} = {expr};")
    return f"sv{_eactr}"

def ea_write(ea, sz, val, pre):
    if ea.kind == "dreg": return f"SETR(c->d[{ea.n}],{sz},{val});"
    if ea.kind == "areg": return f"c->a[{ea.n}] = {val};"
    a = ea_addr(ea, sz, pre)
    return f"rc_write{sz*8}(c,{a},{val});"

def ea_rmw(ea, sz, pre):
    """Resolve destination once; return (read_expr, write_fn(val)->stmt)."""
    global _eactr
    if ea.kind == "dreg":
        return (f"GETR(c->d[{ea.n}],{sz})",
                lambda v: f"SETR(c->d[{ea.n}],{sz},{v});")
    if ea.kind == "areg":
        return (f"c->a[{ea.n}]", lambda v: f"c->a[{ea.n}] = {v};")
    a = ea_addr(ea, sz, pre)
    _eactr += 1
    nm = f"ad{_eactr}"
    pre.append(f"uint32_t {nm} = {a};")
    return (f"rc_read{sz*8}(c,{nm})",
            lambda v, nm=nm: f"rc_write{sz*8}(c,{nm},{v});")

# ---------------------------------------------------------------- instruction emit

CC = {"hi": "(!c->cf && !c->zf)", "ls": "(c->cf || c->zf)", "cc": "(!c->cf)",
      "cs": "(c->cf)", "ne": "(!c->zf)", "eq": "(c->zf)", "vc": "(!c->vf)",
      "vs": "(c->vf)", "pl": "(!c->nf)", "mi": "(c->nf)",
      "ge": "(c->nf == c->vf)", "lt": "(c->nf != c->vf)",
      "gt": "(!c->zf && c->nf == c->vf)", "le": "(c->zf || c->nf != c->vf)",
      "t": "1", "f": "0", "ra": "1", "sr": None}

stats = Counter()

def emit_insn(ins):
    """Return C body lines or None if unsupported."""
    mnem, ops, addr, ln = ins["mnem"], ins["ops"], ins["addr"], ins["len"]
    parts = mnem.split(".")
    base, suf = parts[0], (parts[1] if len(parts) > 1 else "")
    nxt = addr + ln
    L = [f"c->pc = {c32(nxt)};"]
    pre = []

    def sz_of(default="w"):
        return SZ.get(suf if suf in SZ else default)

    def flags_logic(res, sz):
        return [f"SETNZ(c,{res},{sz}); c->vf = 0; c->cf = 0;"]

    try:
        if base in ("move", "movea"):
            sz = sz_of()
            o0, o1 = ops[0].lower(), ops[1].lower()
            if o1 in ("sr", "ccr"):
                src = parse_ea(ops[0], addr)
                r = ea_read(src, sz, pre)
                L += pre + [f"uint32_t t = {r}; c->xf=(t>>4)&1; c->nf=(t>>3)&1; "
                            f"c->zf=(t>>2)&1; c->vf=(t>>1)&1; c->cf=t&1;"]
                if o1 == "sr": L.append("c->sr_high = t & 0xFFE0;")
                return L
            if o0 == "sr":
                dst = parse_ea(ops[1], addr)
                _, wr = ea_rmw(dst, 2, pre)
                L += pre + ["uint32_t t = c->sr_high | (c->xf<<4)|(c->nf<<3)|"
                            "(c->zf<<2)|(c->vf<<1)|c->cf;", wr("t")]
                return L
            if o1 == "usp":
                return L        # writes USP only; no tracked state changes
            if o0 == "usp":
                n = int(ops[1][1])
                L += [f"c->a[{n}] = c->usp;"]
                return L
            src, dst = parse_ea(ops[0], addr), parse_ea(ops[1], addr)
            r = ea_read(src, sz, pre)
            if dst.kind == "areg":
                v = f"(uint32_t)SEXT{sz*8}({r})" if sz == 2 else r
                L += pre + [f"c->a[{dst.n}] = {v};"]
            else:
                L += pre + [f"uint32_t v = {r};", ea_write(dst, sz, "v", pre)] \
                     + flags_logic("v", sz)
        elif base == "moveq":
            v = evalexpr(ops[0][1:]) & 0xFF
            v32 = v | 0xFFFFFF00 if v & 0x80 else v
            n = int(ops[1][1])
            L += [f"c->d[{n}] = {c32(v32)};"] + flags_logic(f"c->d[{n}]", 4)
        elif base == "lea":
            src = parse_ea(ops[0], addr)
            a = ea_addr(src, 4, pre) if src.kind != "abs" else c32(src.val)
            L += pre + [f"c->a[{int(ops[1][1])}] = {a};"]
        elif base == "pea":
            src = parse_ea(ops[0], addr)
            a = ea_addr(src, 4, pre) if src.kind != "abs" else c32(src.val)
            L += pre + [f"c->a[7] -= 4; rc_write32(c, c->a[7], {a});"]
        elif base == "clr":
            sz = sz_of()
            dst = parse_ea(ops[0], addr)
            _, wr = ea_rmw(dst, sz, pre)
            L += pre + [wr("0"), "c->nf = 0; c->zf = 1; c->vf = 0; c->cf = 0;"]
        elif base == "tst":
            sz = sz_of()
            r = ea_read(parse_ea(ops[0], addr), sz, pre)
            L += pre + [f"uint32_t v = {r};"] + flags_logic("v", sz)
        elif base in ("add", "addi", "addq", "sub", "subi", "subq", "cmp", "cmpi",
                      "adda", "suba", "cmpa", "cmpn"):
            sz = sz_of("w" if base in ("adda", "suba", "cmpa") else "w")
            if base == "cmpn": sz = SZ[suf]
            src, dst = parse_ea(ops[0], addr), parse_ea(ops[1], addr)
            adding = base.startswith("add")
            if dst.kind == "areg" and base in ("adda", "suba", "cmpa", "add", "sub",
                                              "cmp", "addq", "subq"):
                s = ea_read(src, sz, pre)
                sv = f"(uint32_t)SEXT{sz*8}({s})" if sz == 2 else s
                if base in ("cmp", "cmpa"):
                    L += pre + [f"uint32_t s = {sv}, d = c->a[{dst.n}], r = d - s;",
                                "SETNZ(c,r,4); c->cf = (s > d); "
                                "c->vf = (((d^s)&(d^r))>>31)&1;"]
                else:
                    op = "+" if adding else "-"
                    L += pre + [f"c->a[{dst.n}] {op}= {sv};"]
            else:
                s = ea_read(src, sz, pre)
                is_cmp = base in ("cmp", "cmpi", "cmpn")
                if is_cmp:
                    d = ea_read(dst, sz, pre); wr = None
                else:
                    d, wr = ea_rmw(dst, sz, pre)
                msb = sz * 8 - 1
                if adding:
                    L += pre + [f"uint32_t s = {s}, d = {d};",
                                f"uint64_t f = (uint64_t)s + d;",
                                f"uint32_t r = MASK((uint32_t)f,{sz});",
                                f"c->cf = (f >> {sz*8}) & 1; c->xf = c->cf;",
                                f"c->vf = ((~(s^d) & (s^r)) >> {msb}) & 1;",
                                f"SETNZ(c,r,{sz});", wr("r")]
                else:
                    x = "" if is_cmp else "c->xf = c->cf;"
                    L += pre + [f"uint32_t s = {s}, d = {d};",
                                f"uint32_t r = MASK(d - s,{sz});",
                                f"c->cf = (s > d); {x}",
                                f"c->vf = (((d^s) & (d^r)) >> {msb}) & 1;",
                                f"SETNZ(c,r,{sz});"]
                    if wr: L.append(wr("r"))
        elif base in ("and", "andi", "or", "ori", "eor", "eori"):
            if ops[1].lower() == "sr": return None
            if ops[1].lower() == "ccr":
                v = evalexpr(ops[0][1:]) & 0x1F
                op = {"and": "&=", "or": "|=", "eor": "^="}[base.rstrip("i")]
                for i, f in enumerate(["cf", "vf", "zf", "nf", "xf"]):
                    b = (v >> i) & 1
                    if base.startswith("and") and b: continue
                    if base.startswith("or") and not b: continue
                    if base.startswith("eor") and not b: continue
                    L.append(f"c->{f} {op} {b};" if not base.startswith("and")
                             else f"c->{f} = 0;")
                return L
            sz = sz_of()
            op = {"and": "&", "or": "|", "eor": "^"}[base.rstrip("i")]
            src, dst = parse_ea(ops[0], addr), parse_ea(ops[1], addr)
            s = ea_read(src, sz, pre)
            d, wr = ea_rmw(dst, sz, pre)
            L += pre + [f"uint32_t r = MASK(({d}) {op} ({s}),{sz});",
                        wr("r")] + flags_logic("r", sz)
        elif base == "not":
            sz = sz_of()
            dst = parse_ea(ops[0], addr)
            d, wr = ea_rmw(dst, sz, pre)
            L += pre + [f"uint32_t r = MASK(~({d}),{sz});",
                        wr("r")] + flags_logic("r", sz)
        elif base == "neg":
            sz = sz_of()
            dst = parse_ea(ops[0], addr)
            d, wr = ea_rmw(dst, sz, pre)
            msb = sz * 8 - 1
            L += pre + [f"uint32_t d = {d}; uint32_t r = MASK(0u - d,{sz});",
                        f"c->cf = (r != 0); c->xf = c->cf;",
                        f"c->vf = ((d & r) >> {msb}) & 1;",
                        f"SETNZ(c,r,{sz});", wr("r")]
        elif base in ("btst", "bset", "bclr", "bchg"):
            dst = parse_ea(ops[1], addr)
            src = parse_ea(ops[0], addr)
            bit = ea_read(src, 1, pre) if src.kind != "imm" else c32(src.val)
            if dst.kind == "dreg":
                L += pre + [f"uint32_t b = ({bit}) & 31;",
                            f"c->zf = !((c->d[{dst.n}] >> b) & 1);"]
                if base == "bset": L.append(f"c->d[{dst.n}] |= (1u << b);")
                if base == "bclr": L.append(f"c->d[{dst.n}] &= ~(1u << b);")
                if base == "bchg": L.append(f"c->d[{dst.n}] ^= (1u << b);")
            else:
                d, wr = ea_rmw(dst, 1, pre)
                L += pre + [f"uint32_t b = ({bit}) & 7; uint32_t v = {d};",
                            f"c->zf = !((v >> b) & 1);"]
                if base == "bset": L += [f"v |= (1u << b);", wr("v")]
                if base == "bclr": L += [f"v &= ~(1u << b);", wr("v")]
                if base == "bchg": L += [f"v ^= (1u << b);", wr("v")]
        elif base in ("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr"):
            sz = sz_of()
            if len(ops) == 1:
                # memory form: shift word at EA by 1
                dst = parse_ea(ops[0], addr)
                d, wr = ea_rmw(dst, 2, pre)
                L += pre + [f"uint32_t v = {d}; uint32_t last;"]
                if base in ("asl", "lsl"):
                    L += ["last = (v >> 15) & 1;",
                          ("uint32_t m0 = last;" if base == "asl" else ""),
                          "v = MASK(v << 1,2);"]
                elif base == "lsr":
                    L += ["last = v & 1; v >>= 1;"]
                elif base == "asr":
                    L += ["last = v & 1; v = (v >> 1) | (v & 0x8000u);"]
                elif base == "rol":
                    L += ["last = (v >> 15) & 1; v = MASK((v << 1) | last,2);"]
                elif base == "ror":
                    L += ["last = v & 1; v = (v >> 1) | (last << 15);"]
                else:
                    return None
                L += [wr("v"), "SETNZ(c,v,2); c->cf = last;"]
                if base not in ("rol", "ror"): L.append("c->xf = last;")
                L.append("c->vf = " +
                         ("(m0 != ((v >> 15) & 1));" if base == "asl" else "0;"))
                return L
            cnt_ea, dst = parse_ea(ops[0], addr), parse_ea(ops[1], addr)
            if dst.kind != "dreg": return None
            if base in ("roxl", "roxr"):
                cnt = c32(cnt_ea.val & 63) if cnt_ea.kind == "imm" else f"(c->d[{cnt_ea.n}] & 63)"
                bits = sz * 8
                L += [f"uint32_t n = {cnt} % {bits + 1};",
                      f"uint32_t v = GETR(c->d[{dst.n}],{sz});",
                      "uint32_t x = c->xf, last;"]
                if base == "roxl":
                    L += [f"for (uint32_t i = 0; i < n; i++) "
                          f"{{ last = (v >> {bits-1}) & 1; v = MASK((v << 1) | x,{sz}); x = last; }}"]
                else:
                    L += [f"for (uint32_t i = 0; i < n; i++) "
                          f"{{ last = v & 1; v = (v >> 1) | (x << {bits-1}); x = last; }}"]
                L += [f"SETR(c->d[{dst.n}],{sz},v); SETNZ(c,v,{sz});",
                      "c->xf = x; c->cf = x; c->vf = 0;"]
                return L
            cnt = c32(cnt_ea.val & 63) if cnt_ea.kind == "imm" else f"(c->d[{cnt_ea.n}] & 63)"
            bits = sz * 8
            left = base in ("asl", "lsl", "rol")
            L += [f"uint32_t n = {cnt}; uint32_t v = GETR(c->d[{dst.n}],{sz});",
                  "uint32_t last = 0, vset = 0, msb0 = " + f"(v >> {bits-1}) & 1;"]
            body = []
            if base in ("asl", "lsl"):
                body = [f"last = (v >> {bits-1}) & 1; v = MASK(v << 1,{sz});",
                        f"if (((v >> {bits-1}) & 1) != msb0) vset = 1;"]
            elif base == "lsr":
                body = ["last = v & 1; v >>= 1;"]
            elif base == "asr":
                body = [f"last = v & 1; v = (v >> 1) | (v & {1 << (bits-1)}u);"]
            elif base == "rol":
                body = [f"last = (v >> {bits-1}) & 1; v = MASK((v << 1) | last,{sz});"]
            elif base == "ror":
                body = [f"last = v & 1; v = (v >> 1) | (last << {bits-1});"]
            L += [f"for (uint32_t i = 0; i < n; i++) {{ {' '.join(body)} }}",
                  f"SETR(c->d[{dst.n}],{sz},v); SETNZ(c,v,{sz});",
                  "if (n) { c->cf = last; " +
                  ("c->xf = last; " if base not in ("rol", "ror") else "") + "} else c->cf = 0;",
                  ("c->vf = vset;" if base == "asl" else "c->vf = 0;")]
        elif base == "swap":
            n = int(ops[0][1])
            L += [f"c->d[{n}] = (c->d[{n}] >> 16) | (c->d[{n}] << 16);"] + \
                 flags_logic(f"c->d[{n}]", 4)
        elif base == "ext":
            n = int(ops[0][1])
            if suf == "w":
                L += [f"SETR(c->d[{n}],2,(uint32_t)SEXT8(c->d[{n}]) & 0xFFFF);",
                      f"SETNZ(c,c->d[{n}],2); c->vf = 0; c->cf = 0;"]
            else:
                L += [f"c->d[{n}] = (uint32_t)SEXT16(c->d[{n}]);"] + \
                     flags_logic(f"c->d[{n}]", 4)
        elif base == "exg":
            def reg(o):
                return f"c->{'d' if o[0].lower()=='d' else 'a'}[{int(o[1])}]"
            a, b = reg(ops[0]), reg(ops[1])
            L += [f"uint32_t t = {a}; {a} = {b}; {b} = t;"]
        elif base in ("mulu", "muls"):
            src = parse_ea(ops[0], addr); n = int(ops[1][1])
            s = ea_read(src, 2, pre)
            if base == "mulu":
                L += pre + [f"c->d[{n}] = (uint32_t)(({s}) * (c->d[{n}] & 0xFFFF));"]
            else:
                L += pre + [f"c->d[{n}] = (uint32_t)((int32_t)SEXT16({s}) * (int32_t)SEXT16(c->d[{n}]));"]
            L += flags_logic(f"c->d[{n}]", 4)
        elif base in ("divu", "divs"):
            src = parse_ea(ops[0], addr); n = int(ops[1][1])
            s = ea_read(src, 2, pre)
            L += pre + [f"uint32_t s = {s};",
                        "if (s == 0) { c->unpred = 1; return; }"]
            if base == "divu":
                L += [f"uint32_t q = c->d[{n}] / s, r = c->d[{n}] %% s;".replace("%%", "%"),
                      "if (q > 0xFFFF) { c->unpred = 1; return; }",
                      f"c->d[{n}] = (r << 16) | q; SETNZ(c,q,2); c->vf = 0; c->cf = 0;"]
            else:
                L += [f"int32_t dd = (int32_t)c->d[{n}], ss = (int32_t)SEXT16(s);",
                      "int32_t q = dd / ss, r = dd % ss;",
                      "if (q > 32767 || q < -32768) { c->unpred = 1; return; }",
                      f"c->d[{n}] = (((uint32_t)r & 0xFFFF) << 16) | ((uint32_t)q & 0xFFFF);",
                      "SETNZ(c,(uint32_t)q & 0xFFFF,2); c->vf = 0; c->cf = 0;"]
        elif base in ("bra", "jmp"):
            ops = [re.sub(r"(?<![\w)])\*", str(addr), ops[0])] + ops[1:]
            tgt = parse_ea(ops[0], addr)
            if tgt.kind == "abs":
                L = [f"c->pc = {c32(tgt.val)};"]
            else:
                a = ea_addr(tgt, 4, pre)
                L = pre + [f"c->pc = {a};"]
        elif base == "bsr":
            t = evalexpr(ops[0]) & 0xFFFFFF
            L = [f"c->a[7] -= 4; rc_write32(c, c->a[7], {c32(nxt)}); c->pc = {c32(t)};"]
        elif base == "jsr":
            tgt = parse_ea(ops[0], addr)
            a = c32(tgt.val) if tgt.kind == "abs" else ea_addr(tgt, 4, pre)
            L = pre + [f"c->a[7] -= 4; rc_write32(c, c->a[7], {c32(nxt)}); c->pc = {a};"]
        elif base == "rts":
            L = ["c->pc = rc_read32(c, c->a[7]) & 0xFFFFFF; c->a[7] += 4;"]
        elif base in ("beq", "bne", "bgt", "blt", "bge", "ble", "bpl", "bmi",
                      "bhi", "bls", "bcc", "bcs", "bvc", "bvs"):
            top = re.sub(r"(?<![\w)])\*", str(addr), ops[0])
            t = evalexpr(top) & 0xFFFFFF
            L = [f"c->pc = {CC[base[1:]]} ? {c32(t)} : {c32(nxt)};"]
        elif base.startswith("db"):
            cc = base[2:]
            cond = CC.get("f" if cc == "f" else cc)
            if cond is None: return None
            t = evalexpr(ops[1]) & 0xFFFFFF
            n = int(ops[0][1])
            L = [f"if ({cond}) {{ c->pc = {c32(nxt)}; }} else {{",
                 f"  uint32_t v = (GETR(c->d[{n}],2) - 1) & 0xFFFF; SETR(c->d[{n}],2,v);",
                 f"  c->pc = (v != 0xFFFF) ? {c32(t)} : {c32(nxt)}; }}"]
        elif base in ("st", "sf") or (base.startswith("s") and base[1:] in CC):
            cc = "t" if base == "st" else ("f" if base == "sf" else base[1:])
            dst = parse_ea(ops[0], addr)
            _, wr = ea_rmw(dst, 1, pre)
            L += pre + [f"uint32_t v = {CC[cc]} ? 0xFF : 0;", wr("v")]
        elif base == "movem":
            sz = SZ[suf]
            def reglist(t):
                regs = []
                t = re.sub(r"\bsp\b", "a7", t, flags=re.I)
                def ridx(k, n): return n + (8 if k == "a" else 0)
                for part in t.split("/"):
                    m = re.match(r"^([da])([0-7])(?:-([da])([0-7]))?$", part.strip().lower())
                    if not m: raise ValueError(part)
                    lo = ridx(m.group(1), int(m.group(2)))
                    hi = ridx(m.group(3), int(m.group(4))) if m.group(3) else lo
                    for i in range(lo, hi + 1):
                        regs.append(("a" if i >= 8 else "d", i & 7))
                return regs
            if re.match(r"^[da][0-7]", ops[0].lower()) and "(" not in ops[0]:
                regs = reglist(ops[0]); dst = parse_ea(ops[1], addr)   # regs -> mem
                if dst.kind == "predec":
                    order = [(t, i) for (t, i) in regs]
                    order = sorted(order, key=lambda r: (r[0] == "d", -r[1]))  # a7..a0,d7..d0
                    stmts = []
                    for t, i in order:
                        stmts.append(f"c->a[{dst.n}] -= {sz}; "
                                     f"rc_write{sz*8}(c, c->a[{dst.n}], GETR(c->{t}[{i}],{sz}));")
                    L += stmts
                else:
                    a = ea_addr(dst, sz, pre)
                    L += pre + [f"uint32_t p = {a};"]
                    for t, i in regs:
                        L.append(f"rc_write{sz*8}(c, p, GETR(c->{t}[{i}],{sz})); p += {sz};")
            else:
                src = parse_ea(ops[0], addr); regs = reglist(ops[1])   # mem -> regs
                if src.kind == "postinc":
                    L += [f"uint32_t p = c->a[{src.n}];"]
                    for t, i in regs:
                        v = f"rc_read{sz*8}(c, p)"
                        if sz == 2: v = f"(uint32_t)SEXT16({v})"
                        L.append(f"c->{t}[{i}] = {v}; p += {sz};")
                    L.append(f"c->a[{src.n}] = p;")
                else:
                    a = ea_addr(src, sz, pre)
                    L += pre + [f"uint32_t p = {a};"]
                    for t, i in regs:
                        v = f"rc_read{sz*8}(c, p)"
                        if sz == 2: v = f"(uint32_t)SEXT16({v})"
                        L.append(f"c->{t}[{i}] = {v}; p += {sz};")
        elif base == "rte":
            L = ["uint32_t sr = rc_read16(c, c->a[7]);",
                 "c->pc = rc_read32(c, c->a[7] + 2) & 0xFFFFFF; c->a[7] += 6;",
                 "c->xf=(sr>>4)&1; c->nf=(sr>>3)&1; c->zf=(sr>>2)&1; "
                 "c->vf=(sr>>1)&1; c->cf=sr&1; c->sr_high = sr & 0xFFE0;"]
        elif base == "nop":
            pass
        else:
            return None
    except Exception as e:
        stats[f"err_{type(e).__name__}"] += 1
        return None
    return L

# ---------------------------------------------------------------- driver

# instructions the disassembly renders as data but the CPU executes
MANUAL = [
    {"addr": 0x10EEE, "len": 2, "mnem": "exg", "ops": ["d1", "d0"],
     "text": "exg d1,d0 (hand-encoded dc.w $C340)"},
    {"addr": 0xF84F6, "len": 2, "mnem": "rts", "ops": [],
     "text": "rts (after embedded data table)"},
]

def main():
    parse_listing()
    # checksum sub-assembly: assembled at org 0, patched in at CalcChecksum
    chk = os.path.join(HERE, "ChecksumListings.asm")
    if os.path.exists(chk) and "CalcChecksum" in sym:
        base = sym["CalcChecksum"]
        n0 = len(insns)
        parse_listing(chk, offset=base)
        # sub-assembly re-lists equates at offset base — those parsed as addr!=0
        # instructions are real; equ lines were filtered by regex already
        print(f"checksum routine: +{len(insns)-n0} instructions at {base:X}")
    insns.extend(MANUAL)
    print(f"parsed: {len(insns)} instructions, {len(sym)} symbols")

    # drop pseudo-instructions at address 0 (macro definitions, equates region)
    insns[:] = [i for i in insns if i["addr"] != 0]

    # rewrite cmpn macros to cmp with immediate (operand text already "$E0,d0")
    for ins in insns:
        if ins["mnem"].startswith("cmpn"):
            szc = ins["mnem"][4]
            ins["mnem"] = f"cmpn.{szc}"
            ins["ops"] = ["#" + ins["ops"][0]] + ins["ops"][1:]

    chunks = [[] for _ in range(NCHUNK)]
    table = []
    done = Counter(); skipped = Counter()
    for k, ins in enumerate(insns):
        body = emit_insn(ins)
        name = ins["mnem"]
        if body is None:
            table.append((ins, None))
            skipped[name] += 1
            continue
        fname = f"I_{ins['addr']:06X}"
        fn = [f"static void {fname}(rcpu_t *c) {{"] + [f"  {l}" for l in body] + ["}"]
        chunks[k % NCHUNK].append("\n".join(fn))
        table.append((ins, fname))
        done[name] += 1

    hdr = ["/* generated by emit.py */", "#ifndef _GEN_INSNS_H_",
           "#define _GEN_INSNS_H_", '#include "rcpu.h"',
           "/* kind: 0 excluded from native run, 1 fixed cycles (base+extra),",
           " * 2 bcc.s, 3 bcc.w, 4 dbcc, 5 scc-on-dreg,",
           " * 6 movem.l (extras-first + skip-refresh), 7 bit-op Dn,Dm (+14 if bit>=16) */",
           "typedef struct { unsigned int addr; void (*fn)(rcpu_t*); const char *m;",
           "                 unsigned char kind, len; short extra; } rc_entry;",
           "extern const rc_entry rc_table[];", "extern const int rc_table_n;", "#endif"]
    open(os.path.join(OUTDIR, "gen_insns.h"), "w").write("\n".join(hdr) + "\n")

    for i, ch in enumerate(chunks):
        src = ['#include "gen_insns.h"', ""] + ch
        # declare externs for table in chunk 0
        open(os.path.join(OUTDIR, f"gen_insns_{i}.c"), "w").write("\n\n".join(src) + "\n")

    def classify(ins):
        """(kind, extra): native-execution eligibility + static cycle extras."""
        mnem, ops = ins["mnem"], ins["ops"]
        parts = mnem.split(".")
        base, suf = parts[0], (parts[1] if len(parts) > 1 else "")
        opl = [o.lower() for o in ops]
        if base in ("mulu", "muls", "divu", "divs", "rte"): return 0, 0
        if any(o in ("sr", "ccr", "usp") for o in opl): return 0, 0
        if base in ("beq","bne","bgt","blt","bge","ble","bpl","bmi",
                    "bhi","bls","bcc","bcs","bvc","bvs"):
            return (2 if suf == "s" else 3), 0
        if base.startswith("db"): return 4, 0
        if base in ("asl","asr","lsl","lsr","rol","ror","roxl","roxr"):
            if len(ops) == 1: return 1, 0          # memory form, base only
            if ops[0].startswith("#"):
                try: return 1, (evalexpr(ops[0][1:]) & 63) * 14
                except Exception: return 0, 0
            return 0, 0                             # count from register
        if base == "movem":
            t = ops[0] if "(" not in ops[0] else ops[1]
            n = 0
            for part in t.replace("sp","a7").split("/"):
                m = re.match(r"^([da])([0-7])(?:-([da])([0-7]))?$", part.strip().lower())
                if not m: return 0, 0
                lo = int(m.group(2)) + (8 if m.group(1)=="a" else 0)
                hi = (int(m.group(4)) + (8 if m.group(3)=="a" else 0)) if m.group(3) else lo
                n += hi - lo + 1
            # GPGX charges movem.l via extras-first + SKIP_BUS_REFRESH, but
            # movem.w handlers have no SKIP — plain base+extra (kind 1)
            if suf == "w": return 1, n * 28
            return 6, n * 56
        if base in ("bset", "bclr", "bchg") and len(ops) == 2 \
           and re.match(r"^d[0-7]$", opl[1]):
            # dreg destination = long op: +2 cycles when bit (mod 32) >= 16
            if ops[0].startswith("#"):
                try: return 1, (14 if (evalexpr(ops[0][1:]) & 31) >= 16 else 0)
                except Exception: return 0, 0
            if re.match(r"^d[0-7]$", opl[0]):
                return 7, 0                    # bit from register, judged at runtime
            return 0, 0
        if (base == "st" or base == "sf" or (base.startswith("s") and base[1:] in CC)) \
           and len(ops) == 1:
            if re.match(r"^d[0-7]$", opl[0]):
                return (1, 0) if base in ("st", "sf") else (5, 0)
            return 1, 0
        return 1, 0

    # table file: needs all fn declarations
    tbl = ['#include "gen_insns.h"', ""]
    for ins, fname in table:
        if fname: tbl.append(f"void {fname}(rcpu_t*);")
    tbl.append("")
    tbl.append("const rc_entry rc_table[] = {")
    for ins, fname in sorted(table, key=lambda t: t[0]["addr"]):
        fn = fname if fname else "0"
        kind, extra = classify(ins) if fname else (0, 0)
        tbl.append(f'  {{0x{ins["addr"]:06X}, {fn}, "{ins["mnem"]}", '
                   f'{kind}, {min(ins["len"],255)}, {extra}}},')
    tbl.append("};")
    tbl.append(f"const int rc_table_n = {len(table)};")
    open(os.path.join(OUTDIR, "gen_table.c"), "w").write("\n".join(tbl) + "\n")

    # de-static the chunk files (functions must be visible to gen_table)
    for i in range(NCHUNK):
        p = os.path.join(OUTDIR, f"gen_insns_{i}.c")
        s = open(p).read().replace("static void I_", "void I_")
        open(p, "w").write(s)

    total = sum(done.values()) + sum(skipped.values())
    print(f"emitted: {sum(done.values())}/{total} "
          f"({100.0*sum(done.values())/total:.1f}%)")
    print("top skipped:", skipped.most_common(15))
    if stats: print("emit errors:", dict(stats))

if __name__ == "__main__":
    main()
