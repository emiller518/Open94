/*
 * controls.c — pad/direction helpers (lifted).
 */
#include "util68k.h"

#define DIR_TABLE 0x0113C0u   /* ROM: 4-bit L/R/U/D mask -> 8-way facing (8 = none) */
#define SHARED_RTS 0x15464u   /* far rts several routines branch to */

/*
 * Controls_MapDirection (sub_113A0; called from sub_11340/11358/11370/11388,
 * the per-controller-byte direction mappers)
 *   in:  d0 = raw controller direction nibble (byte, active-low: 0 = held)
 *   out: d0 = (high nibble of ~d0) | DIR_TABLE[low nibble of ~d0]
 *        d1 = zero-extended byte value of ~d0 (the low byte survives a
 *             push/pop through the stack; upper 16 bits of d1 untouched)
 * DIR_TABLE maps the 4-bit up/down/left/right bitmask to one of the 8
 * facing codes used elsewhere ($54 facing byte); 8 marks an
 * invalid/diagonal-conflict combination.
 */
void Controls_MapDirection(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, (~c->d[0]) & 0xFF));      /* not.b d0 */
  lift_charge(x, 0x113A0);
  setw(&c->d[1], alu_movew(c, 0));                       /* clr.w d1 */
  lift_charge(x, 0x113A2);
  setb(&c->d[1], alu_moveb(c, c->d[0] & 0xFF));          /* move.b d0,d1 */
  lift_charge(x, 0x113A4);
  c->a[7] -= 2;                                          /* move.w d1,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x113A6);
  setw(&c->d[0], alu_andw(c, 0xF0, W(c->d[0])));         /* and.w #$F0,d0 */
  lift_charge(x, 0x113A8);
  setw(&c->d[1], alu_andw(c, 0xF, W(c->d[1])));          /* and.w #$F,d1 */
  lift_charge(x, 0x113AC);
  c->a[0] = DIR_TABLE;                                   /* movea.l: no flags */
  lift_charge(x, 0x113B0);
  setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1]))));  /* move.b (a0,d1.w),d1 */
  lift_charge(x, 0x113B6);
  setw(&c->d[0], alu_movew(c, W(c->d[0]) | W(c->d[1])));  /* or.w d1,d0 */
  lift_charge(x, 0x113BA);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[7])));    /* move.w (sp)+,d1 */
  c->a[7] += 2;
  lift_charge(x, 0x113BC);
  lift_charge(x, 0x113BE);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define PAD_STATE_BEF6 0xFFFFBEF6u   /* raw pad-direction byte (source unclear) */
#define PAD_EDGE_BEEE  0xFFFFBEEEu   /* previous frame's mapped state, same source */

/*
 * Controls_ReadEdge_BEF6 (sub_11340; sibling routines sub_11358/11370/11388
 * repeat this exact pattern for $FFFFBEF7/8/9 and $FFFFBEF0/2/4 — not
 * lifted here)
 *   out: d0 = 8-way direction (via Controls_MapDirection)
 *        d1 = bits newly asserted this frame vs. last (edge-detect)
 *        d2 = bits changed since last frame; d3 = this frame's raw state
 * Reads the raw pad-direction byte, maps it through Controls_MapDirection,
 * and XORs against the stored previous state to find newly-pressed bits.
 */
void Controls_ReadEdge_BEF6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, PAD_STATE_BEF6)));  /* move.b (abs),d0 */
  lift_charge(x, 0x11340);
  lift_call(x, 0x11344, 4, Controls_MapDirection);            /* bsr.w */
  setw(&c->d[2], alu_movew(c, lift_r16(x, PAD_EDGE_BEEE)));  /* move.w (abs),d2 */
  lift_charge(x, 0x11348);
  lift_w16(x, PAD_EDGE_BEEE, alu_movew(c, W(c->d[1])));      /* move.w d1,(abs) */
  lift_charge(x, 0x1134C);
  setw(&c->d[3], alu_movew(c, W(c->d[1])));                   /* move.w d1,d3 */
  lift_charge(x, 0x11350);
  setw(&c->d[2], alu_movew(c, W(c->d[2]) ^ W(c->d[1])));      /* eor.w d1,d2 */
  lift_charge(x, 0x11352);
  setw(&c->d[1], alu_movew(c, W(c->d[1]) & W(c->d[2])));      /* and.w d2,d1 */
  lift_charge(x, 0x11354);
  lift_charge(x, 0x11356);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define PAD_STATE_BEF7 0xFFFFBEF7u   /* raw pad-direction byte, second controller/edge byte */
#define PAD_EDGE_BEF0  0xFFFFBEF0u   /* previous frame's mapped state, same source */

/*
 * Controls_ReadEdge_BEF7 (sub_11358; sibling of Controls_ReadEdge_BEF6
 * above, same pattern for $FFFFBEF7/$FFFFBEF0 — sub_11370/11388 repeat
 * it again for BEF8/BEF2 and BEF9/BEF4, not lifted here)
 *   out: d0 = 8-way direction (via Controls_MapDirection)
 *        d1 = bits newly asserted this frame vs. last (edge-detect)
 *        d2 = bits changed since last frame; d3 = this frame's raw state
 */
void Controls_ReadEdge_BEF7(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, PAD_STATE_BEF7)));  /* move.b (abs),d0 */
  lift_charge(x, 0x11358);
  lift_call(x, 0x1135C, 4, Controls_MapDirection);            /* bsr.w */
  setw(&c->d[2], alu_movew(c, lift_r16(x, PAD_EDGE_BEF0)));  /* move.w (abs),d2 */
  lift_charge(x, 0x11360);
  lift_w16(x, PAD_EDGE_BEF0, alu_movew(c, W(c->d[1])));      /* move.w d1,(abs) */
  lift_charge(x, 0x11364);
  setw(&c->d[3], alu_movew(c, W(c->d[1])));                   /* move.w d1,d3 */
  lift_charge(x, 0x11368);
  setw(&c->d[2], alu_movew(c, W(c->d[2]) ^ W(c->d[1])));      /* eor.w d1,d2 */
  lift_charge(x, 0x1136A);
  setw(&c->d[1], alu_movew(c, W(c->d[1]) & W(c->d[2])));      /* and.w d2,d1 */
  lift_charge(x, 0x1136C);
  lift_charge(x, 0x1136E);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Controls_MaskAmbiguousDir (sub_112F0; called only from
 * Controls_DebounceDirection below)
 *   in:  d1 = caller's direction accumulator, d3 = raw direction bits
 *   out: d1 unchanged if the low nibble of d3 is zero or a single set
 *        bit; otherwise its low nibble is cleared (multiple bits set =
 *        an ambiguous/conflicting reading, so the accumulator is
 *        invalidated). d0/d4/d5 are used as scratch and restored to
 *        their entry values via the closing movem (matches the source's
 *        register list exactly).
 */
void Controls_MaskAmbiguousDir(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/d4-d5,-(sp): store order d5,d4,d0 (d0 lands lowest) */
  uint32_t saved[3] = { c->d[5], c->d[4], c->d[0] };
  for (int r = 0; r < 3; r++)
  {
    c->a[7] -= 4;
    lift_w32(x, c->a[7], saved[r]);
  }
  lift_charge_movem(x, 0x112F0);

  uint32_t d4 = alu_moveql(c, 3);               /* moveq #3,d4 */
  lift_charge(x, 0x112F4);
  uint32_t d0 = alu_movew(c, W(c->d[3]));       /* move.w d3,d0 */
  lift_charge(x, 0x112F6);
  d0 = alu_andw(c, 0xF, d0);                    /* and.w #$F,d0 */
  lift_charge(x, 0x112F8);
  int none = c->zf;
  lift_charge_bcc(x, 0x112FC, none);
  if (!none)
  {
    int matched = 0;
    for (;;)
    {
      uint32_t d5 = alu_movew(c, 0);            /* clr.w d5 */
      lift_charge(x, 0x11300);
      d5 = alu_bset(c, d5, (int)(d4 & 31));     /* bset d4,d5 */
      lift_charge(x, 0x11302);
      alu_cmpw(c, d5, d0);                      /* cmp.w d5,d0 */
      lift_charge(x, 0x11304);
      matched = c->zf;
      int taken = 0, expired = 0;
      if (!matched)
      {
        d4 = (d4 - 1) & 0xFFFFFFFFu;
        expired = (SW(d4) == -1);
        taken = !expired;
      }
      lift_charge_dbcc(x, 0x11306, taken, expired);   /* dbeq d4,loc_11300 */
      if (matched || expired) break;
    }
    lift_charge_bcc(x, 0x1130A, matched);       /* beq.w loc_11312 */
    if (!matched)
    {
      setw(&c->d[1], alu_andw(c, 0xFFF0, W(c->d[1])));  /* and.w #$FFF0,d1 */
      lift_charge(x, 0x1130E);
    }
  }

  /* movem.l (sp)+,d0/d4-d5: d0/d4/d5 were never written back to c->,
   * so restoring is just popping the dead stack transient. */
  c->a[7] += 12;
  lift_charge_movem(x, 0x11312);
  lift_charge(x, 0x11316);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define CTRL_DIR_REPEAT_TIMER 0xFFFFB042u   /* only referenced within this
                                              * routine; behaves like an
                                              * auto-repeat delay: reset to
                                              * $F on a fresh d2 edge, else
                                              * counts down and fires (d1 =
                                              * d3) + resets to 4 when it
                                              * goes negative */

/*
 * Controls_DebounceDirection (sub_11318; called from sub_7CF8+62 each
 * frame during gameplay)
 *   in:  d1 = caller's direction accumulator (see Controls_MaskAmbiguousDir),
 *        d2 = nonzero on a fresh press/edge, d3 = raw direction bits
 *   out: d1 = d3 on the frame the repeat timer fires; otherwise
 *        unchanged (aside from Controls_MaskAmbiguousDir's masking)
 * Bails immediately (no output) if d3's masked value is zero. On a
 * fresh edge (d2 != 0), resets the repeat timer to $F and returns
 * without firing. Otherwise counts the timer down by one each call;
 * while it stays >= 0, no output. Once it goes negative, resets it to
 * 4 (steady repeat interval) and copies d3 into d1 (a repeat fires).
 */
void Controls_DebounceDirection(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x11318, 2, Controls_MaskAmbiguousDir);   /* bsr.s */

  alu_movew(c, W(c->d[3]));                     /* tst.w d3 */
  lift_charge(x, 0x1131A);
  int idle = c->zf;
  lift_charge_bcc(x, 0x1131C, idle);
  if (idle)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_movew(c, W(c->d[2]));                     /* tst.w d2 */
  lift_charge(x, 0x11320);
  int fresh = !c->zf;
  lift_charge_bcc(x, 0x11322, fresh);
  if (fresh)
  {
    lift_w16(x, CTRL_DIR_REPEAT_TIMER, alu_movew(c, 0xF));  /* move.w #$F,(abs) */
    lift_charge(x, 0x11338);
    lift_charge(x, 0x1133E);                    /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, CTRL_DIR_REPEAT_TIMER,             /* subq.w #1,(abs).w */
           alu_subw(c, 1, lift_r16(x, CTRL_DIR_REPEAT_TIMER)));
  lift_charge(x, 0x11326);
  int stillWaiting = !c->nf;                     /* bpl */
  lift_charge_bcc(x, 0x1132A, stillWaiting);
  if (stillWaiting)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, CTRL_DIR_REPEAT_TIMER, alu_movew(c, 4));  /* move.w #4,(abs) */
  lift_charge(x, 0x1132E);
  setw(&c->d[1], alu_movew(c, W(c->d[3])));      /* move.w d3,d1 */
  lift_charge(x, 0x11334);
  lift_charge(x, 0x11336);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define PAD_STATE_BEF8 0xFFFFBEF8u   /* raw pad-direction byte, third controller/edge byte */
#define PAD_EDGE_BEF2  0xFFFFBEF2u   /* previous frame's mapped state, same source */

/*
 * Controls_ReadEdge_BEF8 (sub_11370; sibling of Controls_ReadEdge_BEF6/
 * BEF7, same pattern for $FFFFBEF8/$FFFFBEF2)
 *   out: d0 = 8-way direction (via Controls_MapDirection)
 *        d1 = bits newly asserted this frame vs. last (edge-detect)
 *        d2 = bits changed since last frame; d3 = this frame's raw state
 */
void Controls_ReadEdge_BEF8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, PAD_STATE_BEF8)));  /* move.b (abs),d0 */
  lift_charge(x, 0x11370);
  lift_call(x, 0x11374, 4, Controls_MapDirection);            /* bsr.w */
  setw(&c->d[2], alu_movew(c, lift_r16(x, PAD_EDGE_BEF2)));  /* move.w (abs),d2 */
  lift_charge(x, 0x11378);
  lift_w16(x, PAD_EDGE_BEF2, alu_movew(c, W(c->d[1])));      /* move.w d1,(abs) */
  lift_charge(x, 0x1137C);
  setw(&c->d[3], alu_movew(c, W(c->d[1])));                   /* move.w d1,d3 */
  lift_charge(x, 0x11380);
  setw(&c->d[2], alu_movew(c, W(c->d[2]) ^ W(c->d[1])));      /* eor.w d1,d2 */
  lift_charge(x, 0x11382);
  setw(&c->d[1], alu_movew(c, W(c->d[1]) & W(c->d[2])));      /* and.w d2,d1 */
  lift_charge(x, 0x11384);
  lift_charge(x, 0x11386);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define PAD_STATE_BEF9 0xFFFFBEF9u   /* raw pad-direction byte, fourth controller/edge byte */
#define PAD_EDGE_BEF4  0xFFFFBEF4u   /* previous frame's mapped state, same source */

/*
 * Controls_ReadEdge_BEF9 (sub_11388; sibling of Controls_ReadEdge_BEF6/
 * BEF7/BEF8, same pattern for $FFFFBEF9/$FFFFBEF4 — completes the
 * controls family)
 *   out: d0 = 8-way direction (via Controls_MapDirection)
 *        d1 = bits newly asserted this frame vs. last (edge-detect)
 *        d2 = bits changed since last frame; d3 = this frame's raw state
 */
void Controls_ReadEdge_BEF9(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, PAD_STATE_BEF9)));  /* move.b (abs),d0 */
  lift_charge(x, 0x11388);
  lift_call(x, 0x1138C, 4, Controls_MapDirection);            /* bsr.w */
  setw(&c->d[2], alu_movew(c, lift_r16(x, PAD_EDGE_BEF4)));  /* move.w (abs),d2 */
  lift_charge(x, 0x11390);
  lift_w16(x, PAD_EDGE_BEF4, alu_movew(c, W(c->d[1])));      /* move.w d1,(abs) */
  lift_charge(x, 0x11394);
  setw(&c->d[3], alu_movew(c, W(c->d[1])));                   /* move.w d1,d3 */
  lift_charge(x, 0x11398);
  setw(&c->d[2], alu_movew(c, W(c->d[2]) ^ W(c->d[1])));      /* eor.w d1,d2 */
  lift_charge(x, 0x1139A);
  setw(&c->d[1], alu_movew(c, W(c->d[1]) & W(c->d[2])));      /* and.w d2,d1 */
  lift_charge(x, 0x1139C);
  lift_charge(x, 0x1139E);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_112BC (jsr'd from ROM:7736 and ROM:112B4 — the per-frame pad
 * edge-read entry)
 *   out: d1 = OR of the newly-pressed direction bits across the active
 *        controllers — BEF6|BEF7 when $FFFFD046 == 0, all four
 *        (BEF6|BEF7|BEF8|BEF9) otherwise; d0/d2/d3 as left by the LAST
 *        Controls_ReadEdge_* call (its mapped direction / changed bits /
 *        raw state).
 * Each intermediate d1 is parked on the stack as a word push and OR'd
 * back in reverse order after the remaining reads.
 */
void sub_112BC(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_tstw(c, lift_r16(x, 0xFFFFD046u));           /* tst.w (D046).w */
  lift_charge(x, 0x112BC);
  int t = !c->zf;                                  /* bne.w loc_112D2 */
  lift_charge_bcc(x, 0x112C0, t);
  if (!t)
  {
    lift_call(x, 0x112C4, 4, Controls_ReadEdge_BEF6);   /* bsr.w sub_11340 */
    c->a[7] -= 2;                                  /* move.w d1,-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
    lift_charge(x, 0x112C8);
    lift_call(x, 0x112CA, 4, Controls_ReadEdge_BEF7);   /* bsr.w sub_11358 */
    {
      uint32_t v = lift_r16(x, c->a[7]); c->a[7] += 2;  /* or.w (sp)+,d1 */
      setw(&c->d[1], alu_orw(c, v, W(c->d[1])));
    }
    lift_charge(x, 0x112CE);
    lift_charge(x, 0x112D0);                       /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  /* loc_112D2: 2-controller path */
  lift_call(x, 0x112D2, 4, Controls_ReadEdge_BEF6);    /* bsr.w sub_11340 */
  c->a[7] -= 2;                                    /* move.w d1,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x112D6);
  lift_call(x, 0x112D8, 4, Controls_ReadEdge_BEF7);    /* bsr.w sub_11358 */
  c->a[7] -= 2;                                    /* move.w d1,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x112DC);
  lift_call(x, 0x112DE, 4, Controls_ReadEdge_BEF8);    /* bsr.w sub_11370 */
  c->a[7] -= 2;                                    /* move.w d1,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x112E2);
  lift_call(x, 0x112E4, 4, Controls_ReadEdge_BEF9);    /* bsr.w sub_11388 */
  {
    uint32_t v = lift_r16(x, c->a[7]); c->a[7] += 2;   /* or.w (sp)+,d1 */
    setw(&c->d[1], alu_orw(c, v, W(c->d[1])));
    lift_charge(x, 0x112E8);
    v = lift_r16(x, c->a[7]); c->a[7] += 2;            /* or.w (sp)+,d1 */
    setw(&c->d[1], alu_orw(c, v, W(c->d[1])));
    lift_charge(x, 0x112EA);
    v = lift_r16(x, c->a[7]); c->a[7] += 2;            /* or.w (sp)+,d1 */
    setw(&c->d[1], alu_orw(c, v, W(c->d[1])));
    lift_charge(x, 0x112EC);
  }
  lift_charge(x, 0x112EE);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Controls_ReadPadDispatch (sub_A41E; called from sub_7CF8+58, ROM:816E,
 * and sub_9FB8's input poll)
 *   in:  none
 *   out: whatever the selected Controls_ReadEdge_* sibling returns
 *        (d0 = 8-way direction, d1 = newly-pressed edge bits, d2 =
 *        changed bits, d3 = raw state)
 * Picks WHICH controller/edge slot the caller should read, from three
 * mode words: with ($FFFFD046).w clear (or ($FFFFC316).w zero) it is a
 * plain one-pad read, choosing BEF7 vs BEF6 on bit1 of the rink-flip
 * word ($FFFFC2EC); otherwise ($FFFFC316).w selects the second pair,
 * BEF8 when it is exactly 3 and BEF9 for any other non-zero value.
 * All four exits are tail calls (bra.w/bne.w into the sibling's entry),
 * so the sibling's own rts returns to this routine's caller.
 */
void Controls_ReadPadDispatch(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_tstw(c, lift_r16(x, 0xFFFFD046u));           /* tst.w ($FFFFD046).w */
  lift_charge(x, 0xA41E);
  int alt = !c->zf;
  lift_charge_bcc(x, 0xA422, alt);                 /* bne.w loc_A434 */

  if (alt)
  {
    /* loc_A434 */
    alu_tstw(c, lift_r16(x, 0xFFFFC316u));         /* tst.w ($FFFFC316).w */
    lift_charge(x, 0xA434);
    int zero = c->zf;
    lift_charge_bcc(x, 0xA438, zero);              /* beq.s loc_A426 */

    if (!zero)
    {
      alu_cmpw(c, 3, lift_r16(x, 0xFFFFC316u));    /* cmp.w #3,($FFFFC316).w */
      lift_charge(x, 0xA43A);
      int is3 = c->zf;
      lift_charge_bcc(x, 0xA440, is3);             /* beq.w sub_11370 */
      if (is3) { Controls_ReadEdge_BEF8(x); return; }

      lift_charge(x, 0xA444);                      /* bra.w sub_11388 */
      Controls_ReadEdge_BEF9(x);
      return;
    }
  }

  /* loc_A426 */
  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 1);         /* btst #1,($FFFFC2EC).w */
  lift_charge(x, 0xA426);
  int bit1 = !c->zf;
  lift_charge_bcc(x, 0xA42C, bit1);                /* bne.w sub_11358 */
  if (bit1) { Controls_ReadEdge_BEF7(x); return; }

  lift_charge(x, 0xA430);                          /* bra.w sub_11340 */
  Controls_ReadEdge_BEF6(x);
}

void sub_17572(lift_ctx *);   /* game.c — the $FFFFD5B0 consumer this falls into */

/*
 * Controls_LatchMenuStep (sub_17542) — wave 56. Reads BOTH pad edge slots
 * (Controls_ReadEdge_BEF6 then BEF7, the second one's d3 OR'd with the
 * first's, which is parked as a word push across the call) and turns the
 * combined raw bits into the menu step word at $FFFFD5B0: bit 3 sets it
 * to -2, bit 2 sets it to +2, neither leaves it alone. It then runs on
 * into sub_17572, which consumes $D5B0 — both by an explicit
 * `beq.w sub_17572` and by falling off the end, so sub_17572's rts
 * returns to OUR caller either way.
 *
 * Bit 7 of the combined bits takes the routine's own FUNCTION CHUNK at
 * loc_175D4 (blind spot 4 — 4 bytes, IDA-marked as belonging to this
 * routine, which is the whole reason triage only ever reported
 * "far-branches into mid-routine loc_175D4"). That chunk is a NON-LOCAL
 * EXIT: `addq.w #4,sp` throws away this routine's own return address and
 * the following rts returns to the CALLER'S caller, skipping the
 * $D5B0 step entirely.
 */
void Controls_LatchMenuStep(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;

  lift_call(x, 0x17542, 4, Controls_ReadEdge_BEF6);     /* bsr.w sub_11340 */
  if (x->declined) return;

  alu_movew(c, W(c->d[3]));                     /* move.w d3,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[3]));
  lift_charge(x, 0x17546);

  lift_call(x, 0x17548, 4, Controls_ReadEdge_BEF7);     /* bsr.w sub_11358 */
  if (x->declined) return;

  v = lift_r16(x, c->a[7]);                     /* or.w (sp)+,d3 */
  c->a[7] += 2;
  setw(&c->d[3], alu_orw(c, v, W(c->d[3])));
  lift_charge(x, 0x1754C);

  alu_btst(c, c->d[3], 7);                      /* btst #7,d3 */
  lift_charge(x, 0x1754E);
  lift_charge_bcc(x, 0x17552, !c->zf);          /* bne.w loc_175D4 */
  if (!c->zf)
  {
    /* loc_175D4 — the routine's own chunk: drop our return address and
     * rts to the caller's caller. */
    c->a[7] += 4;                               /* addq.w #4,sp */
    lift_charge(x, 0x175D4);
    lift_charge(x, 0x175D6);                    /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, c->d[3], 3);                      /* btst #3,d3 */
  lift_charge(x, 0x17556);
  lift_charge_bcc(x, 0x1755A, c->zf);           /* beq.w loc_17564 */
  if (!c->zf)
  {
    alu_movew(c, 0xFFFE);                       /* move.w #$FFFE,($D5B0).w */
    lift_w16(x, 0xFFD5B0u, 0xFFFE);
    lift_charge(x, 0x1755E);
  }

  /* loc_17564 */
  alu_btst(c, c->d[3], 2);                      /* btst #2,d3 */
  lift_charge(x, 0x17564);
  lift_charge_bcc(x, 0x17568, c->zf);           /* beq.w sub_17572 — tail */
  if (!c->zf)
  {
    alu_movew(c, 2);                            /* move.w #2,($D5B0).w */
    lift_w16(x, 0xFFD5B0u, 2);
    lift_charge(x, 0x1756C);
  }
  sub_17572(x);                                 /* branched to, or fallen into */
}
