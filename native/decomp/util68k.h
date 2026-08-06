/* 68k ALU helpers for lifted code: each computes the operation's result and
 * sets the condition codes exactly as the original instruction would, so a
 * lifted routine's flag state falls out of writing the computation itself. */
#ifndef _UTIL68K_H_
#define _UTIL68K_H_

#include "../harness/lift.h"

#define W(v) ((v) & 0xFFFFu)
#define SW(v) ((int16_t)(uint16_t)(v))
#define SEW(v) ((uint32_t)(int32_t)SW(v))   /* word sign-extended to long */

static inline uint32_t alu_addw(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t r = W(d + s);
  c->cf = ((W(d) + W(s)) >> 16) & 1;
  c->xf = c->cf;
  c->vf = ((~(s ^ d) & (s ^ r)) >> 15) & 1;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_subw(rcpu_t *c, uint32_t s, uint32_t d)   /* d - s */
{
  uint32_t r = W(d - s);
  c->cf = (W(s) > W(d));
  c->xf = c->cf;
  c->vf = (((d ^ s) & (d ^ r)) >> 15) & 1;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
  return r;
}

static inline void alu_cmpw(rcpu_t *c, uint32_t s, uint32_t d)       /* d - s, no store, no X */
{
  uint32_t r = W(d - s);
  c->cf = (W(s) > W(d));
  c->vf = (((d ^ s) & (d ^ r)) >> 15) & 1;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
}

static inline void alu_cmpb(rcpu_t *c, uint32_t s, uint32_t d)       /* d - s, byte, no store, no X */
{
  uint32_t r = ((d & 0xFF) - (s & 0xFF)) & 0xFF;
  c->cf = ((s & 0xFF) > (d & 0xFF));
  c->vf = (((d ^ s) & (d ^ r)) >> 7) & 1;
  c->nf = (r >> 7) & 1;
  c->zf = (r == 0);
}

static inline uint32_t alu_negw(rcpu_t *c, uint32_t d)
{
  uint32_t r = W(0 - d);
  c->cf = (r != 0);
  c->xf = c->cf;
  c->vf = ((d & r) >> 15) & 1;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_negb(rcpu_t *c, uint32_t d)   /* neg.b: the byte twin of alu_negw */
{
  uint32_t r = (0 - (d & 0xFF)) & 0xFF;
  c->cf = (r != 0);
  c->xf = c->cf;
  c->vf = ((d & r) >> 7) & 1;
  c->nf = (r >> 7) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_notw(rcpu_t *c, uint32_t d)   /* not.w: NZ, V=C=0, X kept */
{
  uint32_t r = W(~d);
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

static inline uint32_t alu_movew(rcpu_t *c, uint32_t v)              /* move/logic NZ, V=C=0 */
{
  c->nf = (W(v) >> 15) & 1;
  c->zf = (W(v) == 0);
  c->vf = 0;
  c->cf = 0;
  return W(v);
}

static inline uint32_t alu_andw(rcpu_t *c, uint32_t a, uint32_t b)
{
  return alu_movew(c, a & b);
}

static inline uint32_t alu_eorw(rcpu_t *c, uint32_t a, uint32_t b)
{
  return alu_movew(c, a ^ b);
}

static inline uint32_t alu_orw(rcpu_t *c, uint32_t a, uint32_t b)
{
  return alu_movew(c, a | b);
}

static inline void alu_tstl(rcpu_t *c, uint32_t v)
{
  c->nf = (v >> 31) & 1;
  c->zf = (v == 0);
  c->vf = 0;
  c->cf = 0;
}

static inline void alu_tstw(rcpu_t *c, uint32_t v)
{
  c->nf = (W(v) >> 15) & 1;
  c->zf = (W(v) == 0);
  c->vf = 0;
  c->cf = 0;
}

static inline void alu_tstb(rcpu_t *c, uint32_t v)
{
  c->nf = ((v & 0xFF) >> 7) & 1;
  c->zf = ((v & 0xFF) == 0);
  c->vf = 0;
  c->cf = 0;
}

static inline uint32_t alu_asrw(rcpu_t *c, uint32_t v, int n)        /* count >= 1 */
{
  uint32_t last = 0;
  v = W(v);
  for (int i = 0; i < n; i++)
  {
    last = v & 1;
    v = (v >> 1) | (v & 0x8000);
  }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_lsrw(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0;
  v = W(v);
  for (int i = 0; i < n; i++) { last = v & 1; v >>= 1; }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = 0;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_moveb(rcpu_t *c, uint32_t v)
{
  c->nf = ((v) >> 7) & 1;
  c->zf = ((v & 0xFF) == 0);
  c->vf = 0;
  c->cf = 0;
  return v & 0xFF;
}

static inline uint32_t alu_addl(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t r = d + s;
  c->cf = (r < d);
  c->xf = c->cf;
  c->vf = ((~(s ^ d) & (s ^ r)) >> 31) & 1;
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_subl(rcpu_t *c, uint32_t s, uint32_t d)   /* d - s, 32-bit */
{
  uint32_t r = d - s;
  c->cf = (s > d);
  c->xf = c->cf;
  c->vf = (((d ^ s) & (d ^ r)) >> 31) & 1;
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_movel(rcpu_t *c, uint32_t v)
{
  alu_tstl(c, v);
  return v;
}

static inline uint32_t alu_andl(rcpu_t *c, uint32_t a, uint32_t b)
{
  return alu_movel(c, a & b);
}

static inline uint32_t alu_lsrl(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0;
  for (int i = 0; i < n; i++) { last = v & 1; v >>= 1; }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = 0;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_extl(rcpu_t *c, uint32_t v)   /* ext.l: word -> long */
{
  uint32_t r = SEW(v);
  alu_tstl(c, r);
  return r;
}

static inline uint32_t alu_extw(rcpu_t *c, uint32_t v)   /* ext.w: byte -> word */
{
  uint32_t r = (uint32_t)(int8_t)(v & 0xFF) & 0xFFFF;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

static inline void alu_cmpl(rcpu_t *c, uint32_t s, uint32_t d)  /* 32-bit, no X */
{
  uint32_t r = d - s;
  c->cf = (s > d);
  c->vf = (((d ^ s) & (d ^ r)) >> 31) & 1;
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
}

/* moveq writes ALL 32 bits of the register — assign the result directly
 * (`c->d[n] = alu_moveql(...)`), NEVER through setw(): setw truncates the
 * write to the low word and silently preserves stale upper bits. That bug
 * passes every register compare (later code rarely reads the upper word)
 * and surfaces only as a wrong dead stack transient when the register is
 * movem-pushed — the $CB50 postgame divergence, 2026-08-02. */
static inline uint32_t alu_moveql(rcpu_t *c, int32_t v)  /* moveq: full 32-bit */
{
  uint32_t r = (uint32_t)v;
  alu_tstl(c, r);
  return r;
}

/* asl: V is cumulative — set if the MSB changed on any step of the shift */
static inline uint32_t alu_aslw(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0, changed = 0;
  v = W(v);
  for (int i = 0; i < n; i++)
  {
    last = (v >> 15) & 1;
    uint32_t nv = W(v << 1);
    changed |= ((nv ^ v) >> 15) & 1;
    v = nv;
  }
  c->cf = last; c->xf = last;
  c->vf = changed;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_lslw(rcpu_t *c, uint32_t v, int n)   /* count >= 1; V always 0 */
{
  uint32_t last = 0;
  v = W(v);
  for (int i = 0; i < n; i++)
  {
    last = (v >> 15) & 1;
    v = W(v << 1);
  }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_asll(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0, changed = 0;
  for (int i = 0; i < n; i++)
  {
    last = (v >> 31) & 1;
    uint32_t nv = v << 1;
    changed |= ((nv ^ v) >> 31) & 1;
    v = nv;
  }
  c->cf = last; c->xf = last;
  c->vf = changed;
  c->nf = (v >> 31) & 1;
  c->zf = (v == 0);
  return v;
}

static inline void alu_btst(rcpu_t *c, uint32_t v, int bit)   /* Z only */
{
  c->zf = !((v >> bit) & 1);
}

static inline uint32_t alu_bset(rcpu_t *c, uint32_t v, int bit)
{
  c->zf = !((v >> bit) & 1);
  return v | (1u << bit);
}

static inline uint32_t alu_bclr(rcpu_t *c, uint32_t v, int bit)
{
  c->zf = !((v >> bit) & 1);
  return v & ~(1u << bit);
}

static inline uint32_t alu_bchg(rcpu_t *c, uint32_t v, int bit)
{
  c->zf = !((v >> bit) & 1);
  return v ^ (1u << bit);
}

/* rol: dynamic count already masked to &63 by caller; C=last bit rotated
 * out (unaffected if count==0), V always 0, X unaffected */
static inline uint32_t alu_rolw(rcpu_t *c, uint32_t v, int count)
{
  v = W(v);
  uint32_t carry = 0;
  for (int i = 0; i < count; i++)
  {
    uint32_t msb = (v >> 15) & 1;
    v = W((v << 1) | msb);
    carry = msb;
  }
  if (count != 0) { c->cf = carry; }
  c->vf = 0;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_asrb(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0;
  v = v & 0xFF;
  for (int i = 0; i < n; i++)
  {
    last = v & 1;
    v = (v >> 1) | (v & 0x80);
  }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = (v >> 7) & 1;
  c->zf = (v == 0);
  return v;
}

/* asl.b: V is cumulative — set if the MSB changed on any step of the shift */
static inline uint32_t alu_notl(rcpu_t *c, uint32_t d)   /* not.l: NZ, V=C=0, X kept */
{
  uint32_t r = ~d;
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

/* rol.l: dynamic count already masked to &63 by caller; C = last bit
 * rotated out (unaffected if count==0), V always 0, X unaffected */
static inline uint32_t alu_roll(rcpu_t *c, uint32_t v, int count)
{
  uint32_t carry = 0;
  for (int i = 0; i < count; i++)
  {
    uint32_t msb = (v >> 31) & 1;
    v = (v << 1) | msb;
    carry = msb;
  }
  if (count != 0) { c->cf = carry; }
  c->vf = 0;
  c->nf = (v >> 31) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_aslb(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0, changed = 0;
  v = v & 0xFF;
  for (int i = 0; i < n; i++)
  {
    last = (v >> 7) & 1;
    uint32_t nv = (v << 1) & 0xFF;
    changed |= ((nv ^ v) >> 7) & 1;
    v = nv;
  }
  c->cf = last; c->xf = last;
  c->vf = changed;
  c->nf = (v >> 7) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_lsrb(rcpu_t *c, uint32_t v, int n)
{
  uint32_t last = 0;
  v = v & 0xFF;
  for (int i = 0; i < n; i++) { last = v & 1; v >>= 1; }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = 0;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_addb(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t r = (d + s) & 0xFF;
  c->cf = (((d & 0xFF) + (s & 0xFF)) >> 8) & 1;
  c->xf = c->cf;
  c->vf = ((~(s ^ d) & (s ^ r)) >> 7) & 1;
  c->nf = (r >> 7) & 1;
  c->zf = (r == 0);
  return r;
}

/* ror: C=last bit rotated out (unaffected if count==0), V always 0 */
static inline uint32_t alu_rorw(rcpu_t *c, uint32_t v, int count)
{
  v = W(v);
  uint32_t carry = 0;
  for (int i = 0; i < count; i++)
  {
    uint32_t lsb = v & 1;
    v = (v >> 1) | (lsb << 15);
    carry = lsb;
  }
  if (count != 0) { c->cf = carry; }
  c->vf = 0;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_subb(rcpu_t *c, uint32_t s, uint32_t d)   /* d - s, byte */
{
  uint32_t r = (d - s) & 0xFF;
  c->cf = ((s & 0xFF) > (d & 0xFF));
  c->xf = c->cf;
  c->vf = (((d ^ s) & (d ^ r)) >> 7) & 1;
  c->nf = (r >> 7) & 1;
  c->zf = (r == 0);
  return r;
}

static inline uint32_t alu_swap(rcpu_t *c, uint32_t v)   /* NZ on 32-bit result */
{
  uint32_t r = (v >> 16) | (v << 16);
  alu_tstl(c, r);
  return r;
}

static inline uint32_t alu_asrl(rcpu_t *c, uint32_t v, int n)  /* count >= 1 */
{
  uint32_t last = 0;
  for (int i = 0; i < n; i++)
  {
    last = v & 1;
    v = (v >> 1) | (v & 0x80000000u);
  }
  c->cf = last; c->xf = last;
  c->vf = 0;
  c->nf = (v >> 31) & 1;
  c->zf = (v == 0);
  return v;
}

/* roxr: X participates in the rotation; C = X after (also when count==0) */
static inline uint32_t alu_orb(rcpu_t *c, uint32_t s, uint32_t d)   /* or.b: NZ, V=C=0, X kept */
{
  uint32_t r = (s | d) & 0xFF;
  c->nf = (r >> 7) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

static inline uint32_t alu_roxlb(rcpu_t *c, uint32_t v, int count)  /* byte twin of alu_roxrw, left */
{
  v &= 0xFF;
  for (int i = 0; i < count; i++)
  {
    uint32_t msb = (v >> 7) & 1;
    v = ((v << 1) & 0xFF) | (uint32_t)(c->xf & 1);
    c->xf = msb;
  }
  c->cf = c->xf;
  c->vf = 0;
  c->nf = (v >> 7) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_roxrw(rcpu_t *c, uint32_t v, int count)
{
  v = W(v);
  for (int i = 0; i < count; i++)
  {
    uint32_t lsb = v & 1;
    v = (v >> 1) | ((uint32_t)(c->xf & 1) << 15);
    c->xf = lsb;
  }
  c->cf = c->xf;
  c->vf = 0;
  c->nf = (v >> 15) & 1;
  c->zf = (v == 0);
  return v;
}

static inline uint32_t alu_negl(rcpu_t *c, uint32_t d)
{
  uint32_t r = 0u - d;
  c->cf = (r != 0);
  c->xf = c->cf;
  c->vf = ((d & r) >> 31) & 1;
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  return r;
}

/* mulu.w s,d — full 32-bit result replaces d; NZ from the long result */
static inline uint32_t alu_mulu(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t r = W(s) * W(d);
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

/* muls.w s,d — signed 16x16 -> 32 */
static inline uint32_t alu_muls(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t r = (uint32_t)((int32_t)SW(s) * (int32_t)SW(d));
  c->nf = (r >> 31) & 1;
  c->zf = (r == 0);
  c->vf = 0;
  c->cf = 0;
  return r;
}

/* divu.w s,d — returns the new 32-bit register value: remainder:quotient,
 * or d unchanged on overflow (quotient >= $10000: V and N set, Z UNTOUCHED).
 * Zero divisor is the caller's problem: charge via lift_charge_divu first,
 * which declines on it — bail before calling this. */
static inline uint32_t alu_divu(rcpu_t *c, uint32_t s, uint32_t d)
{
  uint32_t q = d / W(s), r = d % W(s);
  if (q < 0x10000)
  {
    c->zf = (q == 0);
    c->nf = (q >> 15) & 1;
    c->vf = 0;
    c->cf = 0;
    return (r << 16) | q;
  }
  c->vf = 1;
  c->nf = 1;   /* undocumented 68000 behaviour, mirrored from GPGX */
  c->cf = 0;
  return d;
}

/* divs.w s,d — signed; same overflow contract as alu_divu. The
 * $80000000 / -1 case returns 0 with Z set (GPGX's special case). */
static inline uint32_t alu_divs(rcpu_t *c, uint32_t s, uint32_t d)
{
  int32_t ss = SW(s), sd = (int32_t)d, q, r;
  if (d == 0x80000000u && ss == -1)
  {
    c->zf = 1;
    c->nf = 0;
    c->vf = 0;
    c->cf = 0;
    return 0;
  }
  q = sd / ss;
  r = sd % ss;
  if (q == (int16_t)q)
  {
    c->zf = (q == 0);
    c->nf = ((uint32_t)q >> 15) & 1;
    c->vf = 0;
    c->cf = 0;
    return ((uint32_t)r << 16) | ((uint32_t)q & 0xFFFF);
  }
  c->vf = 1;
  c->nf = 1;   /* undocumented 68000 behaviour, mirrored from GPGX */
  c->cf = 0;
  return d;
}

/* set a data register's low word, upper half preserved */
static inline void setw(uint32_t *reg, uint32_t v)
{
  *reg = (*reg & 0xFFFF0000u) | W(v);
}

/* set a data register's low byte, upper 24 bits preserved */
static inline void setb(uint32_t *reg, uint32_t v)
{
  *reg = (*reg & 0xFFFFFF00u) | (v & 0xFFu);
}

#endif
