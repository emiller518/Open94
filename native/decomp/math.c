/*
 * math.c — arithmetic utility routines (lifted). First users of the
 * data-dependent mul/div cycle model (lift_charge_mulu/divu & friends).
 */
#include "util68k.h"

#define SHARED_RTS 0x15464u   /* far rts several routines branch to */
#define RNG_SEED   0xFFFFD066u /* 32-bit LCG state: hi word D066, lo word D068 */

/*
 * Rng_NextScaled (sub_11086; 17k calls/game — the gameplay RNG)
 *   in:  d0.w = scale, passed via the routine's OWN movem save slot: the
 *        caller's d0 is pushed by the movem below, re-read from 2(sp) as
 *        the multiplier, and its save slot then discarded (addq #4,sp)
 *        instead of restored
 *   out: d0.w = random value in [0, scale); d0's high word = the low word
 *        of the final product (junk the callers ignore); d1/d2 preserved
 * Steps the 32-bit LCG state at $FFFFD066: seed' = seed * $BB40E62D + 1
 * (three 16x16 mulu partial products), then returns
 * ((seed' >> 8) & $FFFF) * scale >> 16.
 */
void Rng_NextScaled(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge_movem(x, 0x11086);                 /* movem.l d0-d2,-(sp) */
  lift_w32(x, c->a[7] - 12, c->d[0]);
  lift_w32(x, c->a[7] - 8, c->d[1]);
  lift_w32(x, c->a[7] - 4, c->d[2]);
  c->a[7] -= 12;

  setw(&c->d[0], alu_movew(c, lift_r16(x, RNG_SEED + 2)));  /* move.w (D068).w,d0 */
  lift_charge(x, 0x1108A);
  setw(&c->d[1], alu_movew(c, W(c->d[0])));      /* move.w d0,d1 */
  lift_charge(x, 0x1108E);
  setw(&c->d[2], alu_movew(c, lift_r16(x, RNG_SEED)));      /* move.w (D066).w,d2 */
  lift_charge(x, 0x11090);
  c->d[0] = alu_mulu(c, 0xE62D, c->d[0]);        /* mulu.w #$E62D,d0 */
  lift_charge_mulu(x, 0x11094, 0xE62D);
  c->d[1] = alu_mulu(c, 0xBB40, c->d[1]);        /* mulu.w #$BB40,d1 */
  lift_charge_mulu(x, 0x11098, 0xBB40);
  c->d[2] = alu_mulu(c, 0xE62D, c->d[2]);        /* mulu.w #$E62D,d2 */
  lift_charge_mulu(x, 0x1109C, 0xE62D);
  setw(&c->d[1], alu_addw(c, W(c->d[2]), W(c->d[1])));      /* add.w d2,d1 */
  lift_charge(x, 0x110A0);
  c->d[0] = alu_swap(c, c->d[0]);                /* swap d0 */
  lift_charge(x, 0x110A2);
  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));      /* add.w d1,d0 */
  lift_charge(x, 0x110A4);
  c->d[0] = alu_swap(c, c->d[0]);                /* swap d0 */
  lift_charge(x, 0x110A6);
  c->d[0] = alu_addl(c, 1, c->d[0]);             /* addq.l #1,d0 */
  lift_charge(x, 0x110A8);
  lift_w32(x, RNG_SEED, alu_movel(c, c->d[0]));  /* move.l d0,(D066).w */
  lift_charge(x, 0x110AA);
  c->d[0] = alu_asrl(c, c->d[0], 8);             /* asr.l #8,d0 */
  lift_charge(x, 0x110AE);
  {
    uint32_t scale = lift_r16(x, c->a[7] + 2);   /* mulu.w 2(sp),d0 */
    c->d[0] = alu_mulu(c, scale, c->d[0]);
    lift_charge_mulu(x, 0x110B0, scale);
  }
  c->d[0] = alu_swap(c, c->d[0]);                /* swap d0 (exit flags) */
  lift_charge(x, 0x110B4);
  c->a[7] += 4;                                  /* addq.w #4,sp: no CCR */
  lift_charge(x, 0x110B6);
  lift_charge_movem(x, 0x110B8);                 /* movem.l (sp)+,d1-d2 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge(x, 0x110BC);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Math_SqrtU32 (sub_110BE)
 *   in:  d0 = unsigned 32-bit value
 *   out: d0.w = floor(sqrt(d0)); three quirks preserved bit-for-bit:
 *        d0 == 0 returns through the shared far rts with d0 untouched,
 *        d0 <= $640 leaves d0's high word zeroed by the subtraction loop,
 *        d0 > $640 leaves d0's ORIGINAL high word intact above the result
 * Three paths: odd-number subtraction (small), Newton's method seeded
 * with (d0 >> 8) + 2 and capped at 10 divu.w iterations (medium), or a
 * 16-bit binary search squaring the midpoint via mulu.w (d0 > $F00000).
 */
void Math_SqrtU32(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  alu_tstl(c, c->d[0]);                          /* tst.l d0 */
  lift_charge(x, 0x110BE);
  if (c->zf)
  {
    lift_charge_bcc(x, 0x110C0, 1);              /* beq.w -> shared far rts */
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_charge_bcc(x, 0x110C0, 0);
  alu_cmpl(c, 0x640, c->d[0]);                   /* cmpi.l #$640,d0 */
  lift_charge(x, 0x110C4);
  if (!(!c->cf && !c->zf))                       /* bhi.w loc_110E0 not taken */
  {
    lift_charge_bcc(x, 0x110CA, 0);
    c->a[7] -= 4;                                /* move.l d1,-(sp) */
    lift_w32(x, c->a[7], alu_movel(c, c->d[1]));
    lift_charge(x, 0x110CE);
    c->d[1] = alu_moveql(c, -1);                 /* moveq #-1,d1 */
    lift_charge(x, 0x110D0);
    for (i = 0; ; i++)
    {
      if (i > 64) { x->declined = 1; return; }   /* can't happen: <= 41 iters */
      setw(&c->d[1], alu_addw(c, 2, W(c->d[1])));    /* addq.w #2,d1 */
      lift_charge(x, 0x110D2);
      setw(&c->d[0], alu_subw(c, W(c->d[1]), W(c->d[0])));  /* sub.w d1,d0 */
      lift_charge(x, 0x110D4);
      lift_charge_bcc(x, 0x110D6, !c->cf);       /* bcc.s loc_110D2 */
      if (c->cf) break;
    }
    setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 1));  /* lsr.w #1,d1 */
    lift_charge(x, 0x110D8);
    setw(&c->d[0], alu_movew(c, W(c->d[1])));    /* move.w d1,d0 */
    lift_charge(x, 0x110DA);
    c->d[1] = alu_movel(c, lift_r32(x, c->a[7])); /* move.l (sp)+,d1 */
    c->a[7] += 4;
    lift_charge(x, 0x110DC);
    lift_charge(x, 0x110DE);                     /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_charge_bcc(x, 0x110CA, 1);                /* bhi.w taken */
  lift_charge_movem(x, 0x110E0);                 /* movem.l d1-d4,-(sp) */
  lift_w32(x, c->a[7] - 16, c->d[1]);
  lift_w32(x, c->a[7] - 12, c->d[2]);
  lift_w32(x, c->a[7] - 8, c->d[3]);
  lift_w32(x, c->a[7] - 4, c->d[4]);
  c->a[7] -= 16;
  c->d[3] = alu_moveql(c, 9);                    /* moveq #9,d3 */
  lift_charge(x, 0x110E4);
  setw(&c->d[1], alu_movew(c, 0x8000));          /* move.w #$8000,d1 */
  lift_charge(x, 0x110E6);
  alu_cmpl(c, 0xF00000, c->d[0]);                /* cmpi.l #$F00000,d0 */
  lift_charge(x, 0x110EA);
  if (!(!c->cf && !c->zf))                       /* bhi.w loc_11112 not taken */
  {
    /* Newton's method */
    lift_charge_bcc(x, 0x110F0, 0);
    c->d[1] = alu_movel(c, c->d[0]);             /* move.l d0,d1 */
    lift_charge(x, 0x110F4);
    c->d[1] = alu_lsrl(c, c->d[1], 8);           /* lsr.l #8,d1 */
    lift_charge(x, 0x110F6);
    setw(&c->d[1], alu_addw(c, 2, W(c->d[1])));  /* addq.w #2,d1 */
    lift_charge(x, 0x110F8);
    for (;;)                                     /* <= 10 iters (dbeq d3) */
    {
      setw(&c->d[2], alu_movew(c, W(c->d[1])));  /* move.w d1,d2 */
      lift_charge(x, 0x110FA);
      c->d[1] = alu_movel(c, c->d[0]);           /* move.l d0,d1 */
      lift_charge(x, 0x110FC);
      lift_charge_divu(x, 0x110FE, W(c->d[2]), c->d[1]);  /* divu.w d2,d1 */
      if (x->declined) return;                   /* zero divisor would trap */
      c->d[1] = alu_divu(c, W(c->d[2]), c->d[1]);
      setw(&c->d[1], alu_addw(c, W(c->d[2]), W(c->d[1]))); /* add.w d2,d1 */
      lift_charge(x, 0x11100);
      setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 1)); /* lsr.w #1,d1 */
      lift_charge(x, 0x11102);
      alu_cmpw(c, W(c->d[1]), W(c->d[2]));       /* cmp.w d1,d2 */
      lift_charge(x, 0x11104);
      if (c->zf)                                 /* dbeq d3,loc_110FA */
      {
        lift_charge_dbcc(x, 0x11106, 0, 0);
        break;
      }
      setw(&c->d[3], W(c->d[3]) - 1);            /* counter: no CCR */
      if (W(c->d[3]) != 0xFFFF)
      {
        lift_charge_dbcc(x, 0x11106, 1, 0);
        continue;
      }
      lift_charge_dbcc(x, 0x11106, 0, 1);        /* expired: fall through */
      break;
    }
  }
  else
  {
    /* 16-bit binary search: lo in d1, hi in d2, mid via roxr (17-bit sum) */
    lift_charge_bcc(x, 0x110F0, 1);              /* bhi.w taken */
    c->d[1] = alu_moveql(c, 0);                  /* moveq #0,d1 */
    lift_charge(x, 0x11112);
    c->d[2] = alu_moveql(c, -1);                 /* moveq #-1,d2 */
    lift_charge(x, 0x11114);
    for (i = 0; ; i++)
    {
      if (i > 32) { x->declined = 1; return; }   /* can't happen: <= ~17 iters */
      setw(&c->d[3], alu_movew(c, W(c->d[1])));  /* move.w d1,d3 */
      lift_charge(x, 0x11116);
      setw(&c->d[3], alu_addw(c, W(c->d[2]), W(c->d[3])));  /* add.w d2,d3 */
      lift_charge(x, 0x11118);
      setw(&c->d[3], alu_roxrw(c, W(c->d[3]), 1)); /* roxr.w #1,d3 */
      lift_charge(x, 0x1111A);
      alu_cmpw(c, W(c->d[3]), W(c->d[1]));       /* cmp.w d3,d1 */
      lift_charge(x, 0x1111C);
      lift_charge_bcc(x, 0x1111E, c->zf);        /* beq.s loc_1110A */
      if (c->zf) break;
      setw(&c->d[4], alu_movew(c, W(c->d[3]))); /* move.w d3,d4 */
      lift_charge(x, 0x11120);
      c->d[3] = alu_mulu(c, W(c->d[3]), c->d[3]); /* mulu.w d3,d3 */
      lift_charge_mulu(x, 0x11122, W(c->d[4]));  /* src operand = old d3 */
      alu_cmpl(c, c->d[3], c->d[0]);             /* cmp.l d3,d0 */
      lift_charge(x, 0x11124);
      if (!c->cf)                                /* bcc.w loc_1112E */
      {
        lift_charge_bcc(x, 0x11126, 1);
        setw(&c->d[1], alu_movew(c, W(c->d[4]))); /* move.w d4,d1 */
        lift_charge(x, 0x1112E);
        lift_charge_bcc(x, 0x11130, 1);          /* bra.s loc_11116 */
      }
      else
      {
        lift_charge_bcc(x, 0x11126, 0);
        setw(&c->d[2], alu_movew(c, W(c->d[4]))); /* move.w d4,d2 */
        lift_charge(x, 0x1112A);
        lift_charge_bcc(x, 0x1112C, 1);          /* bra.s loc_11116 */
      }
    }
  }
  /* loc_1110A — shared epilogue of the two big-value paths */
  setw(&c->d[0], alu_movew(c, W(c->d[1])));      /* move.w d1,d0 (exit flags) */
  lift_charge(x, 0x1110A);
  lift_charge_movem(x, 0x1110C);                 /* movem.l (sp)+,d1-d4 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->d[3] = lift_r32(x, c->a[7] + 8);
  c->d[4] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge(x, 0x11110);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Rng_NextSignedOffset (sub_1107A; calls Rng_NextScaled)
 *   in:  d0.w = magnitude
 *   out: d0.w = a random value in [-magnitude, +magnitude); d1/d2
 *        preserved (Rng_NextScaled itself preserves them)
 * Doubles the magnitude, draws Rng_NextScaled(2*magnitude) — a value in
 * [0, 2*magnitude) — then subtracts the original magnitude back off to
 * re-center the range on zero.
 */
void Rng_NextSignedOffset(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, c->a[7] - 2, W(c->d[0]));          /* move.w d0,-(sp) */
  c->a[7] -= 2;
  lift_charge(x, 0x1107A);

  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));    /* asl.w #1,d0 */
  lift_charge(x, 0x1107C);

  lift_call(x, 0x1107E, 4, Rng_NextScaled);      /* bsr.w sub_11086 */

  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[7]), W(c->d[0])));  /* sub.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0x11082);

  lift_charge(x, 0x11084);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_C644 (called from sub_C566)
 *   in:  d0/d1 = multipliers, d2 = divisor, d3/d4 = minuends (also read
 *        from $FFFFB74A/$FFFFB75E)
 *   out: d4.l = divs.w quotient:remainder of ((d4-B75E)*d0 - (d3-B74A)*d1)
 *        by d2; no memory writes; d0-d3 unchanged
 */
void sub_C644(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_subw(c, lift_r16(x, 0xFFFFB74A), W(c->d[3])));  /* sub.w (B74A).w,d3 */
  lift_charge(x, 0xC644);
  setw(&c->d[4], alu_subw(c, lift_r16(x, 0xFFFFB75E), W(c->d[4])));  /* sub.w (B75E).w,d4 */
  lift_charge(x, 0xC648);
  c->d[4] = alu_muls(c, W(c->d[0]), W(c->d[4]));   /* muls.w d0,d4 */
  lift_charge_muls(x, 0xC64C, W(c->d[0]));
  c->d[3] = alu_muls(c, W(c->d[1]), W(c->d[3]));   /* muls.w d1,d3 */
  lift_charge_muls(x, 0xC64E, W(c->d[1]));
  c->d[4] = alu_subl(c, c->d[3], c->d[4]);         /* sub.l d3,d4 */
  lift_charge(x, 0xC650);
  lift_charge_divs(x, 0xC652, W(c->d[2]), c->d[4]);  /* divs.w d2,d4 */
  if (x->declined) return;                          /* zero divisor would trap */
  c->d[4] = alu_divs(c, W(c->d[2]), c->d[4]);

  lift_charge(x, 0xC654);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_10EB4 (called from sub_132E2)
 *   in:  d0.w = table-length-1 for the word table at $FFFFD036
 *   out: d0.w = word-index into that table (weighted-random pick); d1/a1
 *        preserved
 * Sums d0+1 words from $FFFFD036 into d1, rolls Rng_NextScaled(sum), then
 * walks the table backward from where the sum left off, subtracting
 * entries from the roll until it goes negative — returns how many words
 * it walked back over (i.e. the picked slot).
 */
void sub_10EB4(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, taken;

  lift_charge_movem(x, 0x10EB4);                 /* movem.l d1/a1,-(sp): a1 pushed first (high addr), d1 lands lowest */
  lift_w32(x, c->a[7] - 8, c->d[1]);
  lift_w32(x, c->a[7] - 4, c->a[1]);
  c->a[7] -= 8;

  c->a[1] = 0xFFFFD036u;                         /* move.w #$D036,a1 (sign-extends) */
  lift_charge(x, 0x10EB8);
  setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
  lift_charge(x, 0x10EBC);
  lift_charge(x, 0x10EBE);                       /* bra.w loc_10EC4 */

  for (i = 0; ; i++)
  {
    if (i > 70000) { x->declined = 1; return; }  /* dbf can't loop more than 65536x */
    setw(&c->d[0], W(c->d[0]) - 1);              /* dbf: counter, no CCR */
    if (W(c->d[0]) != 0xFFFF)
    {
      lift_charge_dbcc(x, 0x10EC4, 1, 0);
      setw(&c->d[1], alu_addw(c, lift_r16(x, c->a[1]), W(c->d[1])));  /* add.w (a1)+,d1 */
      c->a[1] += 2;
      lift_charge(x, 0x10EC2);
      continue;
    }
    lift_charge_dbcc(x, 0x10EC4, 0, 1);
    break;
  }

  setw(&c->d[0], alu_movew(c, W(c->d[1])));      /* move.w d1,d0 */
  lift_charge(x, 0x10EC8);

  lift_call(x, 0x10ECA, 4, Rng_NextScaled);      /* bsr.w sub_11086 */

  for (i = 0; ; i++)
  {
    if (i > 70000) { x->declined = 1; return; }
    c->a[1] -= 2;                                /* -(a1) predecrement */
    setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[1]), W(c->d[0])));  /* sub.w -(a1),d0 */
    lift_charge(x, 0x10ECE);
    taken = !c->nf;                              /* bpl.s: branch if N=0 */
    lift_charge_bcc(x, 0x10ED0, taken);
    if (!taken) break;
  }

  c->a[1] -= 0xFFFFD036u;                        /* suba.w #$D036,a1 (sign-extends) */
  lift_charge(x, 0x10ED2);
  setw(&c->d[0], alu_movew(c, W(c->a[1])));      /* move.w a1,d0 */
  lift_charge(x, 0x10ED6);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));    /* lsr.w #1,d0 */
  lift_charge(x, 0x10ED8);

  lift_charge_movem(x, 0x10EDA);                 /* movem.l (sp)+,d1/a1 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->a[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge(x, 0x10EDE);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_132E2 (called from sub_13276; tail-jumps into sub_10EB4)
 *   in: d0.w/d1.w = two small object indices (e.g. line-combo slots)
 *   For each of d0,d1: scales it by 4 to index a pointer table at
 *   $30E, follows that pointer + its own +8(a1) offset word to reach a
 *   per-object byte, and uses bitfields of that byte (bits 4-6 for d0,
 *   bits 0-2 for d1) to pull an 8-byte (2 x 32-bit) entry out of the
 *   table at word_13338. d0's entry overwrites $FFFFD036/$FFFFD03A;
 *   d1's entry is added into them instead. Ends with d0=4 and a tail
 *   jump (bra, not bsr) into sub_10EB4 — its rts returns to sub_132E2's
 *   own caller.
 */
void sub_132E2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t off;

  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0x132E2);
  c->a[1] = 0x30Eu;                                /* move.w #$30E,a1 (sign-extends) */
  lift_charge(x, 0x132E4);
  c->a[1] = lift_r32(x, (c->a[1] + SW(c->d[0])) & 0xFFFFFF); /* move.l (a1,d0.w),a1 */
  lift_charge(x, 0x132E8);
  c->a[1] = (c->a[1] + SW(lift_r16(x, c->a[1] + 8))) & 0xFFFFFF;  /* add.w 8(a1),a1 */
  lift_charge(x, 0x132EC);
  setb(&c->d[0], lift_r8(x, c->a[1]));             /* move.b (a1),d0 */
  lift_charge(x, 0x132F0);
  setw(&c->d[0], alu_andw(c, 0x70, W(c->d[0])));   /* and.w #$70,d0 */
  lift_charge(x, 0x132F2);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));      /* lsr.w #1,d0 */
  lift_charge(x, 0x132F6);
  c->a[1] = 0x13338u;                               /* lea word_13338(pc),a1 */
  lift_charge(x, 0x132F8);
  off = SW(c->d[0]);
  lift_w32(x, 0xFFFFD036u, lift_r32(x, c->a[1] + off));       /* move.l (a1,d0.w),(D036).w */
  lift_charge(x, 0x132FC);
  lift_w32(x, 0xFFFFD03Au, lift_r32(x, c->a[1] + off + 4));   /* move.l 4(a1,d0.w),(D03A).w */
  lift_charge(x, 0x13302);

  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));      /* asl.w #2,d1 */
  lift_charge(x, 0x13308);
  c->a[1] = 0x30Eu;                                 /* move.w #$30E,a1 */
  lift_charge(x, 0x1330A);
  c->a[1] = lift_r32(x, (c->a[1] + SW(c->d[1])) & 0xFFFFFF); /* move.l (a1,d1.w),a1 */
  lift_charge(x, 0x1330E);
  c->a[1] = (c->a[1] + SW(lift_r16(x, c->a[1] + 8))) & 0xFFFFFF;  /* add.w 8(a1),a1 */
  lift_charge(x, 0x13312);
  setb(&c->d[0], lift_r8(x, c->a[1]));              /* move.b (a1),d0 */
  lift_charge(x, 0x13316);
  setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));       /* and.w #7,d0 */
  lift_charge(x, 0x13318);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));       /* asl.w #3,d0 */
  lift_charge(x, 0x1331C);
  c->a[1] = 0x13338u;                                /* lea word_13338(pc),a1 */
  lift_charge(x, 0x1331E);
  off = SW(c->d[0]);
  c->d[1] = alu_movel(c, lift_r32(x, c->a[1] + off));    /* move.l (a1,d0.w),d1 */
  lift_charge(x, 0x13322);
  lift_w32(x, 0xFFFFD036u, alu_addl(c, c->d[1], lift_r32(x, 0xFFFFD036u)));  /* add.l d1,(D036).w */
  lift_charge(x, 0x13326);
  c->d[1] = alu_movel(c, lift_r32(x, c->a[1] + off + 4)); /* move.l 4(a1,d0.w),d1 */
  lift_charge(x, 0x1332A);
  lift_w32(x, 0xFFFFD03Au, alu_addl(c, c->d[1], lift_r32(x, 0xFFFFD03Au))); /* add.l d1,(D03A).w */
  lift_charge(x, 0x1332E);

  c->d[0] = alu_movel(c, 4);                        /* moveq #4,d0 */
  lift_charge(x, 0x13332);
  lift_charge(x, 0x13334);                          /* bra.w sub_10EB4 (tail jump) */
  sub_10EB4(x);                                     /* its rts pops our caller's return addr */
}

/*
 * sub_13276 (called from sub_131F4/sub_1323E)
 *   in: a0 = line-combo/timer struct ($8 = state, $A/$C = accumulators,
 *       $E = flags, (a0)/2(a0) = the two object indices)
 *   Bails via the shared far rts if $8(a0) >= 4. Otherwise: if state==3,
 *   advances it to 5 and compares the two accumulators ($A - $C); if the
 *   difference is outside [-1,1] it reverts to state 3 and sets $E bit1.
 *   If state!=3, increments $8(a0) and runs sub_132E2 twice (swapping
 *   the two object indices) to fold each one's weighted-random pick
 *   into $A(a0)/$C(a0).
 */
void sub_13276(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a0 = c->a[0];

  alu_cmpw(c, 4, lift_r16(x, a0 + 8));               /* cmp.w #4,8(a0) */
  lift_charge(x, 0x13276);
  int bge = (c->nf == c->vf);                        /* bge.w locret_15464 */
  lift_charge_bcc(x, 0x1327C, bge);
  if (bge)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  uint32_t sd[2], sa[2];
  sd[0] = c->d[0]; sd[1] = c->d[1];
  sa[0] = c->a[0]; sa[1] = c->a[1];
  /* movem.l d0-d1/a0-a1,-(sp): a1,a0,d1,d0 in descending-register order */
  c->a[7] -= 4; lift_w32(x, c->a[7], sa[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], sa[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], sd[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], sd[0]);
  lift_charge_movem(x, 0x13280);

  alu_cmpw(c, 3, lift_r16(x, a0 + 8));                /* cmp.w #3,8(a0) */
  lift_charge(x, 0x13284);
  int wasState3 = c->zf;                              /* bne.w loc_132BC */
  lift_charge_bcc(x, 0x1328A, !wasState3);

  if (wasState3)
  {
    lift_w16(x, a0 + 8, alu_movew(c, 5));             /* move.w #5,8(a0) */
    lift_charge(x, 0x1328E);
    setw(&c->d[0], alu_movew(c, lift_r16(x, a0 + 0xA)));  /* move.w $A(a0),d0 */
    lift_charge(x, 0x13294);
    setw(&c->d[0], alu_subw(c, lift_r16(x, a0 + 0xC), W(c->d[0])));  /* sub.w $C(a0),d0 */
    lift_charge(x, 0x13298);
    alu_cmpw(c, 1, W(c->d[0]));                        /* cmp.w #1,d0 */
    lift_charge(x, 0x1329C);
    int gt = (!c->zf && c->nf == c->vf);                /* bgt.w loc_132DC */
    lift_charge_bcc(x, 0x132A0, gt);

    if (!gt)
    {
      alu_cmpw(c, 0xFFFF, W(c->d[0]));                  /* cmp.w #$FFFF,d0 */
      lift_charge(x, 0x132A4);
      int lt = (c->nf != c->vf);                        /* blt.w loc_132DC */
      lift_charge_bcc(x, 0x132A8, lt);

      if (!lt)
      {
        lift_w16(x, a0 + 8, alu_movew(c, 3));           /* move.w #3,8(a0) */
        lift_charge(x, 0x132AC);
        lift_w8(x, a0 + 0xE, alu_bset(c, lift_r8(x, a0 + 0xE), 1));  /* bset #1,$E(a0) */
        lift_charge(x, 0x132B2);
        lift_charge(x, 0x132B8);                         /* bra.w loc_132DC */
      }
    }
  }
  else
  {
    /* loc_132BC */
    lift_w16(x, a0 + 8, alu_addw(c, 1, lift_r16(x, a0 + 8)));  /* addq.w #1,8(a0) */
    lift_charge(x, 0x132BC);
    setw(&c->d[0], alu_movew(c, lift_r16(x, a0)));       /* move.w (a0),d0 */
    lift_charge(x, 0x132C0);
    setw(&c->d[1], alu_movew(c, lift_r16(x, a0 + 2)));   /* move.w 2(a0),d1 */
    lift_charge(x, 0x132C2);
    lift_call(x, 0x132C6, 4, sub_132E2);                 /* bsr.w sub_132E2 */
    if (x->declined) return;
    lift_w16(x, a0 + 0xA, alu_addw(c, W(c->d[0]), lift_r16(x, a0 + 0xA)));  /* add.w d0,$A(a0) */
    lift_charge(x, 0x132CA);
    setw(&c->d[0], alu_movew(c, lift_r16(x, a0 + 2)));   /* move.w 2(a0),d0 */
    lift_charge(x, 0x132CE);
    setw(&c->d[1], alu_movew(c, lift_r16(x, a0)));       /* move.w (a0),d1 */
    lift_charge(x, 0x132D2);
    lift_call(x, 0x132D4, 4, sub_132E2);                 /* bsr.w sub_132E2 */
    if (x->declined) return;
    lift_w16(x, a0 + 0xC, alu_addw(c, W(c->d[0]), lift_r16(x, a0 + 0xC)));  /* add.w d0,$C(a0) */
    lift_charge(x, 0x132D8);
  }

  /* loc_132DC: movem.l (sp)+,d0-d1/a0-a1 (ascending: d0,d1,a0,a1) */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x132DC);

  lift_charge(x, 0x132E0);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1826C (called from sub_1803E)
 *   in:  a3 = base of a 5-word big-endian multi-word dividend; d0.w = divisor
 *   out: d0.w = final remainder; the 5 words at a3 are overwritten with the
 *        quotient (long division propagating each step's remainder into
 *        the next word's high half); d1/d2/a0 preserved
 */
void sub_1826C(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  lift_charge_movem(x, 0x1826C);                 /* movem.l d1-d2/a0,-(sp): a0 pushed first (high addr), d2, d1 lands lowest */
  lift_w32(x, c->a[7] - 12, c->d[1]);
  lift_w32(x, c->a[7] - 8, c->d[2]);
  lift_w32(x, c->a[7] - 4, c->a[0]);
  c->a[7] -= 12;

  c->a[0] = SEW(W(c->a[3]));                     /* movea.w a3,a0: sign-extend */
  lift_charge(x, 0x18270);
  c->d[1] = alu_moveql(c, 4);                    /* moveq #4,d1 */
  lift_charge(x, 0x18272);
  c->d[2] = alu_movel(c, 0);                     /* clr.l d2 */
  lift_charge(x, 0x18274);

  for (i = 0; ; i++)
  {
    if (i > 5) { x->declined = 1; return; }       /* fixed 5-word loop */
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0),d2 */
    lift_charge(x, 0x18276);
    lift_charge_divu(x, 0x18278, W(c->d[0]), c->d[2]);   /* divu.w d0,d2 */
    if (x->declined) return;                      /* zero divisor would trap */
    c->d[2] = alu_divu(c, W(c->d[0]), c->d[2]);
    lift_w16(x, c->a[0], alu_movew(c, W(c->d[2])));  /* move.w d2,(a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0x1827A);

    setw(&c->d[1], W(c->d[1]) - 1);              /* dbf: counter, no CCR */
    if (W(c->d[1]) != 0xFFFF)
    {
      lift_charge_dbcc(x, 0x1827C, 1, 0);
      continue;
    }
    lift_charge_dbcc(x, 0x1827C, 0, 1);
    break;
  }

  c->d[2] = alu_swap(c, c->d[2]);                /* swap d2 */
  lift_charge(x, 0x18280);
  setw(&c->d[0], alu_movew(c, W(c->d[2])));      /* move.w d2,d0 */
  lift_charge(x, 0x18282);

  lift_charge_movem(x, 0x18284);                 /* movem.l (sp)+,d1-d2/a0 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge(x, 0x18288);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}
