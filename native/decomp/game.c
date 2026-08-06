/*
 * game.c — in-game logic helpers (lifted).
 */
#include "util68k.h"

void SRAM_ReadBytes(lift_ctx *);  /* save.c */
void SRAM_WriteBytes(lift_ctx *);  /* save.c */
void Anim_SetScript(lift_ctx *);  /* anim.c */
void Anim_StartScript46A(lift_ctx *);  /* anim.c */
void Text_WriteNullThenByteFwd(lift_ctx *);  /* overlay.c */
void Text_WriteNullThenByteBack(lift_ctx *);  /* overlay.c */
void Math_SqrtU32(lift_ctx *);  /* math.c */
void Rng_NextScaled(lift_ctx *);  /* math.c */
void sub_1803E(lift_ctx *);  /* render.c */
void sub_17D80(lift_ctx *);  /* render.c */
void Piece_AdvanceChain(lift_ctx *);  /* render.c */
void Roster_CountLeadingNibbles(lift_ctx *);   /* $9F40, defined below */
void Roster_CountLineEntries(lift_ctx *);      /* $9F9A, defined below */
void Roster_CacheBothNibbleCounts(lift_ctx *); /* $F9FC0, defined below */
void Text_WriteTwoDigits(lift_ctx *);  /* overlay.c */
void Text_AlignBufferEven(lift_ctx *);  /* overlay.c */
void Text_AppendString(lift_ctx *);  /* overlay.c */
void Text_AppendInlineString(lift_ctx *);  /* overlay.c */
void Object_ScaleImpulseByObjectField(lift_ctx *);  /* forward: defined below in this file */
void Object_ProjectScreenColumn(lift_ctx *);  /* forward: defined below in this file */

#define TEAM_HOME  0xFFFFC6CEu   /* in-game team state block */
#define TEAM_SIZE  0x364u        /* away block follows the home block */

#define OCTANT_TABLE 0x106D0u    /* ROM: 4-bit sign/magnitude mask -> octant */

/*
 * Vector_ToOctant (sub_10676; called from sub_9FD0, sub_B0E8, and others —
 * AI movement/facing logic)
 *   in:  d0 = dx, d1 = dy
 *   out: d0 = octant index 0-7 from OCTANT_TABLE, or 8 if dx==dy==0;
 *        d1/d2/a0 clobbered
 *
 * Builds a 4-bit index (bit0 = dx negative, bit1 = dy negative, bit2 =
 * |dx| > 2*|dy|, bit3 = |dy|/2 > |dx|) from the signs and relative
 * magnitudes of dx/dy, then looks it up in the 16-entry byte table at
 * $106D0 to get the octant (exact geometry TBD; behaviour preserved
 * bit-for-bit regardless).
 */
void Vector_ToOctant(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_a0 = c->a[0], saved_d2 = c->d[2];

  /* movem.l d2/a0,-(sp): a0 pushed first, d2 lands lowest. Staged as
   * word pairs (not one lift_w32 each) so a later narrower overlapping
   * write to this same stack depth (e.g. a composed callee's own word
   * push landing here after this routine returns) can supersede it
   * exactly instead of leaving a stale wide prediction — see CLAUDE.md
   * write-log "superseded" limitation. */
  sp -= 4;
  lift_w16(x, sp, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[0] & 0xFFFF);
  sp -= 4;
  lift_w16(x, sp, (c->d[2] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[2] & 0xFFFF);
  lift_charge_movem(x, 0x10676);

  setw(&c->d[2], alu_movew(c, W(c->d[0])));       /* move.w d0,d2 */
  lift_charge(x, 0x1067A);
  setw(&c->d[2], alu_movew(c, W(c->d[2]) | W(c->d[1])));  /* or.w d1,d2 */
  lift_charge(x, 0x1067C);
  int zero = c->zf;                               /* beq */
  lift_charge_bcc(x, 0x1067E, zero);

  if (zero)
  {
    c->d[0] = alu_moveql(c, 8);                   /* moveq #8,d0 */
    lift_charge(x, 0x106C8);
    sp += 4; sp += 4;                             /* movem.l (sp)+,d2/a0 */
    lift_charge_movem(x, 0x106CA);
    lift_charge(x, 0x106CE);                      /* rts */
    c->d[2] = saved_d2;
    c->a[0] = saved_a0;
    c->pc = lift_r32(x, sp) & 0xFFFFFF;
    c->a[7] = sp + 4;
    return;
  }
  {
    setw(&c->d[2], alu_movew(c, 0));              /* clr.w d2 */
    lift_charge(x, 0x10682);
    alu_movew(c, W(c->d[0]));                     /* tst.w d0 */
    lift_charge(x, 0x10684);
    int neg = c->nf;                              /* bpl (inverted) */
    lift_charge_bcc(x, 0x10686, !neg);
    if (neg)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));
      lift_charge(x, 0x1068A);
      setw(&c->d[2], alu_bset(c, W(c->d[2]), 0));
      lift_charge(x, 0x1068C);
    }

    alu_movew(c, W(c->d[1]));                     /* tst.w d1 */
    lift_charge(x, 0x10690);
    neg = c->nf;
    lift_charge_bcc(x, 0x10692, !neg);
    if (neg)
    {
      setw(&c->d[1], alu_negw(c, W(c->d[1])));
      lift_charge(x, 0x10696);
      setw(&c->d[2], alu_bset(c, W(c->d[2]), 1));
      lift_charge(x, 0x10698);
    }

    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 1));    /* asl.w #1,d1 */
    lift_charge(x, 0x1069C);
    alu_cmpw(c, W(c->d[1]), W(c->d[0]));           /* cmp.w d1,d0 */
    lift_charge(x, 0x1069E);
    int hi = (!c->cf && !c->zf);                   /* bhi */
    lift_charge_bcc(x, 0x106A0, hi);
    if (!hi)
    {
      setw(&c->d[2], alu_bset(c, W(c->d[2]), 2));
      lift_charge(x, 0x106A4);
    }

    setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 1));    /* lsr.w #1,d1 */
    lift_charge(x, 0x106A8);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));    /* asl.w #1,d0 */
    lift_charge(x, 0x106AA);
    alu_cmpw(c, W(c->d[0]), W(c->d[1]));           /* cmp.w d0,d1 */
    lift_charge(x, 0x106AC);
    hi = (!c->cf && !c->zf);                       /* bhi */
    lift_charge_bcc(x, 0x106AE, hi);
    if (!hi)
    {
      setw(&c->d[2], alu_bset(c, W(c->d[2]), 3));
      lift_charge(x, 0x106B2);
    }

    c->a[0] = OCTANT_TABLE;                        /* move.l #imm,a0: no flags */
    lift_charge(x, 0x106B6);
    setw(&c->d[0], alu_movew(c, 0));               /* clr.w d0 */
    lift_charge(x, 0x106BC);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[2]))));  /* move.b (a0,d2.w),d0 */
    lift_charge(x, 0x106BE);
  }

  sp += 4;                                        /* movem.l (sp)+,d2/a0: */
  sp += 4;                                        /* d2 restored first, then a0 */
  lift_charge_movem(x, 0x106C2);
  lift_charge(x, 0x106C6);                        /* rts */
  c->d[2] = saved_d2;
  c->a[0] = saved_a0;
  c->pc = lift_r32(x, sp) & 0xFFFFFF;
  c->a[7] = sp + 4;
}

#define WRAP_PERIOD 0x96u

/*
 * Vector_WrapCounters (sub_10E1A; called from sub_D51C, sub_B0E8, and other
 * gameplay update loops)
 *   in/out: $28(a3), $2A(a3) — two 16-bit counters, each wrapped/clamped
 *           against a $96 (150) period, one field after the other
 *
 * For each field: if negative, add the period; if that's still negative,
 * skip straight to the next field (leaving the added-but-still-negative
 * value); otherwise clamp the (now non-negative) field to 0. Then
 * unconditionally subtract the period, clamping to 0 if that goes
 * negative. The second field's two clamp-taken exits leave via the shared
 * far rts instead of the local rts. Exact tween/counter role TBD;
 * behaviour preserved bit-for-bit.
 */
void Vector_WrapCounters(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_movew(c, lift_r16(x, a3 + 0x28));           /* tst.w $28(a3) */
  lift_charge(x, 0x10E1A);
  int neg = c->nf;
  lift_charge_bcc(x, 0x10E1E, !neg);
  int do_sub1 = 1;
  if (neg)
  {
    lift_w16(x, a3 + 0x28, alu_addw(c, WRAP_PERIOD, lift_r16(x, a3 + 0x28)));
    lift_charge(x, 0x10E22);
    int stillneg = c->nf;
    lift_charge_bcc(x, 0x10E28, stillneg);
    if (stillneg)
    {
      do_sub1 = 0;
    }
    else
    {
      lift_w16(x, a3 + 0x28, alu_movew(c, 0));
      lift_charge(x, 0x10E2C);
    }
  }
  if (do_sub1)
  {
    lift_w16(x, a3 + 0x28, alu_subw(c, WRAP_PERIOD, lift_r16(x, a3 + 0x28)));
    lift_charge(x, 0x10E30);
    int nonneg = !c->nf;
    lift_charge_bcc(x, 0x10E36, nonneg);
    if (!nonneg)
    {
      lift_w16(x, a3 + 0x28, alu_movew(c, 0));
      lift_charge(x, 0x10E3A);
    }
  }

  alu_movew(c, lift_r16(x, a3 + 0x2A));           /* tst.w $2A(a3) */
  lift_charge(x, 0x10E3E);
  neg = c->nf;
  lift_charge_bcc(x, 0x10E42, !neg);
  if (neg)
  {
    lift_w16(x, a3 + 0x2A, alu_addw(c, WRAP_PERIOD, lift_r16(x, a3 + 0x2A)));
    lift_charge(x, 0x10E46);
    int stillneg = c->nf;
    lift_charge_bcc(x, 0x10E4C, stillneg);
    if (stillneg)
    {
      lift_charge(x, 0x15464);                    /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    lift_w16(x, a3 + 0x2A, alu_movew(c, 0));
    lift_charge(x, 0x10E50);
  }

  lift_w16(x, a3 + 0x2A, alu_subw(c, WRAP_PERIOD, lift_r16(x, a3 + 0x2A)));
  lift_charge(x, 0x10E54);
  int nonneg = !c->nf;
  lift_charge_bcc(x, 0x10E5A, nonneg);
  if (nonneg)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_w16(x, a3 + 0x2A, alu_movew(c, 0));
  lift_charge(x, 0x10E5E);

  lift_charge(x, 0x10E62);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_SelectBlocks (sub_13040; called from all over the gameplay logic)
 *   in:  a3 = on-ice object
 *   out: a2 = the object's team state block, a1 = the opponent's;
 *        Z = bit 6 of $62(a3) clear (i.e. Z set for the home team)
 * Bit 6 of the object's status byte $62 says which side it plays for.
 * The home path leaves through the shared far rts at $15464.
 */
void Team_SelectBlocks(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = TEAM_HOME;                            /* movea.w sign-extends */
  lift_charge(x, 0x13040);
  c->a[1] = c->a[2] + TEAM_SIZE;                  /* lea: no flags */
  lift_charge(x, 0x13044);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);
  lift_charge(x, 0x13048);
  int home = c->zf;
  lift_charge_bcc(x, 0x1304E, home);
  if (home)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
  }
  else
  {
    uint32_t t = c->a[1];                         /* exg a1,a2 */
    c->a[1] = c->a[2];
    c->a[2] = t;
    lift_charge(x, 0x13052);
    lift_charge(x, 0x13054);                      /* rts */
  }
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_StatWord (sub_1575A; called constantly from gameplay logic)
 *   in:  a3 = on-ice object
 *   out: a2 = the object's team state block (selected the same way as
 *        Team_SelectBlocks); d0 = word at team_block + $32 + 2*$66(a3) —
 *        a per-roster-slot stat table indexed by the byte at $66(a3).
 */
void Team_StatWord(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  c->a[2] = TEAM_HOME;                            /* movea.w sign-extends */
  lift_charge(x, 0x1575A);
  alu_btst(c, lift_r8(x, a3 + 0x62), 6);
  lift_charge(x, 0x1575E);
  int home = c->zf;
  lift_charge_bcc(x, 0x15764, home);
  if (!home)
  {
    c->a[2] += TEAM_SIZE;                         /* adda.w: no flags */
    lift_charge(x, 0x15768);
  }
  setb(&c->d[1], alu_moveb(c, lift_r8(x, a3 + 0x66)));  /* move.b $66(a3),d1 */
  lift_charge(x, 0x1576C);
  setw(&c->d[1], alu_extw(c, W(c->d[1])));              /* ext.w d1 */
  lift_charge(x, 0x15770);
  setw(&c->d[1], alu_addw(c, W(c->d[1]), W(c->d[1])));  /* add.w d1,d1 */
  lift_charge(x, 0x15772);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x32 + SW(c->d[1]))));
  lift_charge(x, 0x15774);
  lift_charge(x, 0x15778);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_SNAP_HALT    0xFFFFC2EAu  /* bit 4: recording halted */
#define R_SNAP_ARM     0xFFFFC2EEu  /* bit 2: recording armed */
#define R_SNAP_DELAY   0xFFFFDEF0u  /* frames until recording starts (counts down once) */
#define R_SNAP_CURSOR  0xFFFFB036u  /* long: current write pointer into the RAM snapshot buffer */
#define R_SNAP_END     0xFFFFAF54u  /* sentinel cursor value: buffer full, wrap + latch */
#define R_SNAP_DONE    0xFFFFC2ECu  /* bit 4: buffer wrap latched */
#define R_SNAP_STRIDE  0x62u        /* bytes per recorded frame */
#define PLAYER_STRIDE  0x80u

/*
 * Game_RecordSnapshot (sub_A8CA; called from sub_794E)
 * When armed (and not halted), advances the snapshot cursor at
 * R_SNAP_CURSOR by $62 bytes each frame (after an optional startup
 * delay counted down at R_SNAP_DELAY); once the cursor reaches
 * R_SNAP_END it latches R_SNAP_DONE and wraps back to $FFFF0000.
 * Writes one $62-byte frame at the cursor: for each of the 16 player
 * slots at $FFB04A, a packed 4-byte record (facing-derived index +
 * world-Y/attr/frame bits), then a run of misc single-byte/word global
 * state, then packed nibble pairs of jersey-number-low-nibble ($6F) and
 * a clamped $34 value for 12 more player slots, then camera position.
 * Exact per-field meaning TBD; behaviour preserved bit-for-bit.
 */
void Game_RecordSnapshot(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, R_SNAP_HALT), 4);          /* btst #4,(abs) */
  lift_charge(x, 0xA8CA);
  int halted = !c->zf;
  lift_charge_bcc(x, 0xA8D0, halted);
  if (halted)
  {
    lift_charge(x, 0xA9D4);                          /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, R_SNAP_ARM), 2);            /* btst #2,(abs) */
  lift_charge(x, 0xA8D4);
  int armed = c->zf;                                  /* beq.w loc_A8F6 */
  lift_charge_bcc(x, 0xA8DA, armed);

  int advance = 1;
  if (!armed)
  {
    alu_movew(c, lift_r16(x, R_SNAP_DELAY));          /* tst.w (abs) */
    lift_charge(x, 0xA8DE);
    int noDelay = c->zf;                               /* beq.w loc_A918 */
    lift_charge_bcc(x, 0xA8E2, noDelay);
    if (noDelay)
    {
      advance = 0;
    }
    else
    {
      uint32_t d = alu_subw(c, 1, lift_r16(x, R_SNAP_DELAY));  /* subq.w #1,(abs) */
      lift_w16(x, R_SNAP_DELAY, d);
      lift_charge(x, 0xA8E6);
      int stillPos = !c->nf;                            /* bpl.w loc_A8F6 */
      lift_charge_bcc(x, 0xA8EA, stillPos);
      if (stillPos)
      {
        advance = 0;
      }
      else
      {
        lift_w16(x, R_SNAP_DELAY, alu_movew(c, 0));      /* clr.w (abs) */
        lift_charge(x, 0xA8EE);
        lift_charge(x, 0xA8F2);                          /* bra.w loc_A918 */
      }
    }
  }

  if (advance)
  {
    /* loc_A8F6 */
    uint32_t cur = alu_addl(c, 0x62, lift_r32(x, R_SNAP_CURSOR));  /* add.l #$62,(abs) */
    lift_w32(x, R_SNAP_CURSOR, cur);
    lift_charge(x, 0xA8F6);

    alu_cmpl(c, R_SNAP_END, cur);                       /* cmp.l #$FFFFAF54,(abs) */
    lift_charge(x, 0xA8FE);
    int full = c->zf;                                    /* bne.w loc_A918 */
    lift_charge_bcc(x, 0xA906, !full);
    if (full)
    {
      uint32_t byte = alu_bset(c, lift_r8(x, R_SNAP_DONE), 4);  /* bset #4,(abs) */
      lift_w8(x, R_SNAP_DONE, byte);
      lift_charge(x, 0xA90A);
      lift_w32(x, R_SNAP_CURSOR, alu_movel(c, 0xFFFF0000));  /* move.l #$FFFF0000,(abs) */
      lift_charge(x, 0xA910);
    }
  }

  /* loc_A918 */
  c->a[0] = lift_r32(x, R_SNAP_CURSOR);                  /* move.l (abs),a0 */
  lift_charge(x, 0xA918);
  c->d[2] = alu_moveql(c, 0xF);                           /* moveq #$F,d2 */
  lift_charge(x, 0xA91C);
  c->a[3] = 0xFFFFB04A;                                    /* movea.w #$B04A,a3 */
  lift_charge(x, 0xA91E);

  for (;;)
  {
    alu_movel(c, 0);                                        /* clr.l (a0) */
    lift_w16(x, c->a[0], 0);       /* split into words: the later or.l/or.w */
    lift_w16(x, c->a[0] + 2, 0);   /* writes only supersede at word granularity */
    lift_charge(x, 0xA922);
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3])));    /* move.w (a3),d1 */
    lift_charge(x, 0xA924);
    setw(&c->d[1], alu_andw(c, 0x3FF, W(c->d[1])));        /* and.w #$3FF,d1 */
    lift_charge(x, 0xA926);
    lift_w16(x, c->a[0] + 2, alu_movew(c, W(c->d[1])));    /* move.w d1,2(a0) */
    lift_charge(x, 0xA92A);

    c->d[1] = alu_movel(c, 0);                              /* clr.l d1 */
    lift_charge(x, 0xA92E);
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d1 */
    lift_charge(x, 0xA930);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 6));             /* asl.w #6,d1 */
    lift_charge(x, 0xA934);
    c->d[1] = alu_asll(c, c->d[1], 4);                       /* asl.l #4,d1 */
    lift_charge(x, 0xA936);
    {
      uint32_t r = lift_r32(x, c->a[0]) | c->d[1];           /* or.l d1,(a0) */
      alu_tstl(c, r);
      lift_w16(x, c->a[0], (r >> 16) & 0xFFFF);      /* split: the later or.w */
      lift_w16(x, c->a[0] + 2, r & 0xFFFF);          /* writes hit the upper word only */
    }
    lift_charge(x, 0xA938);

    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 6)));  /* move.w 6(a3),d1 */
    lift_charge(x, 0xA93A);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));               /* asl.w #4,d1 */
    lift_charge(x, 0xA93E);
    {
      uint32_t r = alu_movew(c, lift_r16(x, c->a[0]) | W(c->d[1]));  /* or.w d1,(a0) */
      lift_w16(x, c->a[0], r);
    }
    lift_charge(x, 0xA940);

    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 4)));  /* move.w 4(a3),d1 */
    lift_charge(x, 0xA942);
    setw(&c->d[1], alu_andw(c, 0x1800, W(c->d[1])));          /* and.w #$1800,d1 */
    lift_charge(x, 0xA946);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 3));                /* asl.w #3,d1 */
    lift_charge(x, 0xA94A);
    {
      uint32_t r = alu_movew(c, lift_r16(x, c->a[0]) | W(c->d[1]));  /* or.w d1,(a0) */
      lift_w16(x, c->a[0], r);
    }
    lift_charge(x, 0xA94C);

    c->a[0] += 4;                                             /* addq.w #4,a0 */
    lift_charge(x, 0xA94E);
    c->a[3] += PLAYER_STRIDE;                                 /* add.w #$80,a3 */
    lift_charge(x, 0xA950);

    uint32_t nd2 = W(W(c->d[2]) - 1);                          /* dbf d2,loc_A922 */
    setw(&c->d[2], nd2);
    int taken = (nd2 != 0xFFFF);
    lift_charge_dbcc(x, 0xA954, taken, !taken);
    if (!taken) break;
  }

  c->d[2] = alu_moveql(c, 5);                                  /* moveq #5,d2 */
  lift_charge(x, 0xA958);
  c->a[3] = 0xFFFFB04A;                                         /* movea.w #$B04A,a3 */
  lift_charge(x, 0xA95A);

  for (;;)
  {
    lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, c->a[3] + 0x6F)));  /* move.b $6F(a3),(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xA95E);

    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x34)));  /* move.w $34(a3),d0 */
    lift_charge(x, 0xA962);
    int ok = !c->nf;                                             /* bpl.w loc_A96C */
    lift_charge_bcc(x, 0xA966, ok);
    if (!ok)
    {
      c->d[0] = alu_moveql(c, 0xF);                              /* moveq #$F,d0 */
      lift_charge(x, 0xA96A);
    }

    setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));                /* and.w #$F,d0 */
    lift_charge(x, 0xA96C);
    lift_w8(x, c->a[0], alu_moveb(c, W(c->d[0])));                /* move.b d0,(a0) */
    lift_charge(x, 0xA970);
    c->a[3] += PLAYER_STRIDE;                                     /* add.w #$80,a3 */
    lift_charge(x, 0xA972);

    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3] + 0x6F)));      /* move.b $6F(a3),d0 */
    lift_charge(x, 0xA976);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));                    /* asl.w #4,d0 */
    lift_charge(x, 0xA97A);
    {
      uint32_t r = alu_moveb(c, lift_r8(x, c->a[0]) | (W(c->d[0]) & 0xFF));  /* or.b d0,(a0)+ */
      lift_w8(x, c->a[0], r);
      c->a[0] += 1;
    }
    lift_charge(x, 0xA97C);

    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3] + 0x6F)));      /* move.b $6F(a3),d0 */
    lift_charge(x, 0xA97E);
    setb(&c->d[0], alu_lsrb(c, W(c->d[0]), 4));                    /* lsr.b #4,d0 */
    lift_charge(x, 0xA982);
    lift_w8(x, c->a[0], alu_moveb(c, W(c->d[0])));                  /* move.b d0,(a0) */
    lift_charge(x, 0xA984);

    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x34)));      /* move.w $34(a3),d0 */
    lift_charge(x, 0xA986);
    ok = !c->nf;                                                     /* bpl.w loc_B990 */
    lift_charge_bcc(x, 0xA98A, ok);
    if (!ok)
    {
      c->d[0] = alu_moveql(c, 0xF);                                  /* moveq #$F,d0 */
      lift_charge(x, 0xA98E);
    }

    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));                       /* asl.w #4,d0 */
    lift_charge(x, 0xA990);
    {
      uint32_t r = alu_moveb(c, lift_r8(x, c->a[0]) | (W(c->d[0]) & 0xFF));  /* or.b d0,(a0)+ */
      lift_w8(x, c->a[0], r);
      c->a[0] += 1;
    }
    lift_charge(x, 0xA992);

    c->a[3] += PLAYER_STRIDE;                                         /* add.w #$80,a3 */
    lift_charge(x, 0xA994);

    uint32_t nd2b = W(W(c->d[2]) - 1);                                 /* dbf d2,loc_A95E */
    setw(&c->d[2], nd2b);
    int taken2 = (nd2b != 0xFFFF);
    lift_charge_dbcc(x, 0xA998, taken2, !taken2);
    if (!taken2) break;
  }

  lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, 0xFFFFB763)));  /* move.b (abs),(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA99C);
  lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, 0xFFFFB7E3)));  /* move.b (abs),(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA9A0);
  lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, 0xFFFFC017)));  /* move.b (abs),(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA9A4);
  lift_w8(x, 0xFFFFC017, alu_bset(c, lift_r8(x, 0xFFFFC017), 7));  /* bset #7,(abs) */
  lift_charge(x, 0xA9A8);
  lift_w8(x, c->a[0], alu_moveb(c, W(c->d[7])));               /* move.b d7,(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA9AE);
  lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, 0xFFFFBE78)));  /* move.w (abs),(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xA9B0);
  lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, 0xFFFFB89A)));  /* move.w (abs),(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xA9B4);
  lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, 0xFFFFBEE2)));  /* move.w (abs),(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xA9B8);
  lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, 0xFFFFC3EA)));   /* move.b (abs),(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA9BC);

  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, W(c->d[0])));  /* move.w d0,-(sp) */
  lift_charge(x, 0xA9C0);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBE86)));       /* move.w (abs),d0 */
  lift_charge(x, 0xA9C2);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));                   /* lsr.w #4,d0 */
  lift_charge(x, 0xA9C6);
  lift_w8(x, c->a[0], alu_moveb(c, W(c->d[0])));                /* move.b d0,(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0xA9C8);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));            /* move.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0xA9CA);

  lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, 0xFFFFBD1C)));   /* move.w (abs),(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xA9CC);
  lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, 0xFFFFBD18)));   /* move.w (abs),(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xA9D0);

  lift_charge(x, 0xA9D4);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_UNK_C2FE_B1  0xFFFFC2FEu  /* bit 1: set unconditionally on this path (dead alternate clears it) */

/*
 * Flag_SetC2FEBit1 (sub_FEF66; called from sub_A9D6)
 * The routine unconditionally branches over a bclr/bra pair (dead code —
 * nothing else jumps into it) straight to a bset, so it always sets bit 1
 * of $FFFFC2FE. Exact flag role TBD; behaviour preserved bit-for-bit.
 */
void Flag_SetC2FEBit1(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge(x, 0xFEF66);                          /* bra.w loc_FEF74 */

  /* loc_FEF74 */
  lift_w8(x, R_UNK_C2FE_B1, alu_bset(c, lift_r8(x, R_UNK_C2FE_B1), 1));  /* bset #1,(abs) */
  lift_charge(x, 0xFEF74);

  lift_charge(x, 0xFEF7A);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* nullsub_3 (sub_F84F8; called from sub_F8304) — rts-only stub. */
void Nullsub_F84F8(lift_ctx *x)
{
  rcpu_t *c = x->c;
  lift_charge(x, 0xF84F8);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* nullsub_1 (sub_1454A; called from sub_13A1A) — rts-only stub. */
void Nullsub_1454A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  lift_charge(x, 0x1454A);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* nullsub_2 (sub_F6E38; called from sub_159A8) — rts-only stub. */
void Nullsub_F6E38(lift_ctx *x)
{
  rcpu_t *c = x->c;
  lift_charge(x, 0xF6E38);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_TestStatFlag (sub_FEFCC; called from sub_C2F2 and directly at
 * $CD40)
 *   in:  a3 = on-ice object
 *   out: Z/N set from the object's team block's word at +$26 (home/away
 *        selected the same way as Team_SelectBlocks); a1 clobbered then
 *        restored via the movem
 */
void Team_TestStatFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_a1 = c->a[1];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);       /* movem.l a1,-(sp) */
  lift_charge_movem(x, 0xFEFCC);

  c->a[1] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a1 */
  lift_charge(x, 0xFEFD0);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);          /* btst #6,$62(a3) */
  lift_charge(x, 0xFEFD6);
  int home = c->zf;                                     /* beq.w loc_FEFE6 */
  lift_charge_bcc(x, 0xFEFDC, home);
  if (!home)
  {
    c->a[1] = TEAM_HOME + TEAM_SIZE;                    /* movea.l #$FFFFCA32,a1 */
    lift_charge(x, 0xFEFE0);
  }

  /* loc_FEFE6 */
  alu_movew(c, lift_r16(x, c->a[1] + 0x26));           /* tst.w $26(a1) */
  lift_charge(x, 0xFEFE6);

  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;         /* movem.l (sp)+,a1 */
  lift_charge_movem(x, 0xFEFEA);

  lift_charge(x, 0xFEFEE);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}


#define AI_SCAN_TOP 0xFFFFB5CAu  /* PLAYER_TABLE + 11*$80: last of 11 scanned slots */
#define AI_SCAN_STRIDE 0x80u

/*
 * AI_CheckLaneBlocked (sub_DE2A; called from sub_DEEE and sub_E6CE)
 *   in:  a3 = on-ice object
 *   out: d0 = 1 if a clear scan found no blocker, 0 otherwise; $64(a3)
 *        bit0/1 updated to reflect the result
 *
 * Gate: if $62(a3) bit1 is set, clear $64(a3) bits 0/1 (result path
 * below still runs regardless). Bails with d0=0 if |world Y| isn't
 * smaller than $108. Compares $58 (sign flipped per $62 bit7) against
 * world Y to decide whether to set $64(a3) bit0 (near — bail d0=0) or
 * clear it and, if it was already clear, scan up to 11 player slots
 * (from $FFFFB5CA backward by $80) for one whose world Y is beyond d1
 * (direction by d1's sign) with a nonzero $34 field — found = bail
 * d0=0, clean scan = set $64(a3) bit1 and return d0=1. Exact role TBD
 * (looks like an AI passing-lane/marking check); behaviour preserved
 * bit-for-bit.
 */
void AI_CheckLaneBlocked(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_btst(c, lift_r8(x, a3 + 0x62), 1);             /* btst #1,$62(a3) */
  lift_charge(x, 0xDE2A);
  int beqTaken = c->zf;                                /* beq.w loc_DE40 */
  lift_charge_bcc(x, 0xDE30, beqTaken);
  if (!beqTaken)
  {
    lift_w8(x, a3 + 0x64, alu_bclr(c, lift_r8(x, a3 + 0x64), 0));  /* bclr #0,$64(a3) */
    lift_charge(x, 0xDE34);
    lift_w8(x, a3 + 0x64, alu_bclr(c, lift_r8(x, a3 + 0x64), 1));  /* bclr #1,$64(a3) */
    lift_charge(x, 0xDE3A);
  }

  /* loc_DE40 */
  {
    uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_a0 = c->a[0];
    c->a[7] -= 2; lift_w16(x, c->a[7], saved_a0);      /* movem.w d0-d1/a0,-(sp): a0,d1,d0 */
    c->a[7] -= 2; lift_w16(x, c->a[7], saved_d1);
    c->a[7] -= 2; lift_w16(x, c->a[7], saved_d0);
  }
  lift_charge_movem(x, 0xDE40);

  setw(&c->d[0], alu_movew(c, 0x108));                 /* move.w #$108,d0 */
  lift_charge(x, 0xDE44);
  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x14))); /* move.w $14(a3),d1 */
  lift_charge(x, 0xDE48);
  int nonneg = !c->nf;                                  /* bpl.w loc_DE52 */
  lift_charge_bcc(x, 0xDE4C, nonneg);
  if (!nonneg)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));            /* neg.w d1 */
    lift_charge(x, 0xDE50);
  }

  /* loc_DE52 */
  alu_cmpw(c, W(c->d[0]), W(c->d[1]));                  /* cmp.w d0,d1 */
  lift_charge(x, 0xDE52);
  int farOut = (c->nf == c->vf);                        /* bge.w loc_DECC */
  lift_charge_bcc(x, 0xDE54, farOut);
  if (farOut) goto loc_DECC;

  setw(&c->d[0], alu_movew(c, 0x58));                   /* move.w #$58,d0 */
  lift_charge(x, 0xDE58);
  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x14))); /* move.w $14(a3),d1 */
  lift_charge(x, 0xDE5C);
  alu_btst(c, lift_r8(x, a3 + 0x62), 7);                /* btst #7,$62(a3) */
  lift_charge(x, 0xDE60);
  {
    int side = !c->zf;                                    /* bne.w loc_DE76 */
    lift_charge_bcc(x, 0xDE66, side);
    if (!side)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));            /* neg.w d0 */
      lift_charge(x, 0xDE6A);
      alu_cmpw(c, W(c->d[1]), W(c->d[0]));                /* cmp.w d1,d0 */
      lift_charge(x, 0xDE6C);
      int lt = (c->nf != c->vf);                           /* blt.w loc_DE7C */
      lift_charge_bcc(x, 0xDE6E, lt);
      if (lt) goto loc_DE7C;
      lift_charge(x, 0xDE72);                             /* bra.w loc_DE86 */
      goto loc_DE86;
    }
    /* loc_DE76 */
    alu_cmpw(c, W(c->d[1]), W(c->d[0]));                  /* cmp.w d1,d0 */
    lift_charge(x, 0xDE76);
    int lt = (c->nf != c->vf);                             /* blt.w loc_DE86 */
    lift_charge_bcc(x, 0xDE78, lt);
    if (lt) goto loc_DE86;
    /* falls through to loc_DE7C */
  }

loc_DE7C:
  lift_w8(x, a3 + 0x64, alu_bset(c, lift_r8(x, a3 + 0x64), 0));  /* bset #0,$64(a3) */
  lift_charge(x, 0xDE7C);
  lift_charge(x, 0xDE82);                               /* bra.w loc_DECC */
  goto loc_DECC;

loc_DE86:
  lift_w8(x, a3 + 0x64, alu_bclr(c, lift_r8(x, a3 + 0x64), 0));  /* bclr #0,$64(a3) */
  lift_charge(x, 0xDE86);
  {
    int wasClear = c->zf;                                  /* beq.w loc_DECC */
    lift_charge_bcc(x, 0xDE8C, wasClear);
    if (wasClear) goto loc_DECC;
  }

  c->a[0] = AI_SCAN_TOP;                                 /* movea.l #$FFFFB5CA,a0 */
  lift_charge(x, 0xDE90);
  setw(&c->d[0], alu_movew(c, 0xB));                     /* move.w #$B,d0 */
  lift_charge(x, 0xDE96);
  alu_movew(c, W(c->d[1]));                              /* tst.w d1 */
  lift_charge(x, 0xDE9A);
  {
    int negD1 = c->nf;                                     /* bmi.w loc_DED2 */
    lift_charge_bcc(x, 0xDE9C, negD1);
    if (negD1) goto loc_DED2;
  }

loc_DEA0:
  alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1]));  /* cmp.w $14(a0),d1 */
  lift_charge(x, 0xDEA0);
  {
    int ge = (c->nf == c->vf);                             /* bge.w loc_DEB4 */
    lift_charge_bcc(x, 0xDEA4, ge);
    if (!ge)
    {
      alu_movew(c, lift_r16(x, c->a[0] + 0x34));           /* tst.w $34(a0) */
      lift_charge(x, 0xDEA8);
      int zero = c->zf;                                     /* beq.w loc_DEB4 */
      lift_charge_bcc(x, 0xDEAC, zero);
      if (!zero)
      {
        lift_charge(x, 0xDEB0);                             /* bra.w loc_DECC */
        goto loc_DECC;
      }
    }
  }
  /* loc_DEB4 */
  c->a[0] -= AI_SCAN_STRIDE;                              /* suba.w #$80,a0 */
  lift_charge(x, 0xDEB4);
  {
    uint32_t nd0 = W(W(c->d[0]) - 1);                       /* dbf d0,loc_DEA0 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xDEB8, taken, !taken);
    if (taken) goto loc_DEA0;
  }
  goto loc_DEBC;

loc_DED2:
  alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1]));  /* cmp.w $14(a0),d1 */
  lift_charge(x, 0xDED2);
  {
    int le = c->zf || (c->nf != c->vf);                     /* ble.w loc_DEE4 */
    lift_charge_bcc(x, 0xDED6, le);
    if (!le)
    {
      alu_movew(c, lift_r16(x, c->a[0] + 0x34));           /* tst.w $34(a0) */
      lift_charge(x, 0xDEDA);
      int zero = c->zf;                                     /* beq.w loc_DEE4 */
      lift_charge_bcc(x, 0xDEDE, zero);
      if (!zero)
      {
        lift_charge(x, 0xDEE2);                             /* bra.s loc_DECC */
        goto loc_DECC;
      }
    }
  }
  /* loc_DEE4 */
  c->a[0] -= AI_SCAN_STRIDE;                              /* suba.w #$80,a0 */
  lift_charge(x, 0xDEE4);
  {
    uint32_t nd0 = W(W(c->d[0]) - 1);                       /* dbf d0,loc_DED2 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xDEE8, taken, !taken);
    if (taken) goto loc_DED2;
  }
  lift_charge(x, 0xDEEC);                                 /* bra.s loc_DEBC */
  goto loc_DEBC;

loc_DEBC:
  lift_w8(x, a3 + 0x64, alu_bset(c, lift_r8(x, a3 + 0x64), 1));  /* bset #1,$64(a3) */
  lift_charge(x, 0xDEBC);
  setw(&c->d[0], alu_movew(c, 1));                        /* move.w #1,d0 */
  lift_charge(x, 0xDEC2);
  goto loc_DEC6;

loc_DECC:
  setw(&c->d[0], alu_movew(c, 0));                        /* move.w #0,d0 */
  lift_charge(x, 0xDECC);
  lift_charge(x, 0xDED0);                                 /* bra.s loc_DEC6 */

loc_DEC6:
  /* movem.w (sp)+,d0-d1/a0: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  c->d[1] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  c->a[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  lift_charge_movem(x, 0xDEC6);

  lift_charge(x, 0xDECA);                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_AdvanceStateMod8 (sub_10646; called from sub_C710/sub_C882 and
 * tail-jumped into from Object_TriggerStateAdvance below)
 *   in/out: $36(a3) — a 3-bit (mod 8) counter, incremented and wrapped
 *   Also sets bit 1 of $62(a3) unconditionally. Exact role TBD.
 */
void Object_AdvanceStateMod8(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  lift_w16(x, a3 + 0x36, alu_addw(c, 1, lift_r16(x, a3 + 0x36)));  /* addq.w #1,$36(a3) */
  lift_charge(x, 0x10646);
  lift_w16(x, a3 + 0x36, alu_andw(c, 7, lift_r16(x, a3 + 0x36)));  /* and.w #7,$36(a3) */
  lift_charge(x, 0x1064A);
  lift_w8(x, a3 + 0x62, alu_bset(c, lift_r8(x, a3 + 0x62), 1));    /* bset #1,$62(a3) */
  lift_charge(x, 0x10650);

  lift_charge(x, 0x10656);                            /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_CAE0 (DATA XREF from a script/dispatch table at $18DFC; the
 * shootout-mode hot loop, highest-entry unlifted routine profiled under
 * shootout.txt)
 *   in: a3 = on-ice object
 * If $62(a3) bit5 is set, does nothing. Otherwise, if the face-off-commit
 * flags ($FFFFC2F2 bit2 or $FFFFC2FA bit0) are clear, tail-jumps into
 * Object_AdvanceStateMod8 (its rts returns to this routine's own caller).
 * If either flag is set: clears $28/$2A(a3), then sets $14(a3) to $191
 * (or $FE6F if bit7 of $62 is clear on the player object indexed by the
 * $FFFFD408 slot into the $FFFFB04A array) and returns.
 */
void sub_CAE0(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_btst(c, lift_r8(x, a3 + 0x62), 5);               /* btst #5,$62(a3) */
  lift_charge(x, 0xCAE0);
  int skip = !c->zf;                                     /* bne.w locret_CB38 */
  lift_charge_bcc(x, 0xCAE6, skip);
  if (skip)
  {
    lift_charge(x, 0xCB38);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2F2), 2);              /* btst #2,(C2F2).w */
  lift_charge(x, 0xCAEA);
  int commit = !c->zf;                                   /* bne.w loc_CB04 */
  lift_charge_bcc(x, 0xCAF0, commit);
  if (!commit)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2FA), 0);            /* btst #0,(C2FA).w */
    lift_charge(x, 0xCAF4);
    commit = !c->zf;                                     /* bne.w loc_CB04 */
    lift_charge_bcc(x, 0xCAFA, commit);
  }

  if (!commit)
  {
    lift_charge(x, 0xCAFE);                              /* nop */
    lift_charge(x, 0xCB00);                              /* bra.w sub_10646 */
    Object_AdvanceStateMod8(x);                          /* tail: its rts pops our caller's return */
    return;
  }

  /* loc_CB04 */
  lift_w16(x, a3 + 0x28, alu_movew(c, 0));               /* clr.w $28(a3) */
  lift_charge(x, 0xCB04);
  lift_w16(x, a3 + 0x2A, alu_movew(c, 0));               /* clr.w $2A(a3) */
  lift_charge(x, 0xCB08);

  uint32_t sp = c->a[7];
  uint32_t saved_d0 = c->d[0];
  uint32_t saved_a0 = c->a[0];

  /* movem.l d0/a0,-(sp): a0 pushed first, d0 lands lowest (see the
   * word-pair staging note on the sub_10676 movem above). */
  sp -= 4;
  lift_w16(x, sp, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[0] & 0xFFFF);
  sp -= 4;
  lift_w16(x, sp, (c->d[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[0] & 0xFFFF);
  lift_charge_movem(x, 0xCB0C);

  uint32_t a0 = 0xFFFFB04A;                              /* move.l #$FFFFB04A,a0 */
  lift_charge(x, 0xCB10);
  uint32_t d0 = lift_r16(x, 0xFFFFD408);                 /* move.w (D408).w,d0 */
  lift_charge(x, 0xCB16);
  d0 = alu_aslw(c, d0, 7);                               /* asl.w #7,d0 */
  lift_charge(x, 0xCB1A);
  a0 += SW(d0);                                          /* add.w d0,a0 */
  lift_charge(x, 0xCB1C);

  lift_w16(x, a3 + 0x14, alu_movew(c, 0x191));           /* move.w #$191,$14(a3) */
  lift_charge(x, 0xCB1E);

  alu_btst(c, lift_r8(x, a0 + 0x62), 7);                 /* btst #7,$62(a0) */
  lift_charge(x, 0xCB24);
  int hasFlag = !c->zf;                                   /* bne.w loc_CB34 */
  lift_charge_bcc(x, 0xCB2A, hasFlag);
  if (!hasFlag)
  {
    lift_w16(x, a3 + 0x14, alu_movew(c, 0xFE6F));        /* move.w #$FE6F,$14(a3) */
    lift_charge(x, 0xCB2E);
  }

  /* loc_CB34 */
  c->d[0] = saved_d0;                                    /* movem.l (sp)+,d0/a0 */
  c->a[0] = saved_a0;
  sp += 4; sp += 4;
  lift_charge_movem(x, 0xCB34);

  lift_charge(x, 0xCB38);                                /* rts */
  c->pc = lift_r32(x, sp) & 0xFFFFFF;
  c->a[7] = sp + 4;
}

/*
 * Object_TriggerStateAdvance (sub_CB3A; DATA XREF from a script/dispatch
 * table at $18DD4)
 *   in: a3 = on-ice object
 * If $62(a3) bit5 is set, does nothing. Otherwise, if R_UNK_C2EE bit0 is
 * clear, tail-jumps into Object_AdvanceStateMod8 (its rts returns to
 * this routine's own caller); if set, just returns.
 */
void Object_TriggerStateAdvance(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 5);         /* btst #5,$62(a3) */
  lift_charge(x, 0xCB3A);
  int skip = !c->zf;                                    /* bne.w locret_CB4E */
  lift_charge_bcc(x, 0xCB40, skip);
  if (skip)
  {
    lift_charge(x, 0xCB4E);                             /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2EE), 0);              /* btst #0,(abs) */
  lift_charge(x, 0xCB44);
  int doAdvance = c->zf;                                 /* beq.w sub_10646 */
  lift_charge_bcc(x, 0xCB4A, doAdvance);
  if (doAdvance)
  {
    Object_AdvanceStateMod8(x);                          /* fall-through tail: its rts pops our caller's return */
    return;
  }

  lift_charge(x, 0xCB4E);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_RingBufferWriteByte (sub_10662; called from sub_7CB0/sub_B0E8
 * and others, plus tail-fallen-into from Object_RetreatStateMod8 below)
 *   in:  a3 = on-ice object, d0 = byte to store
 *   Stores d0 into the $38(a3)-based ring buffer at the slot indexed by
 *   the mod-8 counter $36(a3) (same counter Object_AdvanceStateMod8
 *   advances), then sets bit1 of $62(a3). d1 saved/restored around the
 *   body (full long, though only its low word is touched).
 */
void Object_RingBufferWriteByte(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];
  uint32_t saved_d1 = c->d[1];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);       /* move.l d1,-(sp) */
  lift_charge(x, 0x10662);

  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x36)));  /* move.w $36(a3),d1 */
  lift_charge(x, 0x10664);
  lift_w8(x, a3 + 0x38 + SW(c->d[1]), alu_moveb(c, W(c->d[0])));  /* move.b d0,$38(a3,d1.w) */
  lift_charge(x, 0x10668);
  lift_w8(x, a3 + 0x62, alu_bset(c, lift_r8(x, a3 + 0x62), 1));   /* bset #1,$62(a3) */
  lift_charge(x, 0x1066C);

  c->d[1] = alu_movel(c, lift_r32(x, c->a[7])); c->a[7] += 4;  /* move.l (sp)+,d1 */
  lift_charge(x, 0x10672);

  lift_charge(x, 0x10674);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_RetreatStateMod8 (sub_10658; called from sub_7B30 and others;
 * tail-falls into Object_RingBufferWriteByte above — its rts returns to
 * THIS routine's own caller)
 *   in/out: $36(a3) — the mod-8 ring-buffer counter, decremented and
 *           wrapped (the opposite direction of Object_AdvanceStateMod8)
 */
void Object_RetreatStateMod8(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  lift_w16(x, a3 + 0x36, alu_subw(c, 1, lift_r16(x, a3 + 0x36)));  /* subq.w #1,$36(a3) */
  lift_charge(x, 0x10658);
  lift_w16(x, a3 + 0x36, alu_andw(c, 7, lift_r16(x, a3 + 0x36)));  /* and.w #7,$36(a3) */
  lift_charge(x, 0x1065C);

  Object_RingBufferWriteByte(x);                        /* fall-through tail */
}

#define R_STAT_CUR   0xFFFFB8A2u  /* current value / remaining countdown */
#define R_STAT_MAX   0xFFFFB8A4u  /* running max of R_STAT_CUR */
#define R_STAT_COUNT 0xFFFFB8A6u  /* sample count */
#define R_STAT_SUM   0xFFFFB8A8u  /* running sum (long) */

/*
 * Stat_AccumulateAndDecay (sub_7A76; called from sub_799E)
 * Updates a running max at R_STAT_MAX from R_STAT_CUR, accumulates
 * R_STAT_CUR (sign-extended) into the long sum at R_STAT_SUM, bumps
 * the sample count at R_STAT_COUNT, then decrements R_STAT_CUR,
 * clamping it at 0 instead of going negative. Exact stat role TBD;
 * behaviour preserved bit-for-bit.
 */
void Stat_AccumulateAndDecay(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, R_STAT_CUR)));  /* move.w (abs),d0 */
  lift_charge(x, 0x7A76);
  alu_cmpw(c, lift_r16(x, R_STAT_MAX), W(c->d[0]));         /* cmp.w (abs),d0 */
  lift_charge(x, 0x7A7A);
  int keep = c->cf || c->zf;                                 /* bls.w loc_7A86 */
  lift_charge_bcc(x, 0x7A7E, keep);
  if (!keep)
  {
    lift_w16(x, R_STAT_MAX, alu_movew(c, W(c->d[0])));       /* move.w d0,(abs) */
    lift_charge(x, 0x7A82);
  }

  /* loc_7A86 */
  c->d[0] = alu_extl(c, W(c->d[0]));                          /* ext.l d0 */
  lift_charge(x, 0x7A86);
  lift_w32(x, R_STAT_SUM, alu_addl(c, c->d[0], lift_r32(x, R_STAT_SUM)));  /* add.l d0,(abs) */
  lift_charge(x, 0x7A88);
  lift_w16(x, R_STAT_COUNT, alu_addw(c, 1, lift_r16(x, R_STAT_COUNT)));    /* addq.w #1,(abs) */
  lift_charge(x, 0x7A8C);
  lift_w16(x, R_STAT_CUR, alu_subw(c, 1, lift_r16(x, R_STAT_CUR)));        /* subq.w #1,(abs) */
  lift_charge(x, 0x7A90);
  int nonneg = !c->nf;                                        /* bpl.w locret_7A9C */
  lift_charge_bcc(x, 0x7A94, nonneg);
  if (!nonneg)
  {
    lift_w16(x, R_STAT_CUR, alu_movew(c, 0));                 /* clr.w (abs) */
    lift_charge(x, 0x7A98);
  }

  lift_charge(x, 0x7A9C);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_BumpStatByDistance (sub_1299C; called from sub_128B2)
 * Picks the home (d1=0) or away (d1=$364=TEAM_SIZE) team block based on
 * whether |R_CAM_SPEED| is within $58 of centre (bails via the shared
 * far rts if it's far away on the negative side without matching);
 * bails via locret_15464 if the value is <= -$58 (negative and beyond
 * the band) without ever reaching loc_129BA. If R_SNAP_DONE bit1 is set,
 * flips the block selection (XOR $364). Then bumps the word stat at
 * team_block+$A. Exact stat role TBD; behaviour preserved bit-for-bit.
 */
void Team_BumpStatByDistance(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[1] = alu_moveql(c, 0);                            /* moveq #0,d1 */
  lift_charge(x, 0x1299C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB75Eu)));   /* move.w (abs),d0 */
  lift_charge(x, 0x1299E);
  alu_cmpw(c, 0x58, W(c->d[0]));                           /* cmp.w #$58,d0 */
  lift_charge(x, 0x129A2);
  int gt = (!c->zf) && (c->nf == c->vf);                    /* bgt.w loc_129BA */
  lift_charge_bcc(x, 0x129A6, gt);

  if (!gt)
  {
    c->d[1] = alu_movel(c, 0x364);                         /* move.l #$364,d1 */
    lift_charge(x, 0x129AA);
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
    lift_charge(x, 0x129B0);
    alu_cmpw(c, 0x58, W(c->d[0]));                          /* cmp.w #$58,d0 */
    lift_charge(x, 0x129B2);
    int lt = (c->nf != c->vf);                              /* blt.w locret_15464 */
    lift_charge_bcc(x, 0x129B6, lt);
    if (lt)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_129BA */
  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 1);                  /* btst #1,(abs) */
  lift_charge(x, 0x129BA);
  int beqSkip = c->zf;                                        /* beq.w loc_129C8 */
  lift_charge_bcc(x, 0x129C0, beqSkip);
  if (!beqSkip)
  {
    setw(&c->d[1], alu_movew(c, W(c->d[1]) ^ 0x364));         /* eor.w #$364,d1 */
    lift_charge(x, 0x129C4);
  }

  /* loc_129C8 */
  c->a[2] = TEAM_HOME;                                        /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x129C8);
  lift_w16(x, c->a[2] + 0xA + SW(c->d[1]), alu_addw(c, 1, lift_r16(x, c->a[2] + 0xA + SW(c->d[1]))));  /* addq.w #1,$A(a2,d1.w) */
  lift_charge(x, 0x129CC);

  lift_charge(x, 0x129D0);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_BumpStatIfFlagged (sub_FE14C; called from sub_128B2)
 * If R_UNK_C2EE bit5 is clear, does nothing. Otherwise picks home
 * (default) or away (if R_UNK_C2EE bit6 is set) team block and bumps
 * the word stat at team_block+$352. Exact stat role TBD.
 */
void Team_BumpStatIfFlagged(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 5);              /* btst #5,(abs) */
  lift_charge(x, 0xFE14C);
  int inactive = c->zf;                                   /* beq.w locret_FE170 */
  lift_charge_bcc(x, 0xFE152, inactive);
  if (inactive)
  {
    lift_charge(x, 0xFE170);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[2] = TEAM_HOME;                                    /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xFE156);
  alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 6);                /* btst #6,(abs) */
  lift_charge(x, 0xFE15C);
  int home = c->zf;                                          /* beq.w loc_FE16C */
  lift_charge_bcc(x, 0xFE162, home);
  if (!home)
  {
    c->a[2] = TEAM_HOME + TEAM_SIZE;                        /* movea.l #$FFFFCA32,a2 */
    lift_charge(x, 0xFE166);
  }

  /* loc_FE16C */
  lift_w16(x, c->a[2] + 0x352, alu_addw(c, 1, lift_r16(x, c->a[2] + 0x352)));  /* addq.w #1,$352(a2) */
  lift_charge(x, 0xFE16C);

  lift_charge(x, 0xFE170);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Clamp_ByteToRange30 (sub_15D84; called from sub_15AA4)
 *   in/out: d3 — clamped to [0,$1E] (treating a negative byte as 0 first,
 *           then comparing the low byte against $1E)
 */
void Clamp_ByteToRange30(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_moveb(c, W(c->d[3]) & 0xFF);                    /* tst.b d3 */
  lift_charge(x, 0x15D84);
  int nonneg = !c->nf;                                  /* bpl.w loc_15D8C */
  lift_charge_bcc(x, 0x15D86, nonneg);
  if (!nonneg)
  {
    setw(&c->d[3], alu_movew(c, 0));                    /* clr.w d3 */
    lift_charge(x, 0x15D8A);
  }

  /* loc_15D8C */
  alu_cmpb(c, 0x1E, W(c->d[3]) & 0xFF);                 /* cmp.b #$1E,d3 */
  lift_charge(x, 0x15D8C);
  int le = c->zf || (c->nf != c->vf);                    /* ble.w locret_15D98 */
  lift_charge_bcc(x, 0x15D90, le);
  if (!le)
  {
    setw(&c->d[3], alu_movew(c, 0x1E));                  /* move.w #$1E,d3 */
    lift_charge(x, 0x15D94);
  }

  lift_charge(x, 0x15D98);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_DecayPenaltyTimers (sub_7A0C; called from Team_ScanPenalties
 * below via bsr for the home team, then fallen into a second time — not
 * via bsr — for the away team, so its own rts returns to the ORIGINAL
 * caller both times)
 *   in: a2 = team block
 *   For each of 26 roster slots (d0 = $32,$30...2,0, byte stride 2 within
 *   the block): if the word at $66(a2,d0) IS the $FFFE sentinel, adds
 *   9 to the word at $32(a2,d0), clamped to a max of $1000.
 *   (The `bne.w loc_7A2E` at $7A14 branches PAST the add, so the add is
 *   the not-taken path — corrected 2026-08-01 when season-menu.txt's
 *   penalties-ON gameplay first executed this loop. Both pre-existing
 *   scripts run penalties OFF, where Team_ScanPenalties bails at its
 *   R_UNK_D058 gate and this loop never runs, so the inversion was
 *   invisible: it produced no logged writes to compare, only cycles.)
 */
void Team_DecayPenaltyTimers(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a2 = c->a[2];

  c->d[0] = alu_moveql(c, 0x32);                        /* moveq #$32,d0 */
  lift_charge(x, 0x7A0C);

  for (;;)
  {
    alu_cmpw(c, 0xFFFE, lift_r16(x, a2 + 0x66 + SEW(c->d[0])));  /* cmp.w #$FFFE,$66(a2,d0.w) */
    lift_charge(x, 0x7A0E);
    int skip = !c->zf;                                          /* bne.w loc_7A2E */
    lift_charge_bcc(x, 0x7A14, skip);
    if (!skip)
    {
      lift_w16(x, a2 + 0x32 + SEW(c->d[0]), alu_addw(c, 9, lift_r16(x, a2 + 0x32 + SEW(c->d[0]))));  /* add.w #9,$32(a2,d0.w) */
      lift_charge(x, 0x7A18);
      alu_cmpw(c, 0x1000, lift_r16(x, a2 + 0x32 + SEW(c->d[0])));  /* cmp.w #$1000,$32(a2,d0.w) */
      lift_charge(x, 0x7A1E);
      int lt = (c->nf != c->vf);                                 /* blt.w loc_7A2E */
      lift_charge_bcc(x, 0x7A24, lt);
      if (!lt)
      {
        lift_w16(x, a2 + 0x32 + SEW(c->d[0]), alu_movew(c, 0x1000));  /* move.w #$1000,$32(a2,d0.w) */
        lift_charge(x, 0x7A28);
      }
    }

    /* loc_7A2E */
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));                 /* subq.w #2,d0 */
    lift_charge(x, 0x7A2E);
    int cont = !c->nf;                                            /* bpl.s loc_7A0E */
    lift_charge_bcc(x, 0x7A30, cont);
    if (!cont) break;
  }

  lift_charge(x, 0x7A32);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_ScanPenalties (sub_79F8; called from sub_799E)
 * Unless R_UNK_D058 is nonzero, runs Team_DecayPenaltyTimers for the
 * home team block, then advances a2 to the away block ($364 further)
 * and tail-falls into it a second time (not a bsr — its rts returns to
 * THIS routine's own caller).
 */
void Team_ScanPenalties(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movew(c, lift_r16(x, 0xFFFFD058u));               /* tst.w (abs) */
  lift_charge(x, 0x79F8);
  int skip = !c->zf;                                       /* bne.w locret_7A32 */
  lift_charge_bcc(x, 0x79FC, skip);
  if (skip)
  {
    lift_charge(x, 0x7A32);                                /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[2] = TEAM_HOME;                                     /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x7A00);
  lift_call(x, 0x7A04, 4, Team_DecayPenaltyTimers);        /* bsr.w sub_7A0C */
  if (x->declined) return;
  c->a[2] += TEAM_SIZE;                                    /* lea $364(a2),a2 */
  lift_charge(x, 0x7A08);

  Team_DecayPenaltyTimers(x);                               /* fall-through tail */
}

#define TBL_15A9C 0x15A9Cu   /* ROM: 7-entry byte table, $FF end marker never indexed here */

/*
 * Object_QueueFrameFromTable (sub_15A88; called from sub_C67A)
 *   in: a3 = on-ice object, d0 = table index
 * Bails via the shared far rts if d0 is negative. Otherwise looks up
 * byte_15A9C[d0] and tail-jumps into Object_RingBufferWriteByte with
 * that byte in d0 — its rts returns to this routine's caller.
 */
void Object_QueueFrameFromTable(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x34)));  /* move.w $34(a3),d0 */
  lift_charge(x, 0x15A88);
  int neg = c->nf;                                         /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0x15A8C, neg);
  if (neg)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = TBL_15A9C;                                     /* lea byte_15A9C(pc),a0 */
  lift_charge(x, 0x15A90);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[0]))));  /* move.b (a0,d0.w),d0 */
  lift_charge(x, 0x15A94);
  lift_charge(x, 0x15A98);                                 /* bra.w sub_10662 */

  Object_RingBufferWriteByte(x);                            /* tail jump */
}

/*
 * Clamp_HalveBelow50 (sub_FEF7C; called from ROM:8E46/FA916)
 *   in/out: d0 — if d0 < $32, d0 = d0/2 + $19 (halves and re-biases);
 *           otherwise unchanged
 */
void Clamp_HalveBelow50(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, 0x32, W(c->d[0]));                      /* cmp.w #$32,d0 */
  lift_charge(x, 0xFEF7C);
  int ge = (c->nf == c->vf);                            /* bge.w locret_FEF8A */
  lift_charge_bcc(x, 0xFEF80, ge);
  if (!ge)
  {
    setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));         /* asr.w #1,d0 */
    lift_charge(x, 0xFEF84);
    setw(&c->d[0], alu_addw(c, 0x19, W(c->d[0])));      /* add.w #$19,d0 */
    lift_charge(x, 0xFEF86);
  }

  lift_charge(x, 0xFEF8A);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Calc_HalvingAccumulator (sub_1828A; called from ROM:813C/sub_130E6)
 *   in:  d0 preserved (saved/restored via the stack)
 *   out: d2 = -16 + sum of (16>>i) for i=0..R_UNK_CEEC (halving series);
 *        d1 = (16 >> (R_UNK_CEEC+1)) - 1
 *   R_UNK_CEEC is presumably a difficulty/setting value driving how many
 *   halving steps run. Exact role TBD; behaviour preserved bit-for-bit.
 */
void Calc_HalvingAccumulator(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);         /* move.l d0,-(sp) */
  lift_charge(x, 0x1828A);

  c->d[2] = alu_moveql(c, -0x10);                        /* moveq #-$10,d2 */
  lift_charge(x, 0x1828C);
  c->d[1] = alu_moveql(c, 0x10);                          /* moveq #$10,d1 */
  lift_charge(x, 0x1828E);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFCEECu))); /* move.w (abs),d0 */
  lift_charge(x, 0x18290);

  for (;;)
  {
    setw(&c->d[2], alu_addw(c, W(c->d[1]), W(c->d[2])));  /* add.w d1,d2 */
    lift_charge(x, 0x18294);
    setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 1));            /* lsr.w #1,d1 */
    lift_charge(x, 0x18296);
    uint32_t nd0 = W(W(c->d[0]) - 1);                       /* dbf d0,loc_18294 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x18298, taken, !taken);
    if (!taken) break;
  }

  setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));              /* subq.w #1,d1 */
  lift_charge(x, 0x1829C);
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7])); c->a[7] += 4;  /* move.l (sp)+,d0 */
  lift_charge(x, 0x1829E);

  lift_charge(x, 0x182A0);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TransferSelectionFlag (sub_C0F8; called from sub_C0BC/sub_C0DA)
 *   in:  d0 = candidate slot index (0-11), d1 = previous slot index (0-11)
 *   out: d0 = the slot index to keep selected (or unchanged if out of
 *        range); other registers/memory per the bit updates below
 *
 * If d1 is in [0,11]: checks bit3 of $63(slot). If set, that slot is
 * already claimed — returns its index in d0 directly (skipping the rest).
 * Otherwise clears bit3 of $62(slot), and if bit3 of $64(slot) is also
 * clear, sets bit1 of $62(slot) (marks it released).
 * Then, if d0 is in [0,11]: unless a gate (C2F2 bit2 or C2FA bit0) or the
 * candidate slot's own $34 field is nonzero, checks a side-dependent
 * flag (D05A/D05C by $62 bit6) — if clear, sets bit3 of $62(candidate)
 * (claims it); if set, d0 is reset to 0 or 6 (by $62 bit6) instead of
 * claiming. Exact role TBD (looks like on-ice player/puck-carrier
 * handoff bookkeeping); behaviour preserved bit-for-bit.
 */
void Object_TransferSelectionFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;
  c->a[0] = 0xFFFFB04Au;                            /* movea.w #$B04A,a0 */
  lift_charge(x, 0xC0F8);

  alu_movew(c, W(c->d[1]));                              /* tst.w d1 */
  lift_charge(x, 0xC0FC);
  int lt0 = c->nf;                                         /* blt.w loc_C132 */
  lift_charge_bcc(x, 0xC0FE, lt0);

  if (!lt0)
  {
    alu_cmpw(c, 0xB, W(c->d[1]));                          /* cmp.w #$B,d1 */
    lift_charge(x, 0xC102);
    int gt = (!c->zf) && (c->nf == c->vf);                  /* bgt.w loc_C132 */
    lift_charge_bcc(x, 0xC106, gt);

    if (!gt)
    {
      setw(&c->d[1], alu_aslw(c, W(c->d[1]), 7));           /* asl.w #7,d1 */
      lift_charge(x, 0xC10A);
      alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x63), 3);   /* btst #3,$63(a0,d1.w) */
      lift_charge(x, 0xC10C);
      int claimed = !c->zf;                                  /* beq.w loc_C11C */
      lift_charge_bcc(x, 0xC112, c->zf);

      if (claimed)
      {
        setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 7));          /* lsr.w #7,d1 */
        lift_charge(x, 0xC116);
        setw(&c->d[0], alu_movew(c, W(c->d[1])));            /* move.w d1,d0 */
        lift_charge(x, 0xC118);
        lift_charge(x, 0xC11A);                              /* rts */
        c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
        c->a[7] += 4;
        return;
      }

      /* loc_C11C */
      lift_w8(x, c->a[0] + SEW(c->d[1]) + 0x62, alu_bclr(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 3));  /* bclr #3,$62(a0,d1.w) */
      lift_charge(x, 0xC11C);
      alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x64), 3);    /* btst #3,$64(a0,d1.w) */
      lift_charge(x, 0xC122);
      int still = !c->zf;                                     /* bne.w loc_C132 */
      lift_charge_bcc(x, 0xC128, still);
      if (!still)
      {
        lift_w8(x, c->a[0] + SEW(c->d[1]) + 0x62, alu_bset(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 1));  /* bset #1,$62(a0,d1.w) */
        lift_charge(x, 0xC12C);
      }
    }
  }

  /* loc_C132 */
  alu_movew(c, W(c->d[0]));                                 /* tst.w d0 */
  lift_charge(x, 0xC132);
  int lt1 = c->nf;                                            /* blt.w locret_C196 */
  lift_charge_bcc(x, 0xC134, lt1);
  if (lt1)
  {
    lift_charge(x, 0xC196);                                   /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_cmpw(c, 0xB, W(c->d[0]));                               /* cmp.w #$B,d0 */
  lift_charge(x, 0xC138);
  int gt1 = (!c->zf) && (c->nf == c->vf);                      /* bgt.w locret_C196 */
  lift_charge_bcc(x, 0xC13C, gt1);
  if (gt1)
  {
    lift_charge(x, 0xC196);                                    /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[1], alu_movew(c, W(c->d[0])));                    /* move.w d0,d1 */
  lift_charge(x, 0xC140);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 7));                  /* asl.w #7,d1 */
  lift_charge(x, 0xC142);
  alu_btst(c, lift_r8(x, 0xFFFFC2F2u), 2);                     /* btst #2,(abs) */
  lift_charge(x, 0xC144);
  int gate1 = !c->zf;                                            /* bne.w loc_C158 */
  lift_charge_bcc(x, 0xC14A, gate1);

  int reachC158 = gate1;
  if (!gate1)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 0);                   /* btst #0,(abs) */
    lift_charge(x, 0xC14E);
    int gate2 = c->zf;                                           /* beq.w loc_C190 */
    lift_charge_bcc(x, 0xC154, gate2);
    if (gate2)
    {
      /* loc_C190 */
      lift_w8(x, c->a[0] + SEW(c->d[1]) + 0x62, alu_bset(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 3));  /* bset #3,$62(a0,d1.w) */
      lift_charge(x, 0xC190);
      lift_charge(x, 0xC196);                                    /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    reachC158 = 1;
  }

  if (reachC158)
  {
    /* loc_C158 */
    alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[1]) + 0x34));          /* tst.w $34(a0,d1.w) */
    lift_charge(x, 0xC158);
    int busy = !c->zf;                                            /* bne.w loc_C190 */
    lift_charge_bcc(x, 0xC15C, busy);
    if (busy)
    {
      lift_w8(x, c->a[0] + SEW(c->d[1]) + 0x62, alu_bset(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 3));  /* bset #3,$62(a0,d1.w) */
      lift_charge(x, 0xC190);
      lift_charge(x, 0xC196);                                     /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 6);           /* btst #6,$62(a0,d1.w) */
    lift_charge(x, 0xC160);
    int away = !c->zf;                                              /* beq.w loc_C172 */
    lift_charge_bcc(x, 0xC166, c->zf);
    if (away)
    {
      alu_movew(c, lift_r16(x, 0xFFFFD05Cu));                      /* tst.w (abs) */
      lift_charge(x, 0xC16A);
      lift_charge(x, 0xC16E);                                       /* bra.w loc_C176 */
    }
    else
    {
      /* loc_C172 */
      alu_movew(c, lift_r16(x, 0xFFFFD05Au));                      /* tst.w (abs) */
      lift_charge(x, 0xC172);
    }

    /* loc_C176 */
    int noSwap = c->zf;                                             /* beq.w loc_C190 */
    lift_charge_bcc(x, 0xC176, noSwap);
    if (noSwap)
    {
      lift_w8(x, c->a[0] + SEW(c->d[1]) + 0x62, alu_bset(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 3));  /* bset #3,$62(a0,d1.w) */
      lift_charge(x, 0xC190);
      lift_charge(x, 0xC196);                                       /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    setw(&c->d[0], alu_movew(c, 0));                                 /* move.w #0,d0 */
    lift_charge(x, 0xC17A);
    alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 0x62), 6);               /* btst #6,$62(a0,d1.w) */
    lift_charge(x, 0xC17E);
    int away2 = !c->zf;                                                /* beq.w loc_C18C */
    lift_charge_bcc(x, 0xC184, c->zf);
    if (away2)
    {
      setw(&c->d[0], alu_movew(c, 6));                                 /* move.w #6,d0 */
      lift_charge(x, 0xC188);
    }

    lift_charge(x, 0xC18C);                                           /* bra.w locret_C196 */
  }

  lift_charge(x, 0xC196);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_SELECTED_SLOT_A 0xFFFFC320u  /* cached selected object-slot index (side A) */

/*
 * Object_UpdateSelectedSlot_A (sub_C0BC; called from sub_B0E8/sub_BE26)
 *   in: d0 = candidate slot index
 *   If d0 already equals the cached R_SELECTED_SLOT_A, does nothing.
 *   Otherwise runs Object_TransferSelectionFlag(d0, cached) and stores
 *   its result back as the new cached value. d1/a0/a1 saved/restored.
 */
void Object_UpdateSelectedSlot_A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, lift_r16(x, R_SELECTED_SLOT_A), W(c->d[0]));  /* cmp.w (abs),d0 */
  lift_charge(x, 0xC0BC);
  int same = c->zf;                                            /* beq.w locret_C0D8 */
  lift_charge_bcc(x, 0xC0C0, same);
  if (same)
  {
    lift_charge(x, 0xC0D8);                                     /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  uint32_t saved_d1 = c->d[1], saved_a0 = c->a[0], saved_a1 = c->a[1];
  /* movem.l d1/a0-a1,-(sp): a1 pushed first (high addr), a0, d1 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  lift_charge_movem(x, 0xC0C4);

  setw(&c->d[1], alu_movew(c, lift_r16(x, R_SELECTED_SLOT_A)));  /* move.w (abs),d1 */
  lift_charge(x, 0xC0C8);
  lift_call(x, 0xC0CC, 4, Object_TransferSelectionFlag);         /* bsr.w sub_C0F8 */
  if (x->declined) return;
  lift_w16(x, R_SELECTED_SLOT_A, alu_movew(c, W(c->d[0])));      /* move.w d0,(abs) */
  lift_charge(x, 0xC0D0);

  /* movem.l (sp)+,d1/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xC0D4);

  lift_charge(x, 0xC0D8);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_SELECTED_SLOT_B 0xFFFFC322u  /* cached selected object-slot index (side B) */

/*
 * Object_UpdateSelectedSlot_B (sub_C0DA; called from sub_B0E8/sub_BE26)
 *   in: d0 = candidate slot index
 *   Mirror of Object_UpdateSelectedSlot_A but for R_SELECTED_SLOT_B.
 */
void Object_UpdateSelectedSlot_B(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, lift_r16(x, R_SELECTED_SLOT_B), W(c->d[0]));  /* cmp.w (abs),d0 */
  lift_charge(x, 0xC0DA);
  int same = c->zf;                                            /* beq.w locret_C0F6 */
  lift_charge_bcc(x, 0xC0DE, same);
  if (same)
  {
    lift_charge(x, 0xC0F6);                                     /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  uint32_t saved_d1 = c->d[1], saved_a0 = c->a[0], saved_a1 = c->a[1];
  /* movem.l d1/a0-a1,-(sp): a1 pushed first (high addr), a0, d1 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  lift_charge_movem(x, 0xC0E2);

  setw(&c->d[1], alu_movew(c, lift_r16(x, R_SELECTED_SLOT_B)));  /* move.w (abs),d1 */
  lift_charge(x, 0xC0E6);
  lift_call(x, 0xC0EA, 4, Object_TransferSelectionFlag);         /* bsr.w sub_C0F8 */
  if (x->declined) return;
  lift_w16(x, R_SELECTED_SLOT_B, alu_movew(c, W(c->d[0])));      /* move.w d0,(abs) */
  lift_charge(x, 0xC0EE);

  /* movem.l (sp)+,d1/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xC0F2);

  lift_charge(x, 0xC0F6);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Range_WrapClamp (sub_F7B0A; called from sub_F7942)
 *   in:  d2,d3,d4,d5 — d3 += d2; if the sum is still < d5, wrap it to
 *        d4-1. Then if the (possibly wrapped) d3 < d4, bail via a bare
 *        rts at locret_F78A0 (leaving d3 as computed); otherwise
 *        d3 = d5 and returns normally.
 */
void Range_WrapClamp(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_addw(c, W(c->d[2]), W(c->d[3])));   /* add.w d2,d3 */
  lift_charge(x, 0xF7B0A);
  alu_cmpw(c, W(c->d[5]), W(c->d[3]));                     /* cmp.w d5,d3 */
  lift_charge(x, 0xF7B0C);
  int ge = (c->nf == c->vf);                                /* bge.w loc_F7B16 */
  lift_charge_bcc(x, 0xF7B0E, ge);
  if (!ge)
  {
    setw(&c->d[3], alu_movew(c, W(c->d[4])));               /* move.w d4,d3 */
    lift_charge(x, 0xF7B12);
    setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));             /* subq.w #1,d3 */
    lift_charge(x, 0xF7B14);
  }

  /* loc_F7B16 */
  alu_cmpw(c, W(c->d[4]), W(c->d[3]));                      /* cmp.w d4,d3 */
  lift_charge(x, 0xF7B16);
  int lt = (c->nf != c->vf);                                 /* blt.w locret_F78A0 */
  lift_charge_bcc(x, 0xF7B18, lt);
  if (lt)
  {
    lift_charge(x, 0xF78A0);                                  /* bare rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[3], alu_movew(c, W(c->d[5])));                   /* move.w d5,d3 */
  lift_charge(x, 0xF7B1C);

  lift_charge(x, 0xF7B1E);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_QueueScoreboardEvent (sub_7E72; called from ROM:7E58/7F3C)
 * Sets R_UNK_B028 to 5, then bails via the bare rts shared with
 * Game_RecordSnapshot's tail (locret_A9D4) if R_UNK_BF78 bit1 is set;
 * otherwise adds 4 to R_UNK_B028 (net effect: 9) and returns normally.
 */
void Anim_QueueScoreboardEvent(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFB028u, alu_movew(c, 5));            /* move.w #5,(abs) */
  lift_charge(x, 0x7E72);
  alu_btst(c, lift_r8(x, 0xFFFFBF78u), 1);               /* btst #1,(abs) */
  lift_charge(x, 0x7E78);
  int skip = !c->zf;                                       /* bne.w locret_A9D4 */
  lift_charge_bcc(x, 0x7E7E, skip);
  if (skip)
  {
    lift_charge(x, 0xA9D4);                                /* bare rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, 0xFFFFB028u, alu_addw(c, 4, lift_r16(x, 0xFFFFB028u)));  /* addq.w #4,(abs) */
  lift_charge(x, 0x7E82);

  lift_charge(x, 0x7E86);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_PENALTY_TOTAL 0xFFFFC3EAu  /* byte: running total adjusted per-slot below */

/*
 * Roster_TickPenaltyCountdowns (sub_15A38; called from sub_15A24)
 *   in:  a0 = roster block base, d1 = tick delta to apply to
 *        R_PENALTY_TOTAL per active slot
 *   Walks 26 roster slots (d0 = $32 downto 0, stride 2) touching the
 *   word at $66(a0,d0.w):
 *     - if <= 0: subtracts d1 from R_PENALTY_TOTAL, then unless the
 *       slot holds one of two sentinels ($FFFD/$FFFC), sets it to $FFFE
 *     - if > 0 and bit4 is set: if the low 11 bits are zero, also
 *       subtracts d1 from R_PENALTY_TOTAL (undoing the add just below)
 *     - if > 0 and bit4 is clear: adds d1 to R_PENALTY_TOTAL
 *   Exact role TBD (penalty-clock bookkeeping); behaviour preserved
 *   bit-for-bit. d2 only touched (and left modified) on the bit4-set
 *   path.
 */
void Roster_TickPenaltyCountdowns(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x32);                          /* moveq #$32,d0 */
  lift_charge(x, 0x15A38);

  for (;;)
  {
    /* loc_15A3A */
    lift_w8(x, R_PENALTY_TOTAL, alu_addb(c, W(c->d[1]) & 0xFF, lift_r8(x, R_PENALTY_TOTAL)));  /* add.b d1,(abs) */
    lift_charge(x, 0x15A3A);
    alu_movew(c, lift_r16(x, c->a[0] + 0x66 + SEW(c->d[0])));  /* tst.w $66(a0,d0.w) */
    lift_charge(x, 0x15A3E);
    int le = c->zf || c->nf;                                  /* ble.w loc_15A64 */
    lift_charge_bcc(x, 0x15A42, le);

    if (!le)
    {
      alu_btst(c, lift_r8(x, c->a[0] + 0x66 + SEW(c->d[0])), 4);  /* btst #4,$66(a0,d0.w) (byte test of the word's low byte) */
      lift_charge(x, 0x15A46);
      int b4 = !c->zf;                                          /* beq.w loc_15A82 */
      lift_charge_bcc(x, 0x15A4C, c->zf);
      if (b4)
      {
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0] + 0x66 + SEW(c->d[0]))));  /* move.w $66(a0,d0.w),d2 */
        lift_charge(x, 0x15A50);
        setw(&c->d[2], alu_andw(c, 0x7FF, W(c->d[2])));          /* and.w #$7FF,d2 */
        lift_charge(x, 0x15A54);
        int nz = !c->zf;                                          /* bne.w loc_15A82 */
        lift_charge_bcc(x, 0x15A58, nz);
        if (!nz)
        {
          lift_w8(x, R_PENALTY_TOTAL, alu_subb(c, W(c->d[1]) & 0xFF, lift_r8(x, R_PENALTY_TOTAL)));  /* sub.b d1,(abs) */
          lift_charge(x, 0x15A5C);
          lift_charge(x, 0x15A60);                               /* bra.w loc_15A82 */
        }
      }
    }
    else
    {
      /* loc_15A64 */
      lift_w8(x, R_PENALTY_TOTAL, alu_subb(c, W(c->d[1]) & 0xFF, lift_r8(x, R_PENALTY_TOTAL)));  /* sub.b d1,(abs) */
      lift_charge(x, 0x15A64);
      alu_cmpw(c, 0xFFFD, lift_r16(x, c->a[0] + 0x66 + SEW(c->d[0])));  /* cmp.w #$FFFD,$66(a0,d0.w) */
      lift_charge(x, 0x15A68);
      int s1 = c->zf;                                             /* beq.w loc_15A82 */
      lift_charge_bcc(x, 0x15A6E, s1);
      if (!s1)
      {
        alu_cmpw(c, 0xFFFC, lift_r16(x, c->a[0] + 0x66 + SEW(c->d[0])));  /* cmp.w #$FFFC,$66(a0,d0.w) */
        lift_charge(x, 0x15A72);
        int s2 = c->zf;                                           /* beq.w loc_15A82 */
        lift_charge_bcc(x, 0x15A78, s2);
        if (!s2)
        {
          lift_w16(x, c->a[0] + 0x66 + SEW(c->d[0]), alu_movew(c, 0xFFFE));  /* move.w #$FFFE,$66(a0,d0.w) */
          lift_charge(x, 0x15A7C);
        }
      }
    }

    /* loc_15A82 */
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));                  /* subq.w #2,d0 */
    lift_charge(x, 0x15A82);
    int cont = !c->nf;                                             /* bpl.s loc_15A3A */
    lift_charge_bcc(x, 0x15A84, cont);
    if (!cont) break;
  }

  lift_charge(x, 0x15A86);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_SelectCue (sub_F87D2; called from sub_F7B20/sub_F8304)
 * Sets the sound cue at R_UNK_D43C to $AA, then overrides it to $53 if
 * R_UNK_D42E bit5 is set. Exact cue meaning TBD.
 */
void Sfx_SelectCue(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFD43Cu, alu_movew(c, 0xAA));         /* move.w #$AA,(abs) */
  lift_charge(x, 0xF87D2);
  alu_btst(c, lift_r8(x, 0xFFFFD42Eu), 5);                /* btst #5,(abs) */
  lift_charge(x, 0xF87D8);
  int alt = !c->zf;                                         /* beq.w locret_F87E8 */
  lift_charge_bcc(x, 0xF87DE, c->zf);
  if (alt)
  {
    lift_w16(x, 0xFFFFD43Cu, alu_movew(c, 0x53));          /* move.w #$53,(abs) */
    lift_charge(x, 0xF87E2);
  }

  lift_charge(x, 0xF87E8);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_ResetVoiceStateTable (sub_1AD54; called from Z80_MuteAllFM and
 * Z80_LoadROM)
 * First pass: fills 8 entries of 6 bytes each (starting at $FFFFD3D4)
 * with -1 (all bytes $FF) as a full long per entry.
 * Second pass: walks 6 entries of 8 bytes each, backward from $FFFFD3D4
 * (i.e. the preceding memory region): sets byte+4 to the loop index
 * (bumped by 1 if the index is >= 3), force-sets bytes 0 and 3, clears
 * bytes 1 and 6. Exact voice/channel bookkeeping role TBD (this routine
 * itself only touches RAM — no Z80/YM hardware access — despite being
 * called from the FM-mute/ROM-load paths); behaviour preserved
 * bit-for-bit.
 */
void Sfx_ResetVoiceStateTable(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = 0xFFFFD3D4u;                                  /* lea (abs).w,a0 */
  lift_charge(x, 0x1AD54);
  c->d[0] = alu_moveql(c, 7);                              /* moveq #7,d0 */
  lift_charge(x, 0x1AD58);
  c->d[1] = alu_moveql(c, -1);                             /* moveq #-1,d1 */
  lift_charge(x, 0x1AD5A);

  for (;;)
  {
    lift_w32(x, c->a[0], alu_movel(c, c->d[1]));            /* move.l d1,(a0) */
    lift_charge(x, 0x1AD5C);
    c->a[0] += 6;                                            /* addq.w #6,a0 */
    lift_charge(x, 0x1AD5E);
    uint32_t nd0 = W(W(c->d[0]) - 1);                        /* dbf d0,loc_1AD5C */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x1AD60, taken, !taken);
    if (!taken) break;
  }

  c->d[0] = alu_moveql(c, 5);                                /* moveq #5,d0 */
  lift_charge(x, 0x1AD64);
  c->a[0] = 0xFFFFD3D4u;                                     /* movea.w #$D3D4,a0 */
  lift_charge(x, 0x1AD66);

  for (;;)
  {
    /* loc_1AD6A */
    c->a[0] -= 8;                                             /* subq.w #8,a0 */
    lift_charge(x, 0x1AD6A);
    lift_w8(x, c->a[0] + 4, alu_moveb(c, W(c->d[0])));         /* move.b d0,4(a0) */
    lift_charge(x, 0x1AD6C);
    alu_cmpw(c, 3, W(c->d[0]));                                /* cmp.w #3,d0 */
    lift_charge(x, 0x1AD70);
    int lt = (c->nf != c->vf);                                  /* blt.w loc_1AD7C */
    lift_charge_bcc(x, 0x1AD74, lt);
    if (!lt)
    {
      lift_w8(x, c->a[0] + 4, alu_addb(c, 1, lift_r8(x, c->a[0] + 4)));  /* addq.b #1,4(a0) */
      lift_charge(x, 0x1AD78);
    }

    /* loc_1AD7C */
    lift_w8(x, c->a[0] + 3, 0xFF);                             /* st 3(a0) */
    lift_charge(x, 0x1AD7C);
    lift_w8(x, c->a[0], 0xFF);                                 /* st (a0) */
    lift_charge(x, 0x1AD80);
    lift_w8(x, c->a[0] + 1, alu_moveb(c, 0));                  /* clr.b 1(a0) */
    lift_charge(x, 0x1AD82);
    lift_w8(x, c->a[0] + 6, alu_moveb(c, 0));                  /* clr.b 6(a0) */
    lift_charge(x, 0x1AD86);

    uint32_t nd0b = W(W(c->d[0]) - 1);                          /* dbf d0,loc_1AD6A */
    setw(&c->d[0], nd0b);
    int taken2 = (nd0b != 0xFFFF);
    lift_charge_dbcc(x, 0x1AD8A, taken2, !taken2);
    if (!taken2) break;
  }

  lift_charge(x, 0x1AD8E);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ProcessLineChangeSlot (sub_12F30; called from sub_14620)
 *
 * A gate chain: bails via the shared far rts if R_UNK_C2EA bit0 or bit4
 * is set. Then bclr's bit4 of R_UNK_C2EE:
 *   - if it WAS set (bne): the "roster swap" path (loc_12F6C) — looks
 *     up a $C46E-based table entry (indexed by R_UNK_C472), picks home
 *     or away team block by that entry's bit7, reads its byte+3 as a
 *     target jersey/roster value, then scans up to 6 slots of the
 *     team's $22-based sub-table for a byte-$66 match; no match bails
 *     to the shared tail without calling Team_SelectBlocks.
 *   - if it was clear: requires R_UNK_C2F4 bit0 set (else bails via
 *     shared far rts); then bclr's bit3 of R_UNK_C2FE — if it was set,
 *     re-sets it and bails; otherwise the "score/period" path
 *     (loc_12FBC) — bumps R_UNK_B89C by $64 and R_UNK_B8A2 by $A, reads
 *     R_UNK_BED8 as a player-slot index.
 * Either path converges: selects the on-ice object for that index via
 * $B04A + index*$80, calls Team_SelectBlocks(a3) to get a2/a1 = the
 * object's team/opponent blocks, bumps a stat at (a2), and — gated by
 * R_UNK_C2EE bits 5/6 and the object's own $62 bit6 (home/away) —
 * conditionally bumps $354(a2). Then bumps a per-jersey-number counter
 * at a2+$34A+2*R_UNK_C466, and two more per-position counters (a2/a1
 * offset $E8 + the object's/a1's own roster-position byte).
 * Exact stat-tracking role TBD; behaviour preserved bit-for-bit.
 */
void Object_ProcessLineChangeSlot(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 0);              /* btst #0,(abs) */
  lift_charge(x, 0x12F30);
  int block1 = !c->zf;                                     /* bne.w locret_15464 */
  lift_charge_bcc(x, 0x12F36, block1);
  if (block1)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 4);                /* btst #4,(abs) */
  lift_charge(x, 0x12F3A);
  int block2 = !c->zf;                                      /* bne.w locret_15464 */
  lift_charge_bcc(x, 0x12F40, block2);
  if (block2)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w8(x, 0xFFFFC2EEu, alu_bclr(c, lift_r8(x, 0xFFFFC2EEu), 4));  /* bclr #4,(abs) */
  lift_charge(x, 0x12F44);
  int wasSet = !c->zf;                                        /* bne.w loc_12FBC */
  lift_charge_bcc(x, 0x12F4A, wasSet);

  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1];
  uint32_t saved_a1 = c->a[1], saved_a2 = c->a[2], saved_a3 = c->a[3];

  if (wasSet)
  {
    goto loc_12FBC;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 0);                  /* btst #0,(abs) */
  lift_charge(x, 0x12F4E);
  int have = c->zf;                                            /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x12F54, have);
  if (have)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w8(x, 0xFFFFC2FEu, alu_bclr(c, lift_r8(x, 0xFFFFC2FEu), 3));  /* bclr #3,(abs) */
  lift_charge(x, 0x12F58);
  {
    int wasSet2 = !c->zf;                                        /* bne.w loc_12F6C */
    lift_charge_bcc(x, 0x12F5E, wasSet2);
    if (!wasSet2)
    {
      lift_w8(x, 0xFFFFC2FEu, alu_bset(c, lift_r8(x, 0xFFFFC2FEu), 3));  /* bset #3,(abs) */
      lift_charge(x, 0x12F62);
      lift_charge(x, 0x12F68);                                       /* bra.w locret_15464 */
      lift_charge(x, 0x15464);                                       /* the shared far rts itself */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_12F6C */
  {
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a3);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a2);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
    lift_charge_movem(x, 0x12F6C);

    uint32_t saved_a4 = c->a[4];
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a4);            /* move.l a4,-(sp) */
    lift_charge(x, 0x12F70);

    c->a[4] = 0xFFFFC46Eu;                                    /* movea.l #$FFFFC46E,a4 */
    lift_charge(x, 0x12F72);
    c->a[4] += SEW(lift_r16(x, 0xFFFFC472u));                 /* adda.w (abs),a4 */
    lift_charge(x, 0x12F78);
    c->a[2] = 0xFFFFC6CEu;                                     /* movea.l #$FFFFC6CE,a2 */
    lift_charge(x, 0x12F7C);
    alu_btst(c, lift_r8(x, c->a[4] + 2), 7);                   /* btst #7,2(a4) */
    lift_charge(x, 0x12F82);
    int away = !c->zf;                                          /* beq.w loc_12F90 */
    lift_charge_bcc(x, 0x12F88, c->zf);
    if (away)
    {
      c->a[2] += 0x364;                                         /* adda.w #$364,a2 */
      lift_charge(x, 0x12F8C);
    }

    /* loc_12F90 */
    setw(&c->d[0], alu_movew(c, 0));                            /* clr.w d0 */
    lift_charge(x, 0x12F90);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[4] + 3)));      /* move.b 3(a4),d0 */
    lift_charge(x, 0x12F92);
    c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;                /* move.l (sp)+,a4 */
    lift_charge(x, 0x12F96);
    c->a[2] = SEW(lift_r16(x, c->a[2] + 0x22));                  /* movea.w $22(a2),a2 */
    lift_charge(x, 0x12F98);
    setw(&c->d[1], alu_movew(c, 5));                             /* move.w #5,d1 */
    lift_charge(x, 0x12F9C);

    int found = 0;
    for (;;)
    {
      /* loc_12FA0 */
      alu_cmpb(c, lift_r8(x, c->a[2] + 0x66), W(c->d[0]) & 0xFF);  /* cmp.b $66(a2),d0 */
      lift_charge(x, 0x12FA0);
      int eq = c->zf;                                              /* beq.w loc_12FB4 */
      lift_charge_bcc(x, 0x12FA4, eq);
      if (eq) { found = 1; break; }

      c->a[2] += 0x80;                                             /* adda.w #$80,a2 */
      lift_charge(x, 0x12FA8);
      uint32_t nd1 = W(W(c->d[1]) - 1);                            /* dbf d1,loc_12FA0 */
      setw(&c->d[1], nd1);
      int taken = (nd1 != 0xFFFF);
      lift_charge_dbcc(x, 0x12FAC, taken, !taken);
      if (!taken) break;
    }

    if (!found)
    {
      lift_charge(x, 0x12FB0);                                     /* bra.w loc_1303A */
      goto loc_1303A;
    }

    /* loc_12FB4 */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x52)));     /* move.w $52(a2),d0 */
    lift_charge(x, 0x12FB4);
    lift_charge(x, 0x12FB8);                                        /* bra.w loc_12FD0 */
    goto loc_12FD0;
  }

loc_12FBC:
  {
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a3);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a2);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
    lift_charge_movem(x, 0x12FBC);

    lift_w16(x, 0xFFFFB89Cu, alu_addw(c, 0x64, lift_r16(x, 0xFFFFB89Cu)));  /* add.w #$64,(abs) */
    lift_charge(x, 0x12FC0);
    lift_w16(x, 0xFFFFB8A2u, alu_addw(c, 0xA, lift_r16(x, 0xFFFFB8A2u)));   /* add.w #$A,(abs) */
    lift_charge(x, 0x12FC6);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBED8u)));         /* move.w (abs),d0 */
    lift_charge(x, 0x12FCC);
  }

loc_12FD0:
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));                        /* asl.w #7,d0 */
  lift_charge(x, 0x12FD0);
  c->a[3] = 0xFFFFB04Au;                                              /* movea.w #$B04A,a3 */
  lift_charge(x, 0x12FD2);
  c->a[3] += SEW(c->d[0]);                                            /* adda.w d0,a3 */
  lift_charge(x, 0x12FD6);
  lift_call(x, 0x12FD8, 4, Team_SelectBlocks);                        /* bsr.w sub_13040 */
  if (x->declined) return;
  lift_w16(x, c->a[2], alu_addw(c, 1, lift_r16(x, c->a[2])));         /* addq.w #1,(a2) */
  lift_charge(x, 0x12FDC);

  alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 5);                            /* btst #5,(abs) */
  lift_charge(x, 0x12FDE);
  {
    int g5 = !c->zf;                                                    /* beq.w loc_1300C */
    lift_charge_bcc(x, 0x12FE4, c->zf);
    if (g5)
    {
      alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 6);                          /* btst #6,(abs) */
      lift_charge(x, 0x12FE8);
      int g6 = !c->zf;                                                    /* bne.w loc_13004 */
      lift_charge_bcc(x, 0x12FEE, g6);
      if (!g6)
      {
        alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);                       /* btst #6,$62(a3) */
        lift_charge(x, 0x12FF2);
        int side = !c->zf;                                                 /* bne.w loc_1300C */
        lift_charge_bcc(x, 0x12FF8, side);
        if (!side)
        {
          lift_w16(x, c->a[2] + 0x354, alu_addw(c, 1, lift_r16(x, c->a[2] + 0x354)));  /* addq.w #1,$354(a2) */
          lift_charge(x, 0x12FFC);
          lift_charge(x, 0x13000);                                          /* bra.w loc_1300C */
        }
      }
      else
      {
        /* loc_13004 */
        alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);                        /* btst #6,$62(a3) */
        lift_charge(x, 0x13004);
        int side2 = !c->zf;                                                  /* bne.s loc_12FFC */
        lift_charge_bcc(x, 0x1300A, side2);
        if (side2)
        {
          lift_w16(x, c->a[2] + 0x354, alu_addw(c, 1, lift_r16(x, c->a[2] + 0x354)));  /* addq.w #1,$354(a2) */
          lift_charge(x, 0x12FFC);
          lift_charge(x, 0x13000);                                          /* bra.w loc_1300C */
        }
      }
    }
  }

  /* loc_1300C */
  {
    uint32_t saved_a2b = c->a[2];
    c->a[7] -= 4; lift_w32(x, c->a[7], saved_a2b);                      /* move.l a2,-(sp) */
    lift_charge(x, 0x1300C);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC466u)));              /* move.w (abs),d0 */
    lift_charge(x, 0x1300E);
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));                 /* add.w d0,d0 */
    lift_charge(x, 0x13012);
    c->a[2] += SEW(c->d[0]);                                             /* adda.w d0,a2 */
    lift_charge(x, 0x13014);
    lift_w16(x, c->a[2] + 0x34A, alu_addw(c, 1, lift_r16(x, c->a[2] + 0x34A)));  /* addq.w #1,$34A(a2) */
    lift_charge(x, 0x13016);
    c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;                        /* move.l (sp)+,a2 */
    lift_charge(x, 0x1301A);
  }

  setw(&c->d[0], alu_movew(c, 0));                                       /* clr.w d0 */
  lift_charge(x, 0x1301C);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3] + 0x66)));               /* move.b $66(a3),d0 */
  lift_charge(x, 0x1301E);
  setw(&c->d[0], alu_addw(c, 0xE8, W(c->d[0])));                          /* add.w #$E8,d0 */
  lift_charge(x, 0x13022);
  lift_w8(x, c->a[2] + SEW(c->d[0]), alu_addb(c, 1, lift_r8(x, c->a[2] + SEW(c->d[0]))));  /* addq.b #1,(a2,d0.w) */
  lift_charge(x, 0x13026);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0x26)));              /* move.w $26(a1),d0 */
  lift_charge(x, 0x1302A);
  {
    int none = c->nf;                                                       /* bmi.w loc_1303A */
    lift_charge_bcc(x, 0x1302E, none);
    if (!none)
    {
      setw(&c->d[0], alu_addw(c, 0xE8, W(c->d[0])));                        /* add.w #$E8,d0 */
      lift_charge(x, 0x13032);
      lift_w8(x, c->a[1] + SEW(c->d[0]), alu_addb(c, 1, lift_r8(x, c->a[1] + SEW(c->d[0]))));  /* addq.b #1,(a1,d0.w) */
      lift_charge(x, 0x13036);
    }
  }

loc_1303A:
  /* movem.l (sp)+,d0-d1/a1-a3 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x1303A);

  lift_charge(x, 0x1303E);                                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_785E 0x00785Eu  /* ROM: 4-entry word table {$12C,$258,$4B0,$1E} */

/*
 * Lookup_D050Table (sub_784E; called from sub_7814/sub_15AA4)
 *   out: d0 = word_785E[R_UNK_D050] (word-indexed lookup)
 */
void Lookup_D050Table(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD050u)));  /* move.w (abs),d0 */
  lift_charge(x, 0x784E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));                /* asl.w #1,d0 */
  lift_charge(x, 0x7852);
  c->a[0] = TBL_785E;                                          /* lea word_785E(pc),a0 */
  lift_charge(x, 0x7854);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d0 */
  lift_charge(x, 0x7858);

  lift_charge(x, 0x785C);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_7814 (called from sub_7CB0)
 * Looks up a base delay from Lookup_D050Table; if the period is < 3 (a
 * pre-season/exhibition-like check via $FFFFC466) or $FFFFD048 is
 * nonzero, overrides it to $258. Stores that value to three timers
 * ($C468/$C46C/$B048), then rolls a random offset (Rng_NextScaled of
 * half the value) and subtracts it from the $B048 timer. Sets a flag
 * bit at $FFFFC2EA.
 */
void sub_7814(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x7814, 4, Lookup_D050Table);     /* bsr.w sub_784E */

  alu_cmpw(c, 3, lift_r16(x, 0xFFFFC466u));      /* cmp.w #3,(C466).w */
  lift_charge(x, 0x7818);
  int lt = c->nf != c->vf;                       /* blt.w: N!=V */
  lift_charge_bcc(x, 0x781E, lt);
  if (!lt)
  {
    alu_tstw(c, lift_r16(x, 0xFFFFD048u));       /* tst.w (D048).w */
    lift_charge(x, 0x7822);
    int nz = !c->zf;                             /* bne.w */
    lift_charge_bcc(x, 0x7826, nz);
    if (!nz)
    {
      setw(&c->d[0], alu_movew(c, 0x258));       /* move.w #$258,d0 */
      lift_charge(x, 0x782A);
    }
  }

  lift_w16(x, 0xFFFFC468u, W(c->d[0]));          /* move.w d0,(C468).w */
  lift_charge(x, 0x782E);
  lift_w16(x, 0xFFFFC46Cu, W(c->d[0]));          /* move.w d0,(C46C).w */
  lift_charge(x, 0x7832);
  lift_w16(x, 0xFFFFB048u, W(c->d[0]));          /* move.w d0,(B048).w */
  lift_charge(x, 0x7836);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));    /* asr.w #1,d0 */
  lift_charge(x, 0x783A);

  lift_call(x, 0x783C, 6, Rng_NextScaled);       /* jsr sub_11086 */

  lift_w16(x, 0xFFFFB048u, alu_subw(c, W(c->d[0]), lift_r16(x, 0xFFFFB048u)));  /* sub.w d0,(B048).w */
  lift_charge(x, 0x7842);
  lift_w8(x, 0xFFFFC2EAu, alu_bset(c, lift_r8(x, 0xFFFFC2EAu), 0));  /* bset #0,(C2EA).w — byte op */
  lift_charge(x, 0x7846);

  lift_charge(x, 0x784C);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_CompareRoster24 (sub_FECF8; called from sub_F3E4)
 * Compares $24(home) - $24(away); if unequal and the away side's value
 * is higher, swaps the a0/a1 team-block pointers so a0 ends up the
 * "winning" side. Then compares $28(a0) against $13 (result unused —
 * the branch always lands on the very next instruction either way, so
 * only its cycle cost differs by taken/not-taken). All registers are
 * saved/restored via the movem; this routine has no externally visible
 * effect beyond cycles and flags. Exact role TBD.
 */
void Team_CompareRoster24(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d[3] = { c->d[0], c->d[1], c->d[2] };
  uint32_t saved_a[3] = { c->a[0], c->a[1], c->a[2] };

  /* movem.l d0-d2/a0-a2,-(sp): a2 pushed first (high addr) ... d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[0]);
  lift_charge_movem(x, 0xFECF8);

  c->a[0] = 0xFFFFC6CEu;                                  /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFECFC);
  c->a[1] = 0xFFFFCA32u;                                  /* movea.l #$FFFFCA32,a1 */
  lift_charge(x, 0xFED02);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x24)));  /* move.w $24(a0),d0 */
  lift_charge(x, 0xFED08);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[1] + 0x24), W(c->d[0])));  /* sub.w $24(a1),d0 */
  lift_charge(x, 0xFED0C);
  int eq = c->zf;                                           /* beq.w loc_FED24 */
  lift_charge_bcc(x, 0xFED10, eq);

  if (!eq)
  {
    int nonneg = !c->nf;                                     /* bpl.w loc_FED1A */
    lift_charge_bcc(x, 0xFED14, nonneg);
    if (!nonneg)
    {
      uint32_t t = c->a[0];                                  /* exg a0,a1 */
      c->a[0] = c->a[1];
      c->a[1] = t;
      lift_charge(x, 0xFED18);
    }

    /* loc_FED1A */
    alu_cmpw(c, 0x13, lift_r16(x, c->a[0] + 0x28));           /* cmp.w #$13,$28(a0) */
    lift_charge(x, 0xFED1A);
    int ne = !c->zf;                                           /* bne.w *+4 (always lands at loc_FED24) */
    lift_charge_bcc(x, 0xFED20, ne);
  }

  /* loc_FED24: movem.l (sp)+,d0-d2/a0-a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFED24);

  lift_charge(x, 0xFED28);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_17C8E 0x00017C8Eu  /* ROM: 9-word table copied below */

/*
 * Reset_D048Table (sub_17C72; called from ROM:7724)
 * Sets R_UNK_D064's byte to $FF, then copies 9 words from ROM
 * (TBL_17C8E) into RAM at $FFFFD048.
 */
void Reset_D048Table(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFD064u, 0xFF);                          /* st (abs) */
  lift_charge(x, 0x17C72);
  c->a[0] = 0xFFFFD048u;                                    /* movea.l #$FFFFD048,a0 */
  lift_charge(x, 0x17C76);
  c->a[1] = TBL_17C8E;                                       /* movea.l #word_17C8E,a1 */
  lift_charge(x, 0x17C7C);
  setw(&c->d[0], alu_movew(c, 8));                           /* move.w #8,d0 */
  lift_charge(x, 0x17C82);

  for (;;)
  {
    lift_w16(x, c->a[0], alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1)+,(a0)+ */
    c->a[1] += 2;
    c->a[0] += 2;
    lift_charge(x, 0x17C86);
    uint32_t nd0 = W(W(c->d[0]) - 1);                          /* dbf d0,loc_17C86 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x17C88, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x17C8C);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_TickPenaltyCountdownsBothTeams (sub_15A24; called from sub_EF92
 * and ROM:F9D4)
 * Clears R_PENALTY_TOTAL, runs Roster_TickPenaltyCountdowns for the
 * home team with a $10 tick delta, then tail-falls into it again (not
 * via bsr — its rts returns to THIS routine's caller) for the away
 * team with a delta of 1.
 */
void Roster_TickPenaltyCountdownsBothTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, R_PENALTY_TOTAL, alu_moveb(c, 0));           /* clr.b (abs) */
  lift_charge(x, 0x15A24);
  c->d[1] = alu_moveql(c, 0x10);                           /* moveq #$10,d1 */
  lift_charge(x, 0x15A28);
  c->a[0] = TEAM_HOME;                                      /* movea.w #$C6CE,a0 */
  lift_charge(x, 0x15A2A);
  lift_call(x, 0x15A2E, 4, Roster_TickPenaltyCountdowns);   /* bsr.w sub_15A38 */
  if (x->declined) return;
  c->d[1] = alu_moveql(c, 1);                               /* moveq #1,d1 */
  lift_charge(x, 0x15A32);
  c->a[0] += TEAM_SIZE;                                      /* adda.w #$364,a0 */
  lift_charge(x, 0x15A34);

  Roster_TickPenaltyCountdowns(x);                            /* fall-through tail */
}

/*
 * Roster_ResetPenaltySlots (sub_130BE; called from sub_13068/sub_137FC)
 *   in: a2 = roster block base (same $66-indexed, 26-slot layout as
 *       Roster_TickPenaltyCountdowns)
 *   For each slot: unless $66(slot) is the $FFFC sentinel, resets
 *   $32(slot) to $1000; then if $66(slot) was $FFFD, sets it to $FFFE.
 */
void Roster_ResetPenaltySlots(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x32);                          /* moveq #$32,d0 */
  lift_charge(x, 0x130BE);

  for (;;)
  {
    /* loc_130C0 */
    alu_cmpw(c, 0xFFFC, lift_r16(x, c->a[2] + 0x66 + SEW(c->d[0])));  /* cmp.w #$FFFC,$66(a2,d0.w) */
    lift_charge(x, 0x130C0);
    int skip = c->zf;                                        /* beq.w loc_130E0 */
    lift_charge_bcc(x, 0x130C6, skip);
    if (!skip)
    {
      lift_w16(x, c->a[2] + 0x32 + SEW(c->d[0]), alu_movew(c, 0x1000));  /* move.w #$1000,$32(a2,d0.w) */
      lift_charge(x, 0x130CA);
      alu_cmpw(c, 0xFFFD, lift_r16(x, c->a[2] + 0x66 + SEW(c->d[0])));  /* cmp.w #$FFFD,$66(a2,d0.w) */
      lift_charge(x, 0x130D0);
      int ne = !c->zf;                                        /* bne.w loc_130E0 */
      lift_charge_bcc(x, 0x130D6, ne);
      if (!ne)
      {
        lift_w16(x, c->a[2] + 0x66 + SEW(c->d[0]), alu_movew(c, 0xFFFE));  /* move.w #$FFFE,$66(a2,d0.w) */
        lift_charge(x, 0x130DA);
      }
    }

    /* loc_130E0 */
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));               /* subq.w #2,d0 */
    lift_charge(x, 0x130E0);
    int cont = !c->nf;                                          /* bpl.s loc_130C0 */
    lift_charge_bcc(x, 0x130E2, cont);
    if (!cont) break;
  }

  lift_charge(x, 0x130E4);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_TEAM_RATINGS 0x000FE18Eu  /* ROM: TeamOverallRatings byte table */

/*
 * Lookup_TeamOverallRating (sub_FE172; called from sub_FCFB8)
 *   in:  a2 = team block (uses $28(a2) as a byte index)
 *   out: d0 = TeamOverallRatings[$28(a2)] (zero-extended byte)
 *   All registers except a2/a7/d0 are saved/restored via the movem —
 *   staged faithfully even though they're pure scratch here.
 */
void Lookup_TeamOverallRating(lift_ctx *x)
{
  rcpu_t *c = x->c;
  /* movem.l d1-a6,-(sp): push order a6,a5,a4,a3,a2,a1,a0,d7,d6,d5,d4,d3,d2,d1 */
  uint32_t saved[14] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1]
  };
  for (int i = 0; i < 14; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE172);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x28)));  /* move.w $28(a2),d0 */
  lift_charge(x, 0xFE176);
  c->a[0] = TBL_TEAM_RATINGS;                                  /* movea.l #TeamOverallRatings,a0 */
  lift_charge(x, 0xFE17A);
  setw(&c->d[1], alu_movew(c, 0));                              /* clr.w d1 */
  lift_charge(x, 0xFE180);
  setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[0]))));  /* move.b (a0,d0.w),d1 */
  lift_charge(x, 0xFE182);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));                     /* move.w d1,d0 */
  lift_charge(x, 0xFE186);

  /* movem.l (sp)+,d1-a6: pop order d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6 */
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFE188);

  lift_charge(x, 0xFE18C);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_UpdateResultFlag (sub_13068; called from sub_13056)
 *   in: a3, a2 = two team blocks (comparison sides)
 * Runs Roster_ResetPenaltySlots(a2), clears $16(a3), and (unless
 * R_UNK_D058 is nonzero) compares $24(a3) - $24(a2): if equal, bails;
 * otherwise sets $16(a3) to 3, then to 5 if that difference was
 * negative.
 */
void Roster_UpdateResultFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  lift_call(x, 0x13068, 4, Roster_ResetPenaltySlots);      /* bsr.w sub_130BE */
  if (x->declined) return;
  lift_w16(x, a3 + 0x16, alu_movew(c, 0));                  /* clr.w $16(a3) */
  lift_charge(x, 0x1306C);
  alu_movew(c, lift_r16(x, 0xFFFFD058u));                   /* tst.w (abs) */
  lift_charge(x, 0x13070);
  int skip = !c->zf;                                          /* bne.w locret_15464 */
  lift_charge_bcc(x, 0x13074, skip);
  if (skip)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x24)));      /* move.w $24(a3),d0 */
  lift_charge(x, 0x13078);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[2] + 0x24), W(c->d[0])));  /* sub.w $24(a2),d0 */
  lift_charge(x, 0x1307C);
  int same = c->zf;                                            /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x13080, same);
  if (same)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, a3 + 0x16, alu_movew(c, 3));                    /* move.w #3,$16(a3) */
  lift_charge(x, 0x13084);
  alu_movew(c, W(c->d[0]));                                    /* tst.w d0 */
  lift_charge(x, 0x1308A);
  int nonneg = !c->nf;                                          /* bpl.w locret_15464 */
  lift_charge_bcc(x, 0x1308C, nonneg);
  if (nonneg)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, a3 + 0x16, alu_movew(c, 5));                     /* move.w #5,$16(a3) */
  lift_charge(x, 0x13090);

  lift_charge(x, 0x13096);                                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_CountActiveSlots (sub_123DE; called from sub_12002)
 *   in:  a2 = roster block base (same $66-indexed, 26-slot layout as
 *        Roster_TickPenaltyCountdowns/Roster_ResetPenaltySlots)
 *   out: $24(a2) = d1, a count starting at 6 and decremented (floor 4)
 *        once per slot whose $66 field is positive with bits 4-6 all
 *        clear (bit5 gets cleared as a side effect of the check).
 */
void Roster_CountActiveSlots(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[1] = alu_moveql(c, 6);                              /* moveq #6,d1 */
  lift_charge(x, 0x123DE);
  c->d[0] = alu_moveql(c, 0x32);                            /* moveq #$32,d0 */
  lift_charge(x, 0x123E0);

  for (;;)
  {
    /* loc_123E2 */
    alu_movew(c, lift_r16(x, c->a[2] + 0x66 + SEW(c->d[0])));  /* tst.w $66(a2,d0.w) */
    lift_charge(x, 0x123E2);
    int le = c->zf || c->nf;                                   /* ble.w loc_1240E */
    lift_charge_bcc(x, 0x123E6, le);

    if (!le)
    {
      lift_w8(x, c->a[2] + 0x66 + W(c->d[0]),
              alu_bclr(c, lift_r8(x, c->a[2] + 0x66 + SEW(c->d[0])), 5));  /* bclr #5,$66(a2,d0.w) */
      lift_charge(x, 0x123EA);
      alu_btst(c, lift_r8(x, c->a[2] + 0x66 + SEW(c->d[0])), 6);  /* btst #6,$66(a2,d0.w) */
      lift_charge(x, 0x123F0);
      int b6 = !c->zf;                                           /* bne.w loc_1240E */
      lift_charge_bcc(x, 0x123F6, b6);
      if (!b6)
      {
        alu_btst(c, lift_r8(x, c->a[2] + 0x66 + SEW(c->d[0])), 4);  /* btst #4,$66(a2,d0.w) */
        lift_charge(x, 0x123FA);
        int b4 = !c->zf;                                           /* bne.w loc_1240E */
        lift_charge_bcc(x, 0x12400, b4);
        if (!b4)
        {
          alu_cmpw(c, 4, W(c->d[1]));                               /* cmp.w #4,d1 */
          lift_charge(x, 0x12404);
          int atFloor = c->zf;                                       /* beq.w loc_1240E */
          lift_charge_bcc(x, 0x12408, atFloor);
          if (!atFloor)
          {
            setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));              /* subq.w #1,d1 */
            lift_charge(x, 0x1240C);
          }
        }
      }
    }

    /* loc_1240E */
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));                     /* subq.w #2,d0 */
    lift_charge(x, 0x1240E);
    int cont = !c->nf;                                                /* bpl.s loc_123E2 */
    lift_charge_bcc(x, 0x12410, cont);
    if (!cont) break;
  }

  lift_w16(x, c->a[2] + 0x24, alu_movew(c, W(c->d[1])));               /* move.w d1,$24(a2) */
  lift_charge(x, 0x12412);

  lift_charge(x, 0x12416);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_ResetActiveCount (sub_7800; called from sub_77F4, tail-fallen
 * into from Roster_ResetActiveCountHome below)
 *   in: a2 = roster block base
 *   Sets $24(a2) to 6, then resets all 26 $66-indexed slot fields to
 *   the $FFFE sentinel.
 */
void Roster_ResetActiveCount(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, c->a[2] + 0x24, alu_movew(c, 6));            /* move.w #6,$24(a2) */
  lift_charge(x, 0x7800);
  c->d[0] = alu_moveql(c, 0x32);                            /* moveq #$32,d0 */
  lift_charge(x, 0x7806);

  for (;;)
  {
    /* loc_7808 */
    lift_w16(x, c->a[2] + 0x66 + SEW(c->d[0]), alu_movew(c, 0xFFFE));  /* move.w #$FFFE,$66(a2,d0.w) */
    lift_charge(x, 0x7808);
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));               /* subq.w #2,d0 */
    lift_charge(x, 0x780E);
    int cont = !c->nf;                                          /* bpl.s loc_7808 */
    lift_charge_bcc(x, 0x7810, cont);
    if (!cont) break;
  }

  lift_charge(x, 0x7812);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_ResetActiveCountHome (sub_77F4; called from sub_7CB0 and
 * ROM:135D8)
 * Runs Roster_ResetActiveCount for the home team, then tail-falls into
 * it again (not via bsr — its rts returns to THIS routine's caller)
 * for the away team.
 */
void Roster_ResetActiveCountHome(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = TEAM_HOME;                                       /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x77F4);
  lift_call(x, 0x77F8, 4, Roster_ResetActiveCount);          /* bsr.w sub_7800 */
  if (x->declined) return;
  c->a[2] += TEAM_SIZE;                                       /* adda.w #$364,a2 */
  lift_charge(x, 0x77FC);

  Roster_ResetActiveCount(x);                                  /* fall-through tail */
}

/*
 * Sfx_ResetQueueState (sub_17718; called from ROM:FCE5C)
 * Resets a small state block: $FFFFD5AE = $FFFF, $FFFFD5B0 byte = $FF,
 * $FFFFD5B2/D5B4 cleared, and returns a0 = $FFFFD5B8 (a fixed cursor).
 */
void Sfx_ResetQueueState(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFD5AEu, alu_movew(c, 0xFFFF));         /* move.w #$FFFF,(abs) */
  lift_charge(x, 0x17718);
  lift_w8(x, 0xFFFFD5B0u, 0xFF);                            /* st (abs) */
  lift_charge(x, 0x1771E);
  lift_w16(x, 0xFFFFD5B2u, alu_movew(c, 0));                /* clr.w (abs) */
  lift_charge(x, 0x17722);
  lift_w16(x, 0xFFFFD5B4u, alu_movew(c, 0));                /* clr.w (abs) */
  lift_charge(x, 0x17726);
  c->a[0] = 0xFFFFD5B8u;                                     /* movea.w #$D5B8,a0 */
  lift_charge(x, 0x1772A);

  lift_charge(x, 0x1772E);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Calc_QueueDelayCEF0 (sub_17AC8; called from sub_F739E)
 * Bails via the shared far rts if R_UNK_D048 < 2 or == 4. Otherwise
 * picks a base of 7 (or $B if R_CONTROLS_MODE is nonzero), subtracts
 * R_UNK_D04A, and stores the result to R_UNK_CEF0.
 */
void Calc_QueueDelayCEF0(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, 2, lift_r16(x, 0xFFFFD048u));               /* cmp.w #2,(abs) */
  lift_charge(x, 0x17AC8);
  int lt = (c->nf != c->vf);                                /* blt.w locret_15464 */
  lift_charge_bcc(x, 0x17ACE, lt);
  if (lt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_cmpw(c, 4, lift_r16(x, 0xFFFFD048u));                /* cmp.w #4,(abs) */
  lift_charge(x, 0x17AD2);
  int eq4 = c->zf;                                            /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x17AD8, eq4);
  if (eq4)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->d[0] = alu_moveql(c, 7);                                 /* moveq #7,d0 */
  lift_charge(x, 0x17ADC);
  alu_movew(c, lift_r16(x, 0xFFFFD046u));                       /* tst.w (abs) */
  lift_charge(x, 0x17ADE);
  int fourPad = !c->zf;                                          /* beq.w loc_17AEA */
  lift_charge_bcc(x, 0x17AE2, c->zf);
  if (fourPad)
  {
    setw(&c->d[0], alu_movew(c, 0xB));                          /* move.w #$B,d0 */
    lift_charge(x, 0x17AE6);
  }

  /* loc_17AEA */
  setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFD04Au), W(c->d[0])));  /* sub.w (abs),d0 */
  lift_charge(x, 0x17AEA);
  lift_w16(x, 0xFFFFCEF0u, alu_movew(c, W(c->d[0])));            /* move.w d0,(abs) */
  lift_charge(x, 0x17AEE);

  lift_charge(x, 0x17AF2);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Queue_AdvanceHomeCursor (sub_F7144; called from ROM:1792E)
 *   out: d1 = word_BF5E[R_UNK_BF56] (index doubled first); bumps
 *        R_UNK_BF56 if it was negative; a1 = TEAM_HOME (set after the
 *        movem pop, so it's a real persistent output, not scratch).
 *   d0/a0 saved/restored.
 */
void Queue_AdvanceHomeCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  /* movem.l d0/a0,-(sp): a0 pushed first (high addr), d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF7144);

  c->a[0] = 0xFFFFBF5Eu;                                    /* movea.l #$FFFFBF5E,a0 */
  lift_charge(x, 0xF7148);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBF56u)));    /* move.w (abs),d0 */
  lift_charge(x, 0xF714E);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));        /* add.w d0,d0 */
  lift_charge(x, 0xF7152);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d1 */
  lift_charge(x, 0xF7154);
  alu_cmpw(c, 0, lift_r16(x, 0xFFFFBF56u));                    /* cmp.w #0,(abs) */
  lift_charge(x, 0xF7158);
  int ge = (c->nf == c->vf);                                    /* bge.w loc_F7166 */
  lift_charge_bcc(x, 0xF715E, ge);
  if (!ge)
  {
    lift_w16(x, 0xFFFFBF56u, alu_addw(c, 1, lift_r16(x, 0xFFFFBF56u)));  /* addq.w #1,(abs) */
    lift_charge(x, 0xF7162);
  }

  /* loc_F7166: movem.l (sp)+,d0/a0 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF7166);

  c->a[1] = TEAM_HOME;                                          /* movea.l #$FFFFC6CE,a1 */
  lift_charge(x, 0xF716A);

  lift_charge(x, 0xF7170);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Queue_AdvanceAwayCursor (sub_F7172; called from ROM:17938)
 * Sibling of Queue_AdvanceHomeCursor, same pattern for
 * $FFFFBF5C/$FFFFBF54, a1 = TEAM_HOME+TEAM_SIZE.
 */
void Queue_AdvanceAwayCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF7172);

  c->a[0] = 0xFFFFBF5Cu;                                    /* movea.l #$FFFFBF5C,a0 */
  lift_charge(x, 0xF7176);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBF54u)));    /* move.w (abs),d0 */
  lift_charge(x, 0xF717C);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));        /* add.w d0,d0 */
  lift_charge(x, 0xF7180);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d1 */
  lift_charge(x, 0xF7182);
  alu_cmpw(c, 0, lift_r16(x, 0xFFFFBF54u));                    /* cmp.w #0,(abs) */
  lift_charge(x, 0xF7186);
  int ge = (c->nf == c->vf);                                    /* bge.w loc_F7194 */
  lift_charge_bcc(x, 0xF718C, ge);
  if (!ge)
  {
    lift_w16(x, 0xFFFFBF54u, alu_addw(c, 1, lift_r16(x, 0xFFFFBF54u)));  /* addq.w #1,(abs) */
    lift_charge(x, 0xF7190);
  }

  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF7194);

  c->a[1] = TEAM_HOME + TEAM_SIZE;                              /* movea.l #$FFFFCA32,a1 */
  lift_charge(x, 0xF7198);

  lift_charge(x, 0xF719E);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Queue_AdvanceHomeCursor2 (sub_F727C; called from ROM:17942)
 * Sibling of Queue_AdvanceHomeCursor, same pattern for
 * $FFFFBF62/$FFFFBF5A, a1 = TEAM_HOME.
 */
void Queue_AdvanceHomeCursor2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF727C);

  c->a[0] = 0xFFFFBF62u;                                    /* movea.l #$FFFFBF62,a0 */
  lift_charge(x, 0xF7280);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBF5Au)));    /* move.w (abs),d0 */
  lift_charge(x, 0xF7286);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));        /* add.w d0,d0 */
  lift_charge(x, 0xF728A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d1 */
  lift_charge(x, 0xF728C);
  alu_cmpw(c, 0, lift_r16(x, 0xFFFFBF5Au));                    /* cmp.w #0,(abs) */
  lift_charge(x, 0xF7290);
  int ge = (c->nf == c->vf);                                    /* bge.w loc_F729E */
  lift_charge_bcc(x, 0xF7296, ge);
  if (!ge)
  {
    lift_w16(x, 0xFFFFBF5Au, alu_addw(c, 1, lift_r16(x, 0xFFFFBF5Au)));  /* addq.w #1,(abs) */
    lift_charge(x, 0xF729A);
  }

  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF729E);

  c->a[1] = TEAM_HOME;                                          /* movea.l #$FFFFC6CE,a1 */
  lift_charge(x, 0xF72A2);

  lift_charge(x, 0xF72A8);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Queue_AdvanceAwayCursor2 (sub_F72AA; called from ROM:1794C)
 * Sibling of Queue_AdvanceHomeCursor, same pattern for
 * $FFFFBF60/$FFFFBF58, a1 = TEAM_HOME+TEAM_SIZE.
 */
void Queue_AdvanceAwayCursor2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF72AA);

  c->a[0] = 0xFFFFBF60u;                                    /* movea.l #$FFFFBF60,a0 */
  lift_charge(x, 0xF72AE);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBF58u)));    /* move.w (abs),d0 */
  lift_charge(x, 0xF72B4);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));        /* add.w d0,d0 */
  lift_charge(x, 0xF72B8);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d1 */
  lift_charge(x, 0xF72BA);
  alu_cmpw(c, 0, lift_r16(x, 0xFFFFBF58u));                    /* cmp.w #0,(abs) */
  lift_charge(x, 0xF72BE);
  int ge = (c->nf == c->vf);                                    /* bge.w loc_F72CC */
  lift_charge_bcc(x, 0xF72C4, ge);
  if (!ge)
  {
    lift_w16(x, 0xFFFFBF58u, alu_addw(c, 1, lift_r16(x, 0xFFFFBF58u)));  /* addq.w #1,(abs) */
    lift_charge(x, 0xF72C8);
  }

  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF72CC);

  c->a[1] = TEAM_HOME + TEAM_SIZE;                              /* movea.l #$FFFFCA32,a1 */
  lift_charge(x, 0xF72D0);

  lift_charge(x, 0xF72D6);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_InitPlaybackState (sub_FE510; called from ROM:FB4E and
 * sub_14062)
 *   in: d0 = a value stored verbatim to R_UNK_D6B4 (all registers
 *       otherwise saved/restored via the movem — pure scratch)
 *   Resets a playback state block: D6B4=d0, D6B6/D6BA (longs) cleared,
 *   D6AE=1, D6B0 cleared, D6BE byte force-set to $FF.
 */
void Sfx_InitPlaybackState(lift_ctx *x)
{
  rcpu_t *c = x->c;
  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE510);

  lift_w16(x, 0xFFFFD6B4u, alu_movew(c, W(c->d[0])));        /* move.w d0,(abs) */
  lift_charge(x, 0xFE514);
  lift_w32(x, 0xFFFFD6B6u, alu_movel(c, 0));                  /* move.l #0,(abs) */
  lift_charge(x, 0xFE518);
  lift_w32(x, 0xFFFFD6BAu, alu_movel(c, 0));                  /* move.l #0,(abs) */
  lift_charge(x, 0xFE520);
  lift_w16(x, 0xFFFFD6AEu, alu_movew(c, 1));                  /* move.w #1,(abs) */
  lift_charge(x, 0xFE528);
  lift_w16(x, 0xFFFFD6B0u, alu_movew(c, 0));                  /* clr.w (abs) */
  lift_charge(x, 0xFE52E);
  lift_w8(x, 0xFFFFD6BEu, 0xFF);                               /* st (abs) */
  lift_charge(x, 0xFE532);

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFE536);

  lift_charge(x, 0xFE53A);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_SetPlaybackFlag (sub_FE53C; called from sub_F3E4)
 *   in: d0 = value stored to R_UNK_D6BE
 *   Sets R_UNK_C2FC bit0, stores d0 to R_UNK_D6BE.
 */
void Sfx_SetPlaybackFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2FCu, alu_bset(c, lift_r8(x, 0xFFFFC2FCu), 0));  /* bset #0,(abs) */
  lift_charge(x, 0xFE53C);
  lift_w16(x, 0xFFFFD6BEu, alu_movew(c, W(c->d[0])));            /* move.w d0,(abs) */
  lift_charge(x, 0xFE542);

  lift_charge(x, 0xFE546);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_UpdateBothResultFlags (sub_13056; called from sub_130E6)
 * Runs Roster_TickPenaltyCountdownsBothTeams, sets a2=home/a3=away,
 * runs Roster_UpdateResultFlag(a3=away,a2=home) via bsr, swaps a2/a3,
 * then tail-falls into Roster_UpdateResultFlag again (not via bsr —
 * its rts returns to THIS routine's caller) with the sides reversed.
 */
void Roster_UpdateBothResultFlags(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x13056, 4, Roster_TickPenaltyCountdownsBothTeams);  /* bsr.w sub_15A24 */
  if (x->declined) return;
  c->a[2] = TEAM_HOME;                                       /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x1305A);
  c->a[3] = c->a[2] + TEAM_SIZE;                               /* lea $364(a2),a3 */
  lift_charge(x, 0x1305E);
  lift_call(x, 0x13062, 4, Roster_UpdateResultFlag);           /* bsr.w sub_13068 */
  if (x->declined) return;
  {
    uint32_t t = c->a[2];                                        /* exg a2,a3 */
    c->a[2] = c->a[3];
    c->a[3] = t;
  }
  lift_charge(x, 0x13066);

  Roster_UpdateResultFlag(x);                                    /* fall-through tail */
}

/*
 * Team_LoadAndCacheTeamData (sub_171BE; called from Team_RefreshDataCache x2)
 *   in: d0 = team id, a2 = team block (home/away)
 *   Stores the team id at $28(a2) and caches its TeamData_* pointer
 *   (ROM table at $30E, indexed by id) at $1E(a2). If R_UNK_D058 is
 *   nonzero, copies 2 longs then skips 2 longs of source before
 *   copying 12 more into $16A(a2); otherwise copies 14 longs
 *   contiguously (source offset +8 onward) into $16A(a2).
 */
void Team_LoadAndCacheTeamData(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, c->a[2] + 0x28, alu_movew(c, W(c->d[0])));         /* move.w d0,$28(a2) */
  lift_charge(x, 0x171BE);
  c->a[0] = 0x30E;                                                /* movea.w #$30E,a0 */
  lift_charge(x, 0x171C2);
  setw(&c->d[0], alu_aslw(c, c->d[0], 2));                        /* asl.w #2,d0 */
  lift_charge(x, 0x171C6);
  lift_w32(x, c->a[2] + 0x1E,
           alu_movel(c, lift_r32(x, c->a[0] + SEW(c->d[0]))));    /* move.l (a0,d0.w),$1E(a2) */
  lift_charge(x, 0x171C8);

  alu_movew(c, lift_r16(x, 0xFFFFD058u));                         /* tst.w (abs) */
  lift_charge(x, 0x171CE);
  int haveFlag = !c->zf;
  lift_charge_bcc(x, 0x171D2, c->zf);                              /* beq.w loc_171F4 */

  if (haveFlag)
  {
    c->a[0] = lift_r32(x, c->a[2] + 0x1E);                        /* move.l $1E(a2),a0 */
    lift_charge(x, 0x171D6);
    c->a[0] = c->a[0] + SEW(lift_r16(x, c->a[0] + 6));             /* adda.w 6(a0),a0 */
    lift_charge(x, 0x171DA);
    c->a[1] = c->a[2] + 0x16A;                                    /* lea $16A(a2),a1 */
    lift_charge(x, 0x171DE);

    lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[0])));      /* move.l (a0)+,(a1)+ */
    c->a[0] += 4; c->a[1] += 4;
    lift_charge(x, 0x171E2);
    lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[0])));      /* move.l (a0)+,(a1)+ */
    c->a[0] += 4; c->a[1] += 4;
    lift_charge(x, 0x171E4);
    c->a[0] += 8;                                                  /* addq.w #8,a0 */
    lift_charge(x, 0x171E6);
    setw(&c->d[0], alu_movew(c, 0xB));                              /* move.w #$B,d0 */
    lift_charge(x, 0x171E8);

    for (;;)
    {
      lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[0])));    /* move.l (a0)+,(a1)+ */
      c->a[0] += 4; c->a[1] += 4;
      lift_charge(x, 0x171EC);
      uint32_t nd0 = W(W(c->d[0]) - 1);                             /* dbf d0,loc_171EC */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0x171EE, taken, !taken);
      if (!taken) break;
    }
    lift_charge(x, 0x171F2);                                        /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* loc_171F4 */
  c->d[0] = alu_moveql(c, 0xD);                                    /* moveq #$D,d0 */
  lift_charge(x, 0x171F4);
  c->a[0] = lift_r32(x, c->a[2] + 0x1E);                            /* move.l $1E(a2),a0 */
  lift_charge(x, 0x171F6);
  c->a[0] = c->a[0] + SEW(lift_r16(x, c->a[0] + 6));                 /* adda.w 6(a0),a0 */
  lift_charge(x, 0x171FA);
  c->a[0] += 8;                                                     /* addq.w #8,a0 */
  lift_charge(x, 0x171FE);
  c->a[1] = c->a[2] + 0x16A;                                        /* lea $16A(a2),a1 */
  lift_charge(x, 0x17200);

  for (;;)
  {
    lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[0])));       /* move.l (a0)+,(a1)+ */
    c->a[0] += 4; c->a[1] += 4;
    lift_charge(x, 0x17204);
    uint32_t nd0 = W(W(c->d[0]) - 1);                                /* dbf d0,loc_17204 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x17206, taken, !taken);
    if (!taken) break;
  }
  lift_charge(x, 0x1720A);                                           /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_RefreshDataCache (sub_17190; called from ROM:17B68)
 *   Loads home and away team ids from R_UNK_C330/C332, stashes a
 *   literal ($B04A/$B34A) at $22(a2), and calls
 *   Team_LoadAndCacheTeamData for each team block.
 */
void Team_RefreshDataCache(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/a0-a2,-(sp): push order a2,a1,a0,d0 */
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);
  lift_charge_movem(x, 0x17190);

  c->a[2] = TEAM_HOME;                                              /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x17194);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC330u)));            /* move.w (abs),d0 */
  lift_charge(x, 0x17198);
  lift_w16(x, c->a[2] + 0x22, alu_movew(c, 0xB04A));                 /* move.w #$B04A,$22(a2) */
  lift_charge(x, 0x1719C);
  lift_call(x, 0x171A2, 4, Team_LoadAndCacheTeamData);                /* bsr.w sub_171BE */
  if (x->declined) return;

  c->a[2] = TEAM_HOME + TEAM_SIZE;                                    /* movea.w #$CA32,a2 */
  lift_charge(x, 0x171A6);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC332u)));             /* move.w (abs),d0 */
  lift_charge(x, 0x171AA);
  lift_w16(x, c->a[2] + 0x22, alu_movew(c, 0xB34A));                  /* move.w #$B34A,$22(a2) */
  lift_charge(x, 0x171AE);
  lift_call(x, 0x171B4, 4, Team_LoadAndCacheTeamData);                 /* bsr.w sub_171BE */
  if (x->declined) return;

  /* movem.l (sp)+,d0/a0-a2: pop order d0,a0,a1,a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x171B8);

  lift_charge(x, 0x171BC);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TestOrientedSign (sub_F6C44; called from sub_B0E8)
 *   in: a3 = on-ice object slot
 *   d0 = R_UNK_B75E, negated unless $62(a3) bit7 is set. Returns d0=1
 *   if that value is positive/zero, else d0=0. (ROM bytes $F6C62-
 *   $F6CA4 are unreachable dead code: both branches out of loc_F6C58
 *   already cover every path before it, so nothing ever falls or
 *   jumps in there — not lifted.)
 */
void Object_TestOrientedSign(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.w d0-d1,-(sp) */
  c->a[7] -= 4;
  lift_w16(x, c->a[7] + 0, W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xF6C44);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB75Eu)));              /* move.w (abs),d0 */
  lift_charge(x, 0xF6C48);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);                          /* btst #7,$62(a3) */
  lift_charge(x, 0xF6C4C);
  int bneTaken = !c->zf;
  lift_charge_bcc(x, 0xF6C52, bneTaken);                                /* bne.w loc_F6C58 */
  if (!bneTaken)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                           /* neg.w d0 */
    lift_charge(x, 0xF6C56);
  }

  /* loc_F6C58 */
  alu_movew(c, W(c->d[0]));                                            /* tst.w d0 */
  lift_charge(x, 0xF6C58);
  int bpl = !c->nf;
  lift_charge_bcc(x, 0xF6C5A, bpl);                                    /* bpl.w loc_F6CB0 */
  if (!bpl)
  {
    lift_charge(x, 0xF6C5E);                                           /* bra.w loc_F6CA8 */
    /* loc_F6CA8 */
    setw(&c->d[0], alu_movew(c, 0));                                   /* move.w #0,d0 */
    lift_charge(x, 0xF6CA8);
    lift_charge(x, 0xF6CAC);                                           /* bra.w loc_F6CB4 */
  }
  else
  {
    /* loc_F6CB0 */
    setw(&c->d[0], alu_movew(c, 1));                                   /* move.w #1,d0 */
    lift_charge(x, 0xF6CB0);
  }

  /* loc_F6CB4: movem.w (sp)+,d0-d1: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  c->d[1] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  lift_charge_movem(x, 0xF6CB4);

  lift_charge(x, 0xF6CB8);                                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TestFacingOctantAttr (sub_F6D22; called from Object_ComputeApproachGate)
 *   in: a3 = on-ice object slot, d0 = signed X delta (from caller)
 *   Computes ((-d0+8)&7) + $54(a3), &7, compares to 4: if > 4, masks
 *   ~$4(a3) with $800; else masks $4(a3) with $800 directly. Result
 *   (and its Z flag) is the caller-visible outcome.
 */
void Object_TestFacingOctantAttr(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.w d0-d1,-(sp) */
  c->a[7] -= 4;
  lift_w16(x, c->a[7] + 0, W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xF6D22);

  setw(&c->d[0], alu_negw(c, W(c->d[0])));                     /* neg.w d0 */
  lift_charge(x, 0xF6D26);
  setw(&c->d[0], alu_addw(c, 8, W(c->d[0])));                  /* addq.w #8,d0 */
  lift_charge(x, 0xF6D28);
  setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));                  /* and.w #7,d0 */
  lift_charge(x, 0xF6D2A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x54)));    /* move.w $54(a3),d1 */
  lift_charge(x, 0xF6D2E);
  setw(&c->d[1], alu_addw(c, W(c->d[0]), W(c->d[1])));          /* add.w d0,d1 */
  lift_charge(x, 0xF6D32);
  setw(&c->d[1], alu_andw(c, 7, W(c->d[1])));                   /* and.w #7,d1 */
  lift_charge(x, 0xF6D34);
  alu_cmpw(c, 4, W(c->d[1]));                                   /* cmp.w #4,d1 */
  lift_charge(x, 0xF6D38);
  int gt = (!c->zf && c->nf == c->vf);
  lift_charge_bcc(x, 0xF6D3C, gt);                               /* bgt.w loc_F6D50 */

  if (!gt)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 4)));      /* move.w 4(a3),d0 */
    lift_charge(x, 0xF6D40);
    setw(&c->d[0], alu_eorw(c, 0xFFFF, W(c->d[0])));              /* eor.w #$FFFF,d0 */
    lift_charge(x, 0xF6D44);
    setw(&c->d[0], alu_andw(c, 0x800, W(c->d[0])));               /* and.w #$800,d0 */
    lift_charge(x, 0xF6D48);
    lift_charge(x, 0xF6D4C);                                      /* bra.w loc_F6D58 */
  }
  else
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 4)));      /* move.w 4(a3),d0 */
    lift_charge(x, 0xF6D50);
    setw(&c->d[0], alu_andw(c, 0x800, W(c->d[0])));               /* and.w #$800,d0 */
    lift_charge(x, 0xF6D54);
  }

  /* loc_F6D58: movem.w (sp)+,d0-d1: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  c->d[1] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  lift_charge_movem(x, 0xF6D58);

  lift_charge(x, 0xF6D5C);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ComputeApproachGate (sub_F6C0A; called from sub_F693E)
 *   in: a3 = on-ice object slot
 *   d0 = -pos.x; d1 = $108 (negated if $62(a3) bit7 clear) - $14(a3);
 *   runs Vector_ToOctant(d0,d1) then Object_TestFacingOctantAttr; if
 *   the latter left Z set, d1 is left as computed, else d1 <- $92E.
 */
void Object_ComputeApproachGate(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[1], alu_movew(c, 0x7FC));                          /* move.w #$7FC,d1 */
  lift_charge(x, 0xF6C0A);

  /* movem.w d0-d1,-(sp) */
  c->a[7] -= 4;
  lift_w16(x, c->a[7] + 0, W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xF6C0E);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3])));            /* move.w (a3),d0 */
  lift_charge(x, 0xF6C12);
  setw(&c->d[0], alu_negw(c, W(c->d[0])));                       /* neg.w d0 */
  lift_charge(x, 0xF6C14);
  setw(&c->d[1], alu_movew(c, 0x108));                            /* move.w #$108,d1 */
  lift_charge(x, 0xF6C16);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);                     /* btst #7,$62(a3) */
  lift_charge(x, 0xF6C1A);
  int bneTaken = !c->zf;
  lift_charge_bcc(x, 0xF6C20, bneTaken);                          /* bne.w loc_F6C26 */
  if (!bneTaken)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));                      /* neg.w d1 */
    lift_charge(x, 0xF6C24);
  }

  /* loc_F6C26 */
  setw(&c->d[1], alu_subw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[1])));  /* sub.w $14(a3),d1 */
  lift_charge(x, 0xF6C26);
  lift_call(x, 0xF6C2A, 6, Vector_ToOctant);                       /* jsr sub_10676 */
  if (x->declined) return;
  lift_call(x, 0xF6C30, 6, Object_TestFacingOctantAttr);           /* jsr sub_F6D22 */
  if (x->declined) return;

  /* movem.w (sp)+,d0-d1: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  c->d[1] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  lift_charge_movem(x, 0xF6C36);

  int beqTaken = c->zf;
  lift_charge_bcc(x, 0xF6C3A, beqTaken);                           /* beq.w locret_F6C42 */
  if (!beqTaken)
  {
    setw(&c->d[1], alu_movew(c, 0x92E));                           /* move.w #$92E,d1 */
    lift_charge(x, 0xF6C3E);
  }

  lift_charge(x, 0xF6C42);                                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}


/*
 * Team_ResolveDisplayNamePtr (sub_FD5AE; called from ROM:178E6/178FA)
 *   in: a2 = team block (home/away)
 *   Chases a2's cached TeamData_* pointer ($1E(a2)) through two more
 *   word-offset hops to reach a name-table entry; if that entry's
 *   selector word is 2, substitutes a fixed "Home"/"Visitors" string
 *   (length-prefixed) instead, chosen by whether a2 is the home block.
 *   Returns the resolved pointer in a1.
 */
void Team_ResolveDisplayNamePtr(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/a0/a2,-(sp): push order a2,a0,d0 */
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);
  lift_charge_movem(x, 0xFD5AE);

  c->a[1] = c->a[2];                                              /* move.l a2,a1 */
  lift_charge(x, 0xFD5B2);
  c->a[1] = lift_r32(x, c->a[1] + 0x1E);                          /* move.l $1E(a1),a1 */
  lift_charge(x, 0xFD5B4);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1] + 4));               /* add.w 4(a1),a1 */
  lift_charge(x, 0xFD5B8);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1]));                   /* add.w (a1),a1 */
  lift_charge(x, 0xFD5BC);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1]));                   /* add.w (a1),a1 */
  lift_charge(x, 0xFD5BE);

  alu_cmpw(c, 2, lift_r16(x, c->a[1]));                            /* cmp.w #2,(a1) */
  lift_charge(x, 0xFD5C0);
  int bneTaken = !c->zf;
  lift_charge_bcc(x, 0xFD5C4, bneTaken);                            /* bne.w loc_FD5DE */

  if (!bneTaken)
  {
    c->a[1] = 0xFD5E4;                                              /* move.l #word_FD5E4,a1 */
    lift_charge(x, 0xFD5C8);
    alu_cmpl(c, TEAM_HOME, c->a[2]);                                /* cmp.l #$FFFFC6CE,a2 */
    lift_charge(x, 0xFD5CE);
    int beqTaken = c->zf;
    lift_charge_bcc(x, 0xFD5D4, beqTaken);                          /* beq.w loc_FD5DE */
    if (!beqTaken)
    {
      c->a[1] = 0xFD5EA;                                            /* move.l #word_FD5EA,a1 */
      lift_charge(x, 0xFD5D8);
    }
  }

  /* loc_FD5DE: movem.l (sp)+,d0/a0/a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFD5DE);

  lift_charge(x, 0xFD5E2);                                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sram_SyncTeamRecord (sub_F9BE2; called from ROM:9B02/9B60 and others)
 *   in: a0 = RAM buffer, d1 = team/slot index (from caller)
 *   Computes an SRAM byte offset (d1*16 + $B60) and either writes 16
 *   bytes from a0 to SRAM or reads 16 bytes from SRAM into a0,
 *   depending on R_UNK_C2F8 bit6 (set = write, clear = read). This
 *   entry clears the bit first (a sibling entry, sub_F9C18, sets it
 *   then falls into the same shared body — not itself lifted).
 */
static void Sram_SyncTeamRecord_body(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d1/a0-a1,-(sp) */
  c->a[7] -= 16;
  lift_w32(x, c->a[7] + 0, c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0xF9BE8);

  c->d[0] = alu_movel(c, W(c->d[1]));                              /* move.l d1,d0 */
  lift_charge(x, 0xF9BEC);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));                       /* asl.w #4,d0 */
  lift_charge(x, 0xF9BEE);
  c->d[0] = alu_addl(c, 0xB60, c->d[0]);                            /* add.l #$B60,d0 */
  lift_charge(x, 0xF9BF0);
  c->d[1] = alu_moveql(c, 0x10);                                    /* moveq #$10,d1 */
  lift_charge(x, 0xF9BF6);
  alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);                          /* btst #6,(abs) */
  lift_charge(x, 0xF9BF8);
  int beqTaken = c->zf;
  lift_charge_bcc(x, 0xF9BFE, beqTaken);                             /* beq.w loc_F9C0C */
  if (!beqTaken)
  {
    lift_call(x, 0xF9C02, 6, SRAM_WriteBytes);                       /* jsr SRAM_WriteBytes */
    if (x->declined) return;
    lift_charge(x, 0xF9C08);                                         /* bra.w loc_F9C12 */
  }
  else
  {
    lift_call(x, 0xF9C0C, 6, SRAM_ReadBytes);                        /* jsr SRAM_ReadBytes */
    if (x->declined) return;
  }

  /* loc_F9C12: movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7] + 0);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0xF9C12);

  lift_charge(x, 0xF9C16);                                            /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Sram_SyncTeamRecord entry: clear R_UNK_C2F8 bit6 (read mode), then the shared body. */
void Sram_SyncTeamRecord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bclr #6,(abs) */
  lift_charge(x, 0xF9BE2);
  Sram_SyncTeamRecord_body(x);
}

/*
 * Sram_SyncTeamRecordWrite (sub_F9C18) — the WRITE twin of Sram_SyncTeamRecord: sets R_UNK_C2F8
 * bit6 and branches into the very same shared body, which dispatches on
 * that bit to SRAM_WriteBytes instead of SRAM_ReadBytes. Two instructions.
 */
void Sram_SyncTeamRecordWrite(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bset #6,(abs) */
  lift_charge(x, 0xF9C18);
  lift_charge_bcc(x, 0xF9C1E, 1);                                        /* bra -> body */
  Sram_SyncTeamRecord_body(x);
}

/*
 * Sram_SyncFixedD45ABlock (sub_F9C68; called from sub_F739E and others)
 *   Reads or writes a fixed 128-byte SRAM block (index $DA0) to/from
 *   RAM buffer $FFFFD45A, depending on R_UNK_C2F8 bit6 (same
 *   read/write convention as Sram_SyncTeamRecord). This entry clears
 *   the bit first (sub_F9C5E sets it then falls into the same shared
 *   body — not itself lifted).
 */
static void Sram_SyncFixedD45ABlock_body(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF9C6E);

  c->d[1] = alu_movel(c, 0x80);                                     /* move.l #$80,d1 */
  lift_charge(x, 0xF9C72);
  c->d[0] = alu_movel(c, 0xDA0);                                    /* move.l #$DA0,d0 */
  lift_charge(x, 0xF9C78);
  c->a[0] = 0xFFFFD45A;                                             /* move.l #$FFFFD45A,a0 */
  lift_charge(x, 0xF9C7E);
  alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);                          /* btst #6,(abs) */
  lift_charge(x, 0xF9C84);
  int beqTaken = c->zf;
  lift_charge_bcc(x, 0xF9C8A, beqTaken);                             /* beq.w loc_F9C98 */
  if (!beqTaken)
  {
    lift_call(x, 0xF9C8E, 6, SRAM_WriteBytes);                       /* jsr SRAM_WriteBytes */
    if (x->declined) return;
    lift_charge(x, 0xF9C94);                                         /* bra.w loc_F9C9E */
  }
  else
  {
    lift_call(x, 0xF9C98, 6, SRAM_ReadBytes);                        /* jsr SRAM_ReadBytes */
    if (x->declined) return;
  }

  /* loc_F9C9E: movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF9C9E);

  lift_charge(x, 0xF9CA2);                                            /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Sram_SyncFixedD45ABlock entry: clear R_UNK_C2F8 bit6 (read mode), then the shared body. */
void Sram_SyncFixedD45ABlock(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bclr #6,(abs) */
  lift_charge(x, 0xF9C68);
  Sram_SyncFixedD45ABlock_body(x);
}

/*
 * Sram_SyncFixedD45ABlockWrite (sub_F9C5E) — the WRITE twin of Sram_SyncFixedD45ABlock: sets R_UNK_C2F8
 * bit6 and branches into the very same shared body, which dispatches on
 * that bit to SRAM_WriteBytes instead of SRAM_ReadBytes. Two instructions.
 */
void Sram_SyncFixedD45ABlockWrite(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bset #6,(abs) */
  lift_charge(x, 0xF9C5E);
  lift_charge_bcc(x, 0xF9C64, 1);                                        /* bra -> body */
  Sram_SyncFixedD45ABlock_body(x);
}

/*
 * Sram_SyncHomeAwayRecord (sub_F6E8A; called from sub_7CB0)
 *   Runs Sram_SyncTeamRecord for the current team ($FFFFC330,
 *   sign-extended) into buffer $FFFFCF36, then derives a display
 *   value from byte 8 of that buffer (0 -> $50, else the byte itself
 *   masked to a byte) and stores it to R_UNK_C310.
 */
void Sram_SyncHomeAwayRecord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF6E8A);

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC330u)));            /* move.w (abs),d1 */
  lift_charge(x, 0xF6E8E);
  c->d[1] = alu_extl(c, c->d[1]);                                    /* ext.l d1 */
  lift_charge(x, 0xF6E92);
  c->a[0] = 0xFFFFCF36;                                               /* move.l #$FFFFCF36,a0 */
  lift_charge(x, 0xF6E94);
  lift_call(x, 0xF6E9A, 6, Sram_SyncTeamRecord);                      /* jsr sub_F9BE2 */
  if (x->declined) return;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 8)));              /* move.b 8(a0),d0 */
  lift_charge(x, 0xF6EA0);
  int beqTaken = c->zf;
  lift_charge_bcc(x, 0xF6EA4, beqTaken);                               /* beq.w loc_F6EB0 */
  if (!beqTaken)
  {
    setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));                     /* and.w #$FF,d0 */
    lift_charge(x, 0xF6EA8);
    lift_charge(x, 0xF6EAC);                                           /* bra.w loc_F6EB4 */
  }
  else
  {
    setw(&c->d[0], alu_movew(c, 0x50));                                /* move.w #$50,d0 */
    lift_charge(x, 0xF6EB0);
  }

  /* loc_F6EB4 */
  lift_w16(x, 0xFFFFC310u, alu_movew(c, W(c->d[0])));                 /* move.w d0,(abs) */
  lift_charge(x, 0xF6EB4);

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF6EB8);

  lift_charge(x, 0xF6EBC);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Game_ResetPerGameState (sub_FD73C; called from sub_F739E)
 *   Clears three per-game RAM blocks: $FFFFD572 (19 words), the
 *   combined home+away team state block at TEAM_HOME (868 words —
 *   both team blocks in one sweep), $FFFFC3A4 (37 words — the
 *   tracked-entries table Overlay_ProcessTrackedEntries walks), and
 *   six individual status words.
 */
void Game_ResetPerGameState(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xFD73C);

  c->a[0] = 0xFFFFD572;                                              /* movea.l #$FFFFD572,a0 */
  lift_charge(x, 0xFD740);
  c->d[0] = alu_moveql(c, 0x12);                                     /* moveq #$12,d0 */
  lift_charge(x, 0xFD746);
  for (;;)
  {
    lift_w16(x, c->a[0], alu_movew(c, 0));                           /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0xFD748);
    uint32_t nd0 = W(W(c->d[0]) - 1);                                /* dbf d0,loc_FD748 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFD74A, taken, !taken);
    if (!taken) break;
  }

  c->a[0] = TEAM_HOME;                                               /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFD74E);
  setw(&c->d[0], alu_movew(c, 0x363));                                /* move.w #$363,d0 */
  lift_charge(x, 0xFD754);
  for (;;)
  {
    lift_w16(x, c->a[0], alu_movew(c, 0));                           /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0xFD758);
    uint32_t nd0 = W(W(c->d[0]) - 1);                                /* dbf d0,loc_FD758 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFD75A, taken, !taken);
    if (!taken) break;
  }

  c->a[0] = 0xFFFFC3A4;                                               /* movea.l #$FFFFC3A4,a0 */
  lift_charge(x, 0xFD75E);
  c->d[0] = alu_moveql(c, 0x24);                                      /* moveq #$24,d0 */
  lift_charge(x, 0xFD764);
  for (;;)
  {
    lift_w16(x, c->a[0], alu_movew(c, 0));                           /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0xFD766);
    uint32_t nd0 = W(W(c->d[0]) - 1);                                /* dbf d0,loc_FD766 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFD768, taken, !taken);
    if (!taken) break;
  }

  lift_w16(x, 0xFFFFC2F2u, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD76C);
  lift_w16(x, 0xFFFFC2F4u, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD770);
  lift_w16(x, 0xFFFFC2F8u, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD774);
  lift_w16(x, 0xFFFFC2FAu, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD778);
  lift_w16(x, 0xFFFFD42Eu, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD77C);
  lift_w16(x, 0xFFFFD43Eu, alu_movew(c, 0));                          /* clr.w (abs) */
  lift_charge(x, 0xFD780);

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFD784);

  lift_charge(x, 0xFD788);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_InitTableB04AFill (sub_FF3B0; called from ROM:FF262)
 *   Fills 81 (0x51) longs at $FFFFB04A with a pattern starting $FD80
 *   that steps down by 1 every other long, then 97 (0x61) more longs
 *   with a pattern starting $100 that steps up by 4 each long. Also
 *   sets R_UNK_BD1E=$14 and R_UNK_BF14=$50.
 */
void Object_InitTableB04AFill(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d1,-(sp) */
  c->a[7] -= 8;
  lift_w32(x, c->a[7] + 0, c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_charge_movem(x, 0xFF3B0);

  lift_w16(x, 0xFFFFBD1Eu, alu_movew(c, 0x14));                      /* move.w #$14,(abs) */
  lift_charge(x, 0xFF3B4);
  lift_w16(x, 0xFFFFBF14u, alu_movew(c, 0x50));                      /* move.w #$50,(abs) */
  lift_charge(x, 0xFF3BA);
  setw(&c->d[0], alu_movew(c, 0x50));                                /* move.w #$50,d0 */
  lift_charge(x, 0xFF3C0);
  c->a[0] = 0xFFFFB04A;                                               /* movea.l #$FFFFB04A,a0 */
  lift_charge(x, 0xFF3C4);
  c->d[1] = alu_movel(c, 0xFD80);                                     /* move.l #$FD80,d1 */
  lift_charge(x, 0xFF3CA);

  for (;;)
  {
    /* loc_FF3D0 */
    lift_w32(x, c->a[0], alu_movel(c, c->d[1]));                     /* move.l d1,(a0)+ */
    c->a[0] += 4;
    lift_charge(x, 0xFF3D0);
    alu_btst(c, W(c->d[0]), 0);                                       /* btst #0,d0 */
    lift_charge(x, 0xFF3D2);
    int bneTaken = !c->zf;
    lift_charge_bcc(x, 0xFF3D6, bneTaken);                            /* bne.w loc_FF3DC */
    if (!bneTaken)
    {
      setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));                     /* subq.w #1,d1 */
      lift_charge(x, 0xFF3DA);
    }

    /* loc_FF3DC */
    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                /* dbf d0,loc_FF3D0 */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0xFF3DC, taken, !taken);
      if (!taken) break;
    }
  }

  setw(&c->d[0], alu_movew(c, 0x60));                                 /* move.w #$60,d0 */
  lift_charge(x, 0xFF3E0);
  c->d[1] = alu_movel(c, 0x100);                                      /* move.l #$100,d1 */
  lift_charge(x, 0xFF3E4);

  for (;;)
  {
    /* loc_FF3EA */
    lift_w32(x, c->a[0], alu_movel(c, c->d[1]));                      /* move.l d1,(a0)+ */
    c->a[0] += 4;
    lift_charge(x, 0xFF3EA);
    c->d[1] = alu_addl(c, 4, c->d[1]);                                 /* addq.l #4,d1 */
    lift_charge(x, 0xFF3EC);
    uint32_t nd0 = W(W(c->d[0]) - 1);                                  /* dbf d0,loc_FF3EA */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFF3EE, taken, !taken);
    if (!taken) break;
  }

  /* movem.l (sp)+,d0-d1 */
  c->d[0] = lift_r32(x, c->a[7] + 0);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0xFF3F2);

  lift_charge(x, 0xFF3F6);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ResetAndQueueEvent (sub_FEFF0; called from sub_14A54, sub_F693E)
 *   in: a3 = object
 *   Clears three status flag bits, resets two overlay/tracking RAM
 *   locations, starts animation script $50C (Anim_SetScript), then
 *   runs the already-lifted Object_AdvanceStateMod8 (if $34(a3) is
 *   negative) or Object_QueueFrameFromTable (otherwise), and finally
 *   resets the frame cursor and expires the frame countdown.
 */
void Object_ResetAndQueueEvent(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/a0,-(sp): push order a0,d0 */
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);
  lift_charge_movem(x, 0xFEFF0);

  lift_w8(x, c->a[3] + 0x64, alu_bclr(c, lift_r8(x, c->a[3] + 0x64), 3));  /* bclr #3,$64(a3) */
  lift_charge(x, 0xFEFF4);
  lift_w8(x, c->a[3] + 0x62, alu_bclr(c, lift_r8(x, c->a[3] + 0x62), 5));  /* bclr #5,$62(a3) */
  lift_charge(x, 0xFEFFA);
  lift_w8(x, c->a[3] + 0x63, alu_bclr(c, lift_r8(x, c->a[3] + 0x63), 1));  /* bclr #1,$63(a3) */
  lift_charge(x, 0xFF000);
  lift_w16(x, 0xFFFFBF76u, alu_movew(c, 0));                               /* clr.w (abs) */
  lift_charge(x, 0xFF006);
  lift_w8(x, 0xFFFFBF6Cu, 0xFF);                                           /* st (abs), no flags */
  lift_charge(x, 0xFF00A);

  /* move.w d1,-(sp) */
  c->a[7] -= 2; lift_w16(x, c->a[7], W(c->d[1]));
  lift_charge(x, 0xFF00E);
  setw(&c->d[1], alu_movew(c, 0x50C));                                     /* move.w #$50C,d1 */
  lift_charge(x, 0xFF010);
  lift_call(x, 0xFF014, 6, Anim_SetScript);                                /* jsr sub_1073A */
  if (x->declined) return;

  alu_movew(c, lift_r16(x, c->a[3] + 0x34));                               /* tst.w $34(a3) */
  lift_charge(x, 0xFF01A);
  int bplTaken = !c->nf;
  lift_charge_bcc(x, 0xFF01E, bplTaken);                                   /* bpl.w loc_FF02C */
  if (bplTaken)
  {
    lift_call(x, 0xFF02C, 6, Object_QueueFrameFromTable);                  /* jsr sub_15A88 */
    if (x->declined) return;
  }
  else
  {
    lift_call(x, 0xFF022, 6, Object_AdvanceStateMod8);                     /* jsr sub_10646 */
    if (x->declined) return;
    lift_charge(x, 0xFF028);                                                /* bra.w loc_FF032 */
  }

  /* loc_FF032 */
  lift_w16(x, c->a[3] + 0x5A, alu_movew(c, 0));                            /* clr.w $5A(a3) */
  lift_charge(x, 0xFF032);
  lift_w8(x, c->a[3] + 0x5C, 0xFF);                                         /* st $5C(a3), no flags */
  lift_charge(x, 0xFF036);

  /* move.w (sp)+,d1: plain MOVE (not movem) - sets flags, word-only */
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[7]))); c->a[7] += 2;
  lift_charge(x, 0xFF03A);

  /* movem.l (sp)+,d0/a0 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFF03C);

  lift_charge(x, 0xFF040);                                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_ComputeAndSortLineStats (sub_F71F0; called from
 * Team_RefreshLineIndicators x2)
 *   in: a0 = team block (home/away)
 *   For 6 source bytes read from the team's TeamData_* stream, builds
 *   a 6x2-byte array at $FFFFBF20: byte0 = source byte - 1, byte1 =
 *   sum of 13 values looked up from a per-team byte table at
 *   a0(entry)+$1A2 (skipping 2 of the 13 lookup indices), then
 *   descending-bubble-sorts the 6 entries by byte1 until a full pass
 *   makes no swap.
 */
void Team_ComputeAndSortLineStats(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF71F0);

  c->a[2] = c->a[0];                                                  /* move.l a0,a2 */
  lift_charge(x, 0xF71F4);
  c->a[2] = alu_addl(c, 0x1A2, c->a[2]);                               /* add.l #$1A2,a2 */
  lift_charge(x, 0xF71F6);
  c->a[0] = lift_r32(x, c->a[0] + 0x1E);                               /* move.l $1E(a0),a0 */
  lift_charge(x, 0xF71FC);
  c->a[0] = c->a[0] + SEW(lift_r16(x, c->a[0] + 6));                    /* adda.w 6(a0),a0 */
  lift_charge(x, 0xF7200);
  c->a[1] = 0xFFFFBF20;                                                 /* movea.l #$FFFFBF20,a1 */
  lift_charge(x, 0xF7204);
  setw(&c->d[0], alu_movew(c, 5));                                      /* move.w #5,d0 */
  lift_charge(x, 0xF720A);

  for (;;)
  {
    /* loc_F720E */
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0])));                  /* move.b (a0)+,d1 */
    c->a[0] += 1;
    lift_charge(x, 0xF720E);
    setb(&c->d[1], alu_subb(c, 1, W(c->d[1]) & 0xFF));                  /* subq.b #1,d1 */
    lift_charge(x, 0xF7210);
    lift_w8(x, c->a[1], alu_moveb(c, W(c->d[1]) & 0xFF));               /* move.b d1,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xF7212);
    setw(&c->d[1], alu_extw(c, W(c->d[1]) & 0xFF));                     /* ext.w d1 */
    lift_charge(x, 0xF7214);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));                         /* asl.w #4,d1 */
    lift_charge(x, 0xF7216);
    lift_w8(x, c->a[1], alu_moveb(c, 0));                               /* clr.b (a1) */
    lift_charge(x, 0xF7218);
    setw(&c->d[1], alu_addw(c, 3, W(c->d[1])));                         /* addq.w #3,d1 */
    lift_charge(x, 0xF721A);
    setw(&c->d[7], alu_movew(c, 3));                                    /* move.w #3,d7 */
    lift_charge(x, 0xF721C);

    for (;;)
    {
      /* loc_F7220 */
      setw(&c->d[2], alu_movew(c, 0));                                   /* clr.w d2 */
      lift_charge(x, 0xF7220);
      setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[1]))));  /* move.b (a2,d1.w),d2 */
      lift_charge(x, 0xF7222);

      alu_cmpb(c, 9, W(c->d[7]) & 0xFF);                                 /* cmp.b #9,d7 */
      lift_charge(x, 0xF7226);
      int skip = c->zf;
      lift_charge_bcc(x, 0xF722A, skip);                                 /* beq.w loc_F7238 */
      if (!skip)
      {
        alu_cmpb(c, 0xD, W(c->d[7]) & 0xFF);                             /* cmp.b #$D,d7 */
        lift_charge(x, 0xF722E);
        skip = c->zf;
        lift_charge_bcc(x, 0xF7232, skip);                               /* beq.w loc_F7238 */
        if (!skip)
        {
          lift_w8(x, c->a[1], alu_addb(c, W(c->d[2]) & 0xFF, lift_r8(x, c->a[1])));  /* add.b d2,(a1) */
          lift_charge(x, 0xF7236);
        }
      }

      /* loc_F7238 */
      setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));                        /* addq.w #1,d1 */
      lift_charge(x, 0xF7238);
      setw(&c->d[7], alu_addw(c, 1, W(c->d[7])));                        /* addq.w #1,d7 */
      lift_charge(x, 0xF723A);
      alu_cmpb(c, 0x10, W(c->d[7]) & 0xFF);                              /* cmp.b #$10,d7 */
      lift_charge(x, 0xF723C);
      int innerTaken = !c->zf;
      lift_charge_bcc(x, 0xF7240, innerTaken);                            /* bne.s loc_F7220 */
      if (!innerTaken) break;
    }

    alu_moveb(c, lift_r8(x, c->a[1]));                                    /* tst.b (a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xF7242);

    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                   /* dbf d0,loc_F720E */
      setw(&c->d[0], nd0);
      int outerTaken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0xF7244, outerTaken, !outerTaken);
      if (!outerTaken) break;
    }
  }

  for (;;)
  {
    /* loc_F7248 */
    c->a[1] = 0xFFFFBF20;                                                 /* movea.l #$FFFFBF20,a1 */
    lift_charge(x, 0xF7248);
    setw(&c->d[1], alu_movew(c, 0));                                       /* clr.w d1 */
    lift_charge(x, 0xF724E);
    setw(&c->d[0], alu_movew(c, 4));                                       /* move.w #4,d0 */
    lift_charge(x, 0xF7250);

    for (;;)
    {
      /* loc_F7254 */
      setb(&c->d[6], alu_moveb(c, lift_r8(x, c->a[1] + 3)));               /* move.b 3(a1),d6 */
      lift_charge(x, 0xF7254);
      alu_cmpb(c, lift_r8(x, c->a[1] + 1), W(c->d[6]) & 0xFF);             /* cmp.b 1(a1),d6 */
      lift_charge(x, 0xF7258);
      int le = (c->zf || c->nf != c->vf);                                  /* ble */
      lift_charge_bcc(x, 0xF725C, le);                                     /* ble.w loc_F726C */
      if (!le)
      {
        setb(&c->d[1], 0xFF);                                              /* st d1: byte, flags unaffected */
        lift_charge(x, 0xF7260);
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1] + 2)));            /* move.w 2(a1),d2 */
        lift_charge(x, 0xF7262);
        lift_w16(x, c->a[1] + 2, alu_movew(c, lift_r16(x, c->a[1])));      /* move.w (a1),2(a1) */
        lift_charge(x, 0xF7266);
        lift_w16(x, c->a[1], alu_movew(c, W(c->d[2])));                    /* move.w d2,(a1) */
        lift_charge(x, 0xF726A);
      }

      /* loc_F726C */
      alu_movew(c, lift_r16(x, c->a[1]));                                  /* tst.w (a1)+ */
      c->a[1] += 2;
      lift_charge(x, 0xF726C);

      {
        uint32_t nd0b = W(W(c->d[0]) - 1);                                 /* dbf d0,loc_F7254 */
        setw(&c->d[0], nd0b);
        int innerTaken2 = (nd0b != 0xFFFF);
        lift_charge_dbcc(x, 0xF726E, innerTaken2, !innerTaken2);
        if (!innerTaken2) break;
      }
    }

    alu_movew(c, W(c->d[1]));                                              /* tst.w d1 */
    lift_charge(x, 0xF7272);
    int again = !c->zf;
    lift_charge_bcc(x, 0xF7274, again);                                    /* bne.s loc_F7248 */
    if (!again) break;
  }

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF7276);

  lift_charge(x, 0xF727A);                                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_RefreshLineIndicators (sub_F71A2; called from ROM:FCC7C/FCCBA)
 *   Clears two overlay RAM words, then for home then away: runs
 *   Team_ComputeAndSortLineStats on the team block, then
 *   Text_WriteNullThenByteFwd/Back into that team's two scratch
 *   arrays ($FFFFBF5E/BF62 home, $FFFFBF5C/BF60 away).
 */
void Team_RefreshLineIndicators(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  lift_w16(x, 0xFFFFBF54u, alu_movew(c, 0));                            /* clr.w (abs) */
  lift_charge(x, 0xF71A2);
  lift_w16(x, 0xFFFFBF56u, alu_movew(c, 0));                            /* clr.w (abs) */
  lift_charge(x, 0xF71A6);

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF71AA);

  c->a[0] = TEAM_HOME;                                                   /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xF71AE);
  lift_call(x, 0xF71B4, 4, Team_ComputeAndSortLineStats);                 /* bsr.w sub_F71F0 */
  if (x->declined) return;
  c->a[0] = 0xFFFFBF5E;                                                   /* movea.l #$FFFFBF5E,a0 */
  lift_charge(x, 0xF71B8);
  lift_call(x, 0xF71BE, 4, Text_WriteNullThenByteFwd);                    /* bsr.w sub_F72DA */
  if (x->declined) return;
  c->a[0] = 0xFFFFBF62;                                                   /* movea.l #$FFFFBF62,a0 */
  lift_charge(x, 0xF71C2);
  lift_call(x, 0xF71C8, 4, Text_WriteNullThenByteBack);                   /* bsr.w sub_F72FA */
  if (x->declined) return;

  c->a[0] = TEAM_HOME + TEAM_SIZE;                                        /* movea.l #$FFFFCA32,a0 */
  lift_charge(x, 0xF71CC);
  lift_call(x, 0xF71D2, 4, Team_ComputeAndSortLineStats);                 /* bsr.w sub_F71F0 */
  if (x->declined) return;
  c->a[0] = 0xFFFFBF5C;                                                   /* movea.l #$FFFFBF5C,a0 */
  lift_charge(x, 0xF71D6);
  lift_call(x, 0xF71DC, 4, Text_WriteNullThenByteFwd);                    /* bsr.w sub_F72DA */
  if (x->declined) return;
  c->a[0] = 0xFFFFBF60;                                                   /* movea.l #$FFFFBF60,a0 */
  lift_charge(x, 0xF71E0);
  lift_call(x, 0xF71E6, 4, Text_WriteNullThenByteBack);                   /* bsr.w sub_F72FA */
  if (x->declined) return;

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF71EA);

  lift_charge(x, 0xF71EE);                                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_SumLineStatBytes (sub_F737E; called from Team_ComputeLineDelta x2)
 *   in: a0 = team block (home/away)
 *   Runs Team_ComputeAndSortLineStats on the team, then sums the 7
 *   byte1 values (sign-extended) from the just-built $FFFFBF20 array
 *   into d1.
 */
void Team_SumLineStatBytes(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0xF737E, 4, Team_ComputeAndSortLineStats);              /* bsr.w sub_F71F0 */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, 6));                                     /* move.w #6,d0 */
  lift_charge(x, 0xF7382);
  c->a[0] = 0xFFFFBF20;                                                 /* movea.l #$FFFFBF20,a0 */
  lift_charge(x, 0xF7386);
  setw(&c->d[1], alu_movew(c, 0));                                      /* clr.w d1 */
  lift_charge(x, 0xF738C);

  for (;;)
  {
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + 1)));               /* move.b 1(a0),d2 */
    lift_charge(x, 0xF738E);
    setw(&c->d[2], alu_extw(c, W(c->d[2]) & 0xFF));                      /* ext.w d2 */
    lift_charge(x, 0xF7392);
    setw(&c->d[1], alu_addw(c, W(c->d[2]), W(c->d[1])));                 /* add.w d2,d1 */
    lift_charge(x, 0xF7394);
    alu_movew(c, lift_r16(x, c->a[0]));                                  /* tst.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0xF7396);

    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                  /* dbf d0,loc_F738E */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0xF7398, taken, !taken);
      if (!taken) break;
    }
  }

  lift_charge(x, 0xF739C);                                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_ComputeLineDelta (sub_F7318; called from ROM:FCE72)
 *   Sums each team's line-stat bytes via Team_SumLineStatBytes,
 *   caches them at R_UNK_BF12 (home)/BF14 (away), sets R_UNK_BF50 to
 *   1 if away-home is negative else 0, and classifies |away-home|
 *   into d0: -1 if < $5E, $22 if < $BD, else $23. (d0 is the
 *   caller-visible return value - not restored by this routine's own
 *   movem, which excludes d0.)
 */
void Team_ComputeLineDelta(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d1-a6,-(sp): push order a6..a0,d7..d1 (d1 lands lowest/top) */
  {
    uint32_t saved[14] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1]
    };
    for (i = 0; i < 14; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF7318);

  c->a[0] = TEAM_HOME;                                                    /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xF731C);
  lift_call(x, 0xF7322, 4, Team_SumLineStatBytes);                        /* bsr.w sub_F737E */
  if (x->declined) return;
  lift_w16(x, 0xFFFFBF12u, alu_movew(c, W(c->d[1])));                     /* move.w d1,(abs) */
  lift_charge(x, 0xF7326);

  c->a[0] = TEAM_HOME + TEAM_SIZE;                                         /* movea.l #$FFFFCA32,a0 */
  lift_charge(x, 0xF732A);
  lift_call(x, 0xF7330, 4, Team_SumLineStatBytes);                        /* bsr.w sub_F737E */
  if (x->declined) return;
  lift_w16(x, 0xFFFFBF14u, alu_movew(c, W(c->d[1])));                     /* move.w d1,(abs) */
  lift_charge(x, 0xF7334);

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFBF12u)));                  /* move.w (abs),d1 */
  lift_charge(x, 0xF7338);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFBF14u)));                  /* move.w (abs),d2 */
  lift_charge(x, 0xF733C);
  setw(&c->d[2], alu_subw(c, W(c->d[1]), W(c->d[2])));                     /* sub.w d1,d2 */
  lift_charge(x, 0xF7340);
  lift_w16(x, 0xFFFFBF50u, alu_movew(c, 0));                               /* move.w #0,(abs) */
  lift_charge(x, 0xF7342);

  alu_movew(c, W(c->d[2]));                                                /* tst.w d2 */
  lift_charge(x, 0xF7348);
  int neg1 = c->nf;
  lift_charge_bcc(x, 0xF734A, neg1);                                        /* bmi.w loc_F7354 */
  if (!neg1)
  {
    lift_w16(x, 0xFFFFBF50u, alu_movew(c, 1));                             /* move.w #1,(abs) */
    lift_charge(x, 0xF734E);
  }

  /* loc_F7354 */
  alu_movew(c, W(c->d[2]));                                                /* tst.w d2 */
  lift_charge(x, 0xF7354);
  int pos = !c->nf;
  lift_charge_bcc(x, 0xF7356, pos);                                         /* bpl.w loc_F735C */
  if (!pos)
  {
    setw(&c->d[2], alu_negw(c, W(c->d[2])));                                /* neg.w d2 */
    lift_charge(x, 0xF735A);
  }

  /* loc_F735C */
  setw(&c->d[0], alu_movew(c, 0xFFFF));                                    /* move.w #$FFFF,d0 */
  lift_charge(x, 0xF735C);
  alu_cmpw(c, 0x5E, W(c->d[2]));                                           /* cmp.w #$5E,d2 */
  lift_charge(x, 0xF7360);
  int lt1 = (c->nf != c->vf);                                              /* blt */
  lift_charge_bcc(x, 0xF7364, lt1);                                        /* blt.w loc_F7378 */
  if (!lt1)
  {
    setw(&c->d[0], alu_movew(c, 0x22));                                    /* move.w #$22,d0 */
    lift_charge(x, 0xF7368);
    alu_cmpw(c, 0xBD, W(c->d[2]));                                          /* cmp.w #$BD,d2 */
    lift_charge(x, 0xF736C);
    int lt2 = (c->nf != c->vf);                                            /* blt */
    lift_charge_bcc(x, 0xF7370, lt2);                                       /* blt.w loc_F7378 */
    if (!lt2)
    {
      setw(&c->d[0], alu_movew(c, 0x23));                                   /* move.w #$23,d0 */
      lift_charge(x, 0xF7374);
    }
  }

  /* loc_F7378: movem.l (sp)+,d1-a6: pop order d1..d7,a0..a6 */
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF7378);

  lift_charge(x, 0xF737C);                                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_ResetVoiceStateFlags (sub_FE548; called from ROM:FCE4)
 *   Sets R_UNK_D6B4 to -1 and clears R_UNK_C2FC bit0.
 */
void Sfx_ResetVoiceStateFlags(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFD6B4u, alu_movew(c, 0xFFFF));                          /* move.w #$FFFF,(abs) */
  lift_charge(x, 0xFE548);
  lift_w8(x, 0xFFFFC2FCu, alu_bclr(c, lift_r8(x, 0xFFFFC2FCu), 0));        /* bclr #0,(abs) */
  lift_charge(x, 0xFE54E);

  lift_charge(x, 0xFE554);                                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sfx_AdvanceCueStreamEntry (sub_FE326; called from sub_FE2C8)
 *   Reads a 2-byte {delay,cue} pair from the sequence pointer at
 *   R_UNK_D6B6, offset by the cursor R_UNK_D6B0, sign-extending both
 *   bytes into R_UNK_D6AE (delay) and R_UNK_D6B2 (cue). If the delay
 *   reads as -1 ($FFFF), the cursor is reset to 0 and this routine
 *   restarts from its own entry (self-loop). If the delay is -2
 *   ($FFFE), runs Sfx_ResetVoiceStateFlags.
 */
void Sfx_AdvanceCueStreamEntry(lift_ctx *x)
{
  rcpu_t *c = x->c;

  for (;;)
  {
    c->a[0] = lift_r32(x, 0xFFFFD6B6u);                                    /* move.l (abs).w,a0 */
    lift_charge(x, 0xFE326);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD6B0u)));                 /* move.w (abs).w,d0 */
    lift_charge(x, 0xFE32A);
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));                    /* add.w d0,d0 */
    lift_charge(x, 0xFE32E);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[0]))));       /* move.b (a0,d0.w),d1 */
    lift_charge(x, 0xFE330);
    setw(&c->d[1], alu_extw(c, W(c->d[1]) & 0xFF));                         /* ext.w d1 */
    lift_charge(x, 0xFE334);
    lift_w16(x, 0xFFFFD6AEu, alu_movew(c, W(c->d[1])));                     /* move.w d1,(abs) */
    lift_charge(x, 0xFE336);

    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[0]) + 1)));   /* move.b 1(a0,d0.w),d1 */
    lift_charge(x, 0xFE33A);
    setw(&c->d[1], alu_extw(c, W(c->d[1]) & 0xFF));                         /* ext.w d1 */
    lift_charge(x, 0xFE33E);
    lift_w16(x, 0xFFFFD6B2u, alu_movew(c, W(c->d[1])));                     /* move.w d1,(abs) */
    lift_charge(x, 0xFE340);

    alu_cmpw(c, 0xFFFF, lift_r16(x, 0xFFFFD6AEu));                           /* cmp.w #$FFFF,(abs) */
    lift_charge(x, 0xFE344);
    {
      int bneTaken = !c->zf;
      lift_charge_bcc(x, 0xFE34A, bneTaken);                                 /* bne.w loc_FE354 */
      if (!bneTaken)
      {
        lift_w16(x, 0xFFFFD6B0u, alu_movew(c, 0));                           /* clr.w (abs) */
        lift_charge(x, 0xFE34E);
        lift_charge(x, 0xFE352);                                             /* bra.s sub_FE326 */
        continue;
      }
    }

    /* loc_FE354 */
    alu_cmpw(c, 0xFFFE, lift_r16(x, 0xFFFFD6AEu));                           /* cmp.w #$FFFE,(abs) */
    lift_charge(x, 0xFE354);
    {
      int bneTaken2 = !c->zf;                                                /* bne.w locret_FE362 */
      lift_charge_bcc(x, 0xFE35A, bneTaken2);
      if (!bneTaken2)
      {
        lift_call(x, 0xFE35E, 4, Sfx_ResetVoiceStateFlags);                   /* bsr.w sub_FE548 */
        if (x->declined) return;
      }
    }

    lift_charge(x, 0xFE362);                                                 /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
}

/*
 * Object_ApproachAndSetScript (sub_DBBA; called from sub_B0E8, sub_D51C)
 *   in: a0 = target object, a3 = subject object, d3 = a running Y
 *       accumulator (persists across calls); also uses R_UNK_B762/
 *       B776/B7AA and a3's $54 facing/$62/$73/$76 flag bytes.
 *   Computes an octant-relative approach index (0-7) via
 *   Vector_ToOctant, then depending on distance thresholds either
 *   nudges a3's world Y toward the target (adjusting $28(a3), a
 *   Y-velocity-like field) or falls through a second distance check,
 *   arriving at a final index used to look up an animation script
 *   offset in word_DD1A (with a $178->$1AA override under specific
 *   octant/distance conditions). Runs Anim_SetScript with that
 *   offset, bumps two overlay-lead accumulators, and halves (via two
 *   single-bit arithmetic shifts each) $28(a3)/$2A(a3).
 */
void Object_ApproachAndSetScript(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));                    /* move.w (a0),d0 */
  lift_charge(x, 0xDBBA);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[3]), W(c->d[0])));         /* sub.w (a3),d0 */
  lift_charge(x, 0xDBBC);
  setw(&c->d[1], alu_movew(c, W(c->d[3])));                              /* move.w d3,d1 */
  lift_charge(x, 0xDBBE);
  setw(&c->d[1], alu_subw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[1])));  /* sub.w $14(a3),d1 */
  lift_charge(x, 0xDBC0);
  lift_call(x, 0xDBC4, 4, Vector_ToOctant);                               /* bsr.w sub_10676 */
  if (x->declined) return;
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[3] + 0x54), W(c->d[0])));  /* sub.w $54(a3),d0 */
  lift_charge(x, 0xDBC8);
  setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));                             /* and.w #7,d0 */
  lift_charge(x, 0xDBCC);
  setw(&c->d[3], alu_movew(c, W(c->d[0])));                               /* move.w d0,d3 */
  lift_charge(x, 0xDBD0);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));                             /* lsr.w #2,d0 */
  lift_charge(x, 0xDBD2);
  alu_btst(c, lift_r8(x, c->a[3] + 4), 3);                                /* btst #3,4(a3) */
  lift_charge(x, 0xDBD4);
  {
    int beqTaken = c->zf;
    lift_charge_bcc(x, 0xDBDA, beqTaken);                                  /* beq.w loc_DBE2 */
    if (!beqTaken)
    {
      setw(&c->d[0], alu_eorw(c, 1, W(c->d[0])));                          /* eor.w #1,d0 */
      lift_charge(x, 0xDBDE);
    }
  }

  /* loc_DBE2 */
  alu_cmpw(c, 8, lift_r16(x, 0xFFFFB762u));                               /* cmp.w #8,(abs) */
  lift_charge(x, 0xDBE2);
  {
    int gt1 = (!c->zf && c->nf == c->vf);                                  /* bgt */
    lift_charge_bcc(x, 0xDBE8, gt1);                                       /* bgt.w loc_DBFA */
    if (gt1) goto loc_DBFA;
  }
  alu_cmpw(c, 0x800, lift_r16(x, 0xFFFFB776u));                           /* cmp.w #$800,(abs) */
  lift_charge(x, 0xDBEC);
  {
    int gt2 = (!c->zf && c->nf == c->vf);                                  /* bgt */
    lift_charge_bcc(x, 0xDBF2, gt2);                                       /* bgt.w loc_DBFA */
    if (gt2) goto loc_DBFA;
  }
  lift_charge(x, 0xDBF6);                                                   /* bra.w loc_DC24 */
  goto loc_DC24;

loc_DBFA:
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);                            /* movem.l d0,-(sp) */
  lift_charge_movem(x, 0xDBFA);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3])));                     /* move.w (a3),d0 */
  lift_charge(x, 0xDBFE);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[0]), W(c->d[0])));          /* sub.w (a0),d0 */
  lift_charge(x, 0xDC00);
  alu_cmpw(c, 0x10, W(c->d[0]));                                          /* cmp.w #$10,d0 */
  lift_charge(x, 0xDC02);
  {
    int gt = (!c->zf && c->nf == c->vf);                                  /* bgt */
    lift_charge_bcc(x, 0xDC06, gt);                                        /* bgt.w loc_DC1C */
    if (gt) goto loc_DC1C;
  }
  alu_cmpw(c, 0xFFF0, W(c->d[0]));                                         /* cmp.w #$FFF0,d0 */
  lift_charge(x, 0xDC0A);
  {
    int lt = (c->nf != c->vf);                                            /* blt */
    lift_charge_bcc(x, 0xDC0E, lt);                                        /* blt.w loc_DC1C */
    if (lt) goto loc_DC1C;
  }
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;                           /* movem.l (sp)+,d0 */
  lift_charge_movem(x, 0xDC12);
  setw(&c->d[0], alu_addw(c, 6, W(c->d[0])));                              /* addq.w #6,d0 */
  lift_charge(x, 0xDC16);
  lift_charge(x, 0xDC18);                                                   /* bra.w loc_DCD0 */
  goto loc_DCD0;

loc_DC1C:
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;                           /* movem.l (sp)+,d0 */
  lift_charge_movem(x, 0xDC1C);
  lift_charge(x, 0xDC20);                                                   /* bra.w loc_DCD0 */
  goto loc_DCD0;

loc_DC24:
  setw(&c->d[0], alu_addw(c, 4, W(c->d[0])));                              /* addq.w #4,d0 */
  lift_charge(x, 0xDC24);
  alu_cmpw(c, 8, lift_r16(x, c->a[0] + 2));                                /* cmp.w #8,2(a0) */
  lift_charge(x, 0xDC26);
  {
    int ls = (c->cf || c->zf);                                            /* bls */
    lift_charge_bcc(x, 0xDC2C, ls);                                        /* bls.w loc_DCAA */
    if (ls) goto loc_DCAA;
  }
  alu_cmpw(c, 2, lift_r16(x, c->a[3] + 0x54));                            /* cmp.w #2,$54(a3) */
  lift_charge(x, 0xDC30);
  {
    int beq1 = c->zf;
    lift_charge_bcc(x, 0xDC36, beq1);                                      /* beq.w loc_DCD0 */
    if (beq1) goto loc_DCD0;
  }
  alu_cmpw(c, 6, lift_r16(x, c->a[3] + 0x54));                            /* cmp.w #6,$54(a3) */
  lift_charge(x, 0xDC3A);
  {
    int beq2 = c->zf;
    lift_charge_bcc(x, 0xDC40, beq2);                                      /* beq.w loc_DCD0 */
    if (beq2) goto loc_DCD0;
  }
  alu_movew(c, lift_r16(x, 0xFFFFB7AAu));                                  /* tst.w (abs) */
  lift_charge(x, 0xDC44);
  {
    int bmi = c->nf;
    lift_charge_bcc(x, 0xDC48, bmi);                                       /* bmi.w loc_DCD0 */
    if (bmi) goto loc_DCD0;
  }
  setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));                              /* subq.w #2,d0 */
  lift_charge(x, 0xDC4C);

  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));           /* move.w d1,-(sp) */
  lift_charge(x, 0xDC4E);
  setw(&c->d[1], alu_movew(c, 0x1000));                                    /* move.w #$1000,d1 */
  lift_charge(x, 0xDC50);
  lift_w16(x, c->a[3] + 0x28, alu_movew(c, W(c->d[1])));                   /* move.w d1,$28(a3) */
  lift_charge(x, 0xDC54);
  setw(&c->d[0], alu_movew(c, W(c->d[0]) | 1));                            /* ori.w #1,d0 */
  lift_charge(x, 0xDC58);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);                              /* btst #7,$62(a3) */
  lift_charge(x, 0xDC5C);
  {
    int bne = !c->zf;
    lift_charge_bcc(x, 0xDC62, bne);                                       /* bne.w loc_DC6A */
    if (!bne)
    {
      setw(&c->d[0], alu_eorw(c, 1, W(c->d[0])));                          /* eor.w #1,d0 */
      lift_charge(x, 0xDC66);
    }
  }

loc_DC6A:
  setw(&c->d[1], alu_movew(c, 0xFFFA));                                    /* move.w #$FFFA,d1 */
  lift_charge(x, 0xDC6A);
  alu_movew(c, lift_r16(x, c->a[3] + 0x28));                               /* tst.w $28(a3) */
  lift_charge(x, 0xDC6E);
  {
    int bmi2 = c->nf;
    lift_charge_bcc(x, 0xDC72, bmi2);                                      /* bmi.w loc_DC7A */
    if (!bmi2)
    {
      lift_w16(x, c->a[3] + 0x28, alu_negw(c, lift_r16(x, c->a[3] + 0x28)));  /* neg.w $28(a3) */
      lift_charge(x, 0xDC76);
    }
  }

loc_DC7A:
  alu_movew(c, lift_r16(x, c->a[3]));                                      /* tst.w (a3) */
  lift_charge(x, 0xDC7A);
  {
    int bpl1 = !c->nf;
    lift_charge_bcc(x, 0xDC7C, bpl1);                                      /* bpl.w loc_DC94 */
    if (!bpl1)
    {
      alu_movew(c, lift_r16(x, c->a[3] + 0x28));                            /* tst.w $28(a3) */
      lift_charge(x, 0xDC80);
      {
        int bpl2 = !c->nf;
        lift_charge_bcc(x, 0xDC84, bpl2);                                   /* bpl.w loc_DC8C */
        if (!bpl2)
        {
          lift_w16(x, c->a[3] + 0x28, alu_negw(c, lift_r16(x, c->a[3] + 0x28)));  /* neg.w $28(a3) */
          lift_charge(x, 0xDC88);
        }
      }
      setw(&c->d[1], alu_movew(c, 6));                                      /* move.w #6,d1 */
      lift_charge(x, 0xDC8C);
      setw(&c->d[0], alu_eorw(c, 1, W(c->d[0])));                           /* eor.w #1,d0 */
      lift_charge(x, 0xDC90);
    }
  }

loc_DC94:
  lift_w16(x, c->a[3], alu_addw(c, W(c->d[1]), lift_r16(x, c->a[3])));     /* add.w d1,(a3) */
  lift_charge(x, 0xDC94);
  alu_btst(c, lift_r8(x, c->a[3] + 0x76), 0);                              /* btst #0,$76(a3) */
  lift_charge(x, 0xDC96);
  {
    int beq3 = c->zf;
    lift_charge_bcc(x, 0xDC9C, beq3);                                      /* beq.w loc_DCA4 */
    if (!beq3)
    {
      setw(&c->d[0], alu_eorw(c, 1, W(c->d[0])));                          /* eor.w #1,d0 */
      lift_charge(x, 0xDCA0);
    }
  }

loc_DCA4:
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[7]))); c->a[7] += 2;        /* move.w (sp)+,d1 */
  lift_charge(x, 0xDCA4);
  lift_charge(x, 0xDCA6);                                                   /* bra.w loc_DCD0 */
  goto loc_DCD0;

loc_DCAA:
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);                             /* movem.l d0,-(sp) */
  lift_charge_movem(x, 0xDCAA);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3])));                      /* move.w (a3),d0 */
  lift_charge(x, 0xDCAE);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[0]), W(c->d[0])));           /* sub.w (a0),d0 */
  lift_charge(x, 0xDCB0);
  alu_cmpw(c, 0x10, W(c->d[0]));                                           /* cmp.w #$10,d0 */
  lift_charge(x, 0xDCB2);
  {
    int le = (c->zf || c->nf != c->vf);                                    /* ble */
    lift_charge_bcc(x, 0xDCB6, le);                                         /* ble.w loc_DCC2 */
    if (le) goto loc_DCC2;
  }
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;                            /* movem.l (sp)+,d0 */
  lift_charge_movem(x, 0xDCBA);
  lift_charge(x, 0xDCBE);                                                    /* bra.w loc_DCCE */
  goto loc_DCCE;

loc_DCC2:
  alu_cmpw(c, 0xFFF0, W(c->d[0]));                                          /* cmp.w #$FFF0,d0 */
  lift_charge(x, 0xDCC2);
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;                            /* movem.l (sp)+,d0 */
  lift_charge_movem(x, 0xDCC6);
  {
    int gt3 = (!c->zf && c->nf == c->vf);                                   /* bgt */
    lift_charge_bcc(x, 0xDCCA, gt3);                                         /* bgt.w loc_DCD0 */
    if (gt3) goto loc_DCD0;
  }

loc_DCCE:
  setw(&c->d[0], alu_addw(c, 4, W(c->d[0])));                               /* addq.w #4,d0 */
  lift_charge(x, 0xDCCE);

loc_DCD0:
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));                      /* add.w d0,d0 */
  lift_charge(x, 0xDCD0);
  c->a[1] = 0xDD1A;                                                          /* lea word_DD1A(pc),a1 */
  lift_charge(x, 0xDCD2);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1] + SEW(c->d[0]))));         /* move.w (a1,d0.w),d1 */
  lift_charge(x, 0xDCD6);
  alu_cmpw(c, 0x178, W(c->d[1]));                                            /* cmp.w #$178,d1 */
  lift_charge(x, 0xDCDA);
  {
    int bne4 = !c->zf;
    lift_charge_bcc(x, 0xDCDE, bne4);                                        /* bne.w loc_DCF8 */
    if (!bne4)
    {
      setw(&c->d[3], alu_andw(c, 3, W(c->d[3])));                            /* and.w #3,d3 */
      lift_charge(x, 0xDCE2);
      int beq4 = c->zf;
      lift_charge_bcc(x, 0xDCE6, beq4);                                      /* beq.w loc_DCF8 */
      if (!beq4)
      {
        alu_cmpb(c, 0xB, lift_r8(x, c->a[3] + 0x73));                         /* cmp.b #$B,$73(a3) */
        lift_charge(x, 0xDCEA);
        int blt3 = (c->nf != c->vf);                                         /* blt */
        lift_charge_bcc(x, 0xDCF0, blt3);                                    /* blt.w loc_DCF8 */
        if (!blt3)
        {
          setw(&c->d[1], alu_movew(c, 0x1AA));                               /* move.w #$1AA,d1 */
          lift_charge(x, 0xDCF4);
        }
      }
    }
  }

  lift_call(x, 0xDCF8, 4, Anim_SetScript);                                   /* bsr.w sub_1073A */
  if (x->declined) return;
  lift_w16(x, 0xFFFFB89Cu, alu_addw(c, 0x96, lift_r16(x, 0xFFFFB89Cu)));     /* add.w #$96,(abs) */
  lift_charge(x, 0xDCFC);
  lift_w16(x, 0xFFFFB8A2u, alu_addw(c, 0xA, lift_r16(x, 0xFFFFB8A2u)));      /* add.w #$A,(abs) */
  lift_charge(x, 0xDD02);
  lift_w16(x, c->a[3] + 0x28, alu_asrw(c, lift_r16(x, c->a[3] + 0x28), 1));  /* asr $28(a3) */
  lift_charge(x, 0xDD08);
  lift_w16(x, c->a[3] + 0x28, alu_asrw(c, lift_r16(x, c->a[3] + 0x28), 1));  /* asr $28(a3) */
  lift_charge(x, 0xDD0C);
  lift_w16(x, c->a[3] + 0x2A, alu_asrw(c, lift_r16(x, c->a[3] + 0x2A), 1));  /* asr $2A(a3) */
  lift_charge(x, 0xDD10);
  lift_w16(x, c->a[3] + 0x2A, alu_asrw(c, lift_r16(x, c->a[3] + 0x2A), 1));  /* asr $2A(a3) */
  lift_charge(x, 0xDD14);

  lift_charge(x, 0xDD18);                                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Lookup_PenaltyOrTierTable (sub_FAE26; called from ROM:FAED4, sub_FD084)
 *   in: d0 = search key, d1 = team/list index
 *   Walks a linked list of 6-byte records (list heads at
 *   off_F92F4[d1]; each record's word at +4 is its key, +0 is a
 *   payload pointer) looking for a record whose key matches d0. If
 *   found, a0 = that record's payload pointer (early exit). If the
 *   list is exhausted with no match, navigates the team's TeamData_*
 *   stream (d0+1 hops of size word(ptr)+8, then one more hop, then a
 *   $A(ptr) hop) to a word value, counts how many 4-bit shifts empty
 *   it (>=1), and compares that count against d0 to select one of
 *   four fixed ROM table addresses for a0, further split by bit0 of
 *   the navigated record's byte at +4.
 */
void Lookup_PenaltyOrTierTable(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d0-d7/a1-a6,-(sp): a0 excluded (it is the return value) */
  {
    uint32_t saved[14] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 14; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xFAE26);

  c->a[0] = 0x0F92F4;                                                    /* movea.l #off_F92F4,a0 */
  lift_charge(x, 0xFAE2A);
  setw(&c->d[2], alu_movew(c, W(c->d[1])));                              /* move.w d1,d2 */
  lift_charge(x, 0xFAE30);
  setw(&c->d[2], alu_aslw(c, W(c->d[2]), 2));                            /* asl.w #2,d2 */
  lift_charge(x, 0xFAE32);
  c->a[0] = lift_r32(x, c->a[0] + SEW(c->d[2]));                         /* move.l (a0,d2.w),a0 */
  lift_charge(x, 0xFAE34);
  setw(&c->d[2], alu_movew(c, 0xFFFF));                                  /* move.w #$FFFF,d2 */
  lift_charge(x, 0xFAE38);
  lift_charge(x, 0xFAE3C);                                                /* bra.w loc_FAE50 */

  for (;;)
  {
    /* loc_FAE50 */
    setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));                          /* addq.w #1,d2 */
    lift_charge(x, 0xFAE50);
    alu_tstl(c, lift_r32(x, c->a[0]));                                    /* tst.l (a0) */
    lift_charge(x, 0xFAE52);
    {
      int bneTaken = !c->zf;
      lift_charge_bcc(x, 0xFAE54, bneTaken);                              /* bne.s loc_FAE40 */
      if (!bneTaken) break;                                               /* end of list, no match */
    }

    /* loc_FAE40 */
    alu_cmpw(c, lift_r16(x, c->a[0] + 4), W(c->d[0]));                    /* cmp.w 4(a0),d0 */
    lift_charge(x, 0xFAE40);
    {
      int match = c->zf;
      lift_charge_bcc(x, 0xFAE44, !match);                                /* bne.w loc_FAE4E */
      if (match)
      {
        c->a[0] = lift_r32(x, c->a[0]);                                   /* move.l (a0),a0 */
        lift_charge(x, 0xFAE48);
        lift_charge(x, 0xFAE4A);                                          /* bra.w loc_FAEB6 */
        goto loc_FAEB6;
      }
    }

    /* loc_FAE4E */
    c->a[0] += 6;                                                          /* addq.l #6,a0: An dest, no flags */
    lift_charge(x, 0xFAE4E);
  }

  /* loc_FAE56: end of list, no match */
  c->a[0] = 0x30E;                                                        /* movea.l #$30E,a0 */
  lift_charge(x, 0xFAE56);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));                             /* asl.w #2,d1 */
  lift_charge(x, 0xFAE5C);
  c->a[0] = lift_r32(x, c->a[0] + SEW(c->d[1]));                         /* move.l (a0,d1.w),a0 */
  lift_charge(x, 0xFAE5E);
  c->a[6] = c->a[0];                                                      /* move.l a0,a6 */
  lift_charge(x, 0xFAE62);
  setw(&c->d[6], alu_movew(c, W(c->d[0])));                               /* move.w d0,d6 */
  lift_charge(x, 0xFAE64);
  c->a[6] = c->a[6] + SEW(lift_r16(x, c->a[6]));                          /* add.w (a6),a6 */
  lift_charge(x, 0xFAE66);
  lift_charge(x, 0xFAE68);                                                 /* bra.w loc_FAE70 */

  for (;;)
  {
    {
      uint32_t nd6 = W(W(c->d[6]) - 1);                                   /* dbf d6,loc_FAE6C */
      setw(&c->d[6], nd6);
      int taken = (nd6 != 0xFFFF);
      lift_charge_dbcc(x, 0xFAE70, taken, !taken);
      if (!taken) break;
    }
    /* loc_FAE6C */
    c->a[6] = c->a[6] + SEW(lift_r16(x, c->a[6]));                        /* add.w (a6),a6 */
    lift_charge(x, 0xFAE6C);
    c->a[6] += 8;                                                          /* addq.w #8,a6: An dest, full 32-bit, no flags */
    lift_charge(x, 0xFAE6E);
  }

  c->a[6] = c->a[6] + SEW(lift_r16(x, c->a[6]));                          /* add.w (a6),a6 */
  lift_charge(x, 0xFAE74);
  c->a[0] = c->a[0] + SEW(lift_r16(x, c->a[0] + 0xA));                    /* add.w $A(a0),a0 */
  lift_charge(x, 0xFAE76);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0])));                     /* move.w (a0),d1 */
  lift_charge(x, 0xFAE7A);
  setw(&c->d[3], alu_movew(c, 0));                                        /* clr.w d3 */
  lift_charge(x, 0xFAE7C);

  for (;;)
  {
    /* loc_FAE7E */
    setw(&c->d[3], alu_addw(c, 1, W(c->d[3])));                           /* addq.w #1,d3 */
    lift_charge(x, 0xFAE7E);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));                           /* asl.w #4,d1 */
    lift_charge(x, 0xFAE80);
    int taken = !c->zf;
    lift_charge_bcc(x, 0xFAE82, taken);                                    /* bne.s loc_FAE7E */
    if (!taken) break;
  }

  c->a[0] = 0x0C6F02;                                                      /* movea.l #unk_C6F02,a0 */
  lift_charge(x, 0xFAE84);
  alu_btst(c, lift_r8(x, c->a[6] + 4), 0);                                 /* btst #0,4(a6) */
  lift_charge(x, 0xFAE8A);
  {
    int bneTaken = !c->zf;
    lift_charge_bcc(x, 0xFAE90, bneTaken);                                 /* bne.w loc_FAE9A */
    if (!bneTaken)
    {
      c->a[0] = 0x0C726C;                                                  /* movea.l #unk_C726C,a0 */
      lift_charge(x, 0xFAE94);
    }
  }

  /* loc_FAE9A */
  alu_cmpw(c, W(c->d[3]), W(c->d[0]));                                     /* cmp.w d3,d0 */
  lift_charge(x, 0xFAE9A);
  {
    int lt = (c->nf != c->vf);                                             /* blt */
    lift_charge_bcc(x, 0xFAE9C, lt);                                        /* blt.w loc_FAEB6 */
    if (lt) goto loc_FAEB6;
  }

  c->a[0] = 0x0C6B98;                                                       /* movea.l #unk_C6B98,a0 */
  lift_charge(x, 0xFAEA0);
  alu_btst(c, lift_r8(x, c->a[6] + 4), 0);                                  /* btst #0,4(a6) */
  lift_charge(x, 0xFAEA6);
  {
    int bneTaken2 = !c->zf;
    lift_charge_bcc(x, 0xFAEAC, bneTaken2);                                 /* bne.w loc_FAEB6 */
    if (bneTaken2) goto loc_FAEB6;
  }

  c->a[0] = 0x0C682E;                                                       /* movea.l #unk_C682E,a0 */
  lift_charge(x, 0xFAEB0);

loc_FAEB6:
  /* movem.l (sp)+,d0-d7/a1-a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFAEB6);

  lift_charge(x, 0xFAEBA);                                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_SortDrawOrderByDepth (sub_1702E; called from sub_9FD0)
 *   Rebuilds a per-object depth cache at $FFFFB84A from each on-ice
 *   object's world Y (or world X when the rink is drawn flipped,
 *   R_UNK_C2EC bit7), then bubble-sorts the 15-entry draw-order byte
 *   array at $FFFFB88A ascending by depth, recording each object's
 *   sorted position into $FFFFB86A, looping full passes until a pass
 *   makes no swap.
 */
void Object_SortDrawOrderByDepth(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d0-d4/a0-a2,-(sp): push order a2,a1,a0,d4..d0 */
  {
    uint32_t saved[8] = {
      c->a[2], c->a[1], c->a[0], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 8; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0x1702E);

  c->a[2] = 0xFFFFB86A;                                                   /* movea.l #$FFFFB86A,a2 */
  lift_charge(x, 0x17032);
  c->a[1] = 0xFFFFB84A;                                                   /* movea.l #$FFFFB84A,a1 */
  lift_charge(x, 0x17038);
  c->a[0] = 0xFFFFB04A;                                                   /* movea.l #$FFFFB04A,a0 */
  lift_charge(x, 0x1703E);
  setw(&c->d[3], alu_movew(c, 0xF));                                      /* move.w #$F,d3 */
  lift_charge(x, 0x17044);

  for (;;)
  {
    /* loc_17048 */
    setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[0] + 0x14)));            /* move.w $14(a0),d4 */
    lift_charge(x, 0x17048);
    alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 7);                              /* btst #7,(abs) */
    lift_charge(x, 0x1704C);
    {
      int beqTaken = c->zf;
      lift_charge_bcc(x, 0x17052, beqTaken);                               /* beq.w loc_17058 */
      if (!beqTaken)
      {
        setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[0])));                /* move.w (a0),d4 */
        lift_charge(x, 0x17056);
      }
    }

    /* loc_17058 */
    lift_w16(x, c->a[1], alu_movew(c, W(c->d[4])));                        /* move.w d4,(a1)+ */
    c->a[1] += 2;
    lift_charge(x, 0x17058);
    c->a[0] = c->a[0] + 0x80;                                              /* adda.w #$80,a0 */
    lift_charge(x, 0x1705A);

    {
      uint32_t nd3 = W(W(c->d[3]) - 1);                                    /* dbf d3,loc_17048 */
      setw(&c->d[3], nd3);
      int taken = (nd3 != 0xFFFF);
      lift_charge_dbcc(x, 0x1705E, taken, !taken);
      if (!taken) break;
    }
  }

  c->a[1] = 0xFFFFB84A;                                                    /* movea.l #$FFFFB84A,a1 */
  lift_charge(x, 0x17062);

  for (;;)
  {
    /* loc_17068 */
    setw(&c->d[4], alu_movew(c, 0));                                        /* clr.w d4 */
    lift_charge(x, 0x17068);
    c->a[0] = 0xFFFFB88A;                                                   /* movea.l #$FFFFB88A,a0 */
    lift_charge(x, 0x1706A);
    setw(&c->d[3], alu_movew(c, 0xE));                                      /* move.w #$E,d3 */
    lift_charge(x, 0x17070);
    setw(&c->d[0], alu_movew(c, 0));                                        /* clr.w d0 */
    lift_charge(x, 0x17074);
    setw(&c->d[1], alu_movew(c, 0));                                        /* clr.w d1 */
    lift_charge(x, 0x17076);

    for (;;)
    {
      /* loc_17078 */
      setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));                     /* move.b (a0)+,d0 */
      c->a[0] += 1;
      lift_charge(x, 0x17078);
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0])));                     /* move.b (a0),d1 */
      lift_charge(x, 0x1707A);
      setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1] + SEW(c->d[0]))));     /* move.w (a1,d0.w),d2 */
      lift_charge(x, 0x1707C);
      alu_cmpw(c, lift_r16(x, c->a[1] + SEW(c->d[1])), W(c->d[2]));          /* cmp.w (a1,d1.w),d2 */
      lift_charge(x, 0x17080);
      {
        int le = (c->zf || c->nf != c->vf);                                  /* ble */
        lift_charge_bcc(x, 0x17084, le);                                      /* ble.w loc_170A2 */
        if (!le)
        {
          lift_w8(x, c->a[0], alu_moveb(c, W(c->d[0]) & 0xFF));               /* move.b d0,(a0) */
          lift_charge(x, 0x17088);
          lift_w8(x, c->a[0] - 1, alu_moveb(c, W(c->d[1]) & 0xFF));           /* move.b d1,-1(a0) */
          lift_charge(x, 0x1708A);
          c->d[2] = alu_movel(c, c->a[0]);                                    /* move.l a0,d2 */
          lift_charge(x, 0x1708E);
          c->d[2] = alu_subl(c, 0xFFFFB88Au, c->d[2]);                        /* sub.l #$FFFFB88A,d2 */
          lift_charge(x, 0x17090);
          lift_w16(x, c->a[2] + SEW(c->d[0]), alu_movew(c, W(c->d[2])));      /* move.w d2,(a2,d0.w) */
          lift_charge(x, 0x17096);
          setw(&c->d[2], alu_subw(c, 1, W(c->d[2])));                          /* subq.w #1,d2 */
          lift_charge(x, 0x1709A);
          lift_w16(x, c->a[2] + SEW(c->d[1]), alu_movew(c, W(c->d[2])));      /* move.w d2,(a2,d1.w) */
          lift_charge(x, 0x1709C);
          setb(&c->d[4], 0xFF);                                                /* st d4: byte, no flags */
          lift_charge(x, 0x170A0);
        }
      }

      /* loc_170A2 */
      {
        uint32_t nd3b = W(W(c->d[3]) - 1);                                     /* dbf d3,loc_17078 */
        setw(&c->d[3], nd3b);
        int taken2 = (nd3b != 0xFFFF);
        lift_charge_dbcc(x, 0x170A2, taken2, !taken2);
        if (!taken2) break;
      }
    }

    alu_movew(c, W(c->d[4]));                                                  /* tst.w d4 */
    lift_charge(x, 0x170A6);
    {
      int again = !c->zf;
      lift_charge_bcc(x, 0x170A8, again);                                      /* bne.s loc_17068 */
      if (!again) break;
    }
  }

  /* movem.l (sp)+,d0-d4/a0-a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x170AA);

  lift_charge(x, 0x170AE);                                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ResetOverlaysAndSlots (sub_16DA2; called from ROM:16AC2,
 * sub_16BAC)
 *   in/out: d4 - a running word accumulator threaded through all
 *   three init loops (caller-supplied, not reset here)
 *   Loads 4 fixed-size records at $FFFFBE88 (stride $14) from ROM
 *   table unk_16E06, then 7 overlay records at $FFFFBDB4/OVERLAY_TABLE
 *   (stride $1C) from unk_16E2E, then for 16 on-ice object slots at
 *   $FFFFB04A (stride $80): clears the slot (32 longs), loads fields
 *   from unk_16EEE, and records a draw-order byte ($FFFFB88A) and
 *   position word ($FFFFB86A) derived from the slot index. Ends by
 *   tail-jumping into the already-lifted Object_SortDrawOrderByDepth
 *   (sub_1702E) — its rts returns to this routine's own caller. No
 *   registers are saved/restored (a0-a4/d0/d6 are scratch; d4 is the
 *   caller-visible accumulator).
 */
void Object_ResetOverlaysAndSlots(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = 0x16E06;                                                      /* movea.l #unk_16E06,a2 */
  lift_charge(x, 0x16DA2);
  c->a[3] = 0xFFFFBE88;                                                    /* movea.w #$BE88,a3 */
  lift_charge(x, 0x16DA8);
  c->d[0] = alu_moveql(c, 3);                                              /* moveq #3,d0 */
  lift_charge(x, 0x16DAC);

  for (;;)
  {
    /* loc_16DAE */
    lift_w16(x, c->a[3] + 8, alu_movew(c, 0xFFFF));                        /* move.w #$FFFF,8(a3) */
    lift_charge(x, 0x16DAE);
    lift_w16(x, c->a[3], alu_movew(c, lift_r16(x, c->a[2])));              /* move.w (a2)+,(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DB4);
    lift_w16(x, c->a[3] + 2, alu_movew(c, lift_r16(x, c->a[2])));          /* move.w (a2)+,2(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DB6);
    lift_w16(x, c->a[3] + 6, alu_movew(c, lift_r16(x, c->a[2])));          /* move.w (a2)+,6(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DBA);
    lift_w16(x, c->a[3] + 4, alu_movew(c, lift_r16(x, c->a[2])));          /* move.w (a2)+,4(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DBE);
    lift_w16(x, c->a[3] + 0x12, alu_movew(c, W(c->d[4])));                 /* move.w d4,$12(a3) */
    lift_charge(x, 0x16DC2);
    setw(&c->d[4], alu_addw(c, lift_r16(x, c->a[2]), W(c->d[4])));         /* add.w (a2)+,d4 */
    c->a[2] += 2;
    lift_charge(x, 0x16DC6);
    c->a[3] += 0x14;                                                        /* adda.w #$14,a3 */
    lift_charge(x, 0x16DC8);

    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                    /* dbf d0,loc_16DAE */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0x16DCC, taken, !taken);
      if (!taken) break;
    }
  }

  c->a[2] = 0x16E2E;                                                       /* movea.l #unk_16E2E,a2 */
  lift_charge(x, 0x16DD0);
  c->a[3] = 0xFFFFBDB4;                                                     /* movea.l #$FFFFBDB4,a3 */
  lift_charge(x, 0x16DD6);
  c->d[0] = alu_moveql(c, 6);                                               /* moveq #6,d0 */
  lift_charge(x, 0x16DDC);

  for (;;)
  {
    /* loc_16DDE */
    lift_w8(x, c->a[3] + 8, 0xFF);                                          /* st 8(a3): no flags */
    lift_charge(x, 0x16DDE);
    lift_w16(x, c->a[3], alu_movew(c, lift_r16(x, c->a[2])));               /* move.w (a2)+,(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DE2);
    lift_w16(x, c->a[3] + 0x14, alu_movew(c, lift_r16(x, c->a[2])));        /* move.w (a2)+,$14(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DE4);
    lift_w16(x, c->a[3] + 0x18, alu_movew(c, lift_r16(x, c->a[2])));        /* move.w (a2)+,$18(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DE8);
    lift_w16(x, c->a[3] + 6, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,6(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DEC);
    lift_w16(x, c->a[3] + 4, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,4(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16DF0);
    lift_w16(x, c->a[3] + 0x12, alu_movew(c, W(c->d[4])));                  /* move.w d4,$12(a3) */
    lift_charge(x, 0x16DF4);
    setw(&c->d[4], alu_addw(c, lift_r16(x, c->a[2]), W(c->d[4])));          /* add.w (a2)+,d4 */
    c->a[2] += 2;
    lift_charge(x, 0x16DF8);
    c->a[3] += 0x1C;                                                         /* adda.w #$1C,a3 */
    lift_charge(x, 0x16DFA);

    {
      uint32_t nd0b = W(W(c->d[0]) - 1);                                    /* dbf d0,loc_16DDE */
      setw(&c->d[0], nd0b);
      int taken2 = (nd0b != 0xFFFF);
      lift_charge_dbcc(x, 0x16DFE, taken2, !taken2);
      if (!taken2) break;
    }
  }

  lift_charge(x, 0x16E02);                                                   /* bra.w loc_16E82 */

  /* loc_16E82 */
  setw(&c->d[6], alu_movew(c, 0));                                           /* clr.w d6 */
  lift_charge(x, 0x16E82);
  c->a[1] = 0xFFFFB88A;                                                       /* movea.l #$FFFFB88A,a1 */
  lift_charge(x, 0x16E84);
  c->a[2] = 0x16EEE;                                                          /* lea unk_16EEE(pc),a2 */
  lift_charge(x, 0x16E88);
  c->a[3] = 0xFFFFB04A;                                                       /* movea.l #$FFFFB04A,a3 */
  lift_charge(x, 0x16E8C);
  c->a[4] = 0xFFFFB86A;                                                       /* movea.l #$FFFFB86A,a4 */
  lift_charge(x, 0x16E90);

  for (;;)
  {
    /* loc_16E94 */
    c->d[0] = alu_moveql(c, 0x1F);                                           /* moveq #$1F,d0 */
    lift_charge(x, 0x16E94);
    c->a[0] = SEW(W(c->a[3]));                                               /* movea.w a3,a0: sign-extend */
    lift_charge(x, 0x16E96);

    for (;;)
    {
      /* loc_16E98 */
      alu_movel(c, 0);                                                        /* clr.l (a0)+ */
      lift_w8(x, c->a[0] + 0, 0);      /* split to bytes: later st/byte field */
      lift_w8(x, c->a[0] + 1, 0);      /* writes (e.g. st 8(a3)) need byte-level */
      lift_w8(x, c->a[0] + 2, 0);      /* supersede granularity, not just word */
      lift_w8(x, c->a[0] + 3, 0);
      c->a[0] += 4;
      lift_charge(x, 0x16E98);

      {
        uint32_t nd0c = W(W(c->d[0]) - 1);                                     /* dbf d0,loc_16E98 */
        setw(&c->d[0], nd0c);
        int taken3 = (nd0c != 0xFFFF);
        lift_charge_dbcc(x, 0x16E9A, taken3, !taken3);
        if (!taken3) break;
      }
    }

    lift_w16(x, c->a[3] + 0x52, alu_movew(c, W(c->d[6])));                    /* move.w d6,$52(a3) */
    lift_charge(x, 0x16E9E);
    lift_w8(x, c->a[3] + 8, 0xFF);                                             /* st 8(a3): no flags */
    lift_charge(x, 0x16EA2);
    lift_w8(x, c->a[3] + 0x66, 0xFF);                                          /* st $66(a3): no flags */
    lift_charge(x, 0x16EA6);
    lift_w16(x, c->a[3], alu_movew(c, lift_r16(x, c->a[2])));                  /* move.w (a2)+,(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EAA);
    lift_w16(x, c->a[3] + 0x14, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,$14(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EAC);
    lift_w16(x, c->a[3] + 0x18, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,$18(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EB0);
    lift_w16(x, c->a[3] + 6, alu_movew(c, lift_r16(x, c->a[2])));              /* move.w (a2)+,6(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EB4);
    lift_w16(x, c->a[3] + 4, alu_movew(c, lift_r16(x, c->a[2])));              /* move.w (a2)+,4(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EB8);
    lift_w16(x, c->a[3] + 0x12, alu_movew(c, W(c->d[4])));                     /* move.w d4,$12(a3) */
    lift_charge(x, 0x16EBC);
    setw(&c->d[4], alu_addw(c, lift_r16(x, c->a[2]), W(c->d[4])));             /* add.w (a2)+,d4 */
    c->a[2] += 2;
    lift_charge(x, 0x16EC0);
    lift_w16(x, c->a[3] + 0x4A, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,$4A(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EC2);
    lift_w16(x, c->a[3] + 0x4C, alu_movew(c, lift_r16(x, c->a[2])));           /* move.w (a2)+,$4C(a3) */
    c->a[2] += 2;
    lift_charge(x, 0x16EC6);
    c->a[2] += 1;                                                               /* addq.w #1,a2: An dest, no flags */
    lift_charge(x, 0x16ECA);
    lift_w8(x, c->a[3] + 0x38, alu_moveb(c, lift_r8(x, c->a[2])));              /* move.b (a2)+,$38(a3) */
    c->a[2] += 1;
    lift_charge(x, 0x16ECC);
    c->a[2] += 1;                                                               /* addq.w #1,a2: An dest, no flags */
    lift_charge(x, 0x16ED0);
    lift_w8(x, c->a[3] + 0x62, alu_moveb(c, lift_r8(x, c->a[2])));              /* move.b (a2)+,$62(a3) */
    c->a[2] += 1;
    lift_charge(x, 0x16ED2);
    c->a[3] += 0x80;                                                            /* adda.w #$80,a3 */
    lift_charge(x, 0x16ED6);
    setw(&c->d[6], alu_aslw(c, W(c->d[6]), 1));                                 /* asl.w #1,d6 */
    lift_charge(x, 0x16EDA);
    lift_w8(x, c->a[1], alu_moveb(c, W(c->d[6]) & 0xFF));                       /* move.b d6,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0x16EDC);
    setw(&c->d[6], alu_lsrw(c, W(c->d[6]), 1));                                 /* lsr.w #1,d6 */
    lift_charge(x, 0x16EDE);
    lift_w16(x, c->a[4], alu_movew(c, W(c->d[6])));                             /* move.w d6,(a4)+ */
    c->a[4] += 2;
    lift_charge(x, 0x16EE0);
    setw(&c->d[6], alu_addw(c, 1, W(c->d[6])));                                 /* addq.w #1,d6 */
    lift_charge(x, 0x16EE2);
    alu_cmpw(c, 0x10, W(c->d[6]));                                              /* cmp.w #$10,d6 */
    lift_charge(x, 0x16EE4);
    {
      int taken4 = !c->zf;
      lift_charge_bcc(x, 0x16EE8, taken4);                                      /* bne.s loc_16E94 */
      if (!taken4) break;
    }
  }

  lift_charge(x, 0x16EEA);                                                       /* bra.w sub_1702E */
  Object_SortDrawOrderByDepth(x);                                                /* tail jump */
}

/*
 * Sram_SyncScoreRecord (sub_F9B94; called from Object_LookupRecordThenSyncScore
 * and others)
 *   in: a0 = RAM buffer, d0 = record number, d1 = category index
 *   Computes an SRAM byte offset (d0*4 + word_F9CA4[d1*2]*4) and
 *   either writes 4 bytes from a0 to SRAM or reads 4 bytes from SRAM
 *   into a0, depending on R_UNK_C2F8 bit6 (set = write, clear =
 *   read). This entry clears the bit first (sibling entry sub_F9BDA
 *   sets it then falls into the same shared body — not itself
 *   lifted).
 */
static void Sram_SyncScoreRecord_body(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d1/a0-a1,-(sp) */
  c->a[7] -= 16;
  lift_w32(x, c->a[7] + 0, c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0xF9B9A);

  c->d[0] = alu_asll(c, c->d[0], 2);                                     /* asl.l #2,d0 */
  lift_charge(x, 0xF9B9E);
  c->d[0] = alu_addl(c, 0, c->d[0]);                                     /* add.l #0,d0 */
  lift_charge(x, 0xF9BA0);
  c->a[1] = 0x0F9CA4;                                                     /* movea.l #word_F9CA4,a1 */
  lift_charge(x, 0xF9BA6);
  setw(&c->d[1], alu_addw(c, W(c->d[1]), W(c->d[1])));                    /* add.w d1,d1 */
  lift_charge(x, 0xF9BAC);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1] + SEW(c->d[1]))));      /* move.w (a1,d1.w),d1 */
  lift_charge(x, 0xF9BAE);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));                             /* asl.w #2,d1 */
  lift_charge(x, 0xF9BB2);
  c->d[1] = alu_extl(c, c->d[1]);                                         /* ext.l d1 */
  lift_charge(x, 0xF9BB4);
  c->d[0] = alu_addl(c, c->d[1], c->d[0]);                                /* add.l d1,d0 */
  lift_charge(x, 0xF9BB6);
  c->d[1] = alu_moveql(c, 4);                                             /* moveq #4,d1 */
  lift_charge(x, 0xF9BB8);
  alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);                                /* btst #6,(abs) */
  lift_charge(x, 0xF9BBA);
  {
    int beqTaken = c->zf;
    lift_charge_bcc(x, 0xF9BC0, beqTaken);                                 /* beq.w loc_F9BCE */
    if (!beqTaken)
    {
      lift_call(x, 0xF9BC4, 6, SRAM_WriteBytes);                           /* jsr SRAM_WriteBytes */
      if (x->declined) return;
      lift_charge(x, 0xF9BCA);                                             /* bra.w loc_F9BD4 */
    }
    else
    {
      lift_call(x, 0xF9BCE, 6, SRAM_ReadBytes);                            /* jsr SRAM_ReadBytes */
      if (x->declined) return;
    }
  }

  /* loc_F9BD4: movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7] + 0);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0xF9BD4);

  lift_charge(x, 0xF9BD8);                                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Sram_SyncScoreRecord entry: clear R_UNK_C2F8 bit6 (read mode), then the shared body. */
void Sram_SyncScoreRecord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bclr #6,(abs) */
  lift_charge(x, 0xF9B94);
  Sram_SyncScoreRecord_body(x);
}

/*
 * Sram_SyncScoreRecordWrite (sub_F9BDA) — the WRITE twin of Sram_SyncScoreRecord: sets R_UNK_C2F8
 * bit6 and branches into the very same shared body, which dispatches on
 * that bit to SRAM_WriteBytes instead of SRAM_ReadBytes. Two instructions.
 */
void Sram_SyncScoreRecordWrite(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bset #6,(abs) */
  lift_charge(x, 0xF9BDA);
  lift_charge_bcc(x, 0xF9BE0, 1);                                        /* bra -> body */
  Sram_SyncScoreRecord_body(x);
}

/*
 * Object_LookupRecordThenSyncScore (sub_F99F2; called from sub_F98C6)
 *   in: d0 = team/list index, d1 = record number
 *   Navigates the team's TeamData_* stream d0 hops (word(ptr) each)
 *   to a data pointer, then swaps d0/d1 via the stack (d0 becomes the
 *   record number, d1 the team index), calls Sram_SyncScoreRecord,
 *   and classifies a nibble-shift count derived from the team's
 *   navigated data against the record number to return d1 = 0 or 1.
 */
void Object_LookupRecordThenSyncScore(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  /* movem.l d2-a6,-(sp): push order a6..a0,d7..d2 */
  {
    uint32_t saved[13] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2]
    };
    for (i = 0; i < 13; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xF99F2);

  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, W(c->d[0])));            /* move.w d0,-(sp) */
  lift_charge(x, 0xF99F6);
  c->a[2] = 0x30E;                                                          /* movea.l #$30E,a2 */
  lift_charge(x, 0xF99F8);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));                               /* asl.w #2,d0 */
  lift_charge(x, 0xF99FE);
  c->a[2] = lift_r32(x, c->a[2] + SEW(c->d[0]));                           /* move.l (a2,d0.w),a2 */
  lift_charge(x, 0xF9A00);
  c->a[2] = c->a[2] + SEW(lift_r16(x, c->a[2]));                           /* adda.w (a2),a2 */
  lift_charge(x, 0xF9A04);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));                                /* move.w d1,d0 */
  lift_charge(x, 0xF9A06);
  lift_charge(x, 0xF9A08);                                                  /* bra.w loc_F9A10 */

  for (;;)
  {
    /* loc_F9A10: test-at-top via the initial bra, so this runs the body
     * exactly the original d0 times (not d0+1) */
    uint32_t nd0 = W(W(c->d[0]) - 1);                                       /* dbf d0,loc_F9A0C */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xF9A10, taken, !taken);
    if (!taken) break;

    /* loc_F9A0C */
    c->a[2] = c->a[2] + SEW(lift_r16(x, c->a[2]));                          /* adda.w (a2),a2 */
    lift_charge(x, 0xF9A0C);
    c->a[2] += 8;                                                            /* addq.w #8,a2: An dest, no flags */
    lift_charge(x, 0xF9A0E);
  }

  setw(&c->d[0], alu_movew(c, W(c->d[1])));                                  /* move.w d1,d0 */
  lift_charge(x, 0xF9A14);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[7]))); c->a[7] += 2;          /* move.w (sp)+,d1: plain MOVE */
  lift_charge(x, 0xF9A16);
  setw(&c->d[5], alu_movew(c, W(c->d[0])));                                  /* move.w d0,d5 */
  lift_charge(x, 0xF9A18);
  setw(&c->d[4], alu_movew(c, W(c->d[1])));                                  /* move.w d1,d4 */
  lift_charge(x, 0xF9A1A);
  c->d[0] = alu_extl(c, W(c->d[0]));                                         /* ext.l d0 */
  lift_charge(x, 0xF9A1C);
  c->a[0] = 0xFFFF0000;                                                       /* movea.l #$FFFF0000,a0 */
  lift_charge(x, 0xF9A1E);
  lift_call(x, 0xF9A24, 4, Sram_SyncScoreRecord);                             /* bsr.w sub_F9B94 */
  if (x->declined) return;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));                          /* move.b (a0),d0 */
  lift_charge(x, 0xF9A28);
  setw(&c->d[0], alu_extw(c, W(c->d[0]) & 0xFF));                             /* ext.w d0 */
  lift_charge(x, 0xF9A2A);
  c->a[5] = 0x30E;                                                            /* movea.l #$30E,a5 */
  lift_charge(x, 0xF9A2C);
  setw(&c->d[4], alu_aslw(c, W(c->d[4]), 2));                                 /* asl.w #2,d4 */
  lift_charge(x, 0xF9A32);
  c->a[5] = lift_r32(x, c->a[5] + SEW(c->d[4]));                             /* move.l (a5,d4.w),a5 */
  lift_charge(x, 0xF9A34);
  c->a[5] = c->a[5] + SEW(lift_r16(x, c->a[5] + 0xA));                       /* add.w $A(a5),a5 */
  lift_charge(x, 0xF9A38);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[5])));                        /* move.w (a5),d1 */
  lift_charge(x, 0xF9A3C);

  /* movem.w d0,-(sp): this slot was the jsr return-address for the
   * Sram_SyncScoreRecord call above (a 4-byte write already popped and
   * dead). Stage this word push as one 4-byte write spanning the same
   * range (re-affirming the untouched neighbor bytes unchanged) so it
   * exactly supersedes that stale wider prediction instead of only
   * partially overlapping it - see CLAUDE.md's write-log "superseded"
   * detection limitation (exact/full-overlap only). */
  c->a[7] -= 2;
  {
    uint32_t stale_hi = lift_r16(x, c->a[7] - 2);
    lift_w32(x, c->a[7] - 2, (stale_hi << 16) | W(c->d[0]));
  }
  lift_charge_movem(x, 0xF9A3E);
  setw(&c->d[0], alu_movew(c, 0));                                            /* clr.w d0 */
  lift_charge(x, 0xF9A42);

  for (;;)
  {
    /* loc_F9A44 */
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));                              /* addq.w #1,d0 */
    lift_charge(x, 0xF9A44);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));                              /* asl.w #4,d1 */
    lift_charge(x, 0xF9A46);
    int taken2 = !c->zf;
    lift_charge_bcc(x, 0xF9A48, taken2);                                     /* bne.s loc_F9A44 */
    if (!taken2) break;
  }

  alu_cmpw(c, W(c->d[5]), W(c->d[0]));                                        /* cmp.w d5,d0 */
  lift_charge(x, 0xF9A4A);
  /* movem.w (sp)+,d0: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7])); c->a[7] += 2;
  lift_charge_movem(x, 0xF9A4C);
  {
    int gt = (!c->zf && c->nf == c->vf);                                     /* bgt */
    lift_charge_bcc(x, 0xF9A50, gt);                                          /* bgt.w loc_F9A5A */
    if (!gt)
    {
      setw(&c->d[1], alu_movew(c, 0));                                        /* clr.w d1 */
      lift_charge(x, 0xF9A54);
      lift_charge(x, 0xF9A56);                                                 /* bra.w loc_F9A5E */
    }
    else
    {
      setw(&c->d[1], alu_movew(c, 1));                                         /* move.w #1,d1 */
      lift_charge(x, 0xF9A5A);
    }
  }

  /* loc_F9A5E: movem.l (sp)+,d2-a6 */
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF9A5E);

  lift_charge(x, 0xF9A62);                                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_SwapLineupBuffers (sub_17102; called from sub_7CB0, ROM:135D2)
 *   Saves two RAM words, swap-copies two 416-byte buffer pairs
 *   ($FFFFC870<->$FFFFD6D2, $FFFFCBD4<->$FFFFD872) out, clears both
 *   team blocks, swap-copies the buffer pairs back, restores the two
 *   saved words, then sets two flag bytes. Has no rts at all — its
 *   last instruction is immediately followed by sub_17190's entry
 *   address, so it falls straight through into the already-lifted
 *   Team_RefreshDataCache.
 */
void Object_SwapLineupBuffers(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC6F4u)));  /* move.w (abs),-(sp) */
  lift_charge(x, 0x17102);
  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFCA58u)));  /* move.w (abs),-(sp) */
  lift_charge(x, 0x17106);

  /* movem.l a1-a3,-(sp): push order a3,a2,a1 */
  {
    uint32_t saved[3] = { c->a[3], c->a[2], c->a[1] };
    for (i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0x1710A);

  setw(&c->d[0], alu_movew(c, 0x19F));                                       /* move.w #$19F,d0 */
  lift_charge(x, 0x1710E);
  c->a[1] = 0xFFFFCBD4;                                                       /* movea.l #$FFFFCBD4,a1 */
  lift_charge(x, 0x17112);
  c->a[0] = 0xFFFFC870;                                                       /* movea.l #$FFFFC870,a0 */
  lift_charge(x, 0x17118);
  c->a[2] = 0xFFFFD6D2;                                                       /* movea.l #$FFFFD6D2,a2 */
  lift_charge(x, 0x1711E);
  c->a[3] = 0xFFFFD872;                                                       /* movea.l #$FFFFD872,a3 */
  lift_charge(x, 0x17124);

  for (;;)
  {
    /* loc_1712A */
    lift_w8(x, c->a[2], alu_moveb(c, lift_r8(x, c->a[0])));                    /* move.b (a0)+,(a2)+ */
    c->a[0] += 1; c->a[2] += 1;
    lift_charge(x, 0x1712A);
    lift_w8(x, c->a[3], alu_moveb(c, lift_r8(x, c->a[1])));                    /* move.b (a1)+,(a3)+ */
    c->a[1] += 1; c->a[3] += 1;
    lift_charge(x, 0x1712C);

    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                        /* dbf d0,loc_1712A */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0x1712E, taken, !taken);
      if (!taken) break;
    }
  }

  /* movem.l (sp)+,a1-a3 */
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x17132);

  c->d[0] = alu_movel(c, 0x363);                                               /* move.l #$363,d0 */
  lift_charge(x, 0x17136);
  c->a[0] = 0xFFFFC6CE;                                                         /* movea.w #$C6CE,a0 */
  lift_charge(x, 0x1713C);

  for (;;)
  {
    /* loc_17140 */
    lift_w8(x, c->a[0], alu_moveb(c, 0));                                       /* clr.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x17140);

    {
      uint32_t nd0b = W(W(c->d[0]) - 1);                                        /* dbf d0,loc_17140 */
      setw(&c->d[0], nd0b);
      int taken2 = (nd0b != 0xFFFF);
      lift_charge_dbcc(x, 0x17142, taken2, !taken2);
      if (!taken2) break;
    }
  }

  c->a[0] = 0xFFFFCA32;                                                         /* movea.w #$CA32,a0 */
  lift_charge(x, 0x17146);
  setw(&c->d[0], alu_movew(c, 0x363));                                          /* move.w #$363,d0 */
  lift_charge(x, 0x1714A);

  for (;;)
  {
    /* loc_1714E */
    lift_w8(x, c->a[0], alu_moveb(c, 0));                                       /* clr.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x1714E);

    {
      uint32_t nd0c = W(W(c->d[0]) - 1);                                        /* dbf d0,loc_1714E */
      setw(&c->d[0], nd0c);
      int taken3 = (nd0c != 0xFFFF);
      lift_charge_dbcc(x, 0x17150, taken3, !taken3);
      if (!taken3) break;
    }
  }

  /* movem.l a1-a3,-(sp) */
  {
    uint32_t saved2[3] = { c->a[3], c->a[2], c->a[1] };
    for (i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved2[i]); }
  }
  lift_charge_movem(x, 0x17154);

  setw(&c->d[0], alu_movew(c, 0x19F));                                          /* move.w #$19F,d0 */
  lift_charge(x, 0x17158);
  c->a[1] = 0xFFFFCBD4;                                                          /* movea.l #$FFFFCBD4,a1 */
  lift_charge(x, 0x1715C);
  c->a[0] = 0xFFFFC870;                                                          /* movea.l #$FFFFC870,a0 */
  lift_charge(x, 0x17162);
  c->a[2] = 0xFFFFD6D2;                                                          /* movea.l #$FFFFD6D2,a2 */
  lift_charge(x, 0x17168);
  c->a[3] = 0xFFFFD872;                                                          /* movea.l #$FFFFD872,a3 */
  lift_charge(x, 0x1716E);

  for (;;)
  {
    /* loc_17174 */
    lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, c->a[2])));                      /* move.b (a2)+,(a0)+ */
    c->a[2] += 1; c->a[0] += 1;
    lift_charge(x, 0x17174);
    lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[3])));                      /* move.b (a3)+,(a1)+ */
    c->a[3] += 1; c->a[1] += 1;
    lift_charge(x, 0x17176);

    {
      uint32_t nd0d = W(W(c->d[0]) - 1);                                         /* dbf d0,loc_17174 */
      setw(&c->d[0], nd0d);
      int taken4 = (nd0d != 0xFFFF);
      lift_charge_dbcc(x, 0x17178, taken4, !taken4);
      if (!taken4) break;
    }
  }

  /* movem.l (sp)+,a1-a3 */
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x1717C);

  {
    uint32_t v = lift_r16(x, c->a[7]); c->a[7] += 2;                             /* move.w (sp)+,(abs) */
    lift_w16(x, 0xFFFFCA58u, alu_movew(c, v));
  }
  lift_charge(x, 0x17180);
  {
    uint32_t v2 = lift_r16(x, c->a[7]); c->a[7] += 2;                            /* move.w (sp)+,(abs) */
    lift_w16(x, 0xFFFFC6F4u, alu_movew(c, v2));
  }
  lift_charge(x, 0x17184);
  lift_w8(x, 0xFFFFC768u, 0xFF);                                                  /* st (abs).w: byte, no flags */
  lift_charge(x, 0x17188);
  lift_w8(x, 0xFFFFCACCu, 0xFF);                                                  /* st (abs).w: byte, no flags */
  lift_charge(x, 0x1718C);

  Team_RefreshDataCache(x);                                                       /* fall-through: no rts */
}

/*
 * Text_ExpandDigitStream (sub_FE98A; called from ROM:F8982, sub_FAF0E)
 *   in: a2 = source stream (word count, then 3-byte triplet records)
 *   Reads a word count from (a2)+, stores it to $FFFFDA1E, and for
 *   count*8 iterations: clears a working long at $FFFFDEA0, copies 3
 *   bytes from (a2)+ into its low 3 bytes, then unpacks that long's
 *   eight 3-bit groups (each masked/shifted out and biased by
 *   5+group-index) into a packed display-digit long written to
 *   (a0)+ ($FFFFDA1E's cursor).
 */
void Text_ExpandDigitStream(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;
  uint32_t acc;

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  }
  lift_charge_movem(x, 0xFE98A);

  c->a[0] = 0xFFFFDA1E;                                                     /* movea.l #$FFFFDA1E,a0 */
  lift_charge(x, 0xFE98E);
  c->a[1] = 0xFFFFDEA0;                                                     /* movea.l #$FFFFDEA0,a1 */
  lift_charge(x, 0xFE994);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2])));                       /* move.w (a2)+,d0 */
  c->a[2] += 2;
  lift_charge(x, 0xFE99A);
  lift_w16(x, c->a[0], alu_movew(c, W(c->d[0])));                           /* move.w d0,(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xFE99C);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));                               /* asl.w #3,d0 */
  lift_charge(x, 0xFE99E);

  if (W(c->d[0]) == 0) { x->declined = 1; return; }   /* dbf would wrap 65536x if the count read as 0 */

  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));                                /* subq.w #1,d0 */
  lift_charge(x, 0xFE9A0);

  for (;;)
  {
    /* loc_FE9A2 */
    alu_movel(c, 0);                                                         /* clr.l (a0) */
    lift_w8(x, c->a[0] + 0, 0);
    lift_w8(x, c->a[0] + 1, 0);
    lift_w8(x, c->a[0] + 2, 0);
    lift_w8(x, c->a[0] + 3, 0);
    lift_charge(x, 0xFE9A2);
    alu_movel(c, 0);                                                         /* clr.l (a1) */
    lift_w8(x, c->a[1] + 0, 0);
    lift_w8(x, c->a[1] + 1, 0);
    lift_w8(x, c->a[1] + 2, 0);
    lift_w8(x, c->a[1] + 3, 0);
    lift_charge(x, 0xFE9A4);
    lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[2])));                  /* move.b (a2)+,(a1) */
    c->a[2] += 1;
    lift_charge(x, 0xFE9A6);
    lift_w8(x, c->a[1] + 1, alu_moveb(c, lift_r8(x, c->a[2])));              /* move.b (a2)+,1(a1) */
    c->a[2] += 1;
    lift_charge(x, 0xFE9A8);
    lift_w8(x, c->a[1] + 2, alu_moveb(c, lift_r8(x, c->a[2])));              /* move.b (a2)+,2(a1) */
    c->a[2] += 1;
    lift_charge(x, 0xFE9AC);

    c->a[7] -= 4; lift_w32(x, c->a[7], alu_movel(c, 0));                     /* move.l #0,-(sp) */
    lift_charge(x, 0xFE9B0);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFE9B6);
    c->d[2] = alu_andl(c, 0xE0000000u, c->d[2]);                             /* and.l #$E0000000,d2 */
    lift_charge(x, 0xFE9B8);
    c->d[2] = alu_lsrl(c, c->d[2], 1);                                       /* lsr.l #1,d2 */
    lift_charge(x, 0xFE9BE);
    c->d[2] = alu_addl(c, 0x50000000u, c->d[2]);                             /* add.l #$50000000,d2 */
    lift_charge(x, 0xFE9C0);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));  /* or.l d2,(sp) */
    lift_charge(x, 0xFE9C6);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFE9C8);
    c->d[2] = alu_andl(c, 0x1C000000u, c->d[2]);                             /* and.l #$1C000000,d2 */
    lift_charge(x, 0xFE9CA);
    c->d[2] = alu_lsrl(c, c->d[2], 2);                                       /* lsr.l #2,d2 */
    lift_charge(x, 0xFE9D0);
    c->d[2] = alu_addl(c, 0x5000000u, c->d[2]);                              /* add.l #$5000000,d2 */
    lift_charge(x, 0xFE9D2);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFE9D8);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFE9DA);
    c->d[2] = alu_andl(c, 0x3800000u, c->d[2]);                              /* and.l #$3800000,d2 */
    lift_charge(x, 0xFE9DC);
    c->d[2] = alu_lsrl(c, c->d[2], 3);                                       /* lsr.l #3,d2 */
    lift_charge(x, 0xFE9E2);
    c->d[2] = alu_addl(c, 0x500000u, c->d[2]);                               /* add.l #$500000,d2 */
    lift_charge(x, 0xFE9E4);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFE9EA);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFE9EC);
    c->d[2] = alu_andl(c, 0x700000u, c->d[2]);                               /* and.l #$700000,d2 */
    lift_charge(x, 0xFE9EE);
    c->d[2] = alu_lsrl(c, c->d[2], 4);                                       /* lsr.l #4,d2 */
    lift_charge(x, 0xFE9F4);
    c->d[2] = alu_addl(c, 0x50000u, c->d[2]);                                /* add.l #unk_50000,d2 */
    lift_charge(x, 0xFE9F6);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFE9FC);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFE9FE);
    c->d[2] = alu_andl(c, 0xE0000u, c->d[2]);                                /* and.l #unk_E0000,d2 */
    lift_charge(x, 0xFEA00);
    c->d[2] = alu_lsrl(c, c->d[2], 5);                                       /* lsr.l #5,d2 */
    lift_charge(x, 0xFEA06);
    c->d[2] = alu_addl(c, 0x5000u, c->d[2]);                                 /* add.l #$5000,d2 */
    lift_charge(x, 0xFEA08);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFEA0E);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFEA10);
    c->d[2] = alu_andl(c, 0x1C000u, c->d[2]);                                /* and.l #unk_1C000,d2 */
    lift_charge(x, 0xFEA12);
    c->d[2] = alu_lsrl(c, c->d[2], 6);                                       /* lsr.l #6,d2 */
    lift_charge(x, 0xFEA18);
    c->d[2] = alu_addl(c, 0x500u, c->d[2]);                                  /* add.l #$500,d2 */
    lift_charge(x, 0xFEA1A);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFEA20);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFEA22);
    c->d[2] = alu_andl(c, 0x3800u, c->d[2]);                                 /* and.l #$3800,d2 */
    lift_charge(x, 0xFEA24);
    c->d[2] = alu_lsrl(c, c->d[2], 7);                                       /* lsr.l #7,d2 */
    lift_charge(x, 0xFEA2A);
    c->d[2] = alu_addl(c, 0x50u, c->d[2]);                                   /* add.l #$50,d2 */
    lift_charge(x, 0xFEA2C);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFEA32);

    c->d[2] = alu_movel(c, lift_r32(x, c->a[1]));                            /* move.l (a1),d2 */
    lift_charge(x, 0xFEA34);
    c->d[2] = alu_andl(c, 0x700u, c->d[2]);                                  /* and.l #$700,d2 */
    lift_charge(x, 0xFEA36);
    setw(&c->d[5], alu_movew(c, 8));                                          /* move.w #8,d5 */
    lift_charge(x, 0xFEA3C);
    c->d[2] = alu_lsrl(c, c->d[2], 8);                                       /* lsr.l d5,d2 (d5=8) */
    lift_charge(x, 0xFEA40);
    c->d[2] = alu_addl(c, 5, c->d[2]);                                        /* addq.l #5,d2 */
    lift_charge(x, 0xFEA42);
    acc = lift_r32(x, c->a[7]); lift_w32(x, c->a[7], alu_movel(c, acc | c->d[2]));
    lift_charge(x, 0xFEA44);

    lift_w32(x, c->a[0], alu_movel(c, lift_r32(x, c->a[7])));                  /* move.l (sp)+,(a0)+ */
    c->a[7] += 4;
    c->a[0] += 4;
    lift_charge(x, 0xFEA46);

    {
      uint32_t nd0 = W(W(c->d[0]) - 1);                                        /* dbf d0,loc_FE9A2 */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0xFEA48, taken, !taken);
      if (!taken) break;
    }
  }

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFEA4C);

  lift_charge(x, 0xFEA50);                                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_ClearFlagIfNoOverLimit (sub_100A6; called from
 * Object_ClassifyZone x2, with a2 swapped between the two calls)
 *   in: a2 = team block
 *   If $30(a2) bit4 is clear, returns via the shared far rts. Else
 *   scans up to 6 roster slots ($22(a2)'s array, stride $80) for one
 *   whose (sign-adjusted by facing bit7) $14 world position exceeds
 *   $58; if found, returns via the shared far rts leaving the flag
 *   set. If none exceeds it after the scan, clears $30(a2) bit4.
 */
void Roster_ClearFlagIfNoOverLimit(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[2] + 0x30), 4);                            /* btst #4,$30(a2) */
  lift_charge(x, 0x100A6);
  {
    int beqTaken = c->zf;
    lift_charge_bcc(x, 0x100AC, beqTaken);                               /* beq.w locret_15464 */
    if (beqTaken)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  c->a[0] = SEW(lift_r16(x, c->a[2] + 0x22));                            /* movea.w $22(a2),a0 */
  lift_charge(x, 0x100B0);
  c->d[1] = alu_moveql(c, 5);                                             /* moveq #5,d1 */
  lift_charge(x, 0x100B4);

  for (;;)
  {
    /* loc_100B6 */
    alu_movew(c, lift_r16(x, c->a[0] + 0x34));                            /* tst.w $34(a0) */
    lift_charge(x, 0x100B6);
    {
      int bmiTaken = c->nf;
      lift_charge_bcc(x, 0x100BA, bmiTaken);                              /* bmi.w loc_100CE */
      if (!bmiTaken)
      {
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x14)));        /* move.w $14(a0),d0 */
        lift_charge(x, 0x100BE);
        alu_btst(c, lift_r8(x, c->a[0] + 0x62), 7);                       /* btst #7,$62(a0) */
        lift_charge(x, 0x100C2);
        int bneTaken = !c->zf;
        lift_charge_bcc(x, 0x100C8, bneTaken);                            /* bne.w loc_100CE */
        if (!bneTaken)
        {
          setw(&c->d[0], alu_negw(c, W(c->d[0])));                        /* neg.w d0 */
          lift_charge(x, 0x100CC);
        }
      }
    }

    /* loc_100CE */
    c->a[0] += 0x80;                                                       /* adda.w #$80,a0 */
    lift_charge(x, 0x100CE);
    alu_cmpw(c, 0x58, W(c->d[0]));                                         /* cmp.w #$58,d0 */
    lift_charge(x, 0x100D2);

    {
      int gt = (!c->zf && c->nf == c->vf);                                  /* dbgt d1,loc_100B6 */
      int taken, expired;
      if (gt) { taken = 0; expired = 0; }
      else
      {
        uint32_t nd1 = W(W(c->d[1]) - 1);
        setw(&c->d[1], nd1);
        expired = (nd1 == 0xFFFF);
        taken = !expired;
      }
      lift_charge_dbcc(x, 0x100D6, taken, expired);
      if (gt || expired) break;
    }
  }

  {
    int gtFinal = (!c->zf && c->nf == c->vf);                              /* bgt.w locret_15464 */
    lift_charge_bcc(x, 0x100DA, gtFinal);
    if (gtFinal)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_w8(x, c->a[2] + 0x30, alu_bclr(c, lift_r8(x, c->a[2] + 0x30), 4));   /* bclr #4,$30(a2) */
  lift_charge(x, 0x100DE);

  lift_charge(x, 0x100E4);                                                   /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ClassifyZone (sub_FFEA; called from sub_FF0C)
 *   in: a3 = object being classified
 *   Refreshes both teams' "over-limit" flags via
 *   Roster_ClearFlagIfNoOverLimit (home then away, swapping a1/a2
 *   between calls), then classifies a3's $14 position against a
 *   fixed threshold (0x54, mirrored for negative positions) to pick
 *   a team (a2) and scan its roster for a matching zone slot,
 *   setting $30(a2) bit4 if found. Two of the four `exg` swaps in
 *   this routine (at $10032/$1007C) are disguised as `dc.w $C549` in
 *   the listing — IDA left them as data, but the bytes decode to a
 *   real `exg.l a2,a1`.
 */
void Object_ClassifyZone(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 5);                              /* btst #5,(abs) */
  lift_charge(x, 0xFFEA);
  {
    int beqTaken = c->zf;
    lift_charge_bcc(x, 0xFFF0, beqTaken);                                /* beq.w locret_15464 */
    if (beqTaken)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  alu_btst(c, lift_r8(x, 0xFFFFC2F2u), 2);                              /* btst #2,(abs) */
  lift_charge(x, 0xFFF4);
  {
    int bneTaken = !c->zf;
    lift_charge_bcc(x, 0xFFFA, bneTaken);                                /* bne.w locret_15464 */
    if (bneTaken)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_FFFE */
  c->a[1] = TEAM_HOME;                                                    /* movea.w #$C6CE,a1 */
  lift_charge(x, 0xFFFE);
  c->a[2] = c->a[1] + TEAM_SIZE;                                          /* lea $364(a1),a2 */
  lift_charge(x, 0x10002);
  lift_call(x, 0x10006, 4, Roster_ClearFlagIfNoOverLimit);                 /* bsr.w sub_100A6 */
  if (x->declined) return;
  {
    uint32_t t = c->a[1]; c->a[1] = c->a[2]; c->a[2] = t;                   /* exg a1,a2 */
  }
  lift_charge(x, 0x1000A);
  lift_call(x, 0x1000C, 4, Roster_ClearFlagIfNoOverLimit);                 /* bsr.w sub_100A6 */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, 0x54));                                      /* move.w #$54,d0 */
  lift_charge(x, 0x10010);
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[0]));                    /* cmp.w $14(a3),d0 */
  lift_charge(x, 0x10014);
  {
    int gt = (!c->zf && c->nf == c->vf);                                    /* bgt */
    lift_charge_bcc(x, 0x10018, gt);                                        /* bgt.w loc_1005C */
    if (gt) goto loc_1005C;
  }
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x20), W(c->d[0]));                     /* cmp.w $20(a3),d0 */
  lift_charge(x, 0x1001C);
  {
    int le = (c->zf || c->nf != c->vf);                                     /* ble */
    lift_charge_bcc(x, 0x10020, le);                                        /* ble.w locret_15464 */
    if (le)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  setw(&c->d[0], alu_addw(c, 0xA, W(c->d[0])));                              /* add.w #$A,d0 */
  lift_charge(x, 0x10024);
  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 1);                                  /* btst #1,(abs) */
  lift_charge(x, 0x10028);
  {
    int beqTaken2 = c->zf;
    lift_charge_bcc(x, 0x1002E, beqTaken2);                                  /* beq.w loc_10034 */
    if (!beqTaken2)
    {
      uint32_t t = c->a[1]; c->a[1] = c->a[2]; c->a[2] = t;                   /* exg.l a2,a1 (disguised dc.w $C549) */
      lift_charge(x, 0x10032);
    }
  }

  /* loc_10034 */
  c->d[2] = alu_moveql(c, 5);                                                /* moveq #5,d2 */
  lift_charge(x, 0x10034);
  c->a[0] = SEW(lift_r16(x, c->a[2] + 0x22));                                /* movea.w $22(a2),a0 */
  lift_charge(x, 0x10036);

  for (;;)
  {
    /* loc_1003A */
    alu_movew(c, lift_r16(x, c->a[0] + 0x34));                               /* tst.w $34(a0) */
    lift_charge(x, 0x1003A);
    {
      int bmiTaken = c->nf;
      lift_charge_bcc(x, 0x1003E, bmiTaken);                                 /* bmi.w loc_10052 */
      if (!bmiTaken)
      {
        alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[0]));                 /* cmp.w $14(a0),d0 */
        lift_charge(x, 0x10042);
        int ge = (c->nf == c->vf);                                            /* bge */
        lift_charge_bcc(x, 0x10046, ge);                                      /* bge.w loc_10052 */
        if (!ge)
        {
          lift_w8(x, c->a[2] + 0x30, alu_bset(c, lift_r8(x, c->a[2] + 0x30), 4));  /* bset #4,$30(a2) */
          lift_charge(x, 0x1004A);
          lift_charge(x, 0x10050);                                              /* rts */
          c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
          c->a[7] += 4;
          return;
        }
      }
    }

    /* loc_10052 */
    c->a[0] += 0x80;                                                          /* adda.w #$80,a0 */
    lift_charge(x, 0x10052);

    {
      uint32_t nd2 = W(W(c->d[2]) - 1);                                        /* dbf d2,loc_1003A */
      setw(&c->d[2], nd2);
      int taken = (nd2 != 0xFFFF);
      lift_charge_dbcc(x, 0x10056, taken, !taken);
      if (!taken) break;
    }
  }

  lift_charge(x, 0x1005A);                                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  return;

loc_1005C:
  setw(&c->d[0], alu_negw(c, W(c->d[0])));                                     /* neg.w d0 */
  lift_charge(x, 0x1005C);
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[0]));                        /* cmp.w $14(a3),d0 */
  lift_charge(x, 0x1005E);
  {
    int lt = (c->nf != c->vf);                                                 /* blt */
    lift_charge_bcc(x, 0x10062, lt);                                           /* blt.w locret_15464 */
    if (lt)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x20), W(c->d[0]));                        /* cmp.w $20(a3),d0 */
  lift_charge(x, 0x10066);
  {
    int ge2 = (c->nf == c->vf);                                                 /* bge */
    lift_charge_bcc(x, 0x1006A, ge2);                                           /* bge.w locret_15464 */
    if (ge2)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  setw(&c->d[0], alu_subw(c, 0xA, W(c->d[0])));                                 /* sub.w #$A,d0 */
  lift_charge(x, 0x1006E);
  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 1);                                      /* btst #1,(abs) */
  lift_charge(x, 0x10072);
  {
    int bneTaken2 = !c->zf;
    lift_charge_bcc(x, 0x10078, bneTaken2);                                     /* bne.w loc_1007E */
    if (!bneTaken2)
    {
      uint32_t t = c->a[1]; c->a[1] = c->a[2]; c->a[2] = t;                      /* exg.l a2,a1 (disguised dc.w $C549) */
      lift_charge(x, 0x1007C);
    }
  }

  /* loc_1007E */
  c->d[2] = alu_moveql(c, 5);                                                    /* moveq #5,d2 */
  lift_charge(x, 0x1007E);
  c->a[0] = SEW(lift_r16(x, c->a[2] + 0x22));                                    /* movea.w $22(a2),a0 */
  lift_charge(x, 0x10080);

  for (;;)
  {
    /* loc_10084 */
    alu_movew(c, lift_r16(x, c->a[0] + 0x34));                                   /* tst.w $34(a0) */
    lift_charge(x, 0x10084);
    {
      int bmiTaken2 = c->nf;
      lift_charge_bcc(x, 0x10088, bmiTaken2);                                     /* bmi.w loc_1009C */
      if (!bmiTaken2)
      {
        alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[0]));                      /* cmp.w $14(a0),d0 */
        lift_charge(x, 0x1008C);
        int le2 = (c->zf || c->nf != c->vf);                                       /* ble */
        lift_charge_bcc(x, 0x10090, le2);                                          /* ble.w loc_1009C */
        if (!le2)
        {
          lift_w8(x, c->a[2] + 0x30, alu_bset(c, lift_r8(x, c->a[2] + 0x30), 4));   /* bset #4,$30(a2) */
          lift_charge(x, 0x10094);
          lift_charge(x, 0x1009A);                                                  /* rts */
          c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
          c->a[7] += 4;
          return;
        }
      }
    }

    /* loc_1009C */
    c->a[0] += 0x80;                                                                /* adda.w #$80,a0 */
    lift_charge(x, 0x1009C);

    {
      uint32_t nd2b = W(W(c->d[2]) - 1);                                            /* dbf d2,loc_10084 */
      setw(&c->d[2], nd2b);
      int taken2 = (nd2b != 0xFFFF);
      lift_charge_dbcc(x, 0x100A0, taken2, !taken2);
      if (!taken2) break;
    }
  }

  lift_charge(x, 0x100A4);                                                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ComputeBoardImpulse (sub_10488; batch: sub_1054C)
 *   in:  d0, d1 = scalars; a3 = object slot pointer (unchanged)
 *   out: d0 += clamped impulse from BF9C, d1 += board-zone offset from
 *        BF9E; both scratch words cleared then possibly rewritten
 *
 * Early-outs via the shared far rts at $15464 when $34(a3) == 0. Otherwise
 * computes a zone index d2 = divs((d1 - $14(a3)) * (a3), (a3) - d0) +
 * $14(a3); if (d0 ^ (a3)) is non-negative the zone lookup is skipped
 * entirely (BF9E stays 0). When negative, d2 selects one of two symmetric
 * "board" bands (positive: [$DB,$121] around $FE/$B8/$144; negative:
 * [-$121,-$DB] mirrored around -$FE/-$FEBC/-$FF48/-$FF02) each producing
 * a table value minus d2 stored to BF9E; outside both bands BF9E stays 0.
 * Then calls Object_ScaleImpulseByObjectField twice — once with
 * (d2,d3) = ($14(a3)-$FE, d1-$FE), once with ($14(a3)+$FE, d1+$FE) — each
 * call may overwrite BF9C, so only the second call's write (if any)
 * survives. Finally d0 += BF9C, d1 += BF9E.
 */
void Object_ComputeBoardImpulse(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFBF9C, alu_movew(c, 0));               /* clr.w ($FFFFBF9C).w */
  lift_charge(x, 0x10488);
  lift_w16(x, 0xFFFFBF9E, alu_movew(c, 0));               /* clr.w ($FFFFBF9E).w */
  lift_charge(x, 0x1048C);

  (void)alu_movew(c, lift_r16(x, c->a[3] + 0x34));         /* tst.w $34(a3) */
  lift_charge(x, 0x10490);
  int noPhys = c->zf;                                      /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x10494, noPhys);
  if (noPhys)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3])));      /* move.w (a3),d2 */
  lift_charge(x, 0x10498);
  setw(&c->d[2], alu_eorw(c, W(c->d[0]), W(c->d[2])));     /* eor.w d0,d2 */
  lift_charge(x, 0x1049A);
  int nonneg = !c->nf;                                     /* bpl.w loc_1051E */
  lift_charge_bcc(x, 0x1049C, nonneg);
  if (nonneg) goto loc_1051E;

  setw(&c->d[2], alu_movew(c, W(c->d[1])));                /* move.w d1,d2 */
  lift_charge(x, 0x104A0);
  setw(&c->d[2], alu_subw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[2])));  /* sub.w $14(a3),d2 */
  lift_charge(x, 0x104A2);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[3])));      /* move.w (a3),d3 */
  lift_charge(x, 0x104A6);
  c->d[2] = alu_muls(c, W(c->d[3]), W(c->d[2]));           /* muls.w d3,d2 */
  lift_charge_muls(x, 0x104A8, W(c->d[3]));
  setw(&c->d[3], alu_subw(c, W(c->d[0]), W(c->d[3])));     /* sub.w d0,d3 */
  lift_charge(x, 0x104AA);

  lift_charge_divs(x, 0x104AC, W(c->d[3]), c->d[2]);       /* divs.w d3,d2 */
  if (x->declined) return;
  c->d[2] = alu_divs(c, W(c->d[3]), c->d[2]);

  setw(&c->d[2], alu_addw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[2])));  /* add.w $14(a3),d2 */
  lift_charge(x, 0x104AE);

  alu_cmpw(c, 0x121, W(c->d[2]));                          /* cmpi.w #$121,d2 */
  lift_charge(x, 0x104B2);
  int gt1 = (!c->zf && c->nf == c->vf);                    /* bgt.w loc_1051E */
  lift_charge_bcc(x, 0x104B6, gt1);
  if (gt1) goto loc_1051E;

  alu_cmpw(c, 0xDB, W(c->d[2]));                           /* cmpi.w #$DB,d2 */
  lift_charge(x, 0x104BA);
  int lt1 = (c->nf != c->vf);                              /* blt.w loc_104EA */
  lift_charge_bcc(x, 0x104BE, lt1);
  if (lt1) goto loc_104EA;

  /* $DB <= d2 <= $121: positive board band */
  setw(&c->d[3], alu_movew(c, 0x144));                     /* move.w #$144,d3 */
  lift_charge(x, 0x104C2);

  alu_cmpw(c, 0xFE, W(c->d[2]));                           /* cmpi.w #$FE,d2 */
  lift_charge(x, 0x104C6);
  {
    int gt2 = (!c->zf && c->nf == c->vf);                  /* bgt.w loc_104E0 */
    lift_charge_bcc(x, 0x104CA, gt2);
    if (!gt2)
    {
      int lt2 = (c->nf != c->vf);                          /* blt.w loc_104DC */
      lift_charge_bcc(x, 0x104CE, lt2);
      if (!lt2)
      {
        alu_cmpw(c, 0xFE, lift_r16(x, c->a[3] + 0x14));    /* cmp.w #$FE,$14(a3) */
        lift_charge(x, 0x104D2);
        int gt3 = (!c->zf && c->nf == c->vf);              /* bgt.w loc_104E0 */
        lift_charge_bcc(x, 0x104D8, gt3);
        if (!gt3)
        {
          setw(&c->d[3], alu_movew(c, 0xB8));              /* loc_104DC: move.w #$B8,d3 */
          lift_charge(x, 0x104DC);
        }
      }
      else
      {
        setw(&c->d[3], alu_movew(c, 0xB8));                /* loc_104DC: move.w #$B8,d3 */
        lift_charge(x, 0x104DC);
      }
    }
  }

  /* loc_104E0 */
  setw(&c->d[3], alu_subw(c, W(c->d[2]), W(c->d[3])));     /* sub.w d2,d3 */
  lift_charge(x, 0x104E0);
  lift_w16(x, 0xFFFFBF9E, alu_movew(c, W(c->d[3])));       /* move.w d3,($FFFFBF9E).w */
  lift_charge(x, 0x104E2);
  lift_charge(x, 0x104E6);                                 /* bra.w loc_1051E */
  goto loc_1051E;

loc_104EA:
  alu_cmpw(c, 0xFF25, W(c->d[2]));                         /* cmpi.w #$FF25,d2 */
  lift_charge(x, 0x104EA);
  {
    int gt4 = (!c->zf && c->nf == c->vf);                  /* bgt.w loc_1051E */
    lift_charge_bcc(x, 0x104EE, gt4);
    if (gt4) goto loc_1051E;
  }
  alu_cmpw(c, 0xFEDF, W(c->d[2]));                         /* cmpi.w #$FEDF,d2 */
  lift_charge(x, 0x104F2);
  {
    int lt4 = (c->nf != c->vf);                            /* blt.w loc_1051E */
    lift_charge_bcc(x, 0x104F6, lt4);
    if (lt4) goto loc_1051E;
  }

  /* -$121 <= d2 <= -$DB: negative board band */
  setw(&c->d[3], alu_movew(c, 0xFF48));                    /* move.w #$FF48,d3 */
  lift_charge(x, 0x104FA);

  alu_cmpw(c, 0xFF02, W(c->d[2]));                         /* cmpi.w #$FF02,d2 */
  lift_charge(x, 0x104FE);
  {
    int gt5 = (!c->zf && c->nf == c->vf);                  /* bgt.w loc_10518 */
    lift_charge_bcc(x, 0x10502, gt5);
    if (!gt5)
    {
      int lt5 = (c->nf != c->vf);                          /* blt.w loc_10514 */
      lift_charge_bcc(x, 0x10506, lt5);
      if (!lt5)
      {
        alu_cmpw(c, 0xFF02, lift_r16(x, c->a[3] + 0x14));  /* cmp.w #$FF02,$14(a3) */
        lift_charge(x, 0x1050A);
        int gt6 = (!c->zf && c->nf == c->vf);              /* bgt.w loc_10518 */
        lift_charge_bcc(x, 0x10510, gt6);
        if (!gt6)
        {
          setw(&c->d[3], alu_movew(c, 0xFEBC));            /* loc_10514: move.w #$FEBC,d3 */
          lift_charge(x, 0x10514);
        }
      }
      else
      {
        setw(&c->d[3], alu_movew(c, 0xFEBC));              /* loc_10514: move.w #$FEBC,d3 */
        lift_charge(x, 0x10514);
      }
    }
  }

  /* loc_10518 */
  setw(&c->d[3], alu_subw(c, W(c->d[2]), W(c->d[3])));     /* sub.w d2,d3 */
  lift_charge(x, 0x10518);
  lift_w16(x, 0xFFFFBF9E, alu_movew(c, W(c->d[3])));       /* move.w d3,($FFFFBF9E).w */
  lift_charge(x, 0x1051A);

loc_1051E:
  setw(&c->d[3], alu_movew(c, W(c->d[1])));                /* move.w d1,d3 */
  lift_charge(x, 0x1051E);
  setw(&c->d[3], alu_subw(c, 0xFE, W(c->d[3])));           /* sub.w #$FE,d3 */
  lift_charge(x, 0x10520);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d2 */
  lift_charge(x, 0x10524);
  setw(&c->d[2], alu_subw(c, 0xFE, W(c->d[2])));           /* sub.w #$FE,d2 */
  lift_charge(x, 0x10528);
  lift_call(x, 0x1052C, 4, Object_ScaleImpulseByObjectField);   /* bsr.w sub_1054C */

  setw(&c->d[3], alu_movew(c, W(c->d[1])));                /* move.w d1,d3 */
  lift_charge(x, 0x10530);
  setw(&c->d[3], alu_addw(c, 0xFE, W(c->d[3])));           /* add.w #$FE,d3 */
  lift_charge(x, 0x10532);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d2 */
  lift_charge(x, 0x10536);
  setw(&c->d[2], alu_addw(c, 0xFE, W(c->d[2])));           /* add.w #$FE,d2 */
  lift_charge(x, 0x1053A);
  lift_call(x, 0x1053E, 4, Object_ScaleImpulseByObjectField);   /* bsr.w sub_1054C */

  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFBF9C), W(c->d[0])));  /* add.w ($FFFFBF9C).w,d0 */
  lift_charge(x, 0x10542);
  setw(&c->d[1], alu_addw(c, lift_r16(x, 0xFFFFBF9E), W(c->d[1])));  /* add.w ($FFFFBF9E).w,d1 */
  lift_charge(x, 0x10546);

  lift_charge(x, 0x1054A);                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ScaleImpulseByObjectField (sub_1054C; called from sub_10488)
 *   in:  d0, d2, d3 = scalars; a3 = object slot pointer (unchanged)
 *   out: writes clamped result word to $FFFFBF9C; d2/d3/d4 clobbered
 *
 * Early-outs via the shared far rts at $15464 when d3^d2 is negative,
 * or when the scaled/divided value falls outside [-$50,$50]. Otherwise
 * computes d4 = divs((d0 - (a3)) * d2, d2 - d3) + (a3), reflects it
 * against +/-$50 (sign taken from d4, with a tst.w (a3) fallback when
 * d4.w == 0), and stores $50-or--$50 minus d4 to $FFFFBF9C.
 */
void Object_ScaleImpulseByObjectField(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, W(c->d[3])));            /* move.w d3,d4 */
  lift_charge(x, 0x1054C);
  setw(&c->d[4], alu_eorw(c, W(c->d[2]), W(c->d[4])));  /* eor.w d2,d4 */
  lift_charge(x, 0x1054E);
  int neg0 = c->nf;                                     /* bpl.w locret_15464 */
  lift_charge_bcc(x, 0x10550, !neg0);
  if (!neg0)
  {
    lift_charge(x, 0x15464);                            /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[4], alu_movew(c, W(c->d[0])));              /* move.w d0,d4 */
  lift_charge(x, 0x10554);
  setw(&c->d[4], alu_subw(c, lift_r16(x, c->a[3]), W(c->d[4])));  /* sub.w (a3),d4 */
  lift_charge(x, 0x10556);
  c->d[4] = alu_muls(c, W(c->d[2]), W(c->d[4]));          /* muls.w d2,d4 */
  lift_charge_muls(x, 0x10558, W(c->d[2]));
  setw(&c->d[2], alu_subw(c, W(c->d[3]), W(c->d[2])));    /* sub.w d3,d2 */
  lift_charge(x, 0x1055A);

  lift_charge_divs(x, 0x1055C, W(c->d[2]), c->d[4]);      /* divs.w d2,d4 */
  if (x->declined) return;                                /* zero divisor */
  c->d[4] = alu_divs(c, W(c->d[2]), c->d[4]);

  setw(&c->d[4], alu_addw(c, lift_r16(x, c->a[3]), W(c->d[4])));  /* add.w (a3),d4 */
  lift_charge(x, 0x1055E);

  alu_cmpw(c, 0x50, W(c->d[4]));                          /* cmpi.w #$50,d4 */
  lift_charge(x, 0x10560);
  int gt = (!c->zf && c->nf == c->vf);                    /* bgt.w locret_15464 */
  lift_charge_bcc(x, 0x10564, gt);
  if (gt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_cmpw(c, 0xFFB0, W(c->d[4]));                        /* cmpi.w #$FFB0,d4 */
  lift_charge(x, 0x10568);
  int lt = (c->nf != c->vf);                              /* blt.w locret_15464 */
  lift_charge_bcc(x, 0x1056C, lt);
  if (lt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->d[3] = alu_moveql(c, 0x50);                          /* moveq #$50,d3 */
  lift_charge(x, 0x10570);

  (void)alu_movew(c, W(c->d[4]));                         /* tst.w d4 */
  lift_charge(x, 0x10572);
  int nz = !c->zf;                                        /* bne.w loc_1057A */
  lift_charge_bcc(x, 0x10574, nz);
  if (!nz)
  {
    (void)alu_movew(c, lift_r16(x, c->a[3]));              /* tst.w (a3) */
    lift_charge(x, 0x10578);
  }

  /* loc_1057A */
  int nonneg = !c->nf;                                    /* bpl.w loc_10580 */
  lift_charge_bcc(x, 0x1057A, nonneg);
  if (!nonneg)
  {
    c->d[3] = alu_negw(c, W(c->d[3]));                     /* neg.w d3 */
    lift_charge(x, 0x1057E);
  }

  /* loc_10580 */
  setw(&c->d[3], alu_subw(c, W(c->d[4]), W(c->d[3])));    /* sub.w d4,d3 */
  lift_charge(x, 0x10580);
  lift_w16(x, 0xFFFFBF9C, alu_movew(c, W(c->d[3])));      /* move.w d3,($FFFFBF9C).w — MOVE sets NZ, clears VC */
  lift_charge(x, 0x10582);

  lift_charge(x, 0x10586);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ProjectScreenColumn (sub_10366; called from sub_10346)
 *   in:  d4 = world coordinate; d1 = fold half-width; a1 = output cursor
 *        (2-word record); globals $FFFFB74A/B75E/B772/B774 = camera
 *        origin/scale/divisor constants
 *   out: (a1) = folded screen offset, 2(a1) = perspective-scaled depth
 *        index; a1 += 4; d0/d2/d4 clobbered, d1 unchanged (negated
 *        twice internally)
 *
 * If $FFFFB774 == 0, or the depth divide (d0-relative asr.l#4 / B774)
 * comes out negative, or the final muls/divs against B772/B774
 * overflows (V set), ONLY 2(a1) is stamped $FFFF (the sentinel) and
 * (a1) itself is left untouched from whatever was there before; a1
 * still advances by 4. Otherwise both words are written: 2(a1) gets
 * the depth index and (a1) gets d0 reflected into [-d1,+d1] by
 * up to two rounds of negate-and-fold (each round adding d1*3 after a
 * negate — a mirror-and-wrap fold), matching the original's back-to-
 * back cmp/blt/neg/add-add-add and cmp/bgt/neg/add-add-add pairs.
 */
void Object_ProjectScreenColumn(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, W(c->d[4])));                /* move.w d4,d0 */
  lift_charge(x, 0x10366);
  setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFB75E), W(c->d[0])));  /* sub.w ($FFFFB75E).w,d0 */
  lift_charge(x, 0x10368);

  (void)alu_movew(c, lift_r16(x, 0xFFFFB774));             /* tst.w ($FFFFB774).w */
  lift_charge(x, 0x1036C);
  int noScale = c->zf;                                     /* beq.w loc_103BE */
  lift_charge_bcc(x, 0x10370, noScale);
  if (noScale) goto loc_103BE;

  c->d[2] = alu_movew(c, W(c->d[0]));                       /* move.w d0,d2 (upper half stale from prior use) */
  lift_charge(x, 0x10374);
  c->d[2] = alu_swap(c, c->d[2]);                           /* swap d2 */
  lift_charge(x, 0x10376);
  setw(&c->d[2], alu_movew(c, 0));                          /* clr.w d2 */
  lift_charge(x, 0x10378);
  c->d[2] = alu_asrl(c, c->d[2], 4);                        /* asr.l #4,d2 */
  lift_charge(x, 0x1037A);

  lift_charge_divs(x, 0x1037C, W(lift_r16(x, 0xFFFFB774)), c->d[2]);  /* divs.w ($FFFFB774).w,d2 */
  if (x->declined) return;
  c->d[2] = alu_divs(c, W(lift_r16(x, 0xFFFFB774)), c->d[2]);

  int neg1 = c->nf;                                         /* bmi.w loc_103BE */
  lift_charge_bcc(x, 0x10380, neg1);
  if (neg1) goto loc_103BE;

  lift_w16(x, c->a[1] + 2, alu_movew(c, W(c->d[2])));       /* move.w d2,2(a1) */
  lift_charge(x, 0x10384);

  c->d[0] = alu_muls(c, W(lift_r16(x, 0xFFFFB772)), W(c->d[0]));  /* muls.w ($FFFFB772).w,d0 */
  lift_charge_muls(x, 0x10388, W(lift_r16(x, 0xFFFFB772)));

  lift_charge_divs(x, 0x1038C, W(lift_r16(x, 0xFFFFB774)), c->d[0]);  /* divs.w ($FFFFB774).w,d0 */
  if (x->declined) return;
  c->d[0] = alu_divs(c, W(lift_r16(x, 0xFFFFB774)), c->d[0]);

  int ovf = c->vf;                                          /* bvs.w loc_103BE */
  lift_charge_bcc(x, 0x10390, ovf);
  if (ovf) goto loc_103BE;

  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFB74A), W(c->d[0])));  /* add.w ($FFFFB74A).w,d0 */
  lift_charge(x, 0x10394);

  alu_cmpw(c, W(c->d[1]), W(c->d[0]));                      /* cmp.w d1,d0 */
  lift_charge(x, 0x10398);
  int lt1 = (c->nf != c->vf);                               /* blt.w loc_103A6 */
  lift_charge_bcc(x, 0x1039A, lt1);
  if (!lt1)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
    lift_charge(x, 0x1039E);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));    /* add.w d1,d0 (x3) */
    lift_charge(x, 0x103A0);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));
    lift_charge(x, 0x103A2);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));
    lift_charge(x, 0x103A4);
  }

  /* loc_103A6 */
  setw(&c->d[1], alu_negw(c, W(c->d[1])));                  /* neg.w d1 */
  lift_charge(x, 0x103A6);
  alu_cmpw(c, W(c->d[1]), W(c->d[0]));                      /* cmp.w d1,d0 */
  lift_charge(x, 0x103A8);
  int gt2 = (!c->zf && c->nf == c->vf);                     /* bgt.w loc_103B6 */
  lift_charge_bcc(x, 0x103AA, gt2);
  if (!gt2)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
    lift_charge(x, 0x103AE);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));    /* add.w d1,d0 (x3) */
    lift_charge(x, 0x103B0);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));
    lift_charge(x, 0x103B2);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));
    lift_charge(x, 0x103B4);
  }

  /* loc_103B6 */
  setw(&c->d[1], alu_negw(c, W(c->d[1])));                  /* neg.w d1 (restores original d1) */
  lift_charge(x, 0x103B6);
  lift_w16(x, c->a[1], alu_movew(c, W(c->d[0])));           /* move.w d0,(a1) */
  lift_charge(x, 0x103B8);
  lift_charge(x, 0x103BA);                                  /* bra.w loc_103C4 */
  goto loc_103C4;

loc_103BE:
  lift_w16(x, c->a[1] + 2, alu_movew(c, 0xFFFF));           /* move.w #$FFFF,2(a1) */
  lift_charge(x, 0x103BE);

loc_103C4:
  c->a[1] += 4;                                             /* addq.w #4,a1 */
  lift_charge(x, 0x103C4);

  lift_charge(x, 0x103C6);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Camera_UpdateShadowGate (sub_F6EBE; called from sub_15EC0)
 *   in:  d7 = per-frame decrement (elsewhere-set); flag/counter bytes at
 *        $FFFFC2EC/C2FA/C2F0/C2F6, counters at $FFFFC304/C306/C308/C30E/
 *        C310/C312/C314, $FFFFB8A2 = a distance-ish value
 *   out: gated updates to the $C2F6 bit-7 flag and the $C30E/C314 tracking
 *        counters via Math_SqrtU32; d0 clobbered
 *
 * Bails immediately if bit7 of $C2EC or bit0 of $C2FA is set. If bit0 of
 * $C2F0 is set, bails via a second (equivalent) rts. Otherwise: while
 * $C2F6 bit7 is clear, counts down a hold timer at $C304 and, once it
 * expires, evaluates squared-distance thresholds against $B8A2 to set
 * the $C2F6 bit7 flag; while bit7 is set, evaluates a second threshold to
 * clear it, then advances a hold/refresh cycle for $C30E/C314 via
 * Math_SqrtU32. The final movem push/pop pair at loc_F6F82 is a no-op
 * (restores every register it just saved) — preserved for cycle-exactness.
 */
void Camera_UpdateShadowGate(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 7);              /* btst #7,($C2EC).w */
  lift_charge(x, 0xF6EBE);
  lift_charge_bcc(x, 0xF6EC4, !c->zf);                  /* bne.w locret_F6F8A */
  if (!c->zf)
  {
    lift_charge(x, 0xF6F8A);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 0);              /* btst #0,($C2FA).w */
  lift_charge(x, 0xF6EC8);
  lift_charge_bcc(x, 0xF6ECE, !c->zf);                  /* bne.w locret_F6F8A */
  if (!c->zf)
  {
    lift_charge(x, 0xF6F8A);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2F0u), 0);              /* btst #0,($C2F0).w */
  lift_charge(x, 0xF6ED2);
  lift_charge_bcc(x, 0xF6ED8, !c->zf);                  /* bne.w locret_F6EFA */
  if (!c->zf)
  {
    lift_charge(x, 0xF6EFA);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2F6u), 7);              /* btst #7,($C2F6).w */
  lift_charge(x, 0xF6EDC);
  lift_charge_bcc(x, 0xF6EE2, !c->zf);                  /* bne.w loc_F6F24 */
  if (!c->zf)
    goto loc_F6F24;

  (void)alu_movew(c, lift_r16(x, 0xFFFFC304u));         /* tst.w ($C304).w */
  lift_charge(x, 0xF6EE6);
  lift_charge_bcc(x, 0xF6EEA, c->zf);                   /* beq.w loc_F6EFC */
  if (c->zf)
    goto loc_F6EFC;

  lift_w16(x, 0xFFFFC304u, alu_subw(c, 1, lift_r16(x, 0xFFFFC304u)));  /* subq.w #1,($C304).w */
  lift_charge(x, 0xF6EEE);
  lift_charge_bcc(x, 0xF6EF2, !c->nf);                  /* bpl.w locret_F6F8A */
  if (!c->nf)
  {
    lift_charge(x, 0xF6F8A);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, 0xFFFFC304u, alu_movew(c, 0));            /* clr.w ($C304).w */
  lift_charge(x, 0xF6EF6);
  /* fall through to locret_F6EFA */
  lift_charge(x, 0xF6EFA);
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  return;

loc_F6EFC:
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC310u)));   /* move.w ($C310).w,d0 */
  lift_charge(x, 0xF6EFC);
  lift_w16(x, 0xFFFFC312u, alu_movew(c, W(c->d[0])));        /* move.w d0,($C312).w */
  lift_charge(x, 0xF6F00);
  lift_w16(x, 0xFFFFC312u, alu_subw(c, 0xB, lift_r16(x, 0xFFFFC312u)));  /* sub.w #$B,($C312).w */
  lift_charge(x, 0xF6F04);
  setw(&c->d[0], alu_subw(c, 0x49, W(c->d[0])));             /* sub.w #$49,d0 */
  lift_charge(x, 0xF6F0A);
  {
    uint32_t src0 = W(c->d[0]);
    c->d[0] = alu_muls(c, src0, src0);                       /* muls.w d0,d0 */
    lift_charge_muls(x, 0xF6F0E, src0);
  }
  c->d[0] = alu_asrl(c, c->d[0], 2);                         /* asr.l #2,d0 */
  lift_charge(x, 0xF6F10);
  setw(&c->d[0], alu_addw(c, 6, W(c->d[0])));                /* addq.w #6,d0 */
  lift_charge(x, 0xF6F12);
  alu_cmpw(c, lift_r16(x, 0xFFFFB8A2u), W(c->d[0]));         /* cmp.w ($B8A2).w,d0 */
  lift_charge(x, 0xF6F14);
  int gt = (!c->zf && c->nf == c->vf);                       /* bgt.w locret_F6F8A */
  lift_charge_bcc(x, 0xF6F18, gt);
  if (gt)
  {
    lift_charge(x, 0xF6F8A);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_w8(x, 0xFFFFC2F6u, alu_bset(c, lift_r8(x, 0xFFFFC2F6u), 7));  /* bset #7,($C2F6).w */
  lift_charge(x, 0xF6F1C);
  lift_charge(x, 0xF6F22);                                   /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  return;

loc_F6F24:
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC312u)));    /* move.w ($C312).w,d0 */
  lift_charge(x, 0xF6F24);
  setw(&c->d[0], alu_subw(c, 0x41, W(c->d[0])));              /* sub.w #$41,d0 */
  lift_charge(x, 0xF6F28);
  {
    uint32_t src0 = W(c->d[0]);
    c->d[0] = alu_muls(c, src0, src0);                        /* muls.w d0,d0 */
    lift_charge_muls(x, 0xF6F2C, src0);
  }
  c->d[0] = alu_asrl(c, c->d[0], 2);                          /* asr.l #2,d0 */
  lift_charge(x, 0xF6F2E);
  alu_cmpw(c, lift_r16(x, 0xFFFFB8A2u), W(c->d[0]));          /* cmp.w ($B8A2).w,d0 */
  lift_charge(x, 0xF6F30);
  int lt = (c->nf != c->vf);                                  /* blt.w loc_F6F3E */
  lift_charge_bcc(x, 0xF6F34, lt);
  if (!lt)
  {
    lift_w8(x, 0xFFFFC2F6u, alu_bclr(c, lift_r8(x, 0xFFFFC2F6u), 7));  /* bclr #7,($C2F6).w */
    lift_charge(x, 0xF6F38);
  }

  /* loc_F6F3E */
  lift_w16(x, 0xFFFFC306u, alu_subw(c, W(c->d[7]), lift_r16(x, 0xFFFFC306u)));  /* sub.w d7,($C306).w */
  lift_charge(x, 0xF6F3E);
  lift_charge_bcc(x, 0xF6F42, !c->nf);                        /* bpl.w locret_F6F8A */
  if (!c->nf)
  {
    lift_charge(x, 0xF6F8A);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, 0xFFFFC306u, alu_movew(c, 0x14));               /* move.w #$14,($C306).w */
  lift_charge(x, 0xF6F46);
  lift_w16(x, 0xFFFFC308u, alu_addw(c, 1, lift_r16(x, 0xFFFFC308u)));  /* addq.w #1,($C308).w */
  lift_charge(x, 0xF6F4C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB8A2u)));     /* move.w ($B8A2).w,d0 */
  lift_charge(x, 0xF6F50);
  c->d[0] = alu_extl(c, W(c->d[0]));                          /* ext.l d0 */
  lift_charge(x, 0xF6F54);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));                 /* asl.w #2,d0 (word only) */
  lift_charge(x, 0xF6F56);

  lift_call(x, 0xF6F58, 6, Math_SqrtU32);                     /* jsr sub_110BE */
  if (x->declined) return;

  setw(&c->d[0], alu_addw(c, 0x41, W(c->d[0])));              /* add.w #$41,d0 */
  lift_charge(x, 0xF6F5E);
  lift_w16(x, 0xFFFFC30Eu, alu_movew(c, W(c->d[0])));         /* move.w d0,($C30E).w */
  lift_charge(x, 0xF6F62);
  alu_cmpw(c, lift_r16(x, 0xFFFFC314u), W(c->d[0]));          /* cmp.w ($C314).w,d0 */
  lift_charge(x, 0xF6F66);
  int ble = (c->zf || c->nf != c->vf);                        /* ble.w loc_F6F72 */
  lift_charge_bcc(x, 0xF6F6A, ble);
  if (!ble)
  {
    lift_w16(x, 0xFFFFC314u, alu_movew(c, W(c->d[0])));       /* move.w d0,($C314).w */
    lift_charge(x, 0xF6F6E);
  }

  /* loc_F6F72 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC30Eu)));     /* move.w ($C30E).w,d0 */
  lift_charge(x, 0xF6F72);
  alu_cmpw(c, lift_r16(x, 0xFFFFC310u), W(c->d[0]));          /* cmp.w ($C310).w,d0 */
  lift_charge(x, 0xF6F76);
  int lt2 = (c->nf != c->vf);                                 /* blt.w loc_F6F82 */
  lift_charge_bcc(x, 0xF6F7A, lt2);
  if (!lt2)
    lift_charge(x, 0xF6F7E);              /* bra.w *+4 — falls straight into loc_F6F82 either way */

  /* loc_F6F82: movem.l d0-a6,-(sp) then movem.l (sp)+,d0-a6 — a no-op
   * pair (every pushed register is popped back to its own value before
   * the rts) preserved for cycle-exactness; every push staged per the
   * dead-stack-transient rule even though the pop immediately restores. */
  {
    uint32_t saved[15] = {
      c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
      c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
    };
    for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
    lift_charge_movem(x, 0xF6F82);

    c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
    c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
    lift_charge_movem(x, 0xF6F86);
  }

  lift_charge(x, 0xF6F8A);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ProjectGoalCreaseColumns (sub_10346; batch: sub_10366)
 *   in:  none (fixed constants)
 *   out: writes two 4-byte projected-column records to $FFFFBEE6/$FFFFBEEA
 *        via Object_ProjectScreenColumn; all of d0-d4/a1-a2 are saved on
 *        entry and restored before return (net no register side effects)
 * Projects world X = +$108 then world X = -$108 (a fixed offset pair,
 * e.g. the goal crease posts) through the shared screen-column projector,
 * fold half-width $88, writing consecutive 4-byte records starting at
 * $FFFFBEE6.
 */
void Object_ProjectGoalCreaseColumns(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_d2 = c->d[2];
  uint32_t saved_d3 = c->d[3], saved_d4 = c->d[4];
  uint32_t saved_a1 = c->a[1], saved_a2 = c->a[2];
  /* movem.l d0-d4/a1-a2,-(sp): a2 pushed first (high addr) ... d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a2);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d4);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d3);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d2);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0x10346);

  c->a[1] = SEW(0xBEE6);                          /* move.w #$BEE6,a1 (sign-extended) */
  lift_charge(x, 0x1034A);
  setw(&c->d[1], alu_movew(c, 0x88));              /* move.w #$88,d1 */
  lift_charge(x, 0x1034E);
  setw(&c->d[4], alu_movew(c, 0x108));             /* move.w #$108,d4 */
  lift_charge(x, 0x10352);

  lift_call(x, 0x10356, 4, Object_ProjectScreenColumn);   /* bsr.w sub_10366 */

  setw(&c->d[4], alu_negw(c, W(c->d[4])));         /* neg.w d4 */
  lift_charge(x, 0x1035A);

  lift_call(x, 0x1035C, 4, Object_ProjectScreenColumn);   /* bsr.w sub_10366 */

  /* movem.l (sp)+,d0-d4/a1-a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x10360);

  lift_charge(x, 0x10364);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TriggerBoardMarkerScript (sub_CB50; DATA XREF from the same
 * script/dispatch table as Object_TriggerStateAdvance)
 *   in: a3 = on-ice object
 * If $62(a3) bit5 is set, bails immediately. If R_UNK_C2EE bit0 is
 * clear, sets $5E(a3)=$14 and tail-jumps to Object_AdvanceStateMod8
 * (its rts returns to our caller). Otherwise: clears bit1 of $62(a3)
 * (and if that bit had been set, also clears $40(a3)); writes a
 * frame-cursor-derived word (($5A(a3)>>2)+1, +3 more unless $76(a3) is
 * nonzero) into the board-marker slot at $FFFFBDA8 (or +4 if bit6 of
 * $62(a3) is set — the away-side slot). If bit3 of $62(a3) is set,
 * bails. Otherwise sets bit1 of $63(a3) and bails if it was already
 * set; else picks a script id in d1 — normally $FEA, but if the crowd
 * noise/hit counter at $FFFFB78A is > $10, rolls Rng_NextScaled(8) and
 * upgrades to $1014 on a nonzero draw — sets bit1 of $63(a3)
 * unconditionally, then tail-jumps into Anim_SetScript(d1) (its rts
 * returns to our caller).
 */
void Object_TriggerBoardMarkerScript(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_btst(c, lift_r8(x, a3 + 0x62), 5);              /* btst #5,$62(a3) */
  lift_charge(x, 0xCB50);
  int bail1 = !c->zf;                                   /* bne.w locret_CBDE */
  lift_charge_bcc(x, 0xCB56, bail1);
  if (bail1)
  {
    lift_charge(x, 0xCBDE);                             /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFC2EE), 0);              /* btst #0,(FFFFC2EE).w */
  lift_charge(x, 0xCB5A);
  int notReady = c->zf;                                  /* beq.w loc_CBD4 */
  lift_charge_bcc(x, 0xCB60, notReady);
  if (notReady)
  {
    lift_w8(x, a3 + 0x5E, alu_moveb(c, 0x14));          /* move.b #$14,$5E(a3) */
    lift_charge(x, 0xCBD4);
    lift_charge(x, 0xCBDA);                              /* bra.w sub_10646 */
    Object_AdvanceStateMod8(x);           /* tail, pops our caller */
    return;
  }

  lift_w8(x, a3 + 0x62, alu_bclr(c, lift_r8(x, a3 + 0x62), 1));  /* bclr #1,$62(a3) */
  lift_charge(x, 0xCB64);
  int wasClear = c->zf;                                  /* beq.w loc_CB72 */
  lift_charge_bcc(x, 0xCB6A, wasClear);
  if (!wasClear)
  {
    lift_w16(x, a3 + 0x40, alu_movew(c, 0));            /* clr.w $40(a3) */
    lift_charge(x, 0xCB6E);
  }

  /* loc_CB72 */
  c->a[0] = 0xFFFFBDA8;                                  /* move.l #$FFFFBDA8,a0 */
  lift_charge(x, 0xCB72);
  alu_btst(c, lift_r8(x, a3 + 0x62), 6);                /* btst #6,$62(a3) */
  lift_charge(x, 0xCB78);
  int away = !c->zf;                                     /* beq.w loc_CB84 */
  lift_charge_bcc(x, 0xCB7E, c->zf);   /* charge uses beq's own taken condition */
  if (away)
  {
    c->a[0] += 4;                                        /* addq.w #4,a0 (no flags) */
    lift_charge(x, 0xCB82);
  }

  /* loc_CB84 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x5A))); /* move.w $5A(a3),d0 */
  lift_charge(x, 0xCB84);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));           /* lsr.w #2,d0 */
  lift_charge(x, 0xCB88);
  setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));           /* addq.w #1,d0 */
  lift_charge(x, 0xCB8A);
  alu_tstb(c, lift_r8(x, a3 + 0x76));                   /* tst.b $76(a3) */
  lift_charge(x, 0xCB8C);
  int skipAdd3 = !c->zf;                                 /* bne.w loc_CB96 */
  lift_charge_bcc(x, 0xCB90, skipAdd3);
  if (!skipAdd3)
  {
    setw(&c->d[0], alu_addw(c, 3, W(c->d[0])));         /* addq.w #3,d0 */
    lift_charge(x, 0xCB94);
  }

  /* loc_CB96 */
  lift_w16(x, c->a[0], alu_movew(c, W(c->d[0])));       /* move.w d0,(a0) */
  lift_charge(x, 0xCB96);
  alu_btst(c, lift_r8(x, a3 + 0x62), 3);                /* btst #3,$62(a3) */
  lift_charge(x, 0xCB98);
  int bail2 = !c->zf;                                    /* bne.w locret_CBDE */
  lift_charge_bcc(x, 0xCB9E, bail2);
  if (bail2)
  {
    lift_charge(x, 0xCBDE);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w8(x, a3 + 0x63, alu_bset(c, lift_r8(x, a3 + 0x63), 1));  /* bset #1,$63(a3) */
  lift_charge(x, 0xCBA2);
  int bail3 = !c->zf;                                     /* bne.w locret_CBDE */
  lift_charge_bcc(x, 0xCBA8, bail3);
  if (bail3)
  {
    lift_charge(x, 0xCBDE);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[1], alu_movew(c, 0xFEA));                  /* move.w #$FEA,d1 */
  lift_charge(x, 0xCBAC);
  alu_cmpw(c, 0x10, lift_r16(x, 0xFFFFB78A));           /* cmpi.w #$10,(FFFFB78A).w */
  lift_charge(x, 0xCBB0);
  int loud = !(c->cf || c->zf);                          /* bls.w loc_CBCA */
  lift_charge_bcc(x, 0xCBB6, !loud);
  if (loud)
  {
    c->d[0] = alu_moveql(c, 8);                         /* moveq #8,d0 — full 32-bit */
    lift_charge(x, 0xCBBA);

    lift_call(x, 0xCBBC, 4, Rng_NextScaled);            /* bsr.w sub_11086 */

    alu_tstw(c, W(c->d[0]));                            /* tst.w d0 */
    lift_charge(x, 0xCBC0);
    int roll0 = c->zf;                                   /* beq.w loc_CBCA */
    lift_charge_bcc(x, 0xCBC2, roll0);
    if (!roll0)
    {
      setw(&c->d[1], alu_movew(c, 0x1014));             /* move.w #$1014,d1 */
      lift_charge(x, 0xCBC6);
    }
  }

  /* loc_CBCA */
  lift_w8(x, a3 + 0x63, alu_bset(c, lift_r8(x, a3 + 0x63), 1));  /* bset #1,$63(a3) */
  lift_charge(x, 0xCBCA);
  lift_charge(x, 0xCBD0);                    /* bra.w sub_1073A */
  Anim_SetScript(x);                         /* tail, pops our caller */
}

/*
 * Team_ComputeClampedTendencyIndex (sub_F70E4; CODE XREF sub_15AA4 x2+)
 *   in:  a3 = on-ice object (bit6 of $62(a3) selects home/away team
 *        block); d3.w = a running index (in/out)
 *   out: d3.w = clamp(d3*5 + table_lookup, 0, 30); d0-d2/a1 preserved
 * Reads a byte from the selected team block's table at +$1A2, indexed
 * by $66(a3)*16, sign-extends it and divides by 3 (signed); adds that
 * quotient to d3*5, then clamps the result to [0, 30].
 */
void Team_ComputeClampedTendencyIndex(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];
  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_d2 = c->d[2], saved_a1 = c->a[1];

  /* movem.l d0-d2/a1,-(sp) */
  lift_w32(x, c->a[7] - 16, saved_d0);
  lift_w32(x, c->a[7] - 12, saved_d1);
  lift_w32(x, c->a[7] - 8, saved_d2);
  lift_w32(x, c->a[7] - 4, saved_a1);
  c->a[7] -= 16;
  lift_charge_movem(x, 0xF70E4);

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFBF14)));  /* move.w (FFFFBF14).w,d1 */
  lift_charge(x, 0xF70E8);
  c->a[1] = 0xFFFFC6CE;                                 /* move.l #$FFFFC6CE,a1 */
  lift_charge(x, 0xF70EC);
  alu_btst(c, lift_r8(x, a3 + 0x62), 6);                /* btst #6,$62(a3) */
  lift_charge(x, 0xF70F2);
  int away = !c->zf;                                     /* beq.w loc_F7102 */
  lift_charge_bcc(x, 0xF70F8, c->zf);
  if (away)
  {
    c->a[1] += 0x364;                                   /* add.l #$364,a1 (ADDA, no flags) */
    lift_charge(x, 0xF70FC);
  }

  /* loc_F7102 */
  setw(&c->d[1], alu_movew(c, 0));                     /* clr.w d1 */
  lift_charge(x, 0xF7102);
  setb(&c->d[1], alu_moveb(c, lift_r8(x, a3 + 0x66)));  /* move.b $66(a3),d1 */
  lift_charge(x, 0xF7104);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));           /* asl.w #4,d1 */
  lift_charge(x, 0xF7108);
  c->a[1] += 0x1A2;                                     /* add.l #$1A2,a1 (ADDA, no flags) */
  lift_charge(x, 0xF710A);
  setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[1]))));  /* move.b (a1,d1.w),d1 */
  lift_charge(x, 0xF7110);
  setw(&c->d[1], alu_extw(c, W(c->d[1])));              /* ext.w d1 */
  lift_charge(x, 0xF7114);
  c->d[1] = alu_extl(c, W(c->d[1]));                    /* ext.l d1 */
  lift_charge(x, 0xF7116);

  lift_charge_divs(x, 0xF7118, 3, c->d[1]);             /* divs.w #3,d1 */
  if (x->declined) return;                              /* can't happen: divisor is a nonzero constant */
  c->d[1] = alu_divs(c, 3, c->d[1]);

  lift_w16(x, c->a[7] - 2, W(c->d[3]));                 /* move.w d3,-(sp) */
  c->a[7] -= 2;
  lift_charge(x, 0xF711C);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));           /* asl.w #2,d3 */
  lift_charge(x, 0xF711E);
  setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[3])));  /* add.w (sp)+,d3 */
  c->a[7] += 2;
  lift_charge(x, 0xF7120);
  setw(&c->d[3], alu_addw(c, W(c->d[1]), W(c->d[3])));  /* add.w d1,d3 */
  lift_charge(x, 0xF7122);

  int neg = c->nf;                                       /* bmi.w loc_F7138 */
  lift_charge_bcc(x, 0xF7124, neg);
  if (!neg)
  {
    alu_cmpw(c, 0x1E, W(c->d[3]));                      /* cmp.w #$1E,d3 */
    lift_charge(x, 0xF7128);
    int lt = (c->nf != c->vf);                           /* blt.w loc_F713A */
    lift_charge_bcc(x, 0xF712C, lt);
    if (!lt)
    {
      setw(&c->d[3], alu_movew(c, 0x1E));               /* move.w #$1E,d3 */
      lift_charge(x, 0xF7130);
      lift_charge(x, 0xF7134);                           /* bra.w loc_F713A */
    }
  }
  else
  {
    /* loc_F7138 */
    setw(&c->d[3], alu_movew(c, 0));                    /* clr.w d3 */
    lift_charge(x, 0xF7138);
  }

  /* loc_F713A */
  setw(&c->d[3], alu_andw(c, 0xFF, W(c->d[3])));        /* and.w #$FF,d3 */
  lift_charge(x, 0xF713A);

  /* movem.l (sp)+,d0-d2/a1 */
  c->d[0] = lift_r32(x, c->a[7]);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0xF713E);

  lift_charge(x, 0xF7142);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ScaledTeamStatWord (sub_15730; calls Team_StatWord)
 *   in:  a0 = on-ice object (moved into a3 as Team_StatWord's input);
 *        d0.w = multiplier (sign-extended from its low byte first)
 *   out: d0.l = Team_StatWord's stat word (or $1000 if bit4 of
 *        $FFFFC2FC is set) * d0's original sign-extended value, shifted
 *        left 4 and rotated (swap) into the high word, then sign-
 *        extended back to a full long. d1/a2/a3 preserved.
 */
void Object_ScaledTeamStatWord(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d1 = c->d[1], saved_a2 = c->a[2], saved_a3 = c->a[3];

  setw(&c->d[0], alu_extw(c, W(c->d[0])));              /* ext.w d0 */
  lift_charge(x, 0x15730);
  lift_w16(x, c->a[7] - 2, alu_movew(c, W(c->d[0])));   /* move.w d0,-(sp) */
  c->a[7] -= 2;
  lift_charge(x, 0x15732);

  /* movem.l d1/a2-a3,-(sp) */
  lift_w32(x, c->a[7] - 12, saved_d1);
  lift_w32(x, c->a[7] - 8, saved_a2);
  lift_w32(x, c->a[7] - 4, saved_a3);
  c->a[7] -= 12;
  lift_charge_movem(x, 0x15734);

  c->a[3] = c->a[0];                                    /* move.l a0,a3 */
  lift_charge(x, 0x15738);

  lift_call(x, 0x1573A, 4, Team_StatWord);              /* bsr.w sub_1575A */

  alu_btst(c, lift_r8(x, 0xFFFFC2FC), 4);               /* btst #4,(FFFFC2FC).w */
  lift_charge(x, 0x1573E);
  int forced = c->zf;                                    /* beq.w loc_1574C */
  lift_charge_bcc(x, 0x15744, forced);
  if (!forced)
  {
    setw(&c->d[0], alu_movew(c, 0x1000));               /* move.w #$1000,d0 */
    lift_charge(x, 0x15748);
  }

  /* loc_1574C */
  c->d[1] = lift_r32(x, c->a[7]);
  c->a[2] = lift_r32(x, c->a[7] + 4);
  c->a[3] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x1574C);

  uint32_t src = lift_r16(x, c->a[7]);                  /* muls.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge_muls(x, 0x15750, src);
  c->d[0] = alu_muls(c, src, W(c->d[0]));

  c->d[0] = alu_asll(c, c->d[0], 4);                    /* asl.l #4,d0 */
  lift_charge(x, 0x15752);
  c->d[0] = alu_swap(c, c->d[0]);                       /* swap d0 */
  lift_charge(x, 0x15754);
  c->d[0] = alu_extl(c, W(c->d[0]));                    /* ext.l d0 */
  lift_charge(x, 0x15756);

  lift_charge(x, 0x15758);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_FormatFixedWidthDecimal (sub_11D3A; writes to the small scratch
 * buffer at $FFFFC008/$FFFFC00A)
 *   in:  d0.w = value to format; d1.w = field width (digit count)
 *   out: word at $FFFFC008 = the formatted string's word-aligned length
 *        (byte-padded with a trailing NUL if odd); ASCII digits (space-
 *        padded on the left, i.e. leading zeros suppressed except the
 *        last digit) written starting at $FFFFC00A. d0-d3/a1 clobbered.
 * First computes d2 = 10^(width-1) via a mulu-by-10 dbf loop, then
 * peels width decimal digits of d0 off high-to-low via a divu-by-10
 * loop, substituting ' ' for suppressed leading zeros.
 */
void Text_FormatFixedWidthDecimal(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_d2 = c->d[2], saved_d3 = c->d[3];

  /* movem.l d0-d3,-(sp) */
  lift_w32(x, c->a[7] - 16, saved_d0);
  lift_w32(x, c->a[7] - 12, saved_d1);
  lift_w32(x, c->a[7] - 8, saved_d2);
  lift_w32(x, c->a[7] - 4, saved_d3);
  c->a[7] -= 16;
  lift_charge_movem(x, 0x11D3A);

  c->a[1] = 0xFFFFC00A;                                 /* move.w #$C00A,a1 */
  lift_charge(x, 0x11D3E);
  c->d[2] = alu_moveql(c, 1);                           /* moveq #1,d2 */
  lift_charge(x, 0x11D42);
  setw(&c->d[1], alu_subw(c, W(c->d[2]), W(c->d[1])));  /* sub.w d2,d1 */
  lift_charge(x, 0x11D44);
  lift_charge(x, 0x11D46);                              /* bra.w loc_11D4E */

  /* build d2 = 10^(width-1) */
  int iters = 0;
  for (;;)
  {
    uint32_t nd1 = W(W(c->d[1]) - 1);                   /* dbf d1,loc_11D4A */
    setw(&c->d[1], nd1);
    int taken = (nd1 != 0xFFFF);
    lift_charge_dbcc(x, 0x11D4E, taken, !taken);
    if (!taken) break;
    if (++iters > 64) { x->declined = 1; return; }      /* guard degenerate width */

    c->d[2] = alu_mulu(c, 0xA, W(c->d[2]));             /* mulu.w #$A,d2 */
    lift_charge_mulu(x, 0x11D4A, 0xA);
  }

  c->d[3] = alu_moveql(c, 0x20);                        /* moveq #$20,d3 (' ') */
  lift_charge(x, 0x11D52);

  /* peel digits high-to-low */
  iters = 0;
  for (;;)
  {
    c->d[0] = alu_extl(c, W(c->d[0]));                  /* ext.l d0 */
    lift_charge(x, 0x11D54);

    lift_charge_divu(x, 0x11D56, W(c->d[2]), c->d[0]);  /* divu.w d2,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, W(c->d[2]), c->d[0]);

    int qNonZero = !c->zf;                              /* bne.w loc_11D6A */
    lift_charge_bcc(x, 0x11D58, qNonZero);

    int emitDigit = qNonZero;
    if (!qNonZero)
    {
      alu_cmpw(c, 1, W(c->d[2]));                       /* cmp.w #1,d2 */
      lift_charge(x, 0x11D5C);
      int lastPlace = c->zf;                             /* beq.w loc_11D6A */
      lift_charge_bcc(x, 0x11D60, lastPlace);
      if (lastPlace)
      {
        emitDigit = 1;
      }
      else
      {
        setw(&c->d[0], alu_movew(c, W(c->d[3])));       /* move.w d3,d0 */
        lift_charge(x, 0x11D64);
        lift_charge(x, 0x11D66);                         /* bra.w loc_11D6E */
      }
    }

    if (emitDigit)
    {
      /* loc_11D6A */
      c->d[3] = alu_moveql(c, 0x30);                    /* moveq #$30,d3 ('0') */
      lift_charge(x, 0x11D6A);
      setw(&c->d[0], alu_addw(c, W(c->d[3]), W(c->d[0])));  /* add.w d3,d0 */
      lift_charge(x, 0x11D6C);
    }

    /* loc_11D6E */
    lift_w8(x, c->a[1], alu_moveb(c, W(c->d[0])));      /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0x11D6E);

    c->d[0] = alu_swap(c, c->d[0]);                     /* swap d0 */
    lift_charge(x, 0x11D70);

    lift_charge_divu(x, 0x11D72, 0xA, c->d[2]);         /* divu.w #$A,d2 */
    if (x->declined) return;
    c->d[2] = alu_divu(c, 0xA, c->d[2]);

    int more = !c->zf;                                   /* bne.s loc_11D54 */
    lift_charge_bcc(x, 0x11D76, more);
    if (!more) break;
    if (++iters > 64) { x->declined = 1; return; }       /* guard degenerate width */
  }

  c->d[0] = alu_movel(c, c->a[1]);                       /* move.l a1,d0 */
  lift_charge(x, 0x11D78);
  setw(&c->d[0], alu_subw(c, 0xC008, W(c->d[0])));       /* sub.w #$C008,d0 */
  lift_charge(x, 0x11D7A);

  alu_btst(c, c->d[0], 0);                               /* btst #0,d0 */
  lift_charge(x, 0x11D7E);
  int odd = !c->zf;                                       /* beq.w loc_11D8A */
  lift_charge_bcc(x, 0x11D82, c->zf);
  if (odd)
  {
    lift_w8(x, c->a[1], alu_moveb(c, 0));                /* clr.b (a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0x11D86);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));          /* addq.w #1,d0 */
    lift_charge(x, 0x11D88);
  }

  /* loc_11D8A */
  c->a[1] = 0xFFFFC008;                                  /* move.w #$C008,a1 */
  lift_charge(x, 0x11D8A);
  lift_w16(x, c->a[1], alu_movew(c, W(c->d[0])));        /* move.w d0,(a1) */
  lift_charge(x, 0x11D8E);

  /* movem.l (sp)+,d0-d3 */
  c->d[0] = lift_r32(x, c->a[7]);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x11D90);

  lift_charge(x, 0x11D94);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_11D06 (called from sub_9316 + others)
 *   in:  d0 = signed value to format
 *   out: a1 = pointer to a length-prefixed ASCII decimal string built
 *        backward into the buffer ending at $FFFFC010 (even-padded with
 *        a leading NUL when the digit count is odd); d0 restored
 * Peels decimal digits low-to-high via divu.w #10, storing ASCII bytes
 * back-to-front, then prefixes the byte count as a word.
 */
void sub_11D06(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, taken;

  c->a[1] = 0xFFFFC010u;                          /* movea.w #$C010,a1 (sign-extends) */
  lift_charge(x, 0x11D06);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);                  /* move.l d0,-(sp) */
  lift_charge(x, 0x11D0A);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);                  /* move.l a1,-(sp) */
  lift_charge(x, 0x11D0C);

  for (i = 0; ; i++)
  {
    if (i > 12) { x->declined = 1; return; }        /* 16-bit dividend: <=5 digits */
    c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
    lift_charge(x, 0x11D0E);
    lift_charge_divu(x, 0x11D10, 10, c->d[0]);      /* divu.w #$A,d0 */
    if (x->declined) return;                        /* zero divisor: can't happen (const 10) */
    c->d[0] = alu_divu(c, 10, c->d[0]);
    c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
    lift_charge(x, 0x11D14);
    setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));  /* add.w #$30,d0 */
    lift_charge(x, 0x11D16);
    c->a[1] -= 1;
    lift_w8(x, c->a[1], alu_moveb(c, W(c->d[0])));  /* move.b d0,-(a1) */
    lift_charge(x, 0x11D1A);
    c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
    lift_charge(x, 0x11D1C);
    alu_tstw(c, W(c->d[0]));                        /* tst.w d0 */
    lift_charge(x, 0x11D1E);
    taken = !c->zf;                                 /* bne.s loc_11D0E */
    lift_charge_bcc(x, 0x11D20, taken);
    if (!taken) break;
  }

  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));     /* move.l (sp)+,d0: original a1 (buffer end) */
  c->a[7] += 4;
  lift_charge(x, 0x11D22);
  c->d[0] = alu_subl(c, c->a[1], c->d[0]);           /* sub.l a1,d0 */
  lift_charge(x, 0x11D24);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));        /* addq.w #2,d0 */
  lift_charge(x, 0x11D26);
  alu_btst(c, W(c->d[0]), 0);                        /* btst #0,d0 */
  lift_charge(x, 0x11D28);
  taken = !c->zf;                                    /* beq.w loc_11D34 (taken = NOT beq) */
  lift_charge_bcc(x, 0x11D2C, c->zf);
  if (taken)
  {
    c->a[1] -= 1;
    lift_w8(x, c->a[1], alu_moveb(c, 0));            /* clr.b -(a1) */
    lift_charge(x, 0x11D30);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));      /* addq.w #1,d0 */
    lift_charge(x, 0x11D32);
  }

  c->a[1] -= 2;
  lift_w16(x, c->a[1], alu_movew(c, W(c->d[0])));    /* move.w d0,-(a1) */
  lift_charge(x, 0x11D34);

  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));      /* move.l (sp)+,d0: restore original d0 */
  c->a[7] += 4;
  lift_charge(x, 0x11D36);

  lift_charge(x, 0x11D38);                            /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_12EF6 (called from sub_E1F4)
 *   in:  a2 = team block; $22(a2) = first roster slot pointer
 *   out: d0 = average, over the 6 slots starting at $22(a2) (each
 *        $80 apart) with $34(slot) > 0, of a2's per-position stat word
 *        at $32(a2, 2*slot's $66 byte); d0 = the raw sum (unaveraged)
 *        if no slot qualified; d1-d3/a0 preserved
 */
void sub_12EF6(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, skip;

  lift_charge_movem(x, 0x12EF6);                 /* movem.l d1-d3/a0,-(sp): a0 pushed first (high addr) ... d1 lands lowest */
  lift_w32(x, c->a[7] - 16, c->d[1]);
  lift_w32(x, c->a[7] - 12, c->d[2]);
  lift_w32(x, c->a[7] - 8, c->d[3]);
  lift_w32(x, c->a[7] - 4, c->a[0]);
  c->a[7] -= 16;

  c->d[0] = alu_movel(c, 0);                     /* clr.l d0 */
  lift_charge(x, 0x12EFA);
  setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
  lift_charge(x, 0x12EFC);
  c->d[2] = alu_moveql(c, 5);                    /* moveq #5,d2 */
  lift_charge(x, 0x12EFE);
  c->a[0] = SEW(lift_r16(x, c->a[2] + 0x22));    /* movea.w $22(a2),a0 */
  lift_charge(x, 0x12F00);

  for (i = 0; ; i++)
  {
    if (i > 6) { x->declined = 1; return; }       /* fixed 6-slot loop */
    alu_tstw(c, lift_r16(x, c->a[0] + 0x34));    /* tst.w $34(a0) */
    lift_charge(x, 0x12F04);
    skip = c->zf || (c->nf != c->vf);            /* ble.w: Z || N!=V */
    lift_charge_bcc(x, 0x12F08, skip);
    if (!skip)
    {
      setw(&c->d[3], alu_movew(c, 0));           /* clr.w d3 */
      lift_charge(x, 0x12F0C);
      setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + 0x66)));  /* move.b $66(a0),d3 */
      lift_charge(x, 0x12F0E);
      setw(&c->d[3], alu_addw(c, W(c->d[3]), W(c->d[3])));       /* add.w d3,d3 */
      lift_charge(x, 0x12F12);
      setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[2] + 0x32 + SEW(c->d[3])), W(c->d[0])));  /* add.w $32(a2,d3.w),d0 */
      lift_charge(x, 0x12F14);
      setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));  /* addq.w #1,d1 */
      lift_charge(x, 0x12F18);
    }

    c->a[0] += 0x80;                             /* adda.w #$80,a0: no CCR */
    lift_charge(x, 0x12F1A);

    setw(&c->d[2], W(c->d[2]) - 1);              /* dbf: counter, no CCR */
    if (W(c->d[2]) != 0xFFFF)
    {
      lift_charge_dbcc(x, 0x12F1E, 1, 0);
      continue;
    }
    lift_charge_dbcc(x, 0x12F1E, 0, 1);
    break;
  }

  alu_tstw(c, W(c->d[1]));                       /* tst.w d1 */
  lift_charge(x, 0x12F22);
  int none = c->zf;                              /* beq.w loc_12F2A */
  lift_charge_bcc(x, 0x12F24, none);
  if (!none)
  {
    lift_charge_divu(x, 0x12F28, W(c->d[1]), c->d[0]);  /* divu.w d1,d0 */
    if (x->declined) return;                     /* zero divisor: can't happen (d1!=0 checked above) */
    c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);
  }

  lift_charge_movem(x, 0x12F2A);                 /* movem.l (sp)+,d1-d3/a0 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->d[3] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge(x, 0x12F2E);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_FECE6 0x000FECE6u   /* ROM: 8 words, sign-extended byte pairs */

/*
 * sub_FECAA (called from sub_F3E4)
 *   out: d0 = a random ROM-table pick (Rng_NextScaled(100) & 7 indexes
 *        the 8-word table at unk_FECE6), overridden to 6 if both camera
 *        offsets ($FFFFBF94/$FFFFBF96) are within their near-field
 *        thresholds; a0 preserved
 */
void sub_FECAA(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge_movem(x, 0xFECAA);                 /* movem.l a0,-(sp) */
  lift_w32(x, c->a[7] - 4, c->a[0]);
  c->a[7] -= 4;

  c->a[0] = TBL_FECE6;                           /* move.l #unk_FECE6,a0 */
  lift_charge(x, 0xFECAE);
  setw(&c->d[0], alu_movew(c, 0x64));            /* move.w #$64,d0 */
  lift_charge(x, 0xFECB4);

  lift_call(x, 0xFECB8, 6, Rng_NextScaled);      /* jsr sub_11086 */

  setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));    /* and.w #7,d0 */
  lift_charge(x, 0xFECBE);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));  /* add.w d0,d0 */
  lift_charge(x, 0xFECC2);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[0]))));  /* move.w (a0,d0.w),d0 */
  lift_charge(x, 0xFECC4);

  alu_cmpw(c, 0xFFC0, lift_r16(x, 0xFFFFBF94u)); /* cmp.w #$FFC0,(BF94).w */
  lift_charge(x, 0xFECC8);
  int taken = !c->zf && (c->nf == c->vf);        /* bgt.w: !Z && N==V */
  lift_charge_bcc(x, 0xFECCE, taken);
  if (!taken)
  {
    alu_cmpw(c, 0xC0, lift_r16(x, 0xFFFFBF96u)); /* cmp.w #$C0,(BF96).w */
    lift_charge(x, 0xFECD2);
    taken = !c->zf && (c->nf == c->vf);          /* bgt.w */
    lift_charge_bcc(x, 0xFECD8, taken);
    if (!taken)
    {
      setw(&c->d[0], alu_movew(c, 6));           /* move.w #6,d0 */
      lift_charge(x, 0xFECDC);
    }
  }

  lift_charge_movem(x, 0xFECE0);                 /* movem.l (sp)+,a0 */
  c->a[0] = lift_r32(x, c->a[7]);
  c->a[7] += 4;
  lift_charge(x, 0xFECE4);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* nullsub_5 (sub_CBE2) — rts-only stub. */
void Nullsub_CBE2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  lift_charge(x, 0xCBE2);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_13396 / sub_13384 (lifted as a pair — sub_13384 falls through into
 * sub_13396 on loop exit)
 *
 * sub_13384: busy-decrements ($FFFFCF2C).w until it goes negative (bails
 *   to the shared far rts) or the pre-decrement value stops matching
 *   ($FFFFCEE6).w; leaves the last-read pre-decrement value in d3.
 * sub_13396: indexes a 16-byte-stride table at $FFFFCE66 by d3 (mulu.w
 *   #$10,d3); if bit1 or bit2 of the entry's +$E byte is set, loops back
 *   into sub_13384; otherwise returns.
 */
void Fn_13384(lift_ctx *x);
void Fn_13396(lift_ctx *x);

void Fn_13384(lift_ctx *x)
{
  rcpu_t *c = x->c;

  for (;;)
  {
    setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFCF2Cu)));  /* move.w (CF2C).w,d3 */
    lift_charge(x, 0x13384);

    int mi = (int16_t)W(c->d[3]) < 0;
    lift_charge_bcc(x, 0x13388, mi);              /* bmi.w locret_15464 */
    if (mi)
    {
      lift_charge(x, 0x15464);                     /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    lift_w16(x, 0xFFFFCF2Cu, alu_subw(c, 1, lift_r16(x, 0xFFFFCF2Cu)));  /* subq.w #1,(CF2C).w */
    lift_charge(x, 0x1338C);

    alu_cmpw(c, lift_r16(x, 0xFFFFCEE6u), W(c->d[3]));  /* cmp.w (CEE6).w,d3 */
    lift_charge(x, 0x13390);

    int eq = c->zf;
    lift_charge_bcc(x, 0x13394, eq);              /* beq.s sub_13384 */
    if (!eq) break;
  }

  Fn_13396(x);
}

void Fn_13396(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x10);                  /* moveq #$10,d0 — full 32-bit */
  lift_charge(x, 0x13396);

  c->d[0] = alu_mulu(c, W(c->d[3]), c->d[0]);     /* mulu.w d3,d0 */
  lift_charge_mulu(x, 0x13398, W(c->d[3]));

  c->a[0] = (uint32_t)(int16_t)0xCE66;            /* move.w #$CE66,a0 */
  lift_charge(x, 0x1339A);

  c->a[0] += SEW(W(c->d[0]));                     /* add.w d0,a0 */
  lift_charge(x, 0x1339E);

  alu_btst(c, lift_r8(x, c->a[0] + 0xE), 1);     /* btst #1,$E(a0) */
  lift_charge(x, 0x133A0);

  int taken = !c->zf;
  lift_charge_bcc(x, 0x133A6, taken);             /* bne.s sub_13384 */
  if (taken) { Fn_13384(x); return; }

  alu_btst(c, lift_r8(x, c->a[0] + 0xE), 2);     /* btst #2,$E(a0) */
  lift_charge(x, 0x133A8);

  taken = !c->zf;
  lift_charge_bcc(x, 0x133AE, taken);             /* bne.s sub_13384 */
  if (taken) { Fn_13384(x); return; }

  lift_charge(x, 0x133B0);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_13E26
 *   Computes a base delay d0 = (($28 - $73(a3)) >> 1) * $D, doubled if
 *   bit3 of $62(a3) is set; then halved if the object is within a
 *   $FFD8..$28 window of both ($FFFFB74A/B75E) on both axes ((a3)/+$14).
 *   Rolls Rng_NextScaled(d0) (result discarded); if bit1 of $64(a2) is
 *   set, rolls again with a second scale (3/4/7/8) chosen from the
 *   |($14(a2)) - $108| distance (also discarded). Returns d0 = a status
 *   code (0, or $7F if away-team/some-flag gating passes).
 */
void sub_13E26(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0x28));             /* move.w #$28,d0 */
  lift_charge(x, 0x13E26);

  setb(&c->d[0], alu_subb(c, lift_r8(x, c->a[3] + 0x73), c->d[0] & 0xFF));  /* sub.b $73(a3),d0 */
  lift_charge(x, 0x13E2A);

  setb(&c->d[0], alu_lsrb(c, c->d[0] & 0xFF, 1)); /* lsr.b #1,d0 */
  lift_charge(x, 0x13E2E);

  c->d[0] = alu_mulu(c, 0xD, W(c->d[0]));         /* mulu.w #$D,d0 */
  lift_charge_mulu(x, 0x13E30, 0xD);

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 3);    /* btst #3,$62(a3) */
  lift_charge(x, 0x13E34);

  int taken = c->zf;
  lift_charge_bcc(x, 0x13E3A, taken);             /* beq.w loc_13E40 */
  if (!taken)
  {
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));   /* asl.w #1,d0 */
    lift_charge(x, 0x13E3E);
  }

  /* loc_13E40 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3])));  /* move.w (a3),d1 */
  lift_charge(x, 0x13E40);
  setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB74Au), W(c->d[1])));  /* sub.w (B74A).w,d1 */
  lift_charge(x, 0x13E42);

  alu_cmpw(c, 0x28, W(c->d[1]));                  /* cmpn.w #$28,d1 */
  lift_charge(x, 0x13E46);
  taken = !c->zf && (c->nf == c->vf);             /* bgt.w loc_13E70 */
  lift_charge_bcc(x, 0x13E4A, taken);
  int go_halve = 0;
  if (!taken)
  {
    alu_cmpw(c, 0xFFD8, W(c->d[1]));              /* cmpn.w #$FFD8,d1 */
    lift_charge(x, 0x13E4E);
    taken = (c->nf != c->vf);                     /* blt.w loc_13E70 */
    lift_charge_bcc(x, 0x13E52, taken);
    if (!taken)
    {
      setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d1 */
      lift_charge(x, 0x13E56);
      setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB75Eu), W(c->d[1])));  /* sub.w (B75E).w,d1 */
      lift_charge(x, 0x13E5A);

      alu_cmpw(c, 0x28, W(c->d[1]));              /* cmpn.w #$28,d1 */
      lift_charge(x, 0x13E5E);
      taken = !c->zf && (c->nf == c->vf);         /* bgt.w loc_13E70 */
      lift_charge_bcc(x, 0x13E62, taken);
      if (!taken)
      {
        alu_cmpw(c, 0xFFD8, W(c->d[1]));          /* cmpn.w #$FFD8,d1 */
        lift_charge(x, 0x13E66);
        taken = (c->nf != c->vf);                 /* blt.w loc_13E70 */
        lift_charge_bcc(x, 0x13E6A, taken);
        if (!taken) go_halve = 1;
      }
    }
  }
  if (go_halve)
  {
    setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));   /* asr.w #1,d0 */
    lift_charge(x, 0x13E6E);
  }

  /* loc_13E70 */
  lift_call(x, 0x13E70, 4, Rng_NextScaled);       /* bsr.w sub_11086 */

  alu_btst(c, lift_r8(x, c->a[2] + 0x64), 1);    /* btst #1,$64(a2) */
  lift_charge(x, 0x13E74);

  taken = c->zf;
  lift_charge_bcc(x, 0x13E7A, taken);             /* beq.w loc_13EBA */
  if (!taken)
  {
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[2] + 0x14)));  /* move.w $14(a2),d1 */
    lift_charge(x, 0x13E7E);

    int pl = !((int16_t)W(c->d[1]) < 0);
    lift_charge_bcc(x, 0x13E82, !pl);             /* bpl.w loc_13E88 */
    if (!pl)
    {
      setw(&c->d[1], alu_negw(c, W(c->d[1])));    /* neg.w d1 */
      lift_charge(x, 0x13E86);
    }

    /* loc_13E88 */
    setw(&c->d[1], alu_subw(c, 0x108, W(c->d[1])));  /* sub.w #$108,d1 */
    lift_charge(x, 0x13E88);
    setw(&c->d[1], alu_negw(c, W(c->d[1])));      /* neg.w d1 */
    lift_charge(x, 0x13E8C);

    setw(&c->d[0], alu_movew(c, 8));              /* move.w #8,d0 */
    lift_charge(x, 0x13E8E);

    alu_cmpw(c, 0x75, W(c->d[1]));                /* cmpn.w #$75,d1 */
    lift_charge(x, 0x13E92);
    taken = !c->zf && (c->nf == c->vf);           /* bgt.w loc_13EB6 */
    lift_charge_bcc(x, 0x13E96, taken);
    if (!taken)
    {
      setw(&c->d[0], alu_movew(c, 7));            /* move.w #7,d0 */
      lift_charge(x, 0x13E9A);

      alu_cmpw(c, 0x3A, W(c->d[1]));              /* cmpn.w #$3A,d1 */
      lift_charge(x, 0x13E9E);
      taken = !c->zf && (c->nf == c->vf);         /* bgt.w loc_13EB6 */
      lift_charge_bcc(x, 0x13EA2, taken);
      if (!taken)
      {
        setw(&c->d[0], alu_movew(c, 4));          /* move.w #4,d0 */
        lift_charge(x, 0x13EA6);

        alu_cmpw(c, 0x2C, W(c->d[1]));            /* cmpn.w #$2C,d1 */
        lift_charge(x, 0x13EAA);
        taken = !c->zf && (c->nf == c->vf);       /* bgt.w loc_13EB6 */
        lift_charge_bcc(x, 0x13EAE, taken);
        if (!taken)
        {
          setw(&c->d[0], alu_movew(c, 3));        /* move.w #3,d0 */
          lift_charge(x, 0x13EB2);
        }
      }
    }

    /* loc_13EB6 */
    lift_call(x, 0x13EB6, 4, Rng_NextScaled);     /* bsr.w sub_11086 */
  }

  /* loc_13EBA */
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);    /* btst #6,$62(a3) */
  lift_charge(x, 0x13EBA);

  taken = !c->zf;
  lift_charge_bcc(x, 0x13EC0, taken);             /* bne.w loc_13ED2 */
  if (!taken)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC300u), 2);     /* btst #2,(C300).w */
    lift_charge(x, 0x13EC4);

    taken = !c->zf;
    lift_charge_bcc(x, 0x13ECA, taken);           /* bne.w loc_13EDC */
    if (!taken)
    {
      lift_charge_bcc(x, 0x13ECE, 1);             /* bra.w locret_13EE0 */
      lift_charge(x, 0x13EE0);                     /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  else
  {
    /* loc_13ED2 */
    alu_btst(c, lift_r8(x, 0xFFFFC300u), 1);     /* btst #1,(C300).w */
    lift_charge(x, 0x13ED2);

    taken = c->zf;
    lift_charge_bcc(x, 0x13ED8, taken);           /* beq.w locret_13EE0 */
    if (taken)
    {
      lift_charge(x, 0x13EE0);                     /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_13EDC */
  setw(&c->d[0], alu_movew(c, 0x7F));             /* move.w #$7F,d0 */
  lift_charge(x, 0x13EDC);

  /* locret_13EE0 */
  lift_charge(x, 0x13EE0);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_AFB6 — snapshots the camera Y/X ($FFFFBD18/$FFFFBD1C) into a
 * secondary pair ($FFFFBF90/$FFFFBF8E), then sets bit6 of the rink-flip
 * byte ($FFFFC2EC).
 */
void Camera_LatchScrollAndFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFBF90u, alu_movew(c, lift_r16(x, 0xFFFFBD18u)));  /* move.w (BD18).w,(BF90).w */
  lift_charge(x, 0xAFB6);

  lift_w16(x, 0xFFFFBF8Eu, alu_movew(c, lift_r16(x, 0xFFFFBD1Cu)));  /* move.w (BD1C).w,(BF8E).w */
  lift_charge(x, 0xAFBC);

  lift_w8(x, 0xFFFFC2ECu, alu_bset(c, lift_r8(x, 0xFFFFC2ECu), 6));  /* bset #6,(C2EC).w */
  lift_charge(x, 0xAFC2);

  lift_charge(x, 0xAFC8);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_BA04 (d0 in: base table offset; d0 out: sign-extended table byte)
 *   Adjusts d0's base by comparing $388(a2) (or -$340(a2) when a2 !=
 *   Team_HomeBlock) against $24(a2): +$15 once if nonzero, +$15 again
 *   if that difference is also negative; adds $16(a2) three times; then
 *   indexes a small ROM byte table at $BA4A and sign-extends the pick.
 */
void sub_BA04(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge_movem(x, 0xBA04);                   /* movem.l d1-d2,-(sp) */
  lift_w32(x, c->a[7] - 8, c->d[1]);
  lift_w32(x, c->a[7] - 4, c->d[2]);
  c->a[7] -= 8;

  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0x388)));  /* move.w $388(a2),d2 */
  lift_charge(x, 0xBA08);

  alu_cmpl(c, 0xFFFFC6CEu, c->a[2]);              /* cmp.w #$C6CE,a2 */
  lift_charge(x, 0xBA0C);
  int taken = c->zf;
  lift_charge_bcc(x, 0xBA10, taken);              /* beq.w loc_BA18 */
  if (!taken)
  {
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] - 0x340)));  /* move.w -$340(a2),d2 */
    lift_charge(x, 0xBA14);
  }

  /* loc_BA18 */
  setw(&c->d[2], alu_subw(c, lift_r16(x, c->a[2] + 0x24), W(c->d[2])));  /* sub.w $24(a2),d2 */
  lift_charge(x, 0xBA18);

  taken = c->zf;
  lift_charge_bcc(x, 0xBA1C, taken);              /* beq.w loc_BA2E */
  if (!taken)
  {
    setw(&c->d[0], alu_addw(c, 0x15, W(c->d[0])));  /* add.w #$15,d0 */
    lift_charge(x, 0xBA20);

    alu_tstw(c, W(c->d[2]));                      /* tst.w d2 */
    lift_charge(x, 0xBA24);
    taken = c->nf;                                 /* bmi.w loc_BA2E */
    lift_charge_bcc(x, 0xBA26, taken);
    if (!taken)
    {
      setw(&c->d[0], alu_addw(c, 0x15, W(c->d[0])));  /* add.w #$15,d0 */
      lift_charge(x, 0xBA2A);
    }
  }

  /* loc_BA2E */
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[2] + 0x16)));  /* move.w $16(a2),d1 */
  lift_charge(x, 0xBA2E);

  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));  /* add.w d1,d0 */
  lift_charge(x, 0xBA32);
  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));  /* add.w d1,d0 */
  lift_charge(x, 0xBA34);
  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));  /* add.w d1,d0 */
  lift_charge(x, 0xBA36);

  c->a[0] = 0xBA48u;                               /* lea off_BA48(pc),a0 */
  lift_charge(x, 0xBA38);

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(W(c->d[0])))));  /* move.b (a0,d0.w),d0 */
  lift_charge(x, 0xBA3C);

  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0xBA40);

  lift_charge_movem(x, 0xBA42);                   /* movem.l (sp)+,d1-d2 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge(x, 0xBA46);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}



/*
 * sub_13848 / sub_13862 (siblings — copy a 1470-word ROM table into/out
 * of RAM at $FF0000, in opposite directions, plus one extra boundary
 * word via $FFFFC472)
 *
 * sub_13848: RAM($FF0000+) = ROM($C2EA+) for 1470 words, then
 *   RAM(next word) = ($FFFFC472).w.
 */
void sub_13848(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0x5BD));            /* move.w #$5BD,d0 */
  lift_charge(x, 0x13848);

  c->a[1] = (uint32_t)(int16_t)0xC2EA;            /* move.w #$C2EA,a1 */
  lift_charge(x, 0x1384C);

  c->a[2] = 0xFFFF0000u;                          /* move.l #$FFFF0000,a2 */
  lift_charge(x, 0x13850);

  for (;;)
  {
    lift_w16(x, c->a[2], alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1)+,(a2)+ */
    c->a[1] += 2;
    c->a[2] += 2;
    lift_charge(x, 0x13856);

    uint16_t dec = (uint16_t)(W(c->d[0]) - 1);
    setw(&c->d[0], dec);
    int expired = (dec == 0xFFFFu);
    lift_charge_dbcc(x, 0x13858, !expired, expired);  /* dbf d0,loc_13856 */
    if (expired) break;
  }

  lift_w16(x, c->a[2], alu_movew(c, lift_r16(x, 0xFFFFC472u)));  /* move.w (C472).w,(a2)+ */
  c->a[2] += 2;
  lift_charge(x, 0x1385C);

  lift_charge(x, 0x13860);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_13862: mirror of sub_13848 — RAM($C2EA+) = ROM($FF0000+) [wait:
 * literal roles swapped per operand order below] for 1470 words, then
 * ($FFFFC472).w = RAM(next word).
 */
void sub_13862(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0x5BD));            /* move.w #$5BD,d0 */
  lift_charge(x, 0x13862);

  c->a[2] = (uint32_t)(int16_t)0xC2EA;            /* move.w #$C2EA,a2 */
  lift_charge(x, 0x13866);

  c->a[1] = 0xFFFF0000u;                          /* move.l #$FFFF0000,a1 */
  lift_charge(x, 0x1386A);

  for (;;)
  {
    lift_w16(x, c->a[2], alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1)+,(a2)+ */
    c->a[1] += 2;
    c->a[2] += 2;
    lift_charge(x, 0x13870);

    uint16_t dec = (uint16_t)(W(c->d[0]) - 1);
    setw(&c->d[0], dec);
    int expired = (dec == 0xFFFFu);
    lift_charge_dbcc(x, 0x13872, !expired, expired);  /* dbf d0,loc_13870 */
    if (expired) break;
  }

  lift_w16(x, 0xFFFFC472u, alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1)+,(C472).w */
  c->a[1] += 2;
  lift_charge(x, 0x13876);

  lift_charge(x, 0x1387A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_14A94 (called from sub_12002, sub_14620)
 *   out: d0 = ((($FFFFC466).w << 16) >> 2) low word ORed with
 *        ($FFFFC46C).w, minus ($FFFFC468).w.
 */
void sub_14A94(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC466u)));  /* move.w (C466).w,d0 */
  lift_charge(x, 0x14A94);

  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x14A98);

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x14A9A);

  c->d[0] = alu_lsrl(c, c->d[0], 2);               /* lsr.l #2,d0 */
  lift_charge(x, 0x14A9C);

  setw(&c->d[0], alu_orw(c, lift_r16(x, 0xFFFFC46Cu), W(c->d[0])));  /* or.w (C46C).w,d0 */
  lift_charge(x, 0x14A9E);

  setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFC468u), W(c->d[0])));  /* sub.w (C468).w,d0 */
  lift_charge(x, 0x14AA2);

  lift_charge(x, 0x14AA6);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_13276(lift_ctx *x);   /* math.c */

/*
 * sub_1323E (batch: Calc_HalvingAccumulator (sub_1828A), sub_13276)
 *   Rolls d1 = Calc_HalvingAccumulator's d1 output, stores it to
 *   $FFFFCF2C, then walks a $FFFFCE66-based 16-byte-stride table
 *   downward from index d1 to 0: unless the index equals
 *   ($FFFFCEE6).w, clears bit1 of each entry's +$E byte and, if bit2 is
 *   also clear, calls sub_13276. d1 == -1 (the Calc_HalvingAccumulator
 *   degenerate output) would wrap the dbf into a ~65536-iteration loop
 *   never intended by real play — declined in that case.
 */
void sub_1323E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x1323E, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */

  if (W(c->d[1]) == 0xFFFFu) { x->declined = 1; return; }

  lift_w16(x, 0xFFFFCF2Cu, alu_movew(c, W(c->d[1])));  /* move.w d1,(CF2C).w */
  lift_charge(x, 0x13242);

  c->a[0] = (uint32_t)(int16_t)0xCE66;            /* move.w #$CE66,a0 */
  lift_charge(x, 0x13246);

  c->d[0] = alu_moveql(c, 0x10);                  /* moveq #$10,d0 — full 32-bit */
  lift_charge(x, 0x1324A);

  c->d[0] = alu_mulu(c, W(c->d[1]), c->d[0]);     /* mulu.w d1,d0 */
  lift_charge_mulu(x, 0x1324C, W(c->d[1]));

  c->a[0] += SEW(W(c->d[0]));                     /* add.w d0,a0 */
  lift_charge(x, 0x1324E);

  int guard = 0;
  for (;;)
  {
    if (++guard > 70000) { x->declined = 1; return; }  /* dbf can't loop more than 65536x */

    alu_cmpw(c, lift_r16(x, 0xFFFFCEE6u), W(c->d[1]));  /* cmp.w (CEE6).w,d1 */
    lift_charge(x, 0x13250);
    int taken = c->zf;
    lift_charge_bcc(x, 0x13254, taken);           /* beq.w loc_1326C */
    if (!taken)
    {
      lift_w8(x, c->a[0] + 0xE, alu_bclr(c, lift_r8(x, c->a[0] + 0xE), 1));  /* bclr #1,$E(a0) */
      lift_charge(x, 0x13258);

      alu_btst(c, lift_r8(x, c->a[0] + 0xE), 2);  /* btst #2,$E(a0) */
      lift_charge(x, 0x1325E);
      taken = !c->zf;
      lift_charge_bcc(x, 0x13264, taken);         /* bne.w loc_1326C */
      if (!taken)
      {
        lift_call(x, 0x13268, 4, sub_13276);      /* bsr.w sub_13276 */
      }
    }

    /* loc_1326C */
    c->a[0] -= 0x10;                               /* suba.w #$10,a0 */
    lift_charge(x, 0x1326C);

    uint16_t dec = (uint16_t)(W(c->d[1]) - 1);
    setw(&c->d[1], dec);
    int expired = (dec == 0xFFFFu);
    lift_charge_dbcc(x, 0x13270, !expired, expired);  /* dbf d1,loc_13250 */
    if (expired) break;
  }

  lift_charge(x, 0x13274);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F693E (dispatched from the handler table at ROM $18E08)
 *   in: a3 = on-ice object slot
 *   Two behaviours gated by $62(a3) bits 1/3:
 *   - Activation (bit1 of $62 was set, bit3 of $64 was clear): consumes
 *     bit1, sets $64 bit3, resets the $BF76/$BF6E/$BEE0 tracking state,
 *     caches $52(a3) to $BF6C, then (unless $62 bit3 is set or $DED8 is
 *     negative) reruns the slot-selection pass — sub_C0DA when $DED8 != 0
 *     (with the $C322/$C32A pair swapped to the $C326/$C32E "alt" pair
 *     around the call when $DEDA == 1), sub_C0BC otherwise (same swap
 *     dance on $C320/$C328 vs $C324/$C32C when $DEDA == 0). Finally runs
 *     Object_ComputeApproachGate + Anim_SetScript(d1), zeroes the frame
 *     cursor $5A(a3), and sets $62 bit5 / $63 bit1. All registers
 *     saved/restored (movem d0-a6).
 *   - Steady-state (otherwise): if $BF76 bit0 is clear, tests whether the
 *     puck-ish point ($B74A/$B75E) is within +/-$3C of the object on
 *     either axis with the object heading toward it (eor sign vs
 *     $B772/$B774); heading-away exits via a $C2FE bit7 clear + event
 *     requeue (sub_FEFF0). Otherwise, while $B7AA is negative and $C2EA
 *     bit0 clear, runs the approach step: distance/speed quotient
 *     (two divu's — the first can genuinely overflow, V/N set, d0 kept)
 *     compared against a threshold shrinking with the frame cursor
 *     $5A(a3); close -> mirror the cursor ($18 - $5A), far -> accumulate
 *     d7 into $BF6E; plus the $BF76 bit1/bit2 latch dance that freezes
 *     the countdown $5C(a3) once the cursor passes $18. Exits either
 *     re-queue the event (sub_FEFF0) or, when $63 bit1 is set with $BF76
 *     bit0, clear $62 bit5 and return. d2 is clobbered on the deep path
 *     (never saved); d0/d1 come back word-sign-extended when the movem.w
 *     band-check frame pops. Exact role TBD (possession-handoff vs
 *     puck-heading gate per the queue triage); behaviour preserved
 *     bit-for-bit.
 */
void sub_F693E(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int z, t;

  {                                                /* bclr #1,$62(a3) */
    uint32_t b = lift_r8(x, c->a[3] + 0x62);
    lift_w8(x, c->a[3] + 0x62, alu_bclr(c, b, 1));
    lift_charge(x, 0xF693E);
  }
  z = c->zf;                                       /* beq.w loc_F6A3C */
  lift_charge_bcc(x, 0xF6944, z);
  if (z) goto L_F6A3C;

  {                                                /* bset #3,$64(a3) */
    uint32_t b = lift_r8(x, c->a[3] + 0x64);
    lift_w8(x, c->a[3] + 0x64, alu_bset(c, b, 3));
    lift_charge(x, 0xF6948);
  }
  t = !c->zf;                                      /* bne.w loc_F6A3C */
  lift_charge_bcc(x, 0xF694E, t);
  if (t) goto L_F6A3C;

  /* ---- activation path ---- */
  {
    uint32_t b = lift_r8(x, 0xFFFFBF76u);          /* bclr #0,(BF76).w */
    lift_w8(x, 0xFFFFBF76u, alu_bclr(c, b, 0));
    lift_charge(x, 0xF6952);
    b = lift_r8(x, 0xFFFFC2FEu);                   /* bclr #5,(C2FE).w */
    lift_w8(x, 0xFFFFC2FEu, alu_bclr(c, b, 5));
    lift_charge(x, 0xF6958);
  }
  alu_movew(c, 0); lift_w16(x, 0xFFFFBF76u, 0);    /* clr.w (BF76).w */
  lift_charge(x, 0xF695E);
  alu_movew(c, 0); lift_w16(x, 0xFFFFBF6Eu, 0);    /* clr.w (BF6E).w */
  lift_charge(x, 0xF6962);
  lift_w8(x, 0xFFFFBEE0u, 0xFF);                   /* st (BEE0).w */
  lift_charge(x, 0xF6966);

  {
    /* movem.l d0-a6,-(sp): 15 longs, ascending d0..d7,a0..a6 */
    uint32_t sp0 = c->a[7];
    int i;
    for (i = 0; i < 8; i++) lift_w32(x, sp0 - 60 + 4 * i, c->d[i]);
    for (i = 0; i < 7; i++) lift_w32(x, sp0 - 28 + 4 * i, c->a[i]);
    c->a[7] = sp0 - 60;
    lift_charge_movem(x, 0xF696A);

    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x52))); /* move.w $52(a3),d0 */
    lift_charge(x, 0xF696E);
    alu_movew(c, W(c->d[0]));                      /* move.w d0,(BF6C).w */
    lift_w16(x, 0xFFFFBF6Cu, W(c->d[0]));
    lift_charge(x, 0xF6972);

    alu_btst(c, lift_r8(x, c->a[3] + 0x62), 3);    /* btst #3,$62(a3) */
    lift_charge(x, 0xF6976);
    t = !c->zf;                                    /* bne.w loc_F6A16 */
    lift_charge_bcc(x, 0xF697C, t);
    if (t) goto L_F6A16;

    alu_tstw(c, lift_r16(x, 0xFFFFDED8u));         /* tst.w (DED8).w */
    lift_charge(x, 0xF6980);
    t = c->nf;                                     /* bmi.w loc_F6A16 */
    lift_charge_bcc(x, 0xF6984, t);
    if (t) goto L_F6A16;

    z = c->zf;                                     /* beq.w loc_F69D2 */
    lift_charge_bcc(x, 0xF6988, z);
    if (z) goto L_F69D2;

    alu_cmpw(c, 1, lift_r16(x, 0xFFFFDEDAu));      /* cmp.w #1,(DEDA).w */
    lift_charge(x, 0xF698C);
    t = !c->zf;                                    /* bne.w loc_F69C8 */
    lift_charge_bcc(x, 0xF6992, t);
    if (t) goto L_F69C8;

    {                                              /* alt-pair swap around sub_C0DA */
      uint32_t v = lift_r16(x, 0xFFFFC32Au);       /* move.w (C32A).w,-(sp) */
      alu_movew(c, v);
      c->a[7] -= 2; lift_w16(x, c->a[7], v);
      lift_charge(x, 0xF6996);
      v = lift_r16(x, 0xFFFFC322u);                /* move.w (C322).w,-(sp) */
      alu_movew(c, v);
      c->a[7] -= 2; lift_w16(x, c->a[7], v);
      lift_charge(x, 0xF699A);
      v = lift_r16(x, 0xFFFFC32Eu);                /* move.w (C32E).w,(C32A).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC32Au, v);
      lift_charge(x, 0xF699E);
      v = lift_r16(x, 0xFFFFC326u);                /* move.w (C326).w,(C322).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC322u, v);
      lift_charge(x, 0xF69A4);

      lift_call(x, 0xF69AA, 6, Object_UpdateSelectedSlot_B); /* jsr sub_C0DA */

      v = lift_r16(x, 0xFFFFC322u);                /* move.w (C322).w,(C326).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC326u, v);
      lift_charge(x, 0xF69B0);
      v = lift_r16(x, 0xFFFFC32Au);                /* move.w (C32A).w,(C32E).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC32Eu, v);
      lift_charge(x, 0xF69B6);
      v = lift_r16(x, c->a[7]); c->a[7] += 2;      /* move.w (sp)+,(C322).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC322u, v);
      lift_charge(x, 0xF69BC);
      v = lift_r16(x, c->a[7]); c->a[7] += 2;      /* move.w (sp)+,(C32A).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC32Au, v);
      lift_charge(x, 0xF69C0);
    }
    lift_charge(x, 0xF69C4);                       /* bra.w loc_F6A16 */
    goto L_F6A16;

L_F69C8:
    lift_call(x, 0xF69C8, 6, Object_UpdateSelectedSlot_B); /* jsr sub_C0DA */
    lift_charge(x, 0xF69CE);                       /* bra.w loc_F6A16 */
    goto L_F6A16;

L_F69D2:
    alu_tstw(c, lift_r16(x, 0xFFFFDEDAu));         /* tst.w (DEDA).w */
    lift_charge(x, 0xF69D2);
    t = !c->zf;                                    /* bne.w loc_F6A0C */
    lift_charge_bcc(x, 0xF69D6, t);
    if (t) goto L_F6A0C;

    {                                              /* alt-pair swap around sub_C0BC */
      uint32_t v = lift_r16(x, 0xFFFFC328u);       /* move.w (C328).w,-(sp) */
      alu_movew(c, v);
      c->a[7] -= 2; lift_w16(x, c->a[7], v);
      lift_charge(x, 0xF69DA);
      v = lift_r16(x, 0xFFFFC320u);                /* move.w (C320).w,-(sp) */
      alu_movew(c, v);
      c->a[7] -= 2; lift_w16(x, c->a[7], v);
      lift_charge(x, 0xF69DE);
      v = lift_r16(x, 0xFFFFC32Cu);                /* move.w (C32C).w,(C328).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC328u, v);
      lift_charge(x, 0xF69E2);
      v = lift_r16(x, 0xFFFFC324u);                /* move.w (C324).w,(C320).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC320u, v);
      lift_charge(x, 0xF69E8);

      lift_call(x, 0xF69EE, 6, Object_UpdateSelectedSlot_A); /* jsr sub_C0BC */

      v = lift_r16(x, 0xFFFFC320u);                /* move.w (C320).w,(C324).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC324u, v);
      lift_charge(x, 0xF69F4);
      v = lift_r16(x, 0xFFFFC328u);                /* move.w (C328).w,(C32C).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC32Cu, v);
      lift_charge(x, 0xF69FA);
      v = lift_r16(x, c->a[7]); c->a[7] += 2;      /* move.w (sp)+,(C320).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC320u, v);
      lift_charge(x, 0xF6A00);
      v = lift_r16(x, c->a[7]); c->a[7] += 2;      /* move.w (sp)+,(C328).w */
      alu_movew(c, v); lift_w16(x, 0xFFFFC328u, v);
      lift_charge(x, 0xF6A04);
    }
    lift_charge(x, 0xF6A08);                       /* bra.w loc_F6A16 */
    goto L_F6A16;

L_F6A0C:
    lift_call(x, 0xF6A0C, 6, Object_UpdateSelectedSlot_A); /* jsr sub_C0BC */
    lift_charge(x, 0xF6A12);                       /* bra.w *+4 (falls to loc_F6A16) */

L_F6A16:
    alu_movew(c, W(c->d[0]));                      /* move.w d0,-(sp) */
    c->a[7] -= 2; lift_w16(x, c->a[7], W(c->d[0]));
    lift_charge(x, 0xF6A16);

    lift_call(x, 0xF6A18, 4, Object_ComputeApproachGate); /* bsr.w sub_F6C0A */
    if (x->declined) return;

    alu_movew(c, 0); lift_w16(x, c->a[3] + 0x5A, 0); /* clr.w $5A(a3) */
    lift_charge(x, 0xF6A1C);

    lift_call(x, 0xF6A20, 6, Anim_SetScript);      /* jsr sub_1073A */
    if (x->declined) return;

    {
      uint32_t b = lift_r8(x, c->a[3] + 0x62);     /* bset #5,$62(a3) */
      lift_w8(x, c->a[3] + 0x62, alu_bset(c, b, 5));
      lift_charge(x, 0xF6A26);
      b = lift_r8(x, c->a[3] + 0x63);              /* bset #1,$63(a3) */
      lift_w8(x, c->a[3] + 0x63, alu_bset(c, b, 1));
      lift_charge(x, 0xF6A2C);
    }
    {
      uint32_t v = lift_r16(x, c->a[7]); c->a[7] += 2; /* move.w (sp)+,d0 */
      setw(&c->d[0], alu_movew(c, v));
      lift_charge(x, 0xF6A32);
    }
    /* movem.l (sp)+,d0-a6 — restores the entry values staged above */
    for (i = 0; i < 8; i++) { c->d[i] = lift_r32(x, c->a[7]); c->a[7] += 4; }
    for (i = 0; i < 7; i++) { c->a[i] = lift_r32(x, c->a[7]); c->a[7] += 4; }
    lift_charge_movem(x, 0xF6A34);
    lift_charge(x, 0xF6A38);                       /* bra.w locret_F6C08 */
    goto L_ret;
  }

  /* ---- steady-state path ---- */
L_F6A3C:
  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 0);         /* btst #0,(BF76).w */
  lift_charge(x, 0xF6A3C);
  t = !c->zf;                                      /* bne.w loc_F6A96 */
  lift_charge_bcc(x, 0xF6A42, t);
  if (t) goto L_F6A96;

  /* movem.w d0-d1,-(sp): two words, ascending d0.w, d1.w */
  c->a[7] -= 4;
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xF6A46);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB74Au))); /* move.w (B74A).w,d0 */
  lift_charge(x, 0xF6A4A);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[3]), W(c->d[0]))); /* sub.w (a3),d0 */
  lift_charge(x, 0xF6A4E);
  alu_cmpw(c, 0x3C, W(c->d[0]));                   /* cmp.w #$3C,d0 */
  lift_charge(x, 0xF6A50);
  t = !c->zf && (c->nf == c->vf);                  /* bgt.w loc_F6A60 */
  lift_charge_bcc(x, 0xF6A54, t);
  if (!t)
  {
    alu_cmpw(c, 0xFFC4, W(c->d[0]));               /* cmp.w #-$3C,d0 */
    lift_charge(x, 0xF6A58);
    t = !c->zf && (c->nf == c->vf);                /* bgt.w loc_F6A72 */
    lift_charge_bcc(x, 0xF6A5C, t);
    if (t) goto L_F6A72;
  }
  /* loc_F6A60 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB772u))); /* move.w (B772).w,d1 */
  lift_charge(x, 0xF6A60);
  setw(&c->d[0], alu_eorw(c, W(c->d[1]), W(c->d[0])));    /* eor.w d1,d0 */
  lift_charge(x, 0xF6A64);
  t = c->nf;                                       /* bmi.w loc_F6A72 */
  lift_charge_bcc(x, 0xF6A66, t);
  if (t) goto L_F6A72;

L_F6A6A:
  /* movem.w (sp)+,d0-d1: word restore sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7]));
  c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
  c->a[7] += 4;
  lift_charge_movem(x, 0xF6A6A);
  lift_charge(x, 0xF6A6E);                         /* bra.w loc_F6A9E */
  goto L_F6A9E;

L_F6A72:
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB75Eu))); /* move.w (B75E).w,d0 */
  lift_charge(x, 0xF6A72);
  setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[0]))); /* sub.w $14(a3),d0 */
  lift_charge(x, 0xF6A76);
  alu_cmpw(c, 0x3C, W(c->d[0]));                   /* cmp.w #$3C,d0 */
  lift_charge(x, 0xF6A7A);
  t = !c->zf && (c->nf == c->vf);                  /* bgt.w loc_F6A8A */
  lift_charge_bcc(x, 0xF6A7E, t);
  if (!t)
  {
    alu_cmpw(c, 0xFFC4, W(c->d[0]));               /* cmp.w #-$3C,d0 */
    lift_charge(x, 0xF6A82);
    t = !c->zf && (c->nf == c->vf);                /* bgt.w loc_F6A92 */
    lift_charge_bcc(x, 0xF6A86, t);
    if (t) goto L_F6A92;
  }
  /* loc_F6A8A */
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB774u))); /* move.w (B774).w,d1 */
  lift_charge(x, 0xF6A8A);
  setw(&c->d[0], alu_eorw(c, W(c->d[1]), W(c->d[0])));    /* eor.w d1,d0 */
  lift_charge(x, 0xF6A8E);
  t = !c->nf;                                      /* bpl.s loc_F6A6A */
  lift_charge_bcc(x, 0xF6A90, t);
  if (t) goto L_F6A6A;

L_F6A92:
  /* movem.w (sp)+,d0-d1: sign-extends */
  c->d[0] = SEW(lift_r16(x, c->a[7]));
  c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
  c->a[7] += 4;
  lift_charge_movem(x, 0xF6A92);

L_F6A96:
  alu_tstw(c, lift_r16(x, 0xFFFFB7AAu));           /* tst.w (B7AA).w */
  lift_charge(x, 0xF6A96);
  t = c->nf;                                       /* bmi.w loc_F6AA8 */
  lift_charge_bcc(x, 0xF6A9A, t);
  if (t) goto L_F6AA8;

L_F6A9E:
  {
    uint32_t b = lift_r8(x, 0xFFFFC2FEu);          /* bclr #7,(C2FE).w */
    lift_w8(x, 0xFFFFC2FEu, alu_bclr(c, b, 7));
    lift_charge(x, 0xF6A9E);
  }
  lift_charge(x, 0xF6AA4);                         /* bra.w loc_F6C02 */
  goto L_F6C02;

L_F6AA8:
  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 0);         /* btst #0,(C2EA).w */
  lift_charge(x, 0xF6AA8);
  t = !c->zf;                                      /* bne.w loc_F6C02 */
  lift_charge_bcc(x, 0xF6AAE, t);
  if (t) goto L_F6C02;

  /* movem.l d0-d1,-(sp): ascending d0, d1 */
  lift_w32(x, c->a[7] - 8, c->d[0]);
  lift_w32(x, c->a[7] - 4, c->d[1]);
  c->a[7] -= 8;
  lift_charge_movem(x, 0xF6AB2);

  alu_cmpw(c, 0x10, lift_r16(x, c->a[3] + 0x5A));  /* cmp.w #$10,$5A(a3) */
  lift_charge(x, 0xF6AB6);
  t = (c->nf == c->vf);                            /* bge.w loc_F6B58 */
  lift_charge_bcc(x, 0xF6ABC, t);
  if (t) goto L_F6B58;

  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 2);         /* btst #2,(BF76).w */
  lift_charge(x, 0xF6AC0);
  t = !c->zf;                                      /* bne.w loc_F6B58 */
  lift_charge_bcc(x, 0xF6AC6, t);
  if (t) goto L_F6B58;

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3]))); /* move.w (a3),d0 */
  lift_charge(x, 0xF6ACA);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB74Au))); /* move.w (B74A).w,d1 */
  lift_charge(x, 0xF6ACC);
  setw(&c->d[0], alu_subw(c, W(c->d[1]), W(c->d[0])));    /* sub.w d1,d0 */
  lift_charge(x, 0xF6AD0);
  t = !c->nf;                                      /* bpl.w loc_F6AD8 */
  lift_charge_bcc(x, 0xF6AD2, t);
  if (!t)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));       /* neg.w d0 */
    lift_charge(x, 0xF6AD6);
  }
  /* loc_F6AD8 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x14))); /* move.w $14(a3),d1 */
  lift_charge(x, 0xF6AD8);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFB75Eu)));    /* move.w (B75E).w,d2 */
  lift_charge(x, 0xF6ADC);
  setw(&c->d[1], alu_subw(c, W(c->d[2]), W(c->d[1])));       /* sub.w d2,d1 */
  lift_charge(x, 0xF6AE0);
  t = !c->nf;                                      /* bpl.w loc_F6AE8 */
  lift_charge_bcc(x, 0xF6AE2, t);
  if (!t)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));       /* neg.w d1 */
    lift_charge(x, 0xF6AE6);
  }
  /* loc_F6AE8 */
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFB772u)));    /* move.w (B772).w,d2 */
  lift_charge(x, 0xF6AE8);
  z = c->zf;                                       /* beq.w loc_F6AF6 */
  lift_charge_bcc(x, 0xF6AEC, z);
  if (!z)
  {
    alu_cmpw(c, W(c->d[0]), W(c->d[1]));           /* cmp.w d0,d1 */
    lift_charge(x, 0xF6AF0);
    t = c->zf || (c->nf != c->vf);                 /* ble.w loc_F6AFC */
    lift_charge_bcc(x, 0xF6AF2, t);
    if (t) goto L_F6AFC;
  }
  /* loc_F6AF6 */
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFB774u)));    /* move.w (B774).w,d2 */
  lift_charge(x, 0xF6AF6);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));        /* move.w d1,d0 */
  lift_charge(x, 0xF6AFA);

L_F6AFC:
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xF6AFC);
  c->d[0] = alu_andl(c, 0xFFFF0000u, c->d[0]);     /* and.l #$FFFF0000,d0 */
  lift_charge(x, 0xF6AFE);
  alu_tstw(c, W(c->d[2]));                         /* tst.w d2 */
  lift_charge(x, 0xF6B04);
  t = !c->nf;                                      /* bpl.w loc_F6B0C */
  lift_charge_bcc(x, 0xF6B06, t);
  if (!t)
  {
    setw(&c->d[2], alu_negw(c, W(c->d[2])));       /* neg.w d2 */
    lift_charge(x, 0xF6B0A);
  }
  /* loc_F6B0C */
  setw(&c->d[1], alu_movew(c, 0x11));              /* move.w #$11,d1 */
  lift_charge(x, 0xF6B0C);
  alu_tstw(c, lift_r16(x, 0xFFFFD06Eu));           /* tst.w (D06E).w */
  lift_charge(x, 0xF6B10);
  z = c->zf;                                       /* beq.w loc_F6B1C */
  lift_charge_bcc(x, 0xF6B14, z);
  if (!z)
  {
    setw(&c->d[1], alu_movew(c, 0x16));            /* move.w #$16,d1 */
    lift_charge(x, 0xF6B18);
  }
  /* loc_F6B1C */
  alu_tstw(c, W(c->d[2]));                         /* tst.w d2 */
  lift_charge(x, 0xF6B1C);
  t = !c->zf;                                      /* bne.w loc_F6B26 */
  lift_charge_bcc(x, 0xF6B1E, t);
  if (!t)
  {
    setw(&c->d[2], alu_movew(c, 1));               /* move.w #1,d2 */
    lift_charge(x, 0xF6B22);
  }
  /* loc_F6B26: divu.w d2,d0 — the quotient (dist<<16)/d2 genuinely
   * overflows whenever dist >= d2; alu_divu keeps d0 and sets V/N */
  lift_charge_divu(x, 0xF6B26, W(c->d[2]), c->d[0]);
  if (x->declined) return;
  c->d[0] = alu_divu(c, W(c->d[2]), c->d[0]);
  c->d[0] = alu_andl(c, 0xFFFF, c->d[0]);          /* and.l #$FFFF,d0 */
  lift_charge(x, 0xF6B28);
  lift_charge_divu(x, 0xF6B2E, W(c->d[1]), c->d[0]); /* divu.w d1,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);

  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 0x5A))); /* move.w $5A(a3),d2 */
  lift_charge(x, 0xF6B30);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 2));      /* lsr.w #2,d2 */
  lift_charge(x, 0xF6B34);
  setw(&c->d[2], alu_subw(c, 6, W(c->d[2])));      /* subq.w #6,d2 */
  lift_charge(x, 0xF6B36);
  setw(&c->d[2], alu_negw(c, W(c->d[2])));         /* neg.w d2 */
  lift_charge(x, 0xF6B38);
  setw(&c->d[2], alu_aslw(c, W(c->d[2]), 2));      /* asl.w #2,d2 */
  lift_charge(x, 0xF6B3A);
  alu_cmpw(c, W(c->d[2]), W(c->d[0]));             /* cmp.w d2,d0 */
  lift_charge(x, 0xF6B3C);
  t = !c->zf && (c->nf == c->vf);                  /* bgt.w loc_F6B50 */
  lift_charge_bcc(x, 0xF6B3E, t);
  if (!t)
  {
    uint32_t v = alu_negw(c, lift_r16(x, c->a[3] + 0x5A)); /* neg.w $5A(a3) */
    lift_w16(x, c->a[3] + 0x5A, v);
    lift_charge(x, 0xF6B42);
    v = alu_addw(c, 0x18, lift_r16(x, c->a[3] + 0x5A));    /* add.w #$18,$5A(a3) */
    lift_w16(x, c->a[3] + 0x5A, v);
    lift_charge(x, 0xF6B46);
    lift_charge(x, 0xF6B4C);                       /* bra.w loc_F6BC8 */
    goto L_F6BC8;
  }
  /* loc_F6B50 */
  {
    uint32_t v = alu_addw(c, W(c->d[7]), lift_r16(x, 0xFFFFBF6Eu)); /* add.w d7,(BF6E).w */
    lift_w16(x, 0xFFFFBF6Eu, v);
    lift_charge(x, 0xF6B50);
  }
  lift_charge(x, 0xF6B54);                         /* bra.w loc_F6B7E */
  goto L_F6B7E;

L_F6B58:
  {
    uint32_t b = lift_r8(x, 0xFFFFBF76u);          /* bset #2,(BF76).w */
    lift_w8(x, 0xFFFFBF76u, alu_bset(c, b, 2));
    lift_charge(x, 0xF6B58);
  }
  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 1);         /* btst #1,(BF76).w */
  lift_charge(x, 0xF6B5E);
  t = !c->zf;                                      /* bne.w loc_F6B7E */
  lift_charge_bcc(x, 0xF6B64, t);
  if (!t)
  {
    alu_cmpw(c, 0x18, lift_r16(x, c->a[3] + 0x5A)); /* cmp.w #$18,$5A(a3) */
    lift_charge(x, 0xF6B68);
    t = !c->zf;                                    /* bne.w loc_F6B7E */
    lift_charge_bcc(x, 0xF6B6E, t);
    if (!t)
    {
      uint32_t v = alu_addw(c, 0x30, lift_r16(x, c->a[3] + 0x5C)); /* add.w #$30,$5C(a3) */
      lift_w16(x, c->a[3] + 0x5C, v);
      lift_charge(x, 0xF6B72);
      uint32_t b = lift_r8(x, 0xFFFFBF76u);        /* bset #1,(BF76).w */
      lift_w8(x, 0xFFFFBF76u, alu_bset(c, b, 1));
      lift_charge(x, 0xF6B78);
    }
  }

L_F6B7E:
  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 1);         /* btst #1,(BF76).w */
  lift_charge(x, 0xF6B7E);
  z = c->zf;                                       /* beq.w loc_F6BA4 */
  lift_charge_bcc(x, 0xF6B84, z);
  if (!z)
  {
    alu_cmpw(c, 0x18, lift_r16(x, c->a[3] + 0x5A)); /* cmp.w #$18,$5A(a3) */
    lift_charge(x, 0xF6B88);
    t = c->zf || (c->nf != c->vf);                 /* ble.w loc_F6BA4 */
    lift_charge_bcc(x, 0xF6B8E, t);
    if (!t)
    {
      alu_btst(c, lift_r8(x, 0xFFFFBF76u), 0);     /* btst #0,(BF76).w */
      lift_charge(x, 0xF6B92);
      t = !c->zf;                                  /* bne.w loc_F6BA4 */
      lift_charge_bcc(x, 0xF6B98, t);
      if (!t)
      {
        /* movem.l (sp)+,d0-d1 */
        c->d[0] = lift_r32(x, c->a[7]);
        c->d[1] = lift_r32(x, c->a[7] + 4);
        c->a[7] += 8;
        lift_charge_movem(x, 0xF6B9C);
        lift_charge(x, 0xF6BA0);                   /* bra.w loc_F6C02 */
        goto L_F6C02;
      }
    }
  }
  /* loc_F6BA4 */
  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 0);         /* btst #0,(BF76).w */
  lift_charge(x, 0xF6BA4);
  z = c->zf;                                       /* beq.w loc_F6BC8 */
  lift_charge_bcc(x, 0xF6BAA, z);
  if (!z)
  {
    alu_cmpw(c, 0x18, lift_r16(x, c->a[3] + 0x5A)); /* cmp.w #$18,$5A(a3) */
    lift_charge(x, 0xF6BAE);
    t = !c->zf;                                    /* bne.w loc_F6BC8 */
    lift_charge_bcc(x, 0xF6BB4, t);
    if (!t)
    {
      alu_cmpw(c, 1, lift_r16(x, c->a[3] + 0x5C)); /* cmp.w #1,$5C(a3) */
      lift_charge(x, 0xF6BB8);
      t = c->zf || (c->nf != c->vf);               /* ble.w loc_F6BC8 */
      lift_charge_bcc(x, 0xF6BBE, t);
      if (!t)
      {
        alu_movew(c, 1);                           /* move.w #1,$5C(a3) */
        lift_w16(x, c->a[3] + 0x5C, 1);
        lift_charge(x, 0xF6BC2);
      }
    }
  }

L_F6BC8:
  alu_btst(c, lift_r8(x, c->a[3] + 0x63), 1);      /* btst #1,$63(a3) */
  lift_charge(x, 0xF6BC8);
  t = !c->zf;                                      /* bne.w loc_F6BDA */
  lift_charge_bcc(x, 0xF6BCE, t);
  if (!t)
  {
    /* movem.l (sp)+,d0-d1 */
    c->d[0] = lift_r32(x, c->a[7]);
    c->d[1] = lift_r32(x, c->a[7] + 4);
    c->a[7] += 8;
    lift_charge_movem(x, 0xF6BD2);
    lift_charge(x, 0xF6BD6);                       /* bra.w loc_F6BF8 */
    goto L_F6BF8;
  }
  /* loc_F6BDA */
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x5A))); /* move.w $5A(a3),d0 */
  lift_charge(x, 0xF6BDA);
  /* movem.l (sp)+,d0-d1 (immediately overwrites the d0 just loaded) */
  c->d[0] = lift_r32(x, c->a[7]);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0xF6BDE);
  alu_btst(c, lift_r8(x, 0xFFFFBF76u), 0);         /* btst #0,(BF76).w */
  lift_charge(x, 0xF6BE2);
  z = c->zf;                                       /* beq.w loc_F6BF6 */
  lift_charge_bcc(x, 0xF6BE8, z);
  if (!z)
  {
    uint32_t b = lift_r8(x, c->a[3] + 0x62);       /* bclr #5,$62(a3) */
    lift_w8(x, c->a[3] + 0x62, alu_bclr(c, b, 5));
    lift_charge(x, 0xF6BEC);
    lift_charge(x, 0xF6BF2);                       /* bra.w loc_F6BF8 */
    goto L_F6BF8;
  }
  lift_charge(x, 0xF6BF6);                         /* nop */

L_F6BF8:
  alu_btst(c, lift_r8(x, c->a[3] + 0x63), 1);      /* btst #1,$63(a3) */
  lift_charge(x, 0xF6BF8);
  t = !c->zf;                                      /* bne.w locret_F6C08 */
  lift_charge_bcc(x, 0xF6BFE, t);
  if (t) goto L_ret;

L_F6C02:
  lift_call(x, 0xF6C02, 6, Object_ResetAndQueueEvent); /* jsr sub_FEFF0 */
  if (x->declined) return;

L_ret:
  lift_charge(x, 0xF6C08);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_CCE8 (bsr'd from ROM:CC60; ALSO entered by fall-through — the
 * bsr.w sub_1073A at $CCE4 returns to $CCE8, which falls into this
 * routine's body)
 *   in: a3 = object. Rolls Rng_NextScaled($78) into $40(a3).
 */
void sub_CCE8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x78);                   /* moveq #$78,d0 */
  lift_charge(x, 0xCCE8);
  lift_call(x, 0xCCEA, 4, Rng_NextScaled);         /* bsr.w sub_11086 */
  alu_movew(c, W(c->d[0]));                        /* move.w d0,$40(a3) */
  lift_w16(x, c->a[3] + 0x40, W(c->d[0]));
  lift_charge(x, 0xCCEE);

  lift_charge(x, 0xCCF2);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}


/*
 * sub_158F2 (bsr'd from sub_15832, twice)
 *   in:  d0.w = candidate roster index (1-based), a2 = team block,
 *        a4 = $FFFFCF14 pick table, d4 = destination slot index
 *   out: Z=0 (accepted: index written to (a4,d4.w), d1 = index) or
 *        Z=1 (rejected: d1 = 0). d0 clobbered (2*(index-1), or the
 *        dbeq scan counter). Rejects a candidate whose $66(a2) status
 *        word is > 0 or -3/-4 (penalized/unavailable), or one already
 *        present in the 6-byte pick table.
 */
void sub_158F2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int rej;

  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0x158F2);
  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));      /* subq.w #1,d0 */
  lift_charge(x, 0x158F4);
  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));  /* add.w d0,d0 */
  lift_charge(x, 0x158F6);
  alu_tstw(c, lift_r16(x, c->a[2] + 0x66 + SW(c->d[0])));  /* tst.w $66(a2,d0.w) */
  lift_charge(x, 0x158F8);
  rej = (!c->zf && (c->nf == c->vf));              /* bgt.w loc_15928 */
  lift_charge_bcc(x, 0x158FC, rej);
  if (!rej)
  {
    alu_cmpw(c, 0xFFFD, lift_r16(x, c->a[2] + 0x66 + SW(c->d[0])));
    lift_charge(x, 0x15900);
    rej = c->zf;
    lift_charge_bcc(x, 0x15906, rej);              /* beq.w loc_15928 */
  }
  if (!rej)
  {
    alu_cmpw(c, 0xFFFC, lift_r16(x, c->a[2] + 0x66 + SW(c->d[0])));
    lift_charge(x, 0x1590A);
    rej = c->zf;
    lift_charge_bcc(x, 0x15910, rej);              /* beq.w loc_15928 */
  }
  if (!rej)
  {
    c->d[0] = alu_moveql(c, 5);                    /* moveq #5,d0 */
    lift_charge(x, 0x15914);
    for (;;)
    {
      /* loc_15916: cmp.b (a4,d0.w),d1 */
      alu_cmpb(c, lift_r8(x, c->a[4] + SW(c->d[0])), c->d[1]);
      lift_charge(x, 0x15916);
      int cond = c->zf, taken = 0, expd = 0;       /* dbeq d0,loc_15916 */
      if (!cond)
      {
        expd = (W(c->d[0]) == 0);
        setw(&c->d[0], W(c->d[0] - 1));
        taken = !expd;
      }
      lift_charge_dbcc(x, 0x1591A, taken, expd);
      if (!taken) break;
    }
    rej = c->zf;                                   /* beq.w loc_15928 */
    lift_charge_bcc(x, 0x1591E, rej);
    if (!rej)
    {
      lift_w8(x, c->a[4] + SW(c->d[4]), alu_moveb(c, c->d[1]));  /* move.b d1,(a4,d4.w) */
      lift_charge(x, 0x15922);
      lift_charge(x, 0x15926);                     /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  /* loc_15928 */
  setw(&c->d[1], alu_movew(c, 0));                 /* clr.w d1 */
  lift_charge(x, 0x15928);
  lift_charge(x, 0x1592A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_15832 (bsr'd from sub_15788; also a direct profiled entry)
 *   in: a2 = team block
 * Rebuilds the 6-entry line pick table at $FFFFCF14 (+6: the raw roster
 * indices): clears it, then for each of the $24(a2) positions takes the
 * default index from the ROM table at $19286 (offset 1 byte when
 * $26(a2) is negative) mapped through the per-team roster bytes at
 * $16A(a2) + 8*$16(a2); an empty pick (mapped byte 0) is provisionally
 * replaced with $26(a2)+1. Second pass replaces any pick whose $66(a2)
 * status word is > 0 or -3/-4 (penalized): candidates come from the ROM
 * fallback chain at $1921C (indexed by the raw pick), each validated by
 * sub_158F2; if the chain runs dry (negative terminator byte), every
 * dressed skater (count from sub_9F9A) is tried highest-first.
 */
void sub_15832(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[4] = 0xFFFFCF14;                            /* movea.w #$CF14,a4 */
  lift_charge(x, 0x15832);
  lift_w32(x, c->a[4], alu_movel(c, 0));           /* clr.l (a4) */
  lift_charge(x, 0x15836);
  lift_w16(x, c->a[4] + 4, alu_movew(c, 0));       /* clr.w 4(a4) */
  lift_charge(x, 0x15838);
  c->a[0] = 0x19286;                               /* movea.l #off_19286,a0 */
  lift_charge(x, 0x1583C);
  alu_tstw(c, lift_r16(x, c->a[2] + 0x26));        /* tst.w $26(a2) */
  lift_charge(x, 0x15842);
  t = !c->nf;
  lift_charge_bcc(x, 0x15846, t);                  /* bpl.w loc_1584C */
  if (!t)
  {
    c->a[0] += 1;                                  /* addq.w #1,a0 */
    lift_charge(x, 0x1584A);
  }
  /* loc_1584C */
  c->a[1] = c->a[2] + 0x16A;                       /* lea $16A(a2),a1 */
  lift_charge(x, 0x1584C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x16)));  /* move.w $16(a2),d0 */
  lift_charge(x, 0x15850);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));      /* asl.w #3,d0 */
  lift_charge(x, 0x15854);
  c->a[1] += SEW(c->d[0]);                         /* adda.w d0,a1 */
  lift_charge(x, 0x15856);
  setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[2] + 0x24)));  /* move.w $24(a2),d4 */
  lift_charge(x, 0x15858);
  if (W(c->d[4]) > 0x40) { x->declined = 1; return; }  /* dbf-wrap guard */
  lift_charge(x, 0x1585C);                         /* bra.w loc_1587E */
  for (;;)
  {
    /* loc_1587E: dbf d4,loc_15860 */
    int expired = (W(c->d[4]) == 0);
    setw(&c->d[4], W(c->d[4] - 1));
    lift_charge_dbcc(x, 0x1587E, !expired, expired);
    if (expired) break;
    /* loc_15860 */
    setw(&c->d[5], alu_movew(c, 0));               /* clr.w d5 */
    lift_charge(x, 0x15860);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[4]))));  /* move.b (a0,d4.w),d5 */
    lift_charge(x, 0x15862);
    {                                              /* move.b (a1,d5.w),(a4,d4.w) */
      uint32_t v = alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[5])));
      lift_w8(x, c->a[4] + SW(c->d[4]), v);
    }
    lift_charge(x, 0x15866);
    lift_w8(x, c->a[4] + SW(c->d[4]) + 6, alu_moveb(c, c->d[5]));  /* move.b d5,6(a4,d4.w) */
    lift_charge(x, 0x1586C);
    t = !c->zf;
    lift_charge_bcc(x, 0x15870, t);                /* bne.w loc_1587E */
    if (!t)
    {
      c->d[3] = alu_moveql(c, 1);                  /* moveq #1,d3 */
      lift_charge(x, 0x15874);
      setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[2] + 0x26), W(c->d[3])));  /* add.w $26(a2),d3 */
      lift_charge(x, 0x15876);
      lift_w8(x, c->a[4] + SW(c->d[4]), alu_moveb(c, c->d[3]));  /* move.b d3,(a4,d4.w) */
      lift_charge(x, 0x1587A);
    }
  }
  c->d[4] = alu_moveql(c, 5);                      /* moveq #5,d4 */
  lift_charge(x, 0x15882);
  for (;;)
  {
    /* loc_15884 */
    setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]))));  /* move.b (a4,d4.w),d3 */
    lift_charge(x, 0x15884);
    t = c->zf;
    lift_charge_bcc(x, 0x15888, t);                /* beq.w loc_158D6 */
    if (!t)
    {
      setw(&c->d[3], alu_extw(c, c->d[3]));        /* ext.w d3 */
      lift_charge(x, 0x1588C);
      setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));  /* subq.w #1,d3 */
      lift_charge(x, 0x1588E);
      setw(&c->d[3], alu_addw(c, W(c->d[3]), W(c->d[3])));  /* add.w d3,d3 */
      lift_charge(x, 0x15890);
      alu_cmpw(c, 0xFFFD, lift_r16(x, c->a[2] + 0x66 + SW(c->d[3])));  /* cmp.w #$FFFD,$66(a2,d3.w) */
      lift_charge(x, 0x15892);
      int hit = c->zf;
      lift_charge_bcc(x, 0x15898, hit);            /* beq.w loc_158AE */
      if (!hit)
      {
        alu_cmpw(c, 0xFFFC, lift_r16(x, c->a[2] + 0x66 + SW(c->d[3])));
        lift_charge(x, 0x1589C);
        hit = c->zf;
        lift_charge_bcc(x, 0x158A2, hit);          /* beq.w loc_158AE */
      }
      int skip = 0;
      if (!hit)
      {
        alu_tstw(c, lift_r16(x, c->a[2] + 0x66 + SW(c->d[3])));  /* tst.w $66(a2,d3.w) */
        lift_charge(x, 0x158A6);
        skip = (c->zf || (c->nf != c->vf));        /* ble.w loc_158D6 */
        lift_charge_bcc(x, 0x158AA, skip);
      }
      if (!skip)
      {
        /* loc_158AE */
        c->a[1] = c->a[2] + 0x16A;                 /* lea $16A(a2),a1 */
        lift_charge(x, 0x158AE);
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]) + 6)));  /* move.b 6(a4,d4.w),d0 */
        lift_charge(x, 0x158B2);
        setw(&c->d[0], alu_extw(c, c->d[0]));      /* ext.w d0 */
        lift_charge(x, 0x158B6);
        setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));  /* asl.w #1,d0 */
        lift_charge(x, 0x158B8);
        c->a[0] = 0x1921C;                         /* movea.l #word_1921C,a0 */
        lift_charge(x, 0x158BA);
        c->a[0] += SEW(lift_r16(x, c->a[0] + SW(c->d[0])));  /* adda.w (a0,d0.w),a0 */
        lift_charge(x, 0x158C0);
        int guard = 0, viaChainEnd = 0;
        for (;;)
        {
          /* loc_158C4 */
          if (++guard > 1024) { x->declined = 1; return; }
          setw(&c->d[0], alu_movew(c, 0));         /* clr.w d0 */
          lift_charge(x, 0x158C4);
          setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,d0 */
          c->a[0] += 1;
          lift_charge(x, 0x158C6);
          t = c->nf;
          lift_charge_bcc(x, 0x158C8, t);          /* bmi.w loc_158DC */
          if (t) { viaChainEnd = 1; break; }
          setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[0]))));  /* move.b (a1,d0.w),d0 */
          lift_charge(x, 0x158CC);
          lift_call(x, 0x158D0, 4, sub_158F2);     /* bsr.w sub_158F2 */
          if (x->declined) return;
          t = c->zf;
          lift_charge_bcc(x, 0x158D4, t);          /* beq.s loc_158C4 */
          if (!t) break;
        }
        if (viaChainEnd)
        {
          /* loc_158DC */
          lift_call(x, 0x158DC, 6, Roster_CountLineEntries);      /* jsr sub_9F9A */
          if (x->declined) return;
          setw(&c->d[3], alu_movew(c, W(c->d[0])));  /* move.w d0,d3 */
          lift_charge(x, 0x158E2);
          for (;;)
          {
            /* loc_158E4 */
            setw(&c->d[0], alu_movew(c, W(c->d[3])));  /* move.w d3,d0 */
            lift_charge(x, 0x158E4);
            setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));  /* subq.w #1,d3 */
            lift_charge(x, 0x158E6);
            t = c->nf;
            lift_charge_bcc(x, 0x158E8, t);        /* bmi.s loc_158D6 */
            if (t) break;
            lift_call(x, 0x158EA, 4, sub_158F2);   /* bsr.w sub_158F2 */
            if (x->declined) return;
            t = c->zf;
            lift_charge_bcc(x, 0x158EE, t);        /* beq.s loc_158E4 */
            if (!t)
            {
              lift_charge(x, 0x158F0);             /* bra.s loc_158D6 */
              break;
            }
          }
        }
      }
    }
    /* loc_158D6: dbf d4,loc_15884 */
    int expired = (W(c->d[4]) == 0);
    setw(&c->d[4], W(c->d[4] - 1));
    lift_charge_dbcc(x, 0x158D6, !expired, expired);
    if (expired) break;
  }
  lift_charge(x, 0x158DA);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_15788 (called from ROM:9E76, sub_BB06+2A and others — the
 * line-change applier)
 *   in: a2 = team block ($22(a2) = first on-ice player slot address)
 * Marks all 6 on-ice player slots' line-assignment bytes $60/$61 stale
 * ($FF), rebuilds the 6-entry pick table at $FFFFCF14 via sub_15832,
 * then applies it in two passes: first each pending pick is matched to
 * the on-ice slot whose roster byte $66(a3) equals the pick (writing
 * the raw index to $60 and the pick-1 to $61, clearing the table
 * entry); leftovers then go to any slot whose $61 is still stale
 * (negative) with a non-negative $34 word — searching on past a
 * negative $34 and remembering the last stale slot in a0. All of
 * d0-d5/a0-a4 movem-saved/restored.
 */
void sub_15788(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 44;                                   /* movem.l d0-d5/a0-a4,-(sp) */
  {
    int i;
    for (i = 0; i < 6; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
    for (i = 0; i < 5; i++) lift_w32(x, c->a[7] + 24 + 4 * i, c->a[i]);
  }
  lift_charge_movem(x, 0x15788);
  c->a[3] = SEW(lift_r16(x, c->a[2] + 0x22));      /* movea.w $22(a2),a3 */
  lift_charge(x, 0x1578C);
  c->d[4] = alu_moveql(c, 5);                      /* moveq #5,d4 */
  lift_charge(x, 0x15790);
  for (;;)
  {
    /* loc_15792 */
    lift_w8(x, c->a[3] + 0x60, 0xFF);              /* st $60(a3): no flags */
    lift_charge(x, 0x15792);
    lift_w8(x, c->a[3] + 0x61, 0xFF);              /* st $61(a3) */
    lift_charge(x, 0x15796);
    c->a[3] += 0x80;                               /* adda.w #$80,a3 */
    lift_charge(x, 0x1579A);
    int expired = (W(c->d[4]) == 0);
    setw(&c->d[4], W(c->d[4] - 1));
    lift_charge_dbcc(x, 0x1579E, !expired, expired);
    if (expired) break;
  }
  lift_call(x, 0x157A2, 4, sub_15832);             /* bsr.w sub_15832 */
  if (x->declined) return;
  c->d[4] = alu_moveql(c, 5);                      /* moveq #5,d4 */
  lift_charge(x, 0x157A6);
  c->a[4] = 0xFFFFCF14;                            /* movea.w #$CF14,a4 */
  lift_charge(x, 0x157A8);
  for (;;)
  {
    /* loc_157AC */
    setw(&c->d[5], alu_movew(c, 0));               /* clr.w d5 */
    lift_charge(x, 0x157AC);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]))));  /* move.b (a4,d4.w),d5 */
    lift_charge(x, 0x157AE);
    t = c->zf;
    lift_charge_bcc(x, 0x157B2, t);                /* beq.w loc_157E0 */
    if (!t)
    {
      setw(&c->d[5], alu_subw(c, 1, W(c->d[5])));  /* subq.w #1,d5 */
      lift_charge(x, 0x157B6);
      c->d[3] = alu_moveql(c, 5);                  /* moveq #5,d3 */
      lift_charge(x, 0x157B8);
      c->a[3] = SEW(lift_r16(x, c->a[2] + 0x22));  /* movea.w $22(a2),a3 */
      lift_charge(x, 0x157BA);
      c->a[3] -= 0x80;                             /* suba.w #$80,a3 */
      lift_charge(x, 0x157BE);
      for (;;)
      {
        /* loc_157C2 */
        c->a[3] += 0x80;                           /* adda.w #$80,a3 */
        lift_charge(x, 0x157C2);
        alu_cmpb(c, lift_r8(x, c->a[3] + 0x66), c->d[5]);  /* cmp.b $66(a3),d5 */
        lift_charge(x, 0x157C6);
        int cond = c->zf, taken = 0, expd = 0;     /* dbeq d3,loc_157C2 */
        if (!cond)
        {
          expd = (W(c->d[3]) == 0);
          setw(&c->d[3], W(c->d[3] - 1));
          taken = !expd;
        }
        lift_charge_dbcc(x, 0x157CA, taken, expd);
        if (!taken) break;
      }
      t = !c->zf;
      lift_charge_bcc(x, 0x157CE, t);              /* bne.w loc_157E0 */
      if (!t)
      {
        {                                          /* move.b 6(a4,d4.w),$60(a3) */
          uint32_t v = alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]) + 6));
          lift_w8(x, c->a[3] + 0x60, v);
        }
        lift_charge(x, 0x157D2);
        lift_w8(x, c->a[3] + 0x61, alu_moveb(c, c->d[5]));  /* move.b d5,$61(a3) */
        lift_charge(x, 0x157D8);
        lift_w8(x, c->a[4] + SW(c->d[4]), alu_moveb(c, 0));  /* clr.b (a4,d4.w) */
        lift_charge(x, 0x157DC);
      }
    }
    /* loc_157E0: dbf d4,loc_157AC */
    int expired = (W(c->d[4]) == 0);
    setw(&c->d[4], W(c->d[4] - 1));
    lift_charge_dbcc(x, 0x157E0, !expired, expired);
    if (expired) break;
  }
  c->d[4] = alu_moveql(c, 5);                      /* moveq #5,d4 */
  lift_charge(x, 0x157E4);
  c->a[4] = 0xFFFFCF14;                            /* movea.w #$CF14,a4 */
  lift_charge(x, 0x157E6);
  for (;;)
  {
    /* loc_157EA */
    setw(&c->d[5], alu_movew(c, 0));               /* clr.w d5 */
    lift_charge(x, 0x157EA);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]))));  /* move.b (a4,d4.w),d5 */
    lift_charge(x, 0x157EC);
    t = c->zf;
    lift_charge_bcc(x, 0x157F0, t);                /* beq.w loc_15828 */
    if (!t)
    {
      setw(&c->d[5], alu_subw(c, 1, W(c->d[5])));  /* subq.w #1,d5 */
      lift_charge(x, 0x157F4);
      c->d[3] = alu_moveql(c, 5);                  /* moveq #5,d3 */
      lift_charge(x, 0x157F6);
      c->a[3] = SEW(lift_r16(x, c->a[2] + 0x22));  /* movea.w $22(a2),a3 */
      lift_charge(x, 0x157F8);
      c->a[3] -= 0x80;                             /* suba.w #$80,a3 */
      lift_charge(x, 0x157FC);
      for (;;)
      {
        /* loc_15800 */
        c->a[3] += 0x80;                           /* adda.w #$80,a3 */
        lift_charge(x, 0x15800);
        alu_tstb(c, lift_r8(x, c->a[3] + 0x61));   /* tst.b $61(a3) */
        lift_charge(x, 0x15804);
        int cond = c->nf, taken = 0, expd = 0;     /* dbmi d3,loc_15800 */
        if (!cond)
        {
          expd = (W(c->d[3]) == 0);
          setw(&c->d[3], W(c->d[3] - 1));
          taken = !expd;
        }
        lift_charge_dbcc(x, 0x15808, taken, expd);
        if (taken) continue;
        t = !c->nf;
        lift_charge_bcc(x, 0x1580C, t);            /* bpl.w loc_15812 */
        if (!t)
        {
          c->a[0] = SEW(c->a[3]);                  /* movea.w a3,a0 */
          lift_charge(x, 0x15810);
        }
        /* loc_15812 */
        alu_tstw(c, lift_r16(x, c->a[3] + 0x34));  /* tst.w $34(a3) */
        lift_charge(x, 0x15812);
        cond = !c->nf; taken = 0; expd = 0;        /* dbpl d3,loc_15800 */
        if (!cond)
        {
          expd = (W(c->d[3]) == 0);
          setw(&c->d[3], W(c->d[3] - 1));
          taken = !expd;
        }
        lift_charge_dbcc(x, 0x15816, taken, expd);
        if (!taken) break;
      }
      {                                            /* move.b 6(a4,d4.w),$60(a0) */
        uint32_t v = alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[4]) + 6));
        lift_w8(x, c->a[0] + 0x60, v);
      }
      lift_charge(x, 0x1581A);
      lift_w8(x, c->a[0] + 0x61, alu_moveb(c, c->d[5]));  /* move.b d5,$61(a0) */
      lift_charge(x, 0x15820);
      lift_w8(x, c->a[4] + SW(c->d[4]), alu_moveb(c, 0));  /* clr.b (a4,d4.w) */
      lift_charge(x, 0x15824);
    }
    /* loc_15828: dbf d4,loc_157EA */
    int expired = (W(c->d[4]) == 0);
    setw(&c->d[4], W(c->d[4] - 1));
    lift_charge_dbcc(x, 0x15828, !expired, expired);
    if (expired) break;
  }
  /* movem.l (sp)+,d0-d5/a0-a4 */
  {
    int i;
    for (i = 0; i < 6; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
    for (i = 0; i < 5; i++) c->a[i] = lift_r32(x, c->a[7] + 24 + 4 * i);
  }
  c->a[7] += 44;
  lift_charge_movem(x, 0x1582C);
  lift_charge(x, 0x15830);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FEB54 (called from sub_A9D6+CA, the gameplay dispatcher)
 *   in: a3 = on-ice object (referee/linesman)
 * Skate-to-position easing, dispatched on the object's anim script
 * offset $58(a3). Scripts $1776/$185A ease world Y ($14(a3)) halfway
 * toward +/-$124 (pulled in to +/-$116 when $FFFFD41C is outside
 * (-$56,$56]), gated to only ever move toward the target. Scripts
 * $17E8/$1A00/$18CC/$193E ease world X ((a3)) halfway toward a
 * per-script constant (+/-$82 or +/-$88), sign flipped by the attr
 * H-flip bit ($4(a3) bit 3). Any other script: no-op.
 */
void sub_FEB54(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];
  int t;

  alu_cmpw(c, 0x193E, lift_r16(x, a3 + 0x58));     /* cmp.w #$193E,$58(a3) */
  lift_charge(x, 0xFEB54);
  t = c->zf; lift_charge_bcc(x, 0xFEB5A, t);
  if (t) goto L_FEC44;
  alu_cmpw(c, 0x1A00, lift_r16(x, a3 + 0x58));
  lift_charge(x, 0xFEB5E);
  t = c->zf; lift_charge_bcc(x, 0xFEB64, t);
  if (t) goto L_FEC0C;
  alu_cmpw(c, 0x18CC, lift_r16(x, a3 + 0x58));
  lift_charge(x, 0xFEB68);
  t = c->zf; lift_charge_bcc(x, 0xFEB6E, t);
  if (t) goto L_FEC28;
  alu_cmpw(c, 0x17E8, lift_r16(x, a3 + 0x58));
  lift_charge(x, 0xFEB72);
  t = c->zf; lift_charge_bcc(x, 0xFEB78, t);
  if (t) goto L_FEBF0;
  alu_cmpw(c, 0x1776, lift_r16(x, a3 + 0x58));
  lift_charge(x, 0xFEB7C);
  t = c->zf; lift_charge_bcc(x, 0xFEB82, t);
  if (t) goto L_FEB94;
  alu_cmpw(c, 0x185A, lift_r16(x, a3 + 0x58));
  lift_charge(x, 0xFEB86);
  t = c->zf; lift_charge_bcc(x, 0xFEB8C, t);
  if (t) goto L_FEBC2;
  lift_charge(x, 0xFEB90);                         /* bra.w locret_FEC5C */
  goto L_ret;

L_FEB94:                                           /* script $1776: Y toward +$124/+$116 */
  setw(&c->d[0], alu_movew(c, 0x124));             /* move.w #$124,d0 */
  lift_charge(x, 0xFEB94);
  alu_cmpw(c, 0x56, lift_r16(x, 0xFFFFD41C));      /* cmp.w #$56,(D41C).w */
  lift_charge(x, 0xFEB98);
  t = (!c->zf && (c->nf == c->vf));                /* bgt.w loc_FEBAC */
  lift_charge_bcc(x, 0xFEB9E, t);
  if (!t)
  {
    alu_cmpw(c, 0xFFAA, lift_r16(x, 0xFFFFD41C));
    lift_charge(x, 0xFEBA2);
    t = (!c->zf && (c->nf == c->vf));              /* bgt.w loc_FEBB0 */
    lift_charge_bcc(x, 0xFEBA8, t);
    if (t) goto L_FEBB0;
  }
  setw(&c->d[0], alu_movew(c, 0x116));             /* loc_FEBAC */
  lift_charge(x, 0xFEBAC);
L_FEBB0:
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3 + 0x14), W(c->d[0])));  /* sub.w $14(a3),d0 */
  lift_charge(x, 0xFEBB0);
  t = c->nf;                                       /* bmi.w locret */
  lift_charge_bcc(x, 0xFEBB4, t);
  if (t) goto L_ret;
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));      /* asr.w #1,d0 */
  lift_charge(x, 0xFEBB8);
  {
    uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3 + 0x14));
    lift_w16(x, a3 + 0x14, v);                     /* add.w d0,$14(a3) */
  }
  lift_charge(x, 0xFEBBA);
  lift_charge(x, 0xFEBBE);                         /* bra.w locret */
  goto L_ret;

L_FEBC2:                                           /* script $185A: Y toward -$124/-$116 */
  setw(&c->d[0], alu_movew(c, 0xFEDC));            /* move.w #$FEDC,d0 */
  lift_charge(x, 0xFEBC2);
  alu_cmpw(c, 0x56, lift_r16(x, 0xFFFFD41C));
  lift_charge(x, 0xFEBC6);
  t = (!c->zf && (c->nf == c->vf));                /* bgt.w loc_FEBDA */
  lift_charge_bcc(x, 0xFEBCC, t);
  if (!t)
  {
    alu_cmpw(c, 0xFFAA, lift_r16(x, 0xFFFFD41C));
    lift_charge(x, 0xFEBD0);
    t = (!c->zf && (c->nf == c->vf));              /* bgt.w loc_FEBDE */
    lift_charge_bcc(x, 0xFEBD6, t);
    if (t) goto L_FEBDE;
  }
  setw(&c->d[0], alu_movew(c, 0xFEEA));            /* loc_FEBDA */
  lift_charge(x, 0xFEBDA);
L_FEBDE:
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3 + 0x14), W(c->d[0])));  /* sub.w $14(a3),d0 */
  lift_charge(x, 0xFEBDE);
  t = !c->nf;                                      /* bpl.w locret */
  lift_charge_bcc(x, 0xFEBE2, t);
  if (t) goto L_ret;
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));
  lift_charge(x, 0xFEBE6);
  {
    uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3 + 0x14));
    lift_w16(x, a3 + 0x14, v);
  }
  lift_charge(x, 0xFEBE8);
  lift_charge(x, 0xFEBEC);                         /* bra.w locret */
  goto L_ret;

L_FEBF0:                                           /* script $17E8: X toward +/-$82 */
  setw(&c->d[0], alu_movew(c, 0x82));
  lift_charge(x, 0xFEBF0);
  alu_btst(c, lift_r8(x, a3 + 4), 3);              /* btst #3,4(a3) */
  lift_charge(x, 0xFEBF4);
  t = c->zf; lift_charge_bcc(x, 0xFEBFA, t);
  if (!t) { setw(&c->d[0], alu_movew(c, 0xFF7E)); lift_charge(x, 0xFEBFE); }
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3), W(c->d[0])));  /* loc_FEC02: sub.w (a3),d0 */
  lift_charge(x, 0xFEC02);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));
  lift_charge(x, 0xFEC04);
  { uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3)); lift_w16(x, a3, v); }
  lift_charge(x, 0xFEC06);
  lift_charge(x, 0xFEC08);                         /* bra.w locret */
  goto L_ret;

L_FEC0C:                                           /* script $1A00: X toward +/-$88 */
  setw(&c->d[0], alu_movew(c, 0x88));
  lift_charge(x, 0xFEC0C);
  alu_btst(c, lift_r8(x, a3 + 4), 3);
  lift_charge(x, 0xFEC10);
  t = c->zf; lift_charge_bcc(x, 0xFEC16, t);
  if (!t) { setw(&c->d[0], alu_movew(c, 0xFF78)); lift_charge(x, 0xFEC1A); }
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3), W(c->d[0])));
  lift_charge(x, 0xFEC1E);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));
  lift_charge(x, 0xFEC20);
  { uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3)); lift_w16(x, a3, v); }
  lift_charge(x, 0xFEC22);
  lift_charge(x, 0xFEC24);                         /* bra.w locret */
  goto L_ret;

L_FEC28:                                           /* script $18CC: X toward -/+$82 */
  setw(&c->d[0], alu_movew(c, 0xFF7E));
  lift_charge(x, 0xFEC28);
  alu_btst(c, lift_r8(x, a3 + 4), 3);
  lift_charge(x, 0xFEC2C);
  t = c->zf; lift_charge_bcc(x, 0xFEC32, t);
  if (!t) { setw(&c->d[0], alu_movew(c, 0x82)); lift_charge(x, 0xFEC36); }
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3), W(c->d[0])));
  lift_charge(x, 0xFEC3A);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));
  lift_charge(x, 0xFEC3C);
  { uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3)); lift_w16(x, a3, v); }
  lift_charge(x, 0xFEC3E);
  lift_charge(x, 0xFEC40);                         /* bra.w locret */
  goto L_ret;

L_FEC44:                                           /* script $193E: X toward -/+$88 */
  setw(&c->d[0], alu_movew(c, 0xFF78));
  lift_charge(x, 0xFEC44);
  alu_btst(c, lift_r8(x, a3 + 4), 3);
  lift_charge(x, 0xFEC48);
  t = c->zf; lift_charge_bcc(x, 0xFEC4E, t);
  if (!t) { setw(&c->d[0], alu_movew(c, 0x88)); lift_charge(x, 0xFEC52); }
  setw(&c->d[0], alu_subw(c, lift_r16(x, a3), W(c->d[0])));
  lift_charge(x, 0xFEC56);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));
  lift_charge(x, 0xFEC58);
  { uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, a3)); lift_w16(x, a3, v); }
  lift_charge(x, 0xFEC5A);
  /* falls through into locret */

L_ret:
  lift_charge(x, 0xFEC5C);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FA9F8 (called from sub_8D4E+22, ROM:FA90C and others — the
 * player-rating scorer)
 *   in:  d0.w = player index into the $1E(a2) roster chain, d4 = a
 *        position-class key (compared against the ROM dwords at $19420/
 *        $19582 to pick one of three weight tables at $FAB1C/$FAB2C/
 *        $FAB3C), a2 = team block
 *   out: d0.w = weighted attribute score, d1.w = $64 * categories
 *        scored (the denominator; clamped so d0 < d1 on return by
 *        d1=d0/d0-=1 when d0 >= d1); d2.w = -1, d3 = last nibble
 *        scratch, d4 = swapped(!) input, a0 = player record, a4 =
 *        $1A2(a2)+index*16+16 adjustment block; d5-d7/a6 preserved.
 * For each set bit i (15..0) of the swapped d4, extracts attribute
 * nibble i from the packed stream just below the player record,
 * averages it over the weight-table window (2 = raw, 4/6 = sum of
 * neighbours halved), then either folds in the per-player adjustment
 * byte (17x + adj, clamped at 0) or — for the $FAB3C "goalie" table —
 * accumulates the adjustment into $FFFFD6CE/$FFFFD6D0 whose divs.w
 * average is added once at the end (with a fixed $64 denominator).
 * Bits 6 and $D score their nibble raw. Exact table roles TBD;
 * behaviour preserved bit-for-bit.
 */
void sub_FA9F8(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  if (W(c->d[0]) > 0x100) { x->declined = 1; return; }   /* chain-walk dbf guard */

  c->a[7] -= 4;                                    /* move.l a6,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[6]));
  lift_charge(x, 0xFA9F8);
  lift_w16(x, 0xFFFFD6CE, alu_movew(c, 0));        /* clr.w (D6CE).w */
  lift_charge(x, 0xFA9FA);
  lift_w16(x, 0xFFFFD6D0, alu_movew(c, 0));        /* clr.w (D6D0).w */
  lift_charge(x, 0xFA9FE);
  c->a[6] = 0xFAB3C;                               /* movea.l #unk_FAB3C,a6 */
  lift_charge(x, 0xFAA02);
  alu_cmpl(c, lift_r32(x, 0x19420), c->d[4]);      /* cmp.l (dword_19420).l,d4 */
  lift_charge(x, 0xFAA08);
  t = !c->zf;
  lift_charge_bcc(x, 0xFAA0E, t);                  /* bne.w loc_FAA18 */
  if (!t)
  {
    c->a[6] = 0xFAB1C;                             /* movea.l #unk_FAB1C,a6 */
    lift_charge(x, 0xFAA12);
  }
  /* loc_FAA18 */
  alu_cmpl(c, lift_r32(x, 0x19582), c->d[4]);      /* cmp.l (dword_19582).l,d4 */
  lift_charge(x, 0xFAA18);
  t = !c->zf;
  lift_charge_bcc(x, 0xFAA1E, t);                  /* bne.w loc_FAA28 */
  if (!t)
  {
    c->a[6] = 0xFAB2C;                             /* movea.l #unk_FAB2C,a6 */
    lift_charge(x, 0xFAA22);
  }
  /* loc_FAA28 */
  c->a[0] = lift_r32(x, c->a[2] + 0x1E);           /* movea.l $1E(a2),a0 */
  lift_charge(x, 0xFAA28);
  c->a[4] = c->a[2] + 0x1A2;                       /* lea $1A2(a2),a4 */
  lift_charge(x, 0xFAA2C);
  c->d[1] = alu_movel(c, 0);                       /* clr.l d1 */
  lift_charge(x, 0xFAA30);
  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0xFAA32);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));      /* asl.w #4,d1 */
  lift_charge(x, 0xFAA34);
  c->a[4] += c->d[1];                              /* adda.l d1,a4 */
  lift_charge(x, 0xFAA36);
  c->a[4] += 0x10;                                 /* adda.l #$10,a4 */
  lift_charge(x, 0xFAA38);
  c->a[0] += SEW(lift_r16(x, c->a[0]));            /* adda.w (a0),a0 */
  lift_charge(x, 0xFAA3E);
  for (;;)
  {
    /* loc_FAA40 */
    c->a[0] += SEW(lift_r16(x, c->a[0]));          /* adda.w (a0),a0 */
    lift_charge(x, 0xFAA40);
    c->a[0] += 8;                                  /* addq.w #8,a0 */
    lift_charge(x, 0xFAA42);
    int expired = (W(c->d[0]) == 0);               /* dbf d0,loc_FAA40 */
    setw(&c->d[0], W(c->d[0] - 1));
    lift_charge_dbcc(x, 0xFAA44, !expired, expired);
    if (expired) break;
  }
  setw(&c->d[0], alu_movew(c, 0));                 /* clr.w d0 */
  lift_charge(x, 0xFAA48);
  setw(&c->d[1], alu_movew(c, 0));                 /* clr.w d1 */
  lift_charge(x, 0xFAA4A);
  c->d[2] = alu_moveql(c, 0xF);                    /* moveq #$F,d2 */
  lift_charge(x, 0xFAA4C);
  c->d[4] = alu_swap(c, c->d[4]);                  /* swap d4 (never undone) */
  lift_charge(x, 0xFAA4E);
  for (;;)
  {
    /* loc_FAA50 */
    alu_btst(c, c->d[4], (int)(c->d[2] & 31));     /* btst d2,d4 */
    lift_charge(x, 0xFAA50);
    t = c->zf;
    lift_charge_bcc(x, 0xFAA52, t);                /* beq.w loc_FAAE6 */
    if (!t)
    {
      setw(&c->d[3], alu_movew(c, W(c->d[2])));    /* move.w d2,d3 */
      lift_charge(x, 0xFAA56);
      setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 1));  /* lsr.w #1,d3 */
      lift_charge(x, 0xFAA58);
      setw(&c->d[3], alu_negw(c, W(c->d[3])));     /* neg.w d3 */
      lift_charge(x, 0xFAA5A);
      setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[3]) - 1)));  /* move.b -1(a0,d3.w),d3 */
      lift_charge(x, 0xFAA5C);
      alu_btst(c, c->d[2], 0);                     /* btst #0,d2 */
      lift_charge(x, 0xFAA60);
      t = c->zf;
      lift_charge_bcc(x, 0xFAA64, t);              /* beq.w loc_FAA6A */
      if (!t)
      {
        setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 4));  /* lsr.w #4,d3 */
        lift_charge(x, 0xFAA68);
      }
      /* loc_FAA6A */
      setw(&c->d[3], alu_andw(c, 0xF, W(c->d[3])));  /* and.w #$F,d3 */
      lift_charge(x, 0xFAA6A);
      alu_cmpw(c, 0xD, W(c->d[2]));                /* cmp.w #$D,d2 */
      lift_charge(x, 0xFAA6E);
      t = !c->zf;
      lift_charge_bcc(x, 0xFAA72, t);              /* bne.w loc_FAA7A */
      if (!t)
      {
        lift_charge(x, 0xFAA76);                   /* bra.w loc_FAAE0 */
        goto L_tally;
      }
      /* loc_FAA7A */
      alu_cmpw(c, 6, W(c->d[2]));                  /* cmp.w #6,d2 */
      lift_charge(x, 0xFAA7A);
      t = c->zf;
      lift_charge_bcc(x, 0xFAA7E, t);              /* beq.w loc_FAAE0 */
      if (t) goto L_tally;
      c->a[7] -= 12;                               /* movem.l d5-d7,-(sp) */
      lift_w32(x, c->a[7], c->d[5]);
      lift_w32(x, c->a[7] + 4, c->d[6]);
      lift_w32(x, c->a[7] + 8, c->d[7]);
      lift_charge_movem(x, 0xFAA82);
      setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[6] + SW(c->d[2]))));  /* move.b (a6,d2.w),d5 */
      lift_charge(x, 0xFAA86);
      setw(&c->d[5], alu_extw(c, c->d[5]));        /* ext.w d5 */
      lift_charge(x, 0xFAA8A);
      alu_cmpw(c, 2, W(c->d[5]));                  /* cmp.w #2,d5 */
      lift_charge(x, 0xFAA8C);
      t = c->zf;
      lift_charge_bcc(x, 0xFAA90, t);              /* beq.w loc_FAAA2 */
      if (!t)
      {
        c->a[7] -= 2;                              /* move.w d3,-(sp) */
        lift_w16(x, c->a[7], W(c->d[3]));
        lift_charge(x, 0xFAA94);
        setw(&c->d[5], alu_subw(c, 2, W(c->d[5])));  /* subq.w #2,d5 */
        lift_charge(x, 0xFAA96);
        if (W(c->d[5]) > 0x100) { x->declined = 1; return; }  /* dbf-wrap guard */
        for (;;)
        {
          /* loc_FAA98: add.w (sp),d3 */
          setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[3])));
          lift_charge(x, 0xFAA98);
          int expired = (W(c->d[5]) == 0);         /* dbf d5,loc_FAA98 */
          setw(&c->d[5], W(c->d[5] - 1));
          lift_charge_dbcc(x, 0xFAA9A, !expired, expired);
          if (expired) break;
        }
        setw(&c->d[3], alu_asrw(c, W(c->d[3]), 1));  /* asr.w #1,d3 */
        lift_charge(x, 0xFAA9E);
        alu_tstw(c, lift_r16(x, c->a[7]));         /* tst.w (sp)+ */
        c->a[7] += 2;
        lift_charge(x, 0xFAAA0);
      }
      /* loc_FAAA2: movem.l (sp)+,d5-d7 */
      c->d[5] = lift_r32(x, c->a[7]);
      c->d[6] = lift_r32(x, c->a[7] + 4);
      c->d[7] = lift_r32(x, c->a[7] + 8);
      c->a[7] += 12;
      lift_charge_movem(x, 0xFAAA2);
      alu_cmpl(c, 0xFAB3C, c->a[6]);               /* cmpa.l #unk_FAB3C,a6 */
      lift_charge(x, 0xFAAA6);
      t = c->zf;
      lift_charge_bcc(x, 0xFAAAC, t);              /* beq.w loc_FAACA */
      if (!t)
      {
        setw(&c->d[2], alu_negw(c, W(c->d[2])));   /* neg.w d2 */
        lift_charge(x, 0xFAAB0);
        c->a[7] -= 2;                              /* move.w d7,-(sp) */
        lift_w16(x, c->a[7], W(c->d[7]));
        lift_charge(x, 0xFAAB2);
        setb(&c->d[7], alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[2]) - 1)));  /* move.b -1(a4,d2.w),d7 */
        lift_charge(x, 0xFAAB4);
        setw(&c->d[7], alu_extw(c, c->d[7]));      /* ext.w d7 */
        lift_charge(x, 0xFAAB8);
        {                                          /* add.w d7,(D6CE).w */
          uint32_t v = alu_addw(c, W(c->d[7]), lift_r16(x, 0xFFFFD6CE));
          lift_w16(x, 0xFFFFD6CE, v);
        }
        lift_charge(x, 0xFAABA);
        {                                          /* addq.w #1,(D6D0).w */
          uint32_t v = alu_addw(c, 1, lift_r16(x, 0xFFFFD6D0));
          lift_w16(x, 0xFFFFD6D0, v);
        }
        lift_charge(x, 0xFAABE);
        setw(&c->d[7], alu_movew(c, lift_r16(x, c->a[7])));  /* move.w (sp)+,d7 */
        c->a[7] += 2;
        lift_charge(x, 0xFAAC2);
        setw(&c->d[2], alu_negw(c, W(c->d[2])));   /* neg.w d2 */
        lift_charge(x, 0xFAAC4);
        lift_charge(x, 0xFAAC6);                   /* bra.w loc_FAAE0 */
        goto L_tally;
      }
      /* loc_FAACA */
      c->a[7] -= 2;                                /* move.w d3,-(sp) */
      lift_w16(x, c->a[7], W(c->d[3]));
      lift_charge(x, 0xFAACA);
      setw(&c->d[3], alu_aslw(c, W(c->d[3]), 4));  /* asl.w #4,d3 */
      lift_charge(x, 0xFAACC);
      setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[3])));  /* add.w (sp),d3 */
      lift_charge(x, 0xFAACE);
      setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[3])));  /* add.w (sp)+,d3 */
      c->a[7] += 2;
      lift_charge(x, 0xFAAD0);
      setw(&c->d[2], alu_negw(c, W(c->d[2])));     /* neg.w d2 */
      lift_charge(x, 0xFAAD2);
      setb(&c->d[3], alu_addb(c, lift_r8(x, c->a[4] + SW(c->d[2]) - 1), c->d[3]));  /* add.b -1(a4,d2.w),d3 */
      lift_charge(x, 0xFAAD4);
      t = !c->nf;
      lift_charge_bcc(x, 0xFAAD8, t);              /* bpl.w loc_FAADE */
      if (!t)
      {
        setb(&c->d[3], alu_moveb(c, 0));           /* clr.b d3 */
        lift_charge(x, 0xFAADC);
      }
      /* loc_FAADE */
      setw(&c->d[2], alu_negw(c, W(c->d[2])));     /* neg.w d2 */
      lift_charge(x, 0xFAADE);
L_tally:
      /* loc_FAAE0 */
      setw(&c->d[0], alu_addw(c, W(c->d[3]), W(c->d[0])));  /* add.w d3,d0 */
      lift_charge(x, 0xFAAE0);
      setw(&c->d[1], alu_addw(c, 0x64, W(c->d[1])));  /* add.w #$64,d1 */
      lift_charge(x, 0xFAAE2);
    }
    /* loc_FAAE6: dbf d2,loc_FAA50 */
    {
      int expired = (W(c->d[2]) == 0);
      setw(&c->d[2], W(c->d[2] - 1));
      lift_charge_dbcc(x, 0xFAAE6, !expired, expired);
      if (expired) break;
    }
  }
  alu_cmpl(c, 0xFAB3C, c->a[6]);                   /* cmpa.l #unk_FAB3C,a6 */
  lift_charge(x, 0xFAAEA);
  t = c->zf;
  lift_charge_bcc(x, 0xFAAF0, t);                  /* beq.w loc_FAB0E */
  if (!t)
  {
    setw(&c->d[1], alu_movew(c, 0x64));            /* move.w #$64,d1 */
    lift_charge(x, 0xFAAF4);
    c->a[7] -= 8;                                  /* movem.l d6-d7,-(sp) */
    lift_w32(x, c->a[7], c->d[6]);
    lift_w32(x, c->a[7] + 4, c->d[7]);
    lift_charge_movem(x, 0xFAAF8);
    setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFD6CE)));  /* move.w (D6CE).w,d6 */
    lift_charge(x, 0xFAAFC);
    c->d[6] = alu_extl(c, c->d[6]);                /* ext.l d6 */
    lift_charge(x, 0xFAB00);
    setw(&c->d[7], alu_movew(c, lift_r16(x, 0xFFFFD6D0)));  /* move.w (D6D0).w,d7 */
    lift_charge(x, 0xFAB02);
    lift_charge_divs(x, 0xFAB06, W(c->d[7]), c->d[6]);  /* divs.w d7,d6 */
    if (x->declined) return;                       /* zero divisor would trap */
    c->d[6] = alu_divs(c, W(c->d[7]), c->d[6]);
    setw(&c->d[0], alu_addw(c, W(c->d[6]), W(c->d[0])));  /* add.w d6,d0 */
    lift_charge(x, 0xFAB08);
    c->d[6] = lift_r32(x, c->a[7]);                /* movem.l (sp)+,d6-d7 */
    c->d[7] = lift_r32(x, c->a[7] + 4);
    c->a[7] += 8;
    lift_charge_movem(x, 0xFAB0A);
  }
  /* loc_FAB0E */
  alu_cmpw(c, W(c->d[1]), W(c->d[0]));             /* cmp.w d1,d0 */
  lift_charge(x, 0xFAB0E);
  t = (c->nf != c->vf);                            /* blt.w loc_FAB18 */
  lift_charge_bcc(x, 0xFAB10, t);
  if (!t)
  {
    setw(&c->d[1], alu_movew(c, W(c->d[0])));      /* move.w d0,d1 */
    lift_charge(x, 0xFAB14);
    setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));    /* subq.w #1,d0 */
    lift_charge(x, 0xFAB16);
  }
  /* loc_FAB18: move.l (sp)+,a6 — movea, no flags */
  c->a[6] = lift_r32(x, c->a[7]);
  c->a[7] += 4;
  lift_charge(x, 0xFAB18);
  lift_charge(x, 0xFAB1A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Object_FrameVector(lift_ctx *);  /* anim.c */

/*
 * sub_BE26 (called from sub_BC40+124 — puck launch / trajectory planner)
 *   in:  a0 = target on-ice object, a3 = source object, a2 = team block,
 *        d4 preserved-in-upper-word scratch (low word overwritten)
 * Head: when the source has $62 bit3 set and its $34 word is zero, sets
 * $64(a0) bit6 and runs the player-selection update (sub_C0BC when the
 * relevant selection word matches, else sub_C0DA), preserving d0/d1.
 * Body: after Team_SelectBlocks, bumps $12(a2), publishes the target's
 * camera zone to $FFFFBEE0, then computes an intercept: dx/dy = target
 * position (+ its current frame vector via sub_106E0) minus the anchor
 * at $FFFFB74A/$FFFFB75E; the target's $28/$2A velocity bytes scaled by
 * $F0 give a velocity vector; solves |p + t*v| = speed*t (speed =
 * $FFFFBEDE/4) via the quadratic's discriminant (Math_SqrtU32) and a
 * two-try divs (dbpl picks the positive root), clamps the step count d2
 * to [1,$18], writes it to $FFFFB776 (capped at $C) and a derived
 * countdown to $40(a0)/$FFFFB7A8, then stores the intercept point to
 * $44/$46(a0) and the per-step deltas (<<16/(d2*$78)) to $FFFFB772/
 * $FFFFB774. Exact role TBD; behaviour preserved bit-for-bit.
 */
void sub_BE26(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 3);      /* btst #3,$62(a3) */
  lift_charge(x, 0xBE26);
  t = c->zf;
  lift_charge_bcc(x, 0xBE2C, t);                   /* beq.w loc_BEAE */
  if (!t)
  {
    alu_tstw(c, lift_r16(x, c->a[3] + 0x34));      /* tst.w $34(a3) */
    lift_charge(x, 0xBE30);
    t = !c->zf;
    lift_charge_bcc(x, 0xBE34, t);                 /* bne.w loc_BEAE */
    if (!t)
    {
      c->a[7] -= 8;                                /* movem.l d0-d1,-(sp) */
      lift_w32(x, c->a[7], c->d[0]);
      lift_w32(x, c->a[7] + 4, c->d[1]);
      lift_charge_movem(x, 0xBE38);
      alu_btst(c, lift_r8(x, c->a[0] + 0x62), 3);  /* btst #3,$62(a0) */
      lift_charge(x, 0xBE3C);
      t = !c->zf;
      lift_charge_bcc(x, 0xBE42, t);               /* bne.w loc_BEAA */
      if (!t)
      {
        {                                          /* bset #6,$64(a0) */
          uint32_t v = alu_bset(c, lift_r8(x, c->a[0] + 0x64), 6);
          lift_w8(x, c->a[0] + 0x64, v);
        }
        lift_charge(x, 0xBE46);
        setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC328)));  /* move.w (C328).w,d1 */
        lift_charge(x, 0xBE4C);
        alu_cmpw(c, lift_r16(x, 0xFFFFC32A), W(c->d[1]));  /* cmp.w (C32A).w,d1 */
        lift_charge(x, 0xBE50);
        t = !c->zf;
        lift_charge_bcc(x, 0xBE54, t);             /* bne.w loc_BE7C */
        if (!t)
        {
          setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x52)));  /* move.w $52(a0),d0 */
          lift_charge(x, 0xBE58);
          setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x52)));  /* move.w $52(a3),d1 */
          lift_charge(x, 0xBE5C);
          alu_cmpw(c, lift_r16(x, 0xFFFFC320), W(c->d[1]));  /* cmp.w (C320).w,d1 */
          lift_charge(x, 0xBE60);
          t = !c->zf;
          lift_charge_bcc(x, 0xBE64, t);           /* bne.w loc_BE72 */
          if (!t)
          {
            lift_call(x, 0xBE68, 6, Object_UpdateSelectedSlot_A);  /* jsr sub_C0BC */
            if (x->declined) return;
            lift_charge(x, 0xBE6E);                /* bra.w loc_BEAA */
          }
          else
          {
            lift_call(x, 0xBE72, 6, Object_UpdateSelectedSlot_B);  /* jsr sub_C0DA */
            if (x->declined) return;
            lift_charge(x, 0xBE78);                /* bra.w loc_BEAA */
          }
        }
        else
        {
          /* loc_BE7C */
          setw(&c->d[1], alu_movew(c, 1));         /* move.w #1,d1 */
          lift_charge(x, 0xBE7C);
          alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);  /* btst #6,$62(a3) */
          lift_charge(x, 0xBE80);
          t = c->zf;
          lift_charge_bcc(x, 0xBE86, t);           /* beq.w loc_BE8E */
          if (!t)
          {
            setw(&c->d[1], alu_movew(c, 2));       /* move.w #2,d1 */
            lift_charge(x, 0xBE8A);
          }
          /* loc_BE8E */
          setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x52)));  /* move.w $52(a0),d0 */
          lift_charge(x, 0xBE8E);
          alu_cmpw(c, lift_r16(x, 0xFFFFC328), W(c->d[1]));  /* cmp.w (C328).w,d1 */
          lift_charge(x, 0xBE92);
          t = !c->zf;
          lift_charge_bcc(x, 0xBE96, t);           /* bne.w loc_BEA4 */
          if (!t)
          {
            lift_call(x, 0xBE9A, 6, Object_UpdateSelectedSlot_A);  /* jsr sub_C0BC */
            if (x->declined) return;
            lift_charge(x, 0xBEA0);                /* bra.w loc_BEAA */
          }
          else
          {
            lift_call(x, 0xBEA4, 6, Object_UpdateSelectedSlot_B);  /* jsr sub_C0DA */
            if (x->declined) return;
          }
        }
      }
      /* loc_BEAA: movem.l (sp)+,d0-d1 */
      c->d[0] = lift_r32(x, c->a[7]);
      c->d[1] = lift_r32(x, c->a[7] + 4);
      c->a[7] += 8;
      lift_charge_movem(x, 0xBEAA);
    }
  }
  /* loc_BEAE */
  lift_call(x, 0xBEAE, 4, Team_SelectBlocks);      /* bsr.w sub_13040 */
  if (x->declined) return;
  {                                                /* addq.w #1,$12(a2) */
    uint32_t v = alu_addw(c, 1, lift_r16(x, c->a[2] + 0x12));
    lift_w16(x, c->a[2] + 0x12, v);
  }
  lift_charge(x, 0xBEB2);
  lift_w16(x, 0xFFFFBEE0, alu_movew(c, lift_r16(x, c->a[0] + 0x52)));  /* move.w $52(a0),(BEE0).w */
  lift_charge(x, 0xBEB6);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFBEDE)));  /* move.w (BEDE).w,d5 */
  lift_charge(x, 0xBEBC);
  setw(&c->d[5], alu_asrw(c, W(c->d[5]), 2));      /* asr.w #2,d5 */
  lift_charge(x, 0xBEC0);
  { uint32_t tmp = c->a[0]; c->a[0] = c->a[3]; c->a[3] = tmp; }  /* exg a0,a3 */
  lift_charge(x, 0xBEC2);
  c->d[0] = alu_movel(c, 0x13);                    /* move.l #$13,d0 */
  lift_charge(x, 0xBEC4);
  lift_call(x, 0xBECA, 4, Object_RetreatStateMod8);  /* bsr.w sub_10658 */
  if (x->declined) return;
  { uint32_t tmp = c->a[0]; c->a[0] = c->a[3]; c->a[3] = tmp; }  /* exg a0,a3 */
  lift_charge(x, 0xBECE);
  c->a[7] -= 4;                                    /* move.l a0,-(sp): stack arg */
  lift_w32(x, c->a[7], alu_movel(c, c->a[0]));
  lift_charge(x, 0xBED0);
  lift_call(x, 0xBED2, 4, Object_FrameVector);     /* bsr.w sub_106E0 — callee pops the arg */
  if (x->declined) return;
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[0]), W(c->d[0])));  /* add.w (a0),d0 */
  lift_charge(x, 0xBED6);
  setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFB74A), W(c->d[0])));  /* sub.w (B74A).w,d0 */
  lift_charge(x, 0xBED8);
  setw(&c->d[1], alu_addw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1])));  /* add.w $14(a0),d1 */
  lift_charge(x, 0xBEDC);
  setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB75E), W(c->d[1])));  /* sub.w (B75E).w,d1 */
  lift_charge(x, 0xBEE0);
  c->a[7] -= 4;                                    /* movem.w d0-d1,-(sp) */
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xBEE4);
  /* movem.w (sp),d2-d3: word restore sign-extends into the full register */
  c->d[2] = SEW(lift_r16(x, c->a[7]));
  c->d[3] = SEW(lift_r16(x, c->a[7] + 2));
  lift_charge_movem(x, 0xBEE8);
  setw(&c->d[2], alu_asrw(c, W(c->d[2]), 2));      /* asr.w #2,d2 */
  lift_charge(x, 0xBEEC);
  setw(&c->d[3], alu_asrw(c, W(c->d[3]), 2));      /* asr.w #2,d3 */
  lift_charge(x, 0xBEEE);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x28)));  /* move.w $28(a0),d0 */
  lift_charge(x, 0xBEF0);
  lift_charge_muls(x, 0xBEF4, 0xF0);               /* muls.w #$F0,d0 */
  c->d[0] = alu_muls(c, 0xF0, W(c->d[0]));
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xBEF8);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0x2A)));  /* move.w $2A(a0),d1 */
  lift_charge(x, 0xBEFA);
  lift_charge_muls(x, 0xBEFE, 0xF0);               /* muls.w #$F0,d1 */
  c->d[1] = alu_muls(c, 0xF0, W(c->d[1]));
  c->d[1] = alu_swap(c, c->d[1]);                  /* swap d1 */
  lift_charge(x, 0xBF02);
  c->a[7] -= 4;                                    /* movem.w d0-d1,-(sp) */
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xBF04);
  lift_charge_muls(x, 0xBF08, W(c->d[2]));         /* muls.w d2,d0 */
  c->d[0] = alu_muls(c, W(c->d[2]), W(c->d[0]));
  lift_charge_muls(x, 0xBF0A, W(c->d[3]));         /* muls.w d3,d1 */
  c->d[1] = alu_muls(c, W(c->d[3]), W(c->d[1]));
  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));  /* add.w d1,d0 */
  lift_charge(x, 0xBF0C);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));      /* asl.w #1,d0 */
  lift_charge(x, 0xBF0E);
  setw(&c->d[4], alu_movew(c, W(c->d[0])));        /* move.w d0,d4 */
  lift_charge(x, 0xBF10);
  /* movem.w (sp),d0-d1: sign-extends */
  c->d[0] = SEW(lift_r16(x, c->a[7]));
  c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
  lift_charge_movem(x, 0xBF12);
  lift_charge_muls(x, 0xBF16, W(c->d[0]));         /* muls.w d0,d0 */
  c->d[0] = alu_muls(c, W(c->d[0]), W(c->d[0]));
  lift_charge_muls(x, 0xBF18, W(c->d[1]));         /* muls.w d1,d1 */
  c->d[1] = alu_muls(c, W(c->d[1]), W(c->d[1]));
  lift_charge_muls(x, 0xBF1A, W(c->d[5]));         /* muls.w d5,d5 */
  c->d[5] = alu_muls(c, W(c->d[5]), W(c->d[5]));
  c->d[5] = alu_negl(c, c->d[5]);                  /* neg.l d5 */
  lift_charge(x, 0xBF1C);
  c->d[5] = alu_addl(c, c->d[0], c->d[5]);         /* add.l d0,d5 */
  lift_charge(x, 0xBF1E);
  c->d[5] = alu_addl(c, c->d[1], c->d[5]);         /* add.l d1,d5 */
  lift_charge(x, 0xBF20);
  lift_charge_muls(x, 0xBF22, W(c->d[2]));         /* muls.w d2,d2 */
  c->d[2] = alu_muls(c, W(c->d[2]), W(c->d[2]));
  lift_charge_muls(x, 0xBF24, W(c->d[3]));         /* muls.w d3,d3 */
  c->d[3] = alu_muls(c, W(c->d[3]), W(c->d[3]));
  c->d[3] = alu_addl(c, c->d[2], c->d[3]);         /* add.l d2,d3 */
  lift_charge(x, 0xBF26);
  lift_charge_muls(x, 0xBF28, W(c->d[5]));         /* muls.w d5,d3 */
  c->d[3] = alu_muls(c, W(c->d[5]), W(c->d[3]));
  c->d[3] = alu_asll(c, c->d[3], 2);               /* asl.l #2,d3 */
  lift_charge(x, 0xBF2A);
  setw(&c->d[0], alu_movew(c, W(c->d[4])));        /* move.w d4,d0 */
  lift_charge(x, 0xBF2C);
  lift_charge_muls(x, 0xBF2E, W(c->d[0]));         /* muls.w d0,d0 */
  c->d[0] = alu_muls(c, W(c->d[0]), W(c->d[0]));
  c->d[0] = alu_subl(c, c->d[3], c->d[0]);         /* sub.l d3,d0 */
  lift_charge(x, 0xBF30);
  lift_call(x, 0xBF32, 4, Math_SqrtU32);           /* bsr.w sub_110BE */
  if (x->declined) return;
  c->d[3] = alu_moveql(c, 1);                      /* moveq #1,d3 */
  lift_charge(x, 0xBF36);
  setw(&c->d[5], alu_asrw(c, W(c->d[5]), 2));      /* asr.w #2,d5 */
  lift_charge(x, 0xBF38);
  t = !c->zf;
  lift_charge_bcc(x, 0xBF3A, t);                   /* bne.w loc_BF40 */
  if (!t)
  {
    c->d[5] = alu_moveql(c, 1);                    /* moveq #1,d5 */
    lift_charge(x, 0xBF3E);
  }
  for (;;)
  {
    /* loc_BF40 */
    setw(&c->d[2], alu_movew(c, W(c->d[0])));      /* move.w d0,d2 */
    lift_charge(x, 0xBF40);
    setw(&c->d[0], alu_negw(c, W(c->d[0])));       /* neg.w d0 */
    lift_charge(x, 0xBF42);
    setw(&c->d[2], alu_subw(c, W(c->d[4]), W(c->d[2])));  /* sub.w d4,d2 */
    lift_charge(x, 0xBF44);
    c->d[2] = alu_extl(c, c->d[2]);                /* ext.l d2 */
    lift_charge(x, 0xBF46);
    lift_charge_divs(x, 0xBF48, W(c->d[5]), c->d[2]);  /* divs.w d5,d2 */
    if (x->declined) return;
    c->d[2] = alu_divs(c, W(c->d[5]), c->d[2]);
    /* dbpl d3,loc_BF40 */
    {
      int cond = !c->nf, taken = 0, expd = 0;
      if (!cond)
      {
        expd = (W(c->d[3]) == 0);
        setw(&c->d[3], W(c->d[3] - 1));
        taken = !expd;
      }
      lift_charge_dbcc(x, 0xBF4A, taken, expd);
      if (!taken) break;
    }
  }
  t = !c->zf;
  lift_charge_bcc(x, 0xBF4E, t);                   /* bne.w loc_BF54 */
  if (!t)
  {
    setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));    /* addq.w #1,d2 */
    lift_charge(x, 0xBF52);
  }
  /* loc_BF54 */
  alu_cmpw(c, 0x18, W(c->d[2]));                   /* cmp.w #$18,d2 */
  lift_charge(x, 0xBF54);
  t = (c->cf || c->zf);                            /* bls.w loc_BF5E */
  lift_charge_bcc(x, 0xBF58, t);
  if (!t)
  {
    c->d[2] = alu_moveql(c, 0x18);                 /* moveq #$18,d2 */
    lift_charge(x, 0xBF5C);
  }
  /* loc_BF5E */
  lift_w8(x, 0xFFFFB776, alu_moveb(c, c->d[2]));   /* move.b d2,(B776).w */
  lift_charge(x, 0xBF5E);
  alu_cmpw(c, 0xC, W(c->d[2]));                    /* cmp.w #$C,d2 */
  lift_charge(x, 0xBF62);
  t = (c->nf != c->vf);                            /* blt.w loc_BF70 */
  lift_charge_bcc(x, 0xBF66, t);
  if (!t)
  {
    lift_w8(x, 0xFFFFB776, alu_moveb(c, 0xC));     /* move.b #$C,(B776).w */
    lift_charge(x, 0xBF6A);
  }
  /* loc_BF70 */
  setw(&c->d[0], alu_movew(c, W(c->d[2])));        /* move.w d2,d0 */
  lift_charge(x, 0xBF70);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));      /* asl.w #3,d0 */
  lift_charge(x, 0xBF72);
  setw(&c->d[0], alu_subw(c, 0xA, W(c->d[0])));    /* sub.w #$A,d0 */
  lift_charge(x, 0xBF74);
  lift_w8(x, c->a[0] + 0x40, alu_moveb(c, c->d[0]));  /* move.b d0,$40(a0) */
  lift_charge(x, 0xBF78);
  setw(&c->d[0], alu_subw(c, 6, W(c->d[0])));      /* subq.w #6,d0 */
  lift_charge(x, 0xBF7C);
  lift_w8(x, 0xFFFFB7A8, alu_moveb(c, c->d[0]));   /* move.b d0,(B7A8).w */
  lift_charge(x, 0xBF7E);
  /* movem.w (sp)+,d0-d1: sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7]));
  c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
  c->a[7] += 4;
  lift_charge_movem(x, 0xBF82);
  lift_charge_muls(x, 0xBF86, W(c->d[2]));         /* muls.w d2,d0 */
  c->d[0] = alu_muls(c, W(c->d[2]), W(c->d[0]));
  c->d[0] = alu_asrl(c, c->d[0], 1);               /* asr.l #1,d0 */
  lift_charge(x, 0xBF88);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[0])));  /* add.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0xBF8A);
  lift_w16(x, c->a[0] + 0x44, alu_movew(c, lift_r16(x, 0xFFFFB74A)));  /* move.w (B74A).w,$44(a0) */
  lift_charge(x, 0xBF8C);
  {                                                /* add.w d0,$44(a0) */
    uint32_t v = alu_addw(c, W(c->d[0]), lift_r16(x, c->a[0] + 0x44));
    lift_w16(x, c->a[0] + 0x44, v);
  }
  lift_charge(x, 0xBF92);
  lift_charge_muls(x, 0xBF96, W(c->d[2]));         /* muls.w d2,d1 */
  c->d[1] = alu_muls(c, W(c->d[2]), W(c->d[1]));
  c->d[1] = alu_asrl(c, c->d[1], 1);               /* asr.l #1,d1 */
  lift_charge(x, 0xBF98);
  setw(&c->d[1], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[1])));  /* add.w (sp)+,d1 */
  c->a[7] += 2;
  lift_charge(x, 0xBF9A);
  lift_w16(x, c->a[0] + 0x46, alu_movew(c, lift_r16(x, 0xFFFFB75E)));  /* move.w (B75E).w,$46(a0) */
  lift_charge(x, 0xBF9C);
  {                                                /* add.w d1,$46(a0) */
    uint32_t v = alu_addw(c, W(c->d[1]), lift_r16(x, c->a[0] + 0x46));
    lift_w16(x, c->a[0] + 0x46, v);
  }
  lift_charge(x, 0xBFA2);
  lift_charge_mulu(x, 0xBFA6, 0x78);               /* mulu.w #$78,d2 */
  c->d[2] = alu_mulu(c, 0x78, W(c->d[2]));
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xBFAA);
  lift_charge_divs(x, 0xBFAC, W(c->d[2]), c->d[0]);  /* divs.w d2,d0 */
  if (x->declined) return;
  c->d[0] = alu_divs(c, W(c->d[2]), c->d[0]);
  lift_w16(x, 0xFFFFB772, alu_movew(c, W(c->d[0])));  /* move.w d0,(B772).w */
  lift_charge(x, 0xBFAE);
  c->d[1] = alu_swap(c, c->d[1]);                  /* swap d1 */
  lift_charge(x, 0xBFB2);
  lift_charge_divs(x, 0xBFB4, W(c->d[2]), c->d[1]);  /* divs.w d2,d1 */
  if (x->declined) return;
  c->d[1] = alu_divs(c, W(c->d[2]), c->d[1]);
  lift_w16(x, 0xFFFFB774, alu_movew(c, W(c->d[1])));  /* move.w d1,(B774).w */
  lift_charge(x, 0xBFB6);
  lift_charge(x, 0xBFBA);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE71C (jsr'd from sub_C566)
 *   in: a3 = on-ice object; $FFFFDA1A = an index
 * Overrides the approach-direction word at $FFFFBEDC: when the rink is
 * drawn un-flipped ($62(a3) bit7 clear), the $FFFFDA1A index is mapped
 * through the ROM word table at $FE744 first; flipped, the raw index is
 * stored. All of d0-d7/a0-a6 movem-saved/restored.
 */
void sub_FE71C(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                   /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFE71C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFDA1A)));  /* move.w (DA1A).w,d0 */
  lift_charge(x, 0xFE720);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);      /* btst #7,$62(a3) */
  lift_charge(x, 0xFE724);
  t = !c->zf;
  lift_charge_bcc(x, 0xFE72A, t);                  /* bne.w loc_FE73A */
  if (!t)
  {
    c->a[0] = 0xFE744;                             /* movea.l #unk_FE744,a0 */
    lift_charge(x, 0xFE72E);
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));  /* add.w d0,d0 */
    lift_charge(x, 0xFE734);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d0 */
    lift_charge(x, 0xFE736);
  }
  /* loc_FE73A */
  lift_w16(x, 0xFFFFBEDC, alu_movew(c, W(c->d[0])));  /* move.w d0,(BEDC).w */
  lift_charge(x, 0xFE73A);
  /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFE73E);
  lift_charge(x, 0xFE742);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_C644(lift_ctx *);  /* math.c */

/*
 * sub_C566 (called from sub_C2F2:loc_C30C — approach-direction picker)
 *   in: a3 = on-ice object
 * Picks the approach-direction word published to $FFFFBEDC: default 8
 * (none). Bails when $64(a3) bit3 is clear and $62(a3) bit3 is set.
 * Scans the 6 player slots of the team picked by $62(a3) bit6 (home
 *  $FFFFB04A / away +$300) for one whose $34 word is zero; if found,
 * computes its half-velocity-led position relative to the anchor at
 * $FFFFB74A/$FFFFB75E, the distance via Math_SqrtU32(dx²+dy²+1), and
 * runs sub_C644 twice (d3 = ±$12 lane offsets, d4 = ±$108 side pick
 * from $62(a3) bit7); when the summed quotients land in [-$2C,$2C] the
 * direction becomes 2 or 6 (by the side-corrected sign), else 0.
 * Finally, when $FFFFC2F2 bit2 or $FFFFC2FA bit0 says so, sub_FE71C
 * overrides the stored word entirely. d0-d5/a0 clobbered.
 */
void sub_C566(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  alu_btst(c, lift_r8(x, c->a[3] + 0x64), 3);      /* btst #3,$64(a3) */
  lift_charge(x, 0xC566);
  t = !c->zf;
  lift_charge_bcc(x, 0xC56C, t);                   /* bne.w loc_C57A */
  if (!t)
  {
    alu_btst(c, lift_r8(x, c->a[3] + 0x62), 3);    /* btst #3,$62(a3) */
    lift_charge(x, 0xC570);
    t = !c->zf;
    lift_charge_bcc(x, 0xC576, t);                 /* bne.w locret_C642 */
    if (t) goto L_ret;
  }
  /* loc_C57A */
  c->d[0] = alu_moveql(c, 8);                      /* moveq #8,d0 */
  lift_charge(x, 0xC57A);
  c->d[1] = alu_moveql(c, 5);                      /* moveq #5,d1 */
  lift_charge(x, 0xC57C);
  c->a[0] = 0xFFFFAFCA;                            /* movea.w #$AFCA,a0 */
  lift_charge(x, 0xC57E);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);      /* btst #6,$62(a3) */
  lift_charge(x, 0xC582);
  t = !c->zf;
  lift_charge_bcc(x, 0xC588, t);                   /* bne.w loc_C590 */
  if (!t)
  {
    c->a[0] += 0x300;                              /* adda.w #$300,a0 */
    lift_charge(x, 0xC58C);
  }
  for (;;)
  {
    /* loc_C590 */
    c->a[0] += 0x80;                               /* adda.w #$80,a0 */
    lift_charge(x, 0xC590);
    alu_tstw(c, lift_r16(x, c->a[0] + 0x34));      /* tst.w $34(a0) */
    lift_charge(x, 0xC594);
    int cond = c->zf, taken = 0, expd = 0;         /* dbeq d1,loc_C590 */
    if (!cond)
    {
      expd = (W(c->d[1]) == 0);
      setw(&c->d[1], W(c->d[1] - 1));
      taken = !expd;
    }
    lift_charge_dbcc(x, 0xC598, taken, expd);
    if (!taken) break;
  }
  t = !c->zf;
  lift_charge_bcc(x, 0xC59C, t);                   /* bne.w loc_C624 */
  if (!t)
  {
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 0x28)));  /* move.b $28(a0),d0 */
    lift_charge(x, 0xC5A0);
    setw(&c->d[0], alu_extw(c, c->d[0]));          /* ext.w d0 */
    lift_charge(x, 0xC5A4);
    setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));    /* asr.w #1,d0 */
    lift_charge(x, 0xC5A6);
    setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[0]), W(c->d[0])));  /* add.w (a0),d0 */
    lift_charge(x, 0xC5A8);
    setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFB74A), W(c->d[0])));  /* sub.w (B74A).w,d0 */
    lift_charge(x, 0xC5AA);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + 0x2A)));  /* move.b $2A(a0),d1 */
    lift_charge(x, 0xC5AE);
    setw(&c->d[1], alu_extw(c, c->d[1]));          /* ext.w d1 */
    lift_charge(x, 0xC5B2);
    setw(&c->d[1], alu_asrw(c, W(c->d[1]), 1));    /* asr.w #1,d1 */
    lift_charge(x, 0xC5B4);
    setw(&c->d[1], alu_addw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1])));  /* add.w $14(a0),d1 */
    lift_charge(x, 0xC5B6);
    setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB75E), W(c->d[1])));  /* sub.w (B75E).w,d1 */
    lift_charge(x, 0xC5BA);
    c->a[7] -= 4;                                  /* movem.w d0-d1,-(sp) */
    lift_w16(x, c->a[7], W(c->d[0]));
    lift_w16(x, c->a[7] + 2, W(c->d[1]));
    lift_charge_movem(x, 0xC5BE);
    lift_charge_muls(x, 0xC5C2, W(c->d[0]));       /* muls.w d0,d0 */
    c->d[0] = alu_muls(c, W(c->d[0]), W(c->d[0]));
    lift_charge_muls(x, 0xC5C4, W(c->d[1]));       /* muls.w d1,d1 */
    c->d[1] = alu_muls(c, W(c->d[1]), W(c->d[1]));
    c->d[0] = alu_addl(c, c->d[1], c->d[0]);       /* add.l d1,d0 */
    lift_charge(x, 0xC5C6);
    c->d[0] = alu_addl(c, 1, c->d[0]);             /* addq.l #1,d0 */
    lift_charge(x, 0xC5C8);
    lift_call(x, 0xC5CA, 4, Math_SqrtU32);         /* bsr.w sub_110BE */
    if (x->declined) return;
    setw(&c->d[2], alu_movew(c, W(c->d[0])));      /* move.w d0,d2 */
    lift_charge(x, 0xC5CE);
    /* movem.w (sp)+,d0-d1: sign-extends into the full register */
    c->d[0] = SEW(lift_r16(x, c->a[7]));
    c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
    c->a[7] += 4;
    lift_charge_movem(x, 0xC5D0);
    c->d[3] = alu_moveql(c, 0x12);                 /* moveq #$12,d3 */
    lift_charge(x, 0xC5D4);
    setw(&c->d[4], alu_movew(c, 0x108));           /* move.w #$108,d4 */
    lift_charge(x, 0xC5D6);
    alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);    /* btst #7,$62(a3) */
    lift_charge(x, 0xC5DA);
    t = !c->zf;
    lift_charge_bcc(x, 0xC5E0, t);                 /* bne.w loc_C5E6 */
    if (!t)
    {
      setw(&c->d[4], alu_negw(c, W(c->d[4])));     /* neg.w d4 */
      lift_charge(x, 0xC5E4);
    }
    /* loc_C5E6: movem.w d3-d4,-(sp) */
    c->a[7] -= 4;
    lift_w16(x, c->a[7], W(c->d[3]));
    lift_w16(x, c->a[7] + 2, W(c->d[4]));
    lift_charge_movem(x, 0xC5E6);
    lift_call(x, 0xC5EA, 4, sub_C644);             /* bsr.w sub_C644 */
    if (x->declined) return;
    setw(&c->d[5], alu_movew(c, W(c->d[4])));      /* move.w d4,d5 */
    lift_charge(x, 0xC5EE);
    /* movem.w (sp)+,d3-d4: sign-extends */
    c->d[3] = SEW(lift_r16(x, c->a[7]));
    c->d[4] = SEW(lift_r16(x, c->a[7] + 2));
    c->a[7] += 4;
    lift_charge_movem(x, 0xC5F0);
    setw(&c->d[3], alu_negw(c, W(c->d[3])));       /* neg.w d3 */
    lift_charge(x, 0xC5F4);
    lift_call(x, 0xC5F6, 4, sub_C644);             /* bsr.w sub_C644 */
    if (x->declined) return;
    setw(&c->d[4], alu_addw(c, W(c->d[5]), W(c->d[4])));  /* add.w d5,d4 */
    lift_charge(x, 0xC5FA);
    setw(&c->d[0], alu_movew(c, 0));               /* clr.w d0 */
    lift_charge(x, 0xC5FC);
    alu_cmpw(c, 0x2C, W(c->d[4]));                 /* cmp.w #$2C,d4 */
    lift_charge(x, 0xC5FE);
    t = (!c->zf && (c->nf == c->vf));              /* bgt.w loc_C624 */
    lift_charge_bcc(x, 0xC602, t);
    if (t) goto L_store;
    alu_cmpw(c, 0xFFD4, W(c->d[4]));               /* cmp.w #$FFD4,d4 */
    lift_charge(x, 0xC606);
    t = (c->nf != c->vf);                          /* blt.w loc_C624 */
    lift_charge_bcc(x, 0xC60A, t);
    if (t) goto L_store;
    alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);    /* btst #7,$62(a3) */
    lift_charge(x, 0xC60E);
    t = c->zf;
    lift_charge_bcc(x, 0xC614, t);                 /* beq.w loc_C61A */
    if (!t)
    {
      setw(&c->d[4], alu_negw(c, W(c->d[4])));     /* neg.w d4 */
      lift_charge(x, 0xC618);
    }
    /* loc_C61A */
    c->d[0] = alu_moveql(c, 2);                    /* moveq #2,d0 */
    lift_charge(x, 0xC61A);
    alu_tstw(c, W(c->d[4]));                       /* tst.w d4 */
    lift_charge(x, 0xC61C);
    t = !c->nf;
    lift_charge_bcc(x, 0xC61E, t);                 /* bpl.w loc_C624 */
    if (!t)
    {
      c->d[0] = alu_moveql(c, 6);                  /* moveq #6,d0 */
      lift_charge(x, 0xC622);
    }
  }
L_store:
  /* loc_C624 */
  lift_w16(x, 0xFFFFBEDC, alu_movew(c, W(c->d[0])));  /* move.w d0,(BEDC).w */
  lift_charge(x, 0xC624);
  alu_btst(c, lift_r8(x, 0xFFFFC2F2), 2);          /* btst #2,(C2F2).w */
  lift_charge(x, 0xC628);
  t = !c->zf;
  lift_charge_bcc(x, 0xC62E, t);                   /* bne.w loc_C63C */
  if (!t)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2FA), 0);        /* btst #0,(C2FA).w */
    lift_charge(x, 0xC632);
    t = c->zf;
    lift_charge_bcc(x, 0xC638, t);                 /* beq.w locret_C642 */
    if (t) goto L_ret;
  }
  /* loc_C63C */
  lift_call(x, 0xC63C, 6, sub_FE71C);              /* jsr sub_FE71C */
  if (x->declined) return;
L_ret:
  lift_charge(x, 0xC642);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE556 (called from sub_7CB0-398, sub_7A34+30 and others)
 * Publishes a cue byte to $FFFFD6C8: -1 when $FFFFC2EA bit4 is set;
 * otherwise clears $FFFFC2FE bit6 and looks up the ROM table at $FE5B0
 * with index $FFFFD6CA*6 + $FFFFD6CC — a zero entry falls back to a
 * random pick (Rng_NextScaled(8) & 7) from the 8-entry table at $FE658.
 * d0-d1/a0-a1 movem-saved/restored. Exact role TBD (the tables hold
 * small nonzero codes, cue/SFX-id shaped).
 */
void sub_FE556(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 16;                                   /* movem.l d0-d1/a0-a1,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0xFE556);
  alu_btst(c, lift_r8(x, 0xFFFFC2EA), 4);          /* btst #4,(C2EA).w */
  lift_charge(x, 0xFE55A);
  t = c->zf;
  lift_charge_bcc(x, 0xFE560, t);                  /* beq.w loc_FE56C */
  if (!t)
  {
    setw(&c->d[1], alu_movew(c, 0xFFFF));          /* move.w #$FFFF,d1 */
    lift_charge(x, 0xFE564);
    lift_charge(x, 0xFE568);                       /* bra.w loc_FE5A6 */
  }
  else
  {
    /* loc_FE56C: bclr #6,(C2FE).w */
    {
      uint32_t v = alu_bclr(c, lift_r8(x, 0xFFFFC2FE), 6);
      lift_w8(x, 0xFFFFC2FE, v);
    }
    lift_charge(x, 0xFE56C);
    c->a[0] = 0xFE5B0;                             /* movea.l #unk_FE5B0,a0 */
    lift_charge(x, 0xFE572);
    c->a[1] = 0xFE658;                             /* movea.l #unk_FE658,a1 */
    lift_charge(x, 0xFE578);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD6CA)));  /* move.w (D6CA).w,d0 */
    lift_charge(x, 0xFE57E);
    lift_charge_mulu(x, 0xFE582, 6);               /* mulu.w #6,d0 */
    c->d[0] = alu_mulu(c, 6, W(c->d[0]));
    setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFD6CC), W(c->d[0])));  /* add.w (D6CC).w,d0 */
    lift_charge(x, 0xFE586);
    setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
    lift_charge(x, 0xFE58A);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[0]))));  /* move.b (a0,d0.w),d1 */
    lift_charge(x, 0xFE58C);
    t = !c->zf;
    lift_charge_bcc(x, 0xFE590, t);                /* bne.w loc_FE5A6 */
    if (!t)
    {
      setw(&c->d[0], alu_movew(c, 8));             /* move.w #8,d0 */
      lift_charge(x, 0xFE594);
      lift_call(x, 0xFE598, 6, Rng_NextScaled);    /* jsr sub_11086 */
      if (x->declined) return;
      setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));  /* and.w #7,d0 */
      lift_charge(x, 0xFE59E);
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[0]))));  /* move.b (a1,d0.w),d1 */
      lift_charge(x, 0xFE5A2);
    }
  }
  /* loc_FE5A6 */
  lift_w16(x, 0xFFFFD6C8, alu_movew(c, W(c->d[1])));  /* move.w d1,(D6C8).w */
  lift_charge(x, 0xFE5A6);
  /* movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7]);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0xFE5AA);
  lift_charge(x, 0xFE5AE);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Object_FindOccupiedZoneBackward(lift_ctx *);  /* render.c */
void Rng_NextSignedOffset(lift_ctx *);             /* math.c */

/*
 * sub_128A4 (called from sub_7B30:loc_7C16, sub_1284A)
 * Clears the 32-word tracked-overlay table at $FFFFC3A4 (the
 * Overlay_ProcessTrackedEntries table). d0 = -1 and a0 = $FFFFC3E4 on
 * exit (no save/restore).
 */
void sub_128A4(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x1F);                   /* moveq #$1F,d0 */
  lift_charge(x, 0x128A4);
  c->a[0] = 0xFFFFC3A4;                            /* movea.w #$C3A4,a0 */
  lift_charge(x, 0x128A6);
  for (;;)
  {
    /* loc_128AA: clr.w (a0)+ */
    lift_w16(x, c->a[0], alu_movew(c, 0));
    c->a[0] += 2;
    lift_charge(x, 0x128AA);
    int expired = (W(c->d[0]) == 0);               /* dbf d0,loc_128AA */
    setw(&c->d[0], W(c->d[0] - 1));
    lift_charge_dbcc(x, 0x128AC, !expired, expired);
    if (expired) break;
  }
  lift_charge(x, 0x128B0);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1298C (bsr'd from sub_1284A, sub_128EC and others)
 *   in:  a0 = cursor into a negative-terminated byte list (one past the
 *        entry being removed)
 * Shifts every byte from (a0) onward down one position (through and
 * including the negative terminator), then steps a0 back one — i.e.
 * deletes the list entry at a0-1. d2 = count of non-negative bytes
 * moved; flags from the last (negative) byte copied.
 */
void sub_1298C(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int guard = 0;

  c->d[2] = alu_moveql(c, -1);                     /* moveq #-1,d2 */
  lift_charge(x, 0x1298C);
  for (;;)
  {
    /* loc_1298E */
    if (++guard > 512) { x->declined = 1; return; }   /* unterminated list */
    setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));    /* addq.w #1,d2 */
    lift_charge(x, 0x1298E);
    {                                              /* move.b (a0,d2.w),-1(a0,d2.w) */
      uint32_t v = alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[2])));
      lift_w8(x, c->a[0] + SW(c->d[2]) - 1, v);
    }
    lift_charge(x, 0x12990);
    int t = !c->nf;
    lift_charge_bcc(x, 0x12996, t);                /* bpl.s loc_1298E */
    if (!t) break;
  }
  c->a[0] -= 1;                                    /* subq.w #1,a0 */
  lift_charge(x, 0x12998);
  lift_charge(x, 0x1299A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1284A (called from sub_14620+39E — penalty-release / return-to-ice)
 *   in: a1 = one team block, a2 = the other ($24 = skater counts,
 *       $9A = negative-terminated pending-return byte list, $66+i =
 *       per-player status words)
 * When a1's team is back to 6 skaters, clears the $FFFFC3A4 overlay
 * table (sub_128A4). Then, while a2's count exceeds a1's, takes the
 * first pending-return entry whose status word bit6 is clear, zeroes
 * that word, deletes the list entry (sub_1298C), sets the dirty bits
 * $FFFFC300/$FFFFC6FE/$FFFFCA62 bit0, and bumps both counts. d0-d2/a0
 * movem-restored.
 */
void sub_1284A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 16;                                   /* movem.l d0-d2/a0,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_charge_movem(x, 0x1284A);
  alu_cmpw(c, 6, lift_r16(x, c->a[1] + 0x24));     /* cmp.w #6,$24(a1) */
  lift_charge(x, 0x1284E);
  t = !c->zf;
  lift_charge_bcc(x, 0x12854, t);                  /* bne.w loc_1285C */
  if (!t)
  {
    lift_call(x, 0x12858, 4, sub_128A4);           /* bsr.w sub_128A4 */
    if (x->declined) return;
  }
  /* loc_1285C */
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0x24)));  /* move.w $24(a2),d2 */
  lift_charge(x, 0x1285C);
  alu_cmpw(c, lift_r16(x, c->a[1] + 0x24), W(c->d[2]));  /* cmp.w $24(a1),d2 */
  lift_charge(x, 0x12860);
  t = (c->zf || (c->nf != c->vf));                 /* ble.w loc_1289E */
  lift_charge_bcc(x, 0x12864, t);
  if (!t)
  {
    c->a[0] = c->a[1] + 0x9A;                      /* lea $9A(a1),a0 */
    lift_charge(x, 0x12868);
    int guard = 0, found = 1;
    for (;;)
    {
      /* loc_1286C */
      if (++guard > 512) { x->declined = 1; return; }
      setw(&c->d[2], alu_movew(c, 0));             /* clr.w d2 */
      lift_charge(x, 0x1286C);
      setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,d2 */
      c->a[0] += 1;
      lift_charge(x, 0x1286E);
      t = c->nf;
      lift_charge_bcc(x, 0x12870, t);              /* bmi.w loc_1289E */
      if (t) { found = 0; break; }
      alu_btst(c, lift_r8(x, c->a[1] + 0x66 + SW(c->d[2])), 6);  /* btst #6,$66(a1,d2.w) */
      lift_charge(x, 0x12874);
      t = !c->zf;
      lift_charge_bcc(x, 0x1287A, t);              /* bne.s loc_1286C */
      if (!t) break;
    }
    if (found)
    {
      lift_w16(x, c->a[1] + 0x66 + SW(c->d[2]), alu_movew(c, 0));  /* clr.w $66(a1,d2.w) */
      lift_charge(x, 0x1287C);
      lift_call(x, 0x12880, 4, sub_1298C);         /* bsr.w sub_1298C */
      if (x->declined) return;
      {                                            /* bset #0,(C300).w */
        uint32_t v = alu_bset(c, lift_r8(x, 0xFFFFC300), 0);
        lift_w8(x, 0xFFFFC300, v);
      }
      lift_charge(x, 0x12884);
      {                                            /* addq.w #1,$24(a1) */
        uint32_t v = alu_addw(c, 1, lift_r16(x, c->a[1] + 0x24));
        lift_w16(x, c->a[1] + 0x24, v);
      }
      lift_charge(x, 0x1288A);
      {                                            /* addq.w #1,2(a2) */
        uint32_t v = alu_addw(c, 1, lift_r16(x, c->a[2] + 2));
        lift_w16(x, c->a[2] + 2, v);
      }
      lift_charge(x, 0x1288E);
      {                                            /* bset #0,(C6FE).w */
        uint32_t v = alu_bset(c, lift_r8(x, 0xFFFFC6FE), 0);
        lift_w8(x, 0xFFFFC6FE, v);
      }
      lift_charge(x, 0x12892);
      {                                            /* bset #0,(CA62).w */
        uint32_t v = alu_bset(c, lift_r8(x, 0xFFFFCA62), 0);
        lift_w8(x, 0xFFFFCA62, v);
      }
      lift_charge(x, 0x12898);
    }
  }
  /* loc_1289E: movem.l (sp)+,d0-d2/a0 */
  c->d[0] = lift_r32(x, c->a[7]);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x1289E);
  lift_charge(x, 0x128A2);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_12174 (jsr'd from sub_1211A)
 *   in:  d0.w = candidate zone/slot, a3 = an object ($66 read), a1
 *        clobbered-then-restored
 * When $FFFFD056 is zero: d1 = -1 (N=1 tells the caller "unavailable").
 * Otherwise sets $FFFFC2F2 bit3 and resolves the face-off(?) pair:
 * Object_FindOccupiedZoneBackward from slot 5 (d0 > 5) or 11, whose
 * failure (-1, N=1 surviving through the movem/bclr epilogue) makes
 * the caller retry; success publishes the block at $FFFFD404-$FFFFD40F
 * (selected-slot flag from $62 bit6, both players' $66 roster bytes,
 * a3's $66) and d1 = the found zone. d1-d3/a1 movem-restored.
 */
void sub_12174(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 16;                                   /* movem.l d1-d3/a1,-(sp) */
  lift_w32(x, c->a[7], c->d[1]);
  lift_w32(x, c->a[7] + 4, c->d[2]);
  lift_w32(x, c->a[7] + 8, c->d[3]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0x12174);
  alu_tstw(c, lift_r16(x, 0xFFFFD056));            /* tst.w (D056).w */
  lift_charge(x, 0x12178);
  t = c->zf;
  lift_charge_bcc(x, 0x1217C, t);                  /* beq.w loc_12216 */
  if (t)
  {
    /* loc_12216 */
    setw(&c->d[1], alu_movew(c, 0xFFFF));          /* move.w #$FFFF,d1 */
    lift_charge(x, 0x12216);
    lift_charge(x, 0x1221A);                       /* bra.s loc_12210 */
  }
  else
  {
    {                                              /* bset #3,(C2F2).w */
      uint32_t v = alu_bset(c, lift_r8(x, 0xFFFFC2F2), 3);
      lift_w8(x, 0xFFFFC2F2, v);
    }
    lift_charge(x, 0x12180);
    c->a[7] -= 2;                                  /* movem.w d0,-(sp) */
    lift_w16(x, c->a[7], W(c->d[0]));
    lift_charge_movem(x, 0x12186);
    setw(&c->d[1], alu_movew(c, 5));               /* move.w #5,d1 */
    lift_charge(x, 0x1218A);
    setw(&c->d[2], alu_movew(c, 0));               /* move.w #0,d2 */
    lift_charge(x, 0x1218E);
    alu_cmpw(c, 5, W(c->d[0]));                    /* cmp.w #5,d0 */
    lift_charge(x, 0x12192);
    t = (!c->zf && (c->nf == c->vf));              /* bgt.w loc_121A2 */
    lift_charge_bcc(x, 0x12196, t);
    if (!t)
    {
      setw(&c->d[1], alu_movew(c, 0xB));           /* move.w #$B,d1 */
      lift_charge(x, 0x1219A);
      setw(&c->d[2], alu_movew(c, 1));             /* move.w #1,d2 */
      lift_charge(x, 0x1219E);
    }
    /* loc_121A2 */
    setw(&c->d[0], alu_movew(c, W(c->d[1])));      /* move.w d1,d0 */
    lift_charge(x, 0x121A2);
    lift_call(x, 0x121A4, 6, Object_FindOccupiedZoneBackward);  /* jsr sub_B86A */
    if (x->declined) return;
    alu_tstw(c, W(c->d[0]));                       /* tst.w d0 */
    lift_charge(x, 0x121AA);
    t = !c->nf;
    lift_charge_bcc(x, 0x121AC, t);                /* bpl.w loc_121BE */
    if (!t)
    {
      /* movem.w (sp)+,d0: sign-extends */
      c->d[0] = SEW(lift_r16(x, c->a[7]));
      c->a[7] += 2;
      lift_charge_movem(x, 0x121B0);
      {                                            /* bclr #3,(C2F2).w — Z only, N survives */
        uint32_t v = alu_bclr(c, lift_r8(x, 0xFFFFC2F2), 3);
        lift_w8(x, 0xFFFFC2F2, v);
      }
      lift_charge(x, 0x121B4);
      lift_charge(x, 0x121BA);                     /* bra.w loc_12210 */
    }
    else
    {
      /* loc_121BE */
      setw(&c->d[1], alu_movew(c, W(c->d[0])));    /* move.w d0,d1 */
      lift_charge(x, 0x121BE);
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));  /* move.w (sp)+,d0 */
      c->a[7] += 2;
      lift_charge(x, 0x121C0);
      lift_w16(x, 0xFFFFD406, alu_movew(c, W(c->d[0])));  /* move.w d0,(D406).w */
      lift_charge(x, 0x121C2);
      c->a[1] = 0xFFFFB04A;                        /* movea.l #$FFFFB04A,a1 */
      lift_charge(x, 0x121C6);
      setw(&c->d[3], alu_movew(c, W(c->d[0])));    /* move.w d0,d3 */
      lift_charge(x, 0x121CC);
      setw(&c->d[3], alu_aslw(c, W(c->d[3]), 7));  /* asl.w #7,d3 */
      lift_charge(x, 0x121CE);
      lift_w16(x, 0xFFFFD404, alu_movew(c, 0));    /* move.w #0,(D404).w */
      lift_charge(x, 0x121D0);
      alu_btst(c, lift_r8(x, c->a[1] + SW(c->d[3]) + 0x62), 6);  /* btst #6,$62(a1,d3.w) */
      lift_charge(x, 0x121D6);
      t = c->zf;
      lift_charge_bcc(x, 0x121DC, t);              /* beq.w loc_121E6 */
      if (!t)
      {
        lift_w16(x, 0xFFFFD404, alu_movew(c, 1));  /* move.w #1,(D404).w */
        lift_charge(x, 0x121E0);
      }
      /* loc_121E6 */
      lift_w16(x, 0xFFFFD40A, alu_movew(c, 0));    /* move.w #0,(D40A).w */
      lift_charge(x, 0x121E6);
      {                                            /* move.b $66(a1,d3.w),(D40B).w */
        uint32_t v = alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[3]) + 0x66));
        lift_w8(x, 0xFFFFD40B, v);
      }
      lift_charge(x, 0x121EC);
      lift_w16(x, 0xFFFFD408, alu_movew(c, W(c->d[1])));  /* move.w d1,(D408).w */
      lift_charge(x, 0x121F2);
      setw(&c->d[1], alu_aslw(c, W(c->d[1]), 7));  /* asl.w #7,d1 */
      lift_charge(x, 0x121F6);
      lift_w16(x, 0xFFFFD40C, alu_movew(c, 0));    /* move.w #0,(D40C).w */
      lift_charge(x, 0x121F8);
      {                                            /* move.b $66(a1,d1.w),(D40D).w */
        uint32_t v = alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[1]) + 0x66));
        lift_w8(x, 0xFFFFD40D, v);
      }
      lift_charge(x, 0x121FE);
      lift_w16(x, 0xFFFFD40E, alu_movew(c, 0));    /* move.w #0,(D40E).w */
      lift_charge(x, 0x12204);
      {                                            /* move.b $66(a3),(D40F).w */
        uint32_t v = alu_moveb(c, lift_r8(x, c->a[3] + 0x66));
        lift_w8(x, 0xFFFFD40F, v);
      }
      lift_charge(x, 0x1220A);
    }
  }
  /* loc_12210: movem.l (sp)+,d1-d3/a1 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->d[3] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x12210);
  lift_charge(x, 0x12214);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1211A (called from sub_13CD8 at several sites)
 *   in:  d0.w = word index into the ROM table at $1913A, a2 = an object
 *        ($64 bit1, $52 camera zone read), a3 passed through to
 *        sub_12174
 *   out: d0 = the mapped face-off code (also stored to $FFFFD410), or
 *        the raw table word when negative, or d0 unchanged with
 *        d1 = -1 on the unavailable paths ($64(a2) bit1 clear with
 *        $FFFFC2F2 bit3 clear, or sub_12174 failure — which also
 *        clears $C2F2 bit3). d1-d3/a0 movem-restored.
 */
void sub_1211A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, avail;

  c->a[7] -= 16;                                   /* movem.l d1-d3/a0,-(sp) */
  lift_w32(x, c->a[7], c->d[1]);
  lift_w32(x, c->a[7] + 4, c->d[2]);
  lift_w32(x, c->a[7] + 8, c->d[3]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_charge_movem(x, 0x1211A);
  alu_btst(c, lift_r8(x, c->a[2] + 0x64), 1);      /* btst #1,$64(a2) */
  lift_charge(x, 0x1211E);
  t = c->zf;
  lift_charge_bcc(x, 0x12124, t);                  /* beq.w loc_12132 */
  avail = 0;
  if (!t)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2F2), 3);        /* btst #3,(C2F2).w */
    lift_charge(x, 0x12128);
    t = c->zf;
    lift_charge_bcc(x, 0x1212E, t);                /* beq.w loc_1213A */
    avail = t;
  }
  if (avail)
  {
    /* loc_1213A */
    c->a[0] = 0x1913A;                             /* movea.l #unk_1913A,a0 */
    lift_charge(x, 0x1213A);
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));  /* move.w (a0,d0.w),d1 */
    lift_charge(x, 0x12140);
    t = c->nf;
    lift_charge_bcc(x, 0x12144, t);                /* bmi.w loc_1216E */
    if (!t)
    {
      setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0x52)));  /* move.w $52(a2),d2 */
      lift_charge(x, 0x12148);
      c->a[7] -= 4;                                /* movem.w d0-d1,-(sp) */
      lift_w16(x, c->a[7], W(c->d[0]));
      lift_w16(x, c->a[7] + 2, W(c->d[1]));
      lift_charge_movem(x, 0x1214C);
      setw(&c->d[0], alu_movew(c, W(c->d[2])));    /* move.w d2,d0 */
      lift_charge(x, 0x12150);
      lift_call(x, 0x12152, 6, sub_12174);         /* jsr sub_12174 */
      if (x->declined) return;
      /* movem.w (sp)+,d0-d1: sign-extends; flags survive from the callee */
      c->d[0] = SEW(lift_r16(x, c->a[7]));
      c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
      c->a[7] += 4;
      lift_charge_movem(x, 0x12158);
      t = !c->nf;
      lift_charge_bcc(x, 0x1215C, t);              /* bpl.w loc_12168 */
      if (!t)
      {
        {                                          /* bclr #3,(C2F2).w */
          uint32_t v = alu_bclr(c, lift_r8(x, 0xFFFFC2F2), 3);
          lift_w8(x, 0xFFFFC2F2, v);
        }
        lift_charge(x, 0x12160);
        lift_charge(x, 0x12166);                   /* bra.s loc_12132 */
        setw(&c->d[1], alu_movew(c, 0xFFFF));      /* loc_12132: move.w #$FFFF,d1 */
        lift_charge(x, 0x12132);
        lift_charge(x, 0x12136);                   /* bra.w loc_1216E */
      }
      else
      {
        /* loc_12168 */
        lift_w16(x, 0xFFFFD410, alu_movew(c, W(c->d[1])));  /* move.w d1,(D410).w */
        lift_charge(x, 0x12168);
        setw(&c->d[0], alu_movew(c, W(c->d[1])));  /* move.w d1,d0 */
        lift_charge(x, 0x1216C);
      }
    }
  }
  else
  {
    /* loc_12132 */
    setw(&c->d[1], alu_movew(c, 0xFFFF));          /* move.w #$FFFF,d1 */
    lift_charge(x, 0x12132);
    lift_charge(x, 0x12136);                       /* bra.w loc_1216E */
  }
  /* loc_1216E: movem.l (sp)+,d1-d3/a0 */
  c->d[1] = lift_r32(x, c->a[7]);
  c->d[2] = lift_r32(x, c->a[7] + 4);
  c->d[3] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x1216E);
  lift_charge(x, 0x12172);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_14A54 (called from sub_14620+3C6)
 *   in: a2 = team block ($22 = first on-ice slot)
 * For each of the 6 on-ice slots that is active ($34 > 0) and not
 * penalty-flagged ($63 bit0): clears $62 bit2, clears $64 bit3 — when
 * bit3 was set, first runs Object_ResetAndQueueEvent — then
 * Object_RetreatStateMod8. a3 saved/restored; d3 = -1 on exit.
 */
void sub_14A54(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 4;                                    /* move.l a3,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[3]));
  lift_charge(x, 0x14A54);
  c->a[3] = SEW(lift_r16(x, c->a[2] + 0x22));      /* movea.w $22(a2),a3 */
  lift_charge(x, 0x14A56);
  c->d[3] = alu_moveql(c, 5);                      /* moveq #5,d3 */
  lift_charge(x, 0x14A5A);
  for (;;)
  {
    /* loc_14A5C */
    alu_tstw(c, lift_r16(x, c->a[3] + 0x34));      /* tst.w $34(a3) */
    lift_charge(x, 0x14A5C);
    t = (c->zf || (c->nf != c->vf));               /* ble.w loc_14A88 */
    lift_charge_bcc(x, 0x14A60, t);
    if (!t)
    {
      alu_btst(c, lift_r8(x, c->a[3] + 0x63), 0);  /* btst #0,$63(a3) */
      lift_charge(x, 0x14A64);
      t = !c->zf;
      lift_charge_bcc(x, 0x14A6A, t);              /* bne.w loc_14A88 */
      if (!t)
      {
        {                                          /* bclr #2,$62(a3) */
          uint32_t v = alu_bclr(c, lift_r8(x, c->a[3] + 0x62), 2);
          lift_w8(x, c->a[3] + 0x62, v);
        }
        lift_charge(x, 0x14A6E);
        {                                          /* bclr #3,$64(a3) */
          uint32_t v = alu_bclr(c, lift_r8(x, c->a[3] + 0x64), 3);
          lift_w8(x, c->a[3] + 0x64, v);
        }
        lift_charge(x, 0x14A74);
        t = c->zf;                                 /* beq.w loc_14A84: bit was clear */
        lift_charge_bcc(x, 0x14A7A, t);
        if (!t)
        {
          lift_call(x, 0x14A7E, 6, Object_ResetAndQueueEvent);  /* jsr sub_FEFF0 */
          if (x->declined) return;
        }
        /* loc_14A84 */
        lift_call(x, 0x14A84, 4, Object_RetreatStateMod8);  /* bsr.w sub_10658 */
        if (x->declined) return;
      }
    }
    /* loc_14A88 */
    c->a[3] += 0x80;                               /* adda.w #$80,a3 */
    lift_charge(x, 0x14A88);
    int expired = (W(c->d[3]) == 0);               /* dbf d3,loc_14A5C */
    setw(&c->d[3], W(c->d[3] - 1));
    lift_charge_dbcc(x, 0x14A8C, !expired, expired);
    if (expired) break;
  }
  c->a[3] = lift_r32(x, c->a[7]);                  /* movea.l (sp)+,a3 */
  c->a[7] += 4;
  lift_charge(x, 0x14A90);
  lift_charge(x, 0x14A92);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_131F4 (called from sub_7CB0-4E4; sibling of sub_1323E)
 * Bails via the shared far rts when $FFFFCEEC > 1. Otherwise rolls d1
 * via Calc_HalvingAccumulator and walks the $FFFFCE66 16-byte-stride
 * structs downward from index d1 to 0: each one that isn't the
 * $FFFFCEE6 index and doesn't have $E bit2 set gets its +8 word
 * cleared and sub_13276 run Rng_NextScaled(3) times. Same
 * `x->declined` guard as sub_1323E against Calc_HalvingAccumulator's
 * documented d1 == -1 degenerate output (would wrap the outer dbf).
 */
void sub_131F4(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  alu_cmpw(c, 1, lift_r16(x, 0xFFFFCEEC));         /* cmp.w #1,(CEEC).w */
  lift_charge(x, 0x131F4);
  t = (!c->zf && (c->nf == c->vf));                /* bgt.w locret_15464 */
  lift_charge_bcc(x, 0x131FA, t);
  if (t)
  {
    lift_charge(x, 0x15464);                       /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_call(x, 0x131FE, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */
  if (x->declined) return;
  if (W(c->d[1]) == 0xFFFFu) { x->declined = 1; return; }  /* dbf-wrap guard */
  c->a[0] = 0xFFFFCE66;                            /* movea.w #$CE66,a0 */
  lift_charge(x, 0x13202);
  c->d[0] = alu_moveql(c, 0x10);                   /* moveq #$10,d0 */
  lift_charge(x, 0x13206);
  c->d[0] = alu_mulu(c, W(c->d[1]), W(c->d[0]));   /* mulu.w d1,d0 */
  lift_charge_mulu(x, 0x13208, W(c->d[1]));
  c->a[0] += SEW(c->d[0]);                         /* adda.w d0,a0 */
  lift_charge(x, 0x1320A);
  for (;;)
  {
    /* loc_1320C */
    alu_cmpw(c, lift_r16(x, 0xFFFFCEE6), W(c->d[1]));  /* cmp.w (CEE6).w,d1 */
    lift_charge(x, 0x1320C);
    t = c->zf;
    lift_charge_bcc(x, 0x13210, t);                /* beq.w loc_13234 */
    if (!t)
    {
      alu_btst(c, lift_r8(x, c->a[0] + 0xE), 2);   /* btst #2,$E(a0) */
      lift_charge(x, 0x13214);
      t = !c->zf;
      lift_charge_bcc(x, 0x1321A, t);              /* bne.w loc_13234 */
      if (!t)
      {
        lift_w16(x, c->a[0] + 8, alu_movew(c, 0)); /* clr.w 8(a0) */
        lift_charge(x, 0x1321E);
        c->d[0] = alu_moveql(c, 3);                /* moveq #3,d0 */
        lift_charge(x, 0x13222);
        lift_call(x, 0x13224, 4, Rng_NextScaled);  /* bsr.w sub_11086 */
        if (x->declined) return;
        lift_charge(x, 0x13228);                   /* bra.w loc_13230 */
        for (;;)
        {
          /* loc_13230: dbf d0,loc_1322C */
          int expired = (W(c->d[0]) == 0);
          setw(&c->d[0], W(c->d[0] - 1));
          lift_charge_dbcc(x, 0x13230, !expired, expired);
          if (expired) break;
          lift_call(x, 0x1322C, 4, sub_13276);     /* bsr.w sub_13276 */
          if (x->declined) return;
        }
      }
    }
    /* loc_13234 */
    c->a[0] -= 0x10;                               /* suba.w #$10,a0 */
    lift_charge(x, 0x13234);
    int expired = (W(c->d[1]) == 0);               /* dbf d1,loc_1320C */
    setw(&c->d[1], W(c->d[1] - 1));
    lift_charge_dbcc(x, 0x13238, !expired, expired);
    if (expired) break;
  }
  lift_charge(x, 0x1323C);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F70A2 (called from sub_7CB0-50C/-500 and others)
 *   in: a0 = block base (a2-team-block shaped: writes land at +$1A2)
 * Fills the $1A0-byte region at a0+$1A2 backward (index $19F down to 0)
 * with independent Rng_NextSignedOffset(9) draws — i.e. random bytes in
 * [-9,9). The per-player rating-adjustment block sub_FA9F8 reads.
 * d0-d7 movem-restored; a0 re-pushed/popped around every store.
 */
void sub_F70A2(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  c->a[7] -= 32;                                   /* movem.l d0-d7,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  lift_charge_movem(x, 0xF70A2);
  setw(&c->d[1], alu_movew(c, 0x19F));             /* move.w #$19F,d1 */
  lift_charge(x, 0xF70A6);
  for (;;)
  {
    /* loc_F70AA */
    setw(&c->d[0], alu_movew(c, 9));               /* move.w #9,d0 */
    lift_charge(x, 0xF70AA);
    lift_call(x, 0xF70AE, 6, Rng_NextSignedOffset);  /* jsr sub_1107A */
    if (x->declined) return;
    c->a[7] -= 4;                                  /* move.l a0,-(sp) */
    lift_w32(x, c->a[7], alu_movel(c, c->a[0]));
    lift_charge(x, 0xF70B4);
    c->a[0] += 0x1A2;                              /* adda.l #$1A2,a0 */
    lift_charge(x, 0xF70B6);
    lift_w8(x, c->a[0] + SW(c->d[1]), alu_moveb(c, c->d[0]));  /* move.b d0,(a0,d1.w) */
    lift_charge(x, 0xF70BC);
    c->a[0] = lift_r32(x, c->a[7]);                /* movea.l (sp)+,a0 */
    c->a[7] += 4;
    lift_charge(x, 0xF70C0);
    int expired = (W(c->d[1]) == 0);               /* dbf d1,loc_F70AA */
    setw(&c->d[1], W(c->d[1] - 1));
    lift_charge_dbcc(x, 0xF70C2, !expired, expired);
    if (expired) break;
  }
  /* movem.l (sp)+,d0-d7 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  c->a[7] += 32;
  lift_charge_movem(x, 0xF70C6);
  lift_charge(x, 0xF70CA);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_C67A (jsr'd from many gameplay sites; grandparent-return)
 *   in: a3 = on-ice object
 *   Commit a pending animation-frame change for the object. Bails via
 *   the bare rts at $C678 when the commit gate ($FFFFC2F2 bit 2) is
 *   set, when $62(a3) bit 3 / $63(a3) bit 4 are set, when both $60/$61
 *   are negative (nothing pending), or — on the changed path — when the
 *   state byte $38(a3,$36(a3).w) is $B or the object sits in the
 *   camera zone cached at $FFFFB7AA.
 *   Both success paths pop the CALLER's return address (addq #4,sp) and
 *   leave through a tail call, so they return to the GRANDPARENT:
 *   - $61(a3) != $66(a3) (pending differs from current): set $63 bit 2,
 *     clear $40(a3), and queue frame code $B via Object_RingBufferWriteByte.
 *   - equal: clear $63/$62 bit 2, set $60/$61 to $FF (consumed), reload
 *     d0 from $34(a3), and re-queue via Object_QueueFrameFromTable.
 *     (The bpl at $C6F6 tests the $FF just stored — statically never
 *     taken; the loc_C700 path is modeled anyway.)
 */
void sub_C67A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];
  int t;

  alu_btst(c, lift_r8(x, 0xFFFFC2F2u), 2);         /* btst #2,($FFFFC2F2).w */
  lift_charge(x, 0xC67A);
  t = !c->zf;                                      /* bne.s locret_C678 */
  lift_charge_bcc(x, 0xC680, t);
  if (t) goto bail;

  alu_btst(c, lift_r8(x, a3 + 0x62), 3);           /* btst #3,$62(a3) */
  lift_charge(x, 0xC682);
  t = !c->zf;                                      /* bne.s locret_C678 */
  lift_charge_bcc(x, 0xC688, t);
  if (t) goto bail;

  alu_btst(c, lift_r8(x, a3 + 0x63), 4);           /* btst #4,$63(a3) */
  lift_charge(x, 0xC68A);
  t = !c->zf;                                      /* bne.s locret_C678 */
  lift_charge_bcc(x, 0xC690, t);
  if (t) goto bail;

  alu_tstb(c, lift_r8(x, a3 + 0x60));              /* tst.b $60(a3) */
  lift_charge(x, 0xC692);
  t = !c->nf;                                      /* bpl.w loc_C6A0 */
  lift_charge_bcc(x, 0xC696, t);
  if (!t)
  {
    alu_tstb(c, lift_r8(x, a3 + 0x61));            /* tst.b $61(a3) */
    lift_charge(x, 0xC69A);
    t = c->nf;                                     /* bmi.s locret_C678 */
    lift_charge_bcc(x, 0xC69E, t);
    if (t) goto bail;
  }

  /* loc_C6A0 */
  setb(&c->d[0], alu_moveb(c, lift_r8(x, a3 + 0x61)));     /* move.b $61(a3),d0 */
  lift_charge(x, 0xC6A0);
  alu_cmpb(c, lift_r8(x, a3 + 0x66), c->d[0]);     /* cmp.b $66(a3),d0 */
  lift_charge(x, 0xC6A4);
  t = c->zf;                                       /* beq.w loc_C6D8 */
  lift_charge_bcc(x, 0xC6A8, t);
  if (!t)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x36)));  /* move.w $36(a3),d0 */
    lift_charge(x, 0xC6AC);
    alu_cmpb(c, 0x0B, lift_r8(x, a3 + 0x38 + SW(c->d[0])));/* cmpi.b #$B,$38(a3,d0.w) */
    lift_charge(x, 0xC6B0);
    t = c->zf;                                     /* beq.s locret_C678 */
    lift_charge_bcc(x, 0xC6B6, t);
    if (t) goto bail;
    setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x52)));  /* move.w $52(a3),d0 */
    lift_charge(x, 0xC6B8);
    alu_cmpw(c, lift_r16(x, 0xFFFFB7AAu), W(c->d[0]));     /* cmp.w ($FFFFB7AA).w,d0 */
    lift_charge(x, 0xC6BC);
    t = c->zf;                                     /* beq.s locret_C678 */
    lift_charge_bcc(x, 0xC6C0, t);
    if (t) goto bail;

    c->a[7] += 4;                                  /* addq.w #4,sp: grandparent return */
    lift_charge(x, 0xC6C2);
    lift_w8(x, a3 + 0x63, alu_bset(c, lift_r8(x, a3 + 0x63), 2)); /* bset #2,$63(a3) */
    lift_charge(x, 0xC6C4);
    lift_w16(x, a3 + 0x40, alu_movew(c, 0));       /* clr.w $40(a3) */
    lift_charge(x, 0xC6CA);
    c->d[0] = alu_movel(c, 0x0B);                  /* move.l #$B,d0 */
    lift_charge(x, 0xC6CE);
    lift_charge(x, 0xC6D4);                        /* bra.w sub_10662 (tail) */
    Object_RingBufferWriteByte(x);                 /* its rts returns to grandparent */
    return;
  }

  /* loc_C6D8 */
  c->a[7] += 4;                                    /* addq.w #4,sp: grandparent return */
  lift_charge(x, 0xC6D8);
  lift_w8(x, a3 + 0x63, alu_bclr(c, lift_r8(x, a3 + 0x63), 2));   /* bclr #2,$63(a3) */
  lift_charge(x, 0xC6DA);
  lift_w8(x, a3 + 0x62, alu_bclr(c, lift_r8(x, a3 + 0x62), 2));   /* bclr #2,$62(a3) */
  lift_charge(x, 0xC6E0);
  lift_w8(x, a3 + 0x61, 0xFF);                     /* st $61(a3): no flags */
  lift_charge(x, 0xC6E6);
  lift_w8(x, a3 + 0x60, 0xFF);                     /* st $60(a3) */
  lift_charge(x, 0xC6EA);
  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x34)));    /* move.w $34(a3),d0 */
  lift_charge(x, 0xC6EE);
  alu_tstb(c, lift_r8(x, a3 + 0x60));              /* tst.b $60(a3) — the $FF just stored */
  lift_charge(x, 0xC6F2);
  t = !c->nf;                                      /* bpl.w loc_C700 */
  lift_charge_bcc(x, 0xC6F6, t);
  if (!t)
  {
    lift_charge(x, 0xC6FA);                        /* jmp sub_15A88 (tail) */
    Object_QueueFrameFromTable(x);
    return;
  }
  /* loc_C700 */
  setb(&c->d[0], alu_moveb(c, lift_r8(x, a3 + 0x60)));     /* move.b $60(a3),d0 */
  lift_charge(x, 0xC700);
  setw(&c->d[0], alu_extw(c, c->d[0]));            /* ext.w d0 */
  lift_charge(x, 0xC704);
  lift_w16(x, a3 + 0x34, alu_movew(c, W(c->d[0])));/* move.w d0,$34(a3) */
  lift_charge(x, 0xC706);
  lift_charge(x, 0xC70A);                          /* jmp sub_15A88 (tail) */
  Object_QueueFrameFromTable(x);
  return;

bail:
  lift_charge(x, 0xC678);                          /* locret_C678: bare rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_F6C6(lift_ctx *x);  /* forward: defined below */

/*
 * sub_F6AA (called from sub_799E)
 *   Run the sub_F6C6 line-change check for both teams: bails when
 *   $FFFFC2EA bit 0 is set; otherwise a2 = home team block ($C6CE),
 *   a1 = away block, d0 = 1, bsr sub_F6C6, then d0 = 2, exg a1/a2 and
 *   FALLS THROUGH into sub_F6C6 for the away side (its exit — shared
 *   far rts or the sub_15788 tail — returns to this routine's caller).
 */
void sub_F6AA(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t tmp;
  int t;

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 0);         /* btst #0,($FFFFC2EA).w */
  lift_charge(x, 0xF6AA);
  t = !c->zf;                                      /* bne.w locret_15464 */
  lift_charge_bcc(x, 0xF6B0, t);
  if (t)
  {
    lift_charge(x, 0x15464);                       /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  c->a[2] = SEW(0xC6CE);                           /* movea.w #$C6CE,a2 (sign-extends) */
  lift_charge(x, 0xF6B4);
  c->a[1] = c->a[2] + 0x364;                       /* lea $364(a2),a1 */
  lift_charge(x, 0xF6B8);
  c->d[0] = alu_moveql(c, 1);                      /* moveq #1,d0 */
  lift_charge(x, 0xF6BC);
  lift_call(x, 0xF6BE, 4, sub_F6C6);               /* bsr.w sub_F6C6 */
  c->d[0] = alu_moveql(c, 2);                      /* moveq #2,d0 */
  lift_charge(x, 0xF6C2);
  tmp = c->a[1]; c->a[1] = c->a[2]; c->a[2] = tmp; /* exg a1,a2: no flags */
  lift_charge(x, 0xF6C4);
  sub_F6C6(x);                                     /* falls through into sub_F6C6 */
}

/*
 * sub_F6C6 (called from sub_F6AA; tail into sub_15788)
 *   in: a2 = team block under test, a1 = the other team block,
 *       d0 = side code (1 home / 2 away), a0 = a player object on some
 *       paths (only its $62 bit 7 is peeked, via $22(a2))
 *   Line-change eligibility check for one team. Bails via the shared
 *   far rts $15464 unless: the team's $26 word is >= 0, the camera zone
 *   word $FFFFB7AA is >= 0, and (zone-6, bit-NOT'd for the away block)
 *   is negative. Then either (a) $FFFFC2EA bit 3 set: mark $26(a2) and
 *   tail into sub_15788, or (b) d0 matches neither $FFFFC328/$C32A
 *   selection cache, $FFFFC466 == 2, $C(a1)-$C(a2) == 2, $FFFFC468
 *   <= $3C, and d1 (= $FFFFB75E, negated unless the $22(a2) player's
 *   $62 bit 7 is set) is negative: mark $26(a2) and tail into
 *   sub_15788. The loc_F712/loc_F724 island inside this routine's IDA
 *   boundary is a separate, externally jsr'd entry point — unreachable
 *   from this entry and not part of this lift.
 */
void sub_F6C6(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a2 = c->a[2];
  int t;

  alu_tstw(c, lift_r16(x, a2 + 0x26));             /* tst.w $26(a2) */
  lift_charge(x, 0xF6C6);
  t = c->nf;                                       /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0xF6CA, t);
  if (t) goto bail;
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB7AAu))); /* move.w ($FFFFB7AA).w,d1 */
  lift_charge(x, 0xF6CE);
  t = c->nf;                                       /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0xF6D2, t);
  if (t) goto bail;
  setw(&c->d[1], alu_subw(c, 6, W(c->d[1])));      /* subq.w #6,d1 */
  lift_charge(x, 0xF6D6);
  alu_cmpl(c, SEW(0xC6CE), c->a[2]);               /* cmpa.w #$C6CE,a2 */
  lift_charge(x, 0xF6D8);
  t = c->zf;                                       /* beq.w loc_F6E2 */
  lift_charge_bcc(x, 0xF6DC, t);
  if (!t)
  {
    setw(&c->d[1], alu_notw(c, W(c->d[1])));       /* not.w d1 */
    lift_charge(x, 0xF6E0);
  }
  /* loc_F6E2 */
  alu_tstw(c, W(c->d[1]));                         /* tst.w d1 */
  lift_charge(x, 0xF6E2);
  t = !c->nf;                                      /* bpl.w locret_15464 */
  lift_charge_bcc(x, 0xF6E4, t);
  if (t) goto bail;
  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 3);         /* btst #3,($FFFFC2EA).w */
  lift_charge(x, 0xF6E8);
  t = c->zf;                                       /* beq.w loc_F6FA */
  lift_charge_bcc(x, 0xF6EE, t);
  if (!t)
  {
    lift_w8(x, a2 + 0x26, 0xFF);                   /* st $26(a2): no flags */
    lift_charge(x, 0xF6F2);
    lift_charge(x, 0xF6F6);                        /* bra.w sub_15788 (tail) */
    sub_15788(x);
    return;
  }
  /* loc_F6FA */
  alu_cmpw(c, lift_r16(x, 0xFFFFC328u), W(c->d[0]));  /* cmp.w ($FFFFC328).w,d0 */
  lift_charge(x, 0xF6FA);
  t = c->zf;                                       /* beq.w locret_15464 */
  lift_charge_bcc(x, 0xF6FE, t);
  if (t) goto bail;
  alu_cmpw(c, lift_r16(x, 0xFFFFC32Au), W(c->d[0]));  /* cmp.w ($FFFFC32A).w,d0 */
  lift_charge(x, 0xF702);
  t = c->zf;                                       /* beq.w locret_15464 */
  lift_charge_bcc(x, 0xF706, t);
  if (t) goto bail;
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB75Eu))); /* move.w ($FFFFB75E).w,d1 */
  lift_charge(x, 0xF70A);
  lift_charge(x, 0xF70E);                          /* bra.w loc_F746 */
  /* loc_F746 */
  alu_cmpw(c, 2, lift_r16(x, 0xFFFFC466u));        /* cmpi.w #2,($FFFFC466).w */
  lift_charge(x, 0xF746);
  t = !c->zf;                                      /* bne.w locret_15464 */
  lift_charge_bcc(x, 0xF74C, t);
  if (t) goto bail;
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0xC)));  /* move.w $C(a1),d0 */
  lift_charge(x, 0xF750);
  setw(&c->d[0], alu_subw(c, lift_r16(x, a2 + 0xC), W(c->d[0])));  /* sub.w $C(a2),d0 */
  lift_charge(x, 0xF754);
  t = c->nf;                                       /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0xF758, t);
  if (t) goto bail;
  alu_cmpw(c, 2, W(c->d[0]));                      /* cmp.w #2,d0 */
  lift_charge(x, 0xF75C);
  t = !c->zf;                                      /* bne.w locret_15464 */
  lift_charge_bcc(x, 0xF760, t);
  if (t) goto bail;
  alu_cmpw(c, 0x3C, lift_r16(x, 0xFFFFC468u));     /* cmpi.w #$3C,($FFFFC468).w */
  lift_charge(x, 0xF764);
  t = (!c->zf && c->nf == c->vf);                  /* bgt.w locret_15464 */
  lift_charge_bcc(x, 0xF76A, t);
  if (t) goto bail;
  c->a[7] -= 4;                                    /* move.l a0,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[0]));
  lift_charge(x, 0xF76E);
  c->a[0] = SEW(lift_r16(x, a2 + 0x22));           /* movea.w $22(a2),a0 (sign-extends) */
  lift_charge(x, 0xF770);
  alu_btst(c, lift_r8(x, c->a[0] + 0x62), 7);      /* btst #7,$62(a0) */
  lift_charge(x, 0xF774);
  c->a[0] = lift_r32(x, c->a[7]);                  /* movea.l (sp)+,a0: no flags */
  c->a[7] += 4;
  lift_charge(x, 0xF77A);
  t = !c->zf;                                      /* bne.w loc_F782 */
  lift_charge_bcc(x, 0xF77C, t);
  if (!t)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));       /* neg.w d1 */
    lift_charge(x, 0xF780);
  }
  /* loc_F782 */
  alu_tstw(c, W(c->d[1]));                         /* tst.w d1 */
  lift_charge(x, 0xF782);
  t = c->nf;                                       /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0xF784, t);
  if (t) goto bail;
  lift_w8(x, a2 + 0x26, 0xFF);                     /* st $26(a2) */
  lift_charge(x, 0xF788);
  lift_charge(x, 0xF78C);                          /* bra.w sub_15788 (tail) */
  sub_15788(x);
  return;

bail:
  lift_charge(x, 0x15464);                         /* shared far rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FF9A8 (jmp'd from sub_BFBC; FUNCTION CHUNK at $C0AE)
 *   in: d4 = selection-table byte index (0 home / 2 away), d6 = current
 *       best zone (kept when no candidate beats it), a3 = object whose
 *       $62 bit 5 the $C0AE chunk sets
 *   Find the eligible player slot nearest the puck and make it the
 *   selection. Computes the puck's world point from $FFFFB772/$B774
 *   (velocity >> 8 added onto $FFFFB74A/$B75E), then scans the 6 player
 *   slots of the d4 team (base $FFFFB04A, +$300 unless (C328+d4) == 1,
 *   stride $80): a slot must have $34 > 0, $63 bit 2 clear, not be the
 *   already-selected zone unless $62 bit 3 clear, pass the goalie
 *   filters ($FFFFD406/$D408) when $FFFFC2F2 bit 2 is set, and have
 *   $62 bit 5 clear; the squared distance (muls dx,dx + muls dy,dy)
 *   must beat the running best (d5, seeded -1) and its zone must not
 *   equal the (a1,d3=d4^2) opposite-selection entry. The winner's
 *   distance/zone land in d5/d6.
 *   Exits by pushing the $FFAA8 epilogue address (pea) and tail-jumping:
 *   d6 == current selection -> chunk $C0AE (set $62(a3) bit 5, d1=$B24,
 *   Anim_SetScript); else d0=d6 and sub_C0BC (d4==0) / sub_C0DA — each
 *   returns to $FFAA8, which pops the movem'd d0-d6/a0-a1 and rts's.
 */
void sub_FF9A8(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, i;

  alu_btst(c, lift_r8(x, 0xFFFFC2F6u), 6);         /* btst #6,($FFFFC2F6).w */
  lift_charge(x, 0xFF9A8);
  t = !c->zf;                                      /* bne.w locret_FFAAC */
  lift_charge_bcc(x, 0xFF9AE, t);
  if (t)
  {
    lift_charge(x, 0xFFAAC);                       /* locret_FFAAC: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* movem.l d0-d6/a0-a1,-(sp) */
  c->a[7] -= 36;
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 4u * i, c->d[i]);
  lift_w32(x, c->a[7] + 28, c->a[0]);
  lift_w32(x, c->a[7] + 32, c->a[1]);
  lift_charge_movem(x, 0xFF9B2);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB772u))); /* move.w ($FFFFB772).w,d0 */
  lift_charge(x, 0xFF9B6);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 8));      /* asr.w #8,d0 */
  lift_charge(x, 0xFF9BA);
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFB74Au), W(c->d[0]))); /* add.w ($B74A).w,d0 */
  lift_charge(x, 0xFF9BC);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB774u))); /* move.w ($FFFFB774).w,d1 */
  lift_charge(x, 0xFF9C0);
  setw(&c->d[1], alu_asrw(c, W(c->d[1]), 8));      /* asr.w #8,d1 */
  lift_charge(x, 0xFF9C4);
  setw(&c->d[1], alu_addw(c, lift_r16(x, 0xFFFFB75Eu), W(c->d[1]))); /* add.w ($B75E).w,d1 */
  lift_charge(x, 0xFF9C6);

  /* movem.w d0-d1,-(sp): the puck point, re-read each loop pass */
  c->a[7] -= 4;
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0xFF9CA);

  c->d[2] = alu_moveql(c, 5);                      /* moveq #5,d2 */
  lift_charge(x, 0xFF9CE);
  setw(&c->d[3], alu_movew(c, W(c->d[4])));        /* move.w d4,d3 */
  lift_charge(x, 0xFF9D0);
  setw(&c->d[3], alu_eorw(c, 2, W(c->d[3])));      /* eori.w #2,d3 */
  lift_charge(x, 0xFF9D2);
  c->d[5] = alu_moveql(c, -1);                     /* moveq #-1,d5 */
  lift_charge(x, 0xFF9D6);
  c->a[0] = SEW(0xB04A);                           /* movea.w #$B04A,a0 */
  lift_charge(x, 0xFF9D8);
  c->a[1] = SEW(0xC328);                           /* movea.w #$C328,a1 */
  lift_charge(x, 0xFF9DC);
  alu_cmpw(c, 1, lift_r16(x, c->a[1] + SW(c->d[4]))); /* cmpi.w #1,(a1,d4.w) */
  lift_charge(x, 0xFF9E0);
  t = c->zf;                                       /* beq.w loc_FF9EE */
  lift_charge_bcc(x, 0xFF9E6, t);
  if (!t)
  {
    c->a[0] += 0x300;                              /* adda.w #$300,a0 */
    lift_charge(x, 0xFF9EA);
  }
  c->a[1] = SEW(0xC320);                           /* movea.w #$C320,a1 */
  lift_charge(x, 0xFF9EE);

  for (;;)
  {
    /* loc_FF9F2 — one player slot */
    alu_tstw(c, lift_r16(x, c->a[0] + 0x34));      /* tst.w $34(a0) */
    lift_charge(x, 0xFF9F2);
    t = (c->zf || c->nf != c->vf);                 /* ble.w loc_FFA84 */
    lift_charge_bcc(x, 0xFF9F6, t);
    if (t) goto next;
    alu_btst(c, lift_r8(x, c->a[0] + 0x63), 2);    /* btst #2,$63(a0) */
    lift_charge(x, 0xFF9FA);
    t = !c->zf;                                    /* bne.w loc_FFA84 */
    lift_charge_bcc(x, 0xFFA00, t);
    if (t) goto next;

    c->a[7] -= 2;                                  /* movem.w d1,-(sp) */
    lift_w16(x, c->a[7], W(c->d[1]));
    lift_charge_movem(x, 0xFFA04);
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0x52))); /* move.w $52(a0),d1 */
    lift_charge(x, 0xFFA08);
    alu_cmpw(c, lift_r16(x, c->a[1] + SW(c->d[4])), W(c->d[1])); /* cmp.w (a1,d4.w),d1 */
    lift_charge(x, 0xFFA0C);
    c->d[1] = SEW(lift_r16(x, c->a[7]));           /* movem.w (sp)+,d1: sign-extends */
    c->a[7] += 2;
    lift_charge_movem(x, 0xFFA10);
    t = c->zf;                                     /* beq.w loc_FFA22 */
    lift_charge_bcc(x, 0xFFA14, t);
    if (!t)
    {
      alu_btst(c, lift_r8(x, c->a[0] + 0x62), 3);  /* btst #3,$62(a0) */
      lift_charge(x, 0xFFA18);
      t = !c->zf;                                  /* bne.w loc_FFA84 */
      lift_charge_bcc(x, 0xFFA1E, t);
      if (t) goto next;
    }
    /* loc_FFA22 */
    alu_btst(c, lift_r8(x, 0xFFFFC2F2u), 2);       /* btst #2,($FFFFC2F2).w */
    lift_charge(x, 0xFFA22);
    t = c->zf;                                     /* beq.w loc_FFA54 */
    lift_charge_bcc(x, 0xFFA28, t);
    if (!t)
    {
      c->a[7] -= 4;                                /* movem.l d0,-(sp) */
      lift_w32(x, c->a[7], c->d[0]);
      lift_charge_movem(x, 0xFFA2C);
      setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD406u))); /* move.w ($D406).w,d0 */
      lift_charge(x, 0xFFA30);
      alu_cmpw(c, lift_r16(x, c->a[0] + 0x52), W(c->d[0]));   /* cmp.w $52(a0),d0 */
      lift_charge(x, 0xFFA34);
      c->d[0] = lift_r32(x, c->a[7]);              /* movem.l (sp)+,d0 */
      c->a[7] += 4;
      lift_charge_movem(x, 0xFFA38);
      t = c->zf;                                   /* beq.w loc_FFA54 */
      lift_charge_bcc(x, 0xFFA3C, t);
      if (!t)
      {
        c->a[7] -= 4;                              /* movem.l d0,-(sp) */
        lift_w32(x, c->a[7], c->d[0]);
        lift_charge_movem(x, 0xFFA40);
        setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD408u))); /* move.w ($D408).w,d0 */
        lift_charge(x, 0xFFA44);
        alu_cmpw(c, lift_r16(x, c->a[0] + 0x52), W(c->d[0]));   /* cmp.w $52(a0),d0 */
        lift_charge(x, 0xFFA48);
        c->d[0] = lift_r32(x, c->a[7]);            /* movem.l (sp)+,d0 */
        c->a[7] += 4;
        lift_charge_movem(x, 0xFFA4C);
        t = !c->zf;                                /* bne.w loc_FFA84 */
        lift_charge_bcc(x, 0xFFA50, t);
        if (t) goto next;
      }
    }
    /* loc_FFA54 */
    alu_btst(c, lift_r8(x, c->a[0] + 0x62), 5);    /* btst #5,$62(a0) */
    lift_charge(x, 0xFFA54);
    t = !c->zf;                                    /* bne.w loc_FFA84 */
    lift_charge_bcc(x, 0xFFA5A, t);
    if (t) goto next;
    c->d[0] = SEW(lift_r16(x, c->a[7]));           /* movem.w (sp),d0-d1: sign-extends, no pop */
    c->d[1] = SEW(lift_r16(x, c->a[7] + 2));
    lift_charge_movem(x, 0xFFA5E);
    setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[0]), W(c->d[0]))); /* sub.w (a0),d0 */
    lift_charge(x, 0xFFA62);
    {
      uint32_t s = W(c->d[0]);                     /* muls.w d0,d0 */
      c->d[0] = alu_muls(c, s, s);
      lift_charge_muls(x, 0xFFA64, s);
    }
    setw(&c->d[1], alu_subw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1]))); /* sub.w $14(a0),d1 */
    lift_charge(x, 0xFFA66);
    {
      uint32_t s = W(c->d[1]);                     /* muls.w d1,d1 */
      c->d[1] = alu_muls(c, s, s);
      lift_charge_muls(x, 0xFFA6A, s);
    }
    c->d[0] = alu_addl(c, c->d[1], c->d[0]);       /* add.l d1,d0 */
    lift_charge(x, 0xFFA6C);
    alu_cmpl(c, c->d[5], c->d[0]);                 /* cmp.l d5,d0 */
    lift_charge(x, 0xFFA6E);
    t = (!c->cf && !c->zf);                        /* bhi.w loc_FFA84 */
    lift_charge_bcc(x, 0xFFA70, t);
    if (t) goto next;
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0x52))); /* move.w $52(a0),d1 */
    lift_charge(x, 0xFFA74);
    alu_cmpw(c, lift_r16(x, c->a[1] + SW(c->d[3])), W(c->d[1])); /* cmp.w (a1,d3.w),d1 */
    lift_charge(x, 0xFFA78);
    t = c->zf;                                     /* beq.w loc_FFA84 */
    lift_charge_bcc(x, 0xFFA7C, t);
    if (t) goto next;
    c->d[5] = alu_movel(c, c->d[0]);               /* move.l d0,d5 */
    lift_charge(x, 0xFFA80);
    setw(&c->d[6], alu_movew(c, W(c->d[1])));      /* move.w d1,d6 */
    lift_charge(x, 0xFFA82);

next:
    /* loc_FFA84 */
    c->a[0] += 0x80;                               /* adda.w #$80,a0 */
    lift_charge(x, 0xFFA84);
    {
      uint32_t nd2 = (c->d[2] - 1) & 0xFFFF;       /* dbf d2,loc_FF9F2 */
      int taken = (nd2 != 0xFFFF);
      setw(&c->d[2], nd2);
      lift_charge_dbcc(x, 0xFFA88, taken, !taken);
      if (!taken) break;
    }
  }

  c->a[7] += 4;                                    /* addq.w #4,sp: drop the puck point */
  lift_charge(x, 0xFFA8C);
  c->a[7] -= 4;                                    /* pea (loc_FFAA8).l: the epilogue address */
  lift_w32(x, c->a[7], 0x000FFAA8u);
  lift_charge(x, 0xFFA8E);
  alu_cmpw(c, lift_r16(x, c->a[1] + SW(c->d[4])), W(c->d[6])); /* cmp.w (a1,d4.w),d6 */
  lift_charge(x, 0xFFA94);
  t = c->zf;                                       /* beq.w loc_FFAAE */
  lift_charge_bcc(x, 0xFFA98, t);
  if (t)
  {
    /* loc_FFAAE: jmp loc_C0AE — the FUNCTION CHUNK at $C0AE */
    lift_charge(x, 0xFFAAE);
    lift_w8(x, c->a[3] + 0x62, alu_bset(c, lift_r8(x, c->a[3] + 0x62), 5)); /* bset #5,$62(a3) */
    lift_charge(x, 0xC0AE);
    setw(&c->d[1], alu_movew(c, 0xB24));           /* move.w #$B24,d1 */
    lift_charge(x, 0xC0B4);
    lift_charge(x, 0xC0B8);                        /* bra.w sub_1073A (tail) */
    Anim_SetScript(x);                             /* rts pops the pea'd $FFAA8 */
  }
  else
  {
    setw(&c->d[0], alu_movew(c, W(c->d[6])));      /* move.w d6,d0 */
    lift_charge(x, 0xFFA9C);
    alu_tstw(c, W(c->d[4]));                       /* tst.w d4 */
    lift_charge(x, 0xFFA9E);
    t = c->zf;                                     /* beq.w loc_FFAB4 */
    lift_charge_bcc(x, 0xFFAA0, t);
    if (t)
    {
      lift_charge(x, 0xFFAB4);                     /* jmp sub_C0BC */
      Object_UpdateSelectedSlot_A(x);
    }
    else
    {
      lift_charge(x, 0xFFAA4);                     /* bra.w loc_FFABA */
      lift_charge(x, 0xFFABA);                     /* jmp sub_C0DA */
      Object_UpdateSelectedSlot_B(x);
    }
  }

  /* the tail callee's rts popped the pea'd address: pc == $FFAA8.
   * loc_FFAA8: movem.l (sp)+,d0-d6/a0-a1 — restore the entry state */
  for (i = 0; i < 7; i++) c->d[i] = lift_r32(x, c->a[7] + 4u * i);
  c->a[0] = lift_r32(x, c->a[7] + 28);
  c->a[1] = lift_r32(x, c->a[7] + 32);
  c->a[7] += 36;
  lift_charge_movem(x, 0xFFAA8);
  lift_charge(x, 0xFFAAC);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_BFBC — 1-instruction trampoline: jmp sub_FF9A8.
 */
void sub_BFBC(lift_ctx *x)
{
  lift_charge(x, 0xBFBC);                          /* jmp sub_FF9A8 */
  sub_FF9A8(x);
}

/*
 * sub_FD22 (called from sub_EF92/ROM:$FB14; unblocked by the wave-16
 * sub_BFBC lift — its only callee)
 *   Refresh all four player-selection results after a line change:
 *   resets the $FFFFC320/$C322 result words to $FFFF and re-runs the
 *   sub_BFBC nearest-player scan for each side whose $FFFFC328/$C32A
 *   primary selection is nonzero; then, for each nonzero secondary
 *   selection $FFFFC32C/$C32E, temporarily swaps it into the primary
 *   slot (old words parked on the stack), re-runs the scan with the
 *   result slot reset, publishes the result into $FFFFC324/$C326 and
 *   the possibly-updated selection back into $C32C/$C32E, then restores
 *   the parked words.
 */
void sub_FD22(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  lift_w16(x, 0xFFFFC320u, alu_movew(c, 0xFFFF)); /* move.w #$FFFF,($C320).w */
  lift_charge(x, 0xFD22);
  lift_w16(x, 0xFFFFC322u, alu_movew(c, 0xFFFF)); /* move.w #$FFFF,($C322).w */
  lift_charge(x, 0xFD28);
  alu_tstw(c, lift_r16(x, 0xFFFFC328u));          /* tst.w ($C328).w */
  lift_charge(x, 0xFD2E);
  t = c->zf;                                      /* beq.w loc_FD3C */
  lift_charge_bcc(x, 0xFD32, t);
  if (!t)
  {
    setw(&c->d[4], alu_movew(c, 0));              /* clr.w d4 */
    lift_charge(x, 0xFD36);
    lift_call(x, 0xFD38, 4, sub_BFBC);            /* bsr.w sub_BFBC */
  }
  /* loc_FD3C */
  alu_tstw(c, lift_r16(x, 0xFFFFC32Au));          /* tst.w ($C32A).w */
  lift_charge(x, 0xFD3C);
  t = c->zf;                                      /* beq.w loc_FD4A */
  lift_charge_bcc(x, 0xFD40, t);
  if (!t)
  {
    c->d[4] = alu_moveql(c, 2);                   /* moveq #2,d4 */
    lift_charge(x, 0xFD44);
    lift_call(x, 0xFD46, 4, sub_BFBC);            /* bsr.w sub_BFBC */
  }
  /* loc_FD4A */
  alu_tstw(c, lift_r16(x, 0xFFFFC32Cu));          /* tst.w ($C32C).w */
  lift_charge(x, 0xFD4A);
  t = c->zf;                                      /* beq.w loc_FD82 */
  lift_charge_bcc(x, 0xFD4E, t);
  if (!t)
  {
    c->a[7] -= 2;                                 /* move.w ($C328).w,-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC328u)));
    lift_charge(x, 0xFD52);
    lift_w16(x, 0xFFFFC328u, alu_movew(c, lift_r16(x, 0xFFFFC32Cu))); /* move.w ($C32C).w,($C328).w */
    lift_charge(x, 0xFD56);
    c->a[7] -= 2;                                 /* move.w ($C320).w,-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC320u)));
    lift_charge(x, 0xFD5C);
    lift_w16(x, 0xFFFFC320u, alu_movew(c, 0xFFFF)); /* move.w #$FFFF,($C320).w */
    lift_charge(x, 0xFD60);
    setw(&c->d[4], alu_movew(c, 0));              /* clr.w d4 */
    lift_charge(x, 0xFD66);
    lift_call(x, 0xFD68, 6, sub_BFBC);            /* jsr sub_BFBC */
    lift_w16(x, 0xFFFFC324u, alu_movew(c, lift_r16(x, 0xFFFFC320u))); /* move.w ($C320).w,($C324).w */
    lift_charge(x, 0xFD6E);
    lift_w16(x, 0xFFFFC32Cu, alu_movew(c, lift_r16(x, 0xFFFFC328u))); /* move.w ($C328).w,($C32C).w */
    lift_charge(x, 0xFD74);
    lift_w16(x, 0xFFFFC320u, alu_movew(c, lift_r16(x, c->a[7])));     /* move.w (sp)+,($C320).w */
    c->a[7] += 2;
    lift_charge(x, 0xFD7A);
    lift_w16(x, 0xFFFFC328u, alu_movew(c, lift_r16(x, c->a[7])));     /* move.w (sp)+,($C328).w */
    c->a[7] += 2;
    lift_charge(x, 0xFD7E);
  }
  /* loc_FD82 */
  alu_tstw(c, lift_r16(x, 0xFFFFC32Eu));          /* tst.w ($C32E).w */
  lift_charge(x, 0xFD82);
  t = c->zf;                                      /* beq.w locret_FDBC */
  lift_charge_bcc(x, 0xFD86, t);
  if (!t)
  {
    c->a[7] -= 2;                                 /* move.w ($C32A).w,-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC32Au)));
    lift_charge(x, 0xFD8A);
    lift_w16(x, 0xFFFFC32Au, alu_movew(c, lift_r16(x, 0xFFFFC32Eu))); /* move.w ($C32E).w,($C32A).w */
    lift_charge(x, 0xFD8E);
    c->a[7] -= 2;                                 /* move.w ($C322).w,-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC322u)));
    lift_charge(x, 0xFD94);
    lift_w16(x, 0xFFFFC322u, alu_movew(c, 0xFFFF)); /* move.w #$FFFF,($C322).w */
    lift_charge(x, 0xFD98);
    setw(&c->d[4], alu_movew(c, 2));              /* move.w #2,d4 */
    lift_charge(x, 0xFD9E);
    lift_call(x, 0xFDA2, 6, sub_BFBC);            /* jsr sub_BFBC */
    lift_w16(x, 0xFFFFC326u, alu_movew(c, lift_r16(x, 0xFFFFC322u))); /* move.w ($C322).w,($C326).w */
    lift_charge(x, 0xFDA8);
    lift_w16(x, 0xFFFFC32Eu, alu_movew(c, lift_r16(x, 0xFFFFC32Au))); /* move.w ($C32A).w,($C32E).w */
    lift_charge(x, 0xFDAE);
    lift_w16(x, 0xFFFFC322u, alu_movew(c, lift_r16(x, c->a[7])));     /* move.w (sp)+,($C322).w */
    c->a[7] += 2;
    lift_charge(x, 0xFDB4);
    lift_w16(x, 0xFFFFC32Au, alu_movew(c, lift_r16(x, c->a[7])));     /* move.w (sp)+,($C32A).w */
    c->a[7] += 2;
    lift_charge(x, 0xFDB8);
  }
  lift_charge(x, 0xFDBC);                         /* locret_FDBC: rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE7FC (called from sub_FE756+28 and sub_FE864's loc_FE8DA; all
 * registers saved/restored via the movem — pure scratch)
 *   in: a3 = on-ice object (only $76(a3) bit0 read)
 * If $FFFFDA16 byte == $80: does nothing (early exit). Otherwise bumps
 * $FFFFDA14 and indexes a per-$FFFFDA12-bank word table (off_FE788) by
 * it to fetch a 8-byte record into $FFFFDA16/DA18 (negating DA16's word
 * if $76(a3) bit0 is set); if the record's 3rd byte == $80, also sets
 * $FFFFC2FC bit3 and copies its remaining fields into
 * $FFFFDA1C/DA1D/DA1A.
 */
void sub_FE7FC(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE7FC);

  alu_cmpb(c, 0x80, lift_r8(x, 0xFFFFDA16u));         /* cmp.b #$80,(abs) */
  lift_charge(x, 0xFE800);
  int early = c->zf;                                    /* beq.w loc_FE85E */
  lift_charge_bcc(x, 0xFE806, early);

  if (!early)
  {
    lift_w16(x, 0xFFFFDA14u, alu_addw(c, 1, lift_r16(x, 0xFFFFDA14u)));  /* addq.w #1,(abs) */
    lift_charge(x, 0xFE80A);
    uint32_t d0 = lift_r16(x, 0xFFFFDA14u);             /* move.w (abs),d0 */
    lift_charge(x, 0xFE80E);
    d0 = alu_aslw(c, d0, 2);                            /* asl.w #2,d0 */
    lift_charge(x, 0xFE812);
    uint32_t a0 = 0xFE788;                              /* move.l #off_FE788,a0 */
    lift_charge(x, 0xFE814);
    uint32_t d1 = lift_r16(x, 0xFFFFDA12u);             /* move.w (abs),d1 */
    lift_charge(x, 0xFE81A);
    d1 = alu_aslw(c, d1, 2);                            /* asl.w #2,d1 */
    lift_charge(x, 0xFE81E);
    a0 = lift_r32(x, a0 + W(d1));                       /* move.l (a0,d1.w),a0 */
    lift_charge(x, 0xFE820);

    lift_w16(x, 0xFFFFDA16u, alu_movew(c, lift_r16(x, a0 + W(d0))));  /* move.w (a0,d0.w),(abs) */
    lift_charge(x, 0xFE824);

    alu_btst(c, lift_r8(x, a3 + 0x76), 0);              /* btst #0,$76(a3) */
    lift_charge(x, 0xFE82A);
    int neg = !c->zf;                                     /* beq.w loc_FE838 */
    lift_charge_bcc(x, 0xFE830, !neg);
    if (neg)
    {
      lift_w16(x, 0xFFFFDA16u, alu_negw(c, lift_r16(x, 0xFFFFDA16u)));  /* neg.w (abs) */
      lift_charge(x, 0xFE834);
    }

    /* loc_FE838 */
    lift_w16(x, 0xFFFFDA18u, alu_movew(c, lift_r16(x, a0 + W(d0) + 2)));  /* move.w 2(a0,d0.w),(abs) */
    lift_charge(x, 0xFE838);
    alu_cmpb(c, 0x80, lift_r8(x, a0 + W(d0) + 4));      /* cmp.b #$80,4(a0,d0.w) */
    lift_charge(x, 0xFE83E);
    int rec = !c->zf;                                     /* bne.w loc_FE85E */
    lift_charge_bcc(x, 0xFE844, rec);

    if (!rec)
    {
      lift_w8(x, 0xFFFFC2FCu, alu_bset(c, lift_r8(x, 0xFFFFC2FCu), 3));  /* bset #3,(abs) */
      lift_charge(x, 0xFE848);
      lift_w16(x, 0xFFFFDA1Cu, alu_movew(c, 0));        /* clr.w (abs) */
      lift_charge(x, 0xFE84E);
      lift_w8(x, 0xFFFFDA1Du, alu_moveb(c, lift_r8(x, a0 + W(d0) + 5)));  /* move.b 5(a0,d0.w),(abs) */
      lift_charge(x, 0xFE852);
      lift_w16(x, 0xFFFFDA1Au, alu_movew(c, lift_r16(x, a0 + W(d0) + 6)));  /* move.w 6(a0,d0.w),(abs) */
      lift_charge(x, 0xFE858);
    }
  }

  /* loc_FE85E: movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFE85E);

  lift_charge(x, 0xFE862);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE864 (called from sub_DEEE's loc_E0EE; d2-a6 saved/restored, d0/d1
 * are live out — the caller uses them after the call)
 *   in: a3 = on-ice object; $FFFFDA16/DA18 = candidate word offsets
 *       (mirrored if $62(a3) bit7 clear)
 *   out: d0 = |mirrored-DA16 - (a3)|, d1 = |mirrored-DA18 - $14(a3)|
 *        (refreshed from DA16/DA18 again at the end, so these end up
 *        holding whatever sub_FE7FC left there when the gate below
 *        fires, or the pre-call values otherwise)
 * Gate: if $28(a3)/$2A(a3) are both zero, tightens the "close enough"
 * band to <=$12 (else <=$A) on d0, then the same on d1; if both distances
 * clear their band, calls sub_FE7FC to pick a fresh candidate. Early-outs
 * (skipping the whole body) if $FFFFDA16 byte == $80.
 */
void sub_FE864(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  /* movem.l d2-a6,-(sp): push order a6..a0,d7..d2 (d2 lands lowest/top) */
  uint32_t saved[13] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2]
  };
  for (int i = 0; i < 13; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE864);

  alu_cmpb(c, 0x80, lift_r8(x, 0xFFFFDA16u));         /* cmp.b #$80,(abs) */
  lift_charge(x, 0xFE868);
  int early = c->zf;                                    /* beq.w loc_FE8E6 */
  lift_charge_bcc(x, 0xFE86E, early);

  if (!early)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFDA16u)));  /* move.w (abs),d0 */
    lift_charge(x, 0xFE872);
    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFDA18u)));  /* move.w (abs),d1 */
    lift_charge(x, 0xFE876);

    alu_btst(c, lift_r8(x, a3 + 0x62), 7);              /* btst #7,$62(a3) */
    lift_charge(x, 0xFE87A);
    int mirror = !c->zf;                                  /* bne.w loc_FE888 */
    lift_charge_bcc(x, 0xFE880, mirror);
    if (!mirror)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));          /* neg.w d0 */
      lift_charge(x, 0xFE884);
      setw(&c->d[1], alu_negw(c, W(c->d[1])));          /* neg.w d1 */
      lift_charge(x, 0xFE886);
    }

    /* loc_FE888 */
    setw(&c->d[0], alu_subw(c, lift_r16(x, a3), W(c->d[0])));  /* sub.w (a3),d0 */
    lift_charge(x, 0xFE888);
    int neg0 = c->nf;                                     /* bpl.w loc_FE890 */
    lift_charge_bcc(x, 0xFE88A, !neg0);
    if (neg0)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));          /* neg.w d0 */
      lift_charge(x, 0xFE88E);
    }

    /* loc_FE890 */
    alu_tstw(c, lift_r16(x, a3 + 0x28));                /* tst.w $28(a3) */
    lift_charge(x, 0xFE890);
    int wide = !c->zf;                                    /* bne.w loc_FE8A8 */
    lift_charge_bcc(x, 0xFE894, wide);
    if (!wide)
    {
      alu_tstw(c, lift_r16(x, a3 + 0x2A));              /* tst.w $2A(a3) */
      lift_charge(x, 0xFE898);
      wide = !c->zf;                                      /* bne.w loc_FE8A8 */
      lift_charge_bcc(x, 0xFE89C, wide);
    }

    int passX;
    if (!wide)
    {
      alu_cmpw(c, 0x12, W(c->d[0]));                    /* cmp.w #$12,d0 */
      lift_charge(x, 0xFE8A0);
      passX = c->zf || (c->nf != c->vf);                 /* ble.w loc_FE8B0 */
      lift_charge_bcc(x, 0xFE8A4, passX);
    }
    else
    {
      passX = 0;
    }

    if (!passX)
    {
      /* loc_FE8A8 */
      alu_cmpw(c, 0xA, W(c->d[0]));                     /* cmp.w #$A,d0 */
      lift_charge(x, 0xFE8A8);
      int bail = !c->zf && (c->nf == c->vf);              /* bgt.w loc_FE8DE */
      lift_charge_bcc(x, 0xFE8AC, bail);
      if (bail)
      {
        setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFDA16u)));  /* loc_FE8DE: move.w (abs),d0 */
        lift_charge(x, 0xFE8DE);
        setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFDA18u)));  /* move.w (abs),d1 */
        lift_charge(x, 0xFE8E2);
        goto sub_FE864_pop;
      }
    }

    /* loc_FE8B0 */
    setw(&c->d[1], alu_subw(c, lift_r16(x, a3 + 0x14), W(c->d[1])));  /* sub.w $14(a3),d1 */
    lift_charge(x, 0xFE8B0);
    int neg1 = c->nf;                                     /* bpl.w loc_FE8BA */
    lift_charge_bcc(x, 0xFE8B4, !neg1);
    if (neg1)
    {
      setw(&c->d[1], alu_negw(c, W(c->d[1])));          /* neg.w d1 */
      lift_charge(x, 0xFE8B8);
    }

    /* loc_FE8BA */
    alu_tstw(c, lift_r16(x, a3 + 0x28));                /* tst.w $28(a3) */
    lift_charge(x, 0xFE8BA);
    int wide1 = !c->zf;                                   /* bne.w loc_FE8D2 */
    lift_charge_bcc(x, 0xFE8BE, wide1);
    if (!wide1)
    {
      alu_tstw(c, lift_r16(x, a3 + 0x2A));              /* tst.w $2A(a3) */
      lift_charge(x, 0xFE8C2);
      wide1 = !c->zf;                                     /* bne.w loc_FE8D2 */
      lift_charge_bcc(x, 0xFE8C6, wide1);
    }

    int passY;
    if (!wide1)
    {
      alu_cmpw(c, 0x12, W(c->d[1]));                    /* cmp.w #$12,d1 */
      lift_charge(x, 0xFE8CA);
      passY = c->zf || (c->nf != c->vf);                 /* ble.w loc_FE8DA */
      lift_charge_bcc(x, 0xFE8CE, passY);
    }
    else
    {
      passY = 0;
    }

    if (!passY)
    {
      /* loc_FE8D2 */
      alu_cmpw(c, 0xA, W(c->d[1]));                     /* cmp.w #$A,d1 */
      lift_charge(x, 0xFE8D2);
      int bail1 = !c->zf && (c->nf == c->vf);             /* bgt.w loc_FE8DE */
      lift_charge_bcc(x, 0xFE8D6, bail1);
      if (bail1)
      {
        setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFDA16u)));  /* loc_FE8DE: move.w (abs),d0 */
        lift_charge(x, 0xFE8DE);
        setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFDA18u)));  /* move.w (abs),d1 */
        lift_charge(x, 0xFE8E2);
        goto sub_FE864_pop;
      }
    }

    /* loc_FE8DA */
    lift_call(x, 0xFE8DA, 4, sub_FE7FC);                  /* bsr.w sub_FE7FC */
    if (x->declined) return;

    /* loc_FE8DE */
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFDA16u)));  /* move.w (abs),d0 */
    lift_charge(x, 0xFE8DE);
    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFDA18u)));  /* move.w (abs),d1 */
    lift_charge(x, 0xFE8E2);
  }

sub_FE864_pop:
  /* loc_FE8E6: movem.l (sp)+,d2-a6: pop order d2..d7,a0..a6 */
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFE8E6);

  lift_charge(x, 0xFE8EA);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE8EC (called from sub_DEEE's loc_E0A4; all of d0-a6 saved/restored
 * via the movem — the real output is the Z flag at return, not d0)
 *   in: a3 = on-ice object
 * Gate chain deciding whether the shootout candidate at DA16/DA18 is
 * still valid for a3's zone/period-remaining state; if it reaches
 * loc_FE942 it re-derives the |mirrored-DA16 - (a3)| / |mirrored-DA18 -
 * $14(a3)| distances (same computation as sub_FE864) and compares both
 * against the DA1C band. Z set (clr.w d0 path) = still valid; Z clear
 * (move.w #1,d0 path) = invalid.
 */
void sub_FE8EC(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE8EC);

  int invalid = 0;

  alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 1);            /* btst #1,(abs) */
  lift_charge(x, 0xFE8F0);
  int gateOpen = !c->zf;                                /* beq.w loc_FE980 */
  lift_charge_bcc(x, 0xFE8F6, !gateOpen);

  if (!gateOpen)
  {
    invalid = 1;
  }
  else
  {
    uint32_t d0 = lift_r16(x, a3 + 0x52);              /* move.w $52(a3),d0 */
    lift_charge(x, 0xFE8FA);
    alu_cmpw(c, lift_r16(x, 0xFFFFB7AAu), d0);          /* cmp.w (abs),d0 */
    lift_charge(x, 0xFE8FE);
    int mismatch = !c->zf;                                /* bne.w loc_FE980 */
    lift_charge_bcc(x, 0xFE902, mismatch);

    if (mismatch)
    {
      invalid = 1;
    }
    else
    {
      alu_cmpw(c, 3, lift_r16(x, 0xFFFFD454u));         /* cmp.w #3,(abs) */
      lift_charge(x, 0xFE906);
      int shallow = c->zf || (c->nf != c->vf);            /* ble.w loc_FE97A */
      lift_charge_bcc(x, 0xFE90C, shallow);

      if (shallow)
      {
        goto sub_FE8EC_valid;
      }

      alu_cmpb(c, 0x80, lift_r8(x, 0xFFFFDA16u));       /* cmp.b #$80,(abs) */
      lift_charge(x, 0xFE910);
      int noCand = c->zf;                                 /* beq.w loc_FE97A */
      lift_charge_bcc(x, 0xFE916, noCand);

      if (noCand)
      {
        goto sub_FE8EC_valid;
      }

      alu_btst(c, lift_r8(x, 0xFFFFC2FCu), 3);          /* btst #3,(abs) */
      lift_charge(x, 0xFE91A);
      int recorded = !c->zf;                              /* bne.w loc_FE942 */
      lift_charge_bcc(x, 0xFE920, recorded);

      if (recorded)
      {
        goto sub_FE8EC_recheck;
      }

      alu_tstw(c, lift_r16(x, 0xFFFFB772u));            /* tst.w (abs) */
      lift_charge(x, 0xFE924);
      int busyA = !c->zf;                                 /* bne.w loc_FE980 */
      lift_charge_bcc(x, 0xFE928, busyA);
      if (busyA)
      {
        invalid = 1;
        goto sub_FE8EC_settle;
      }

      alu_tstw(c, lift_r16(x, 0xFFFFB774u));            /* tst.w (abs) */
      lift_charge(x, 0xFE92C);
      int busyB = !c->zf;                                 /* bne.w loc_FE980 */
      lift_charge_bcc(x, 0xFE930, busyB);
      if (busyB)
      {
        invalid = 1;
        goto sub_FE8EC_settle;
      }

      alu_cmpw(c, 0xF, lift_r16(x, 0xFFFFD454u));       /* cmp.w #$F,(abs) */
      lift_charge(x, 0xFE934);
      int early = (c->nf != c->vf);                       /* blt.w loc_FE97A */
      lift_charge_bcc(x, 0xFE93A, early);

      if (early)
      {
        goto sub_FE8EC_valid;
      }

      lift_charge(x, 0xFE93E);                            /* bra.w loc_FE980 */
      invalid = 1;
      goto sub_FE8EC_settle;
    }
  }

  goto sub_FE8EC_settle;

sub_FE8EC_recheck:
  /* loc_FE942 */
  {
    uint32_t d0 = lift_r16(x, 0xFFFFDA16u);             /* move.w (abs),d0 */
    lift_charge(x, 0xFE942);
    uint32_t d1 = lift_r16(x, 0xFFFFDA18u);             /* move.w (abs),d1 */
    lift_charge(x, 0xFE946);

    alu_btst(c, lift_r8(x, a3 + 0x62), 7);              /* btst #7,$62(a3) */
    lift_charge(x, 0xFE94A);
    int mirror = !c->zf;                                  /* bne.w loc_FE958 */
    lift_charge_bcc(x, 0xFE950, mirror);
    if (!mirror)
    {
      d0 = alu_negw(c, d0);                             /* neg.w d0 */
      lift_charge(x, 0xFE954);
      d1 = alu_negw(c, d1);                             /* neg.w d1 */
      lift_charge(x, 0xFE956);
    }

    /* loc_FE958 */
    d0 = alu_subw(c, lift_r16(x, a3), d0);              /* sub.w (a3),d0 */
    lift_charge(x, 0xFE958);
    int neg0 = c->nf;                                     /* bpl.w loc_FE960 */
    lift_charge_bcc(x, 0xFE95A, !neg0);
    if (neg0)
    {
      d0 = alu_negw(c, d0);                             /* neg.w d0 */
      lift_charge(x, 0xFE95E);
    }

    /* loc_FE960 */
    d1 = alu_subw(c, lift_r16(x, a3 + 0x14), d1);       /* sub.w $14(a3),d1 */
    lift_charge(x, 0xFE960);
    int neg1 = c->nf;                                     /* bpl.w loc_FE96A */
    lift_charge_bcc(x, 0xFE964, !neg1);
    if (neg1)
    {
      d1 = alu_negw(c, d1);                             /* neg.w d1 */
      lift_charge(x, 0xFE968);
    }

    /* loc_FE96A */
    alu_cmpw(c, lift_r16(x, 0xFFFFDA1Cu), W(d0));       /* cmp.w (abs),d0 */
    lift_charge(x, 0xFE96A);
    int outX = !c->zf && (c->nf == c->vf);                /* bgt.w loc_FE980 */
    lift_charge_bcc(x, 0xFE96E, outX);
    if (outX)
    {
      invalid = 1;
      goto sub_FE8EC_settle;
    }

    alu_cmpw(c, lift_r16(x, 0xFFFFDA1Cu), W(d1));       /* cmp.w (abs),d1 */
    lift_charge(x, 0xFE972);
    int outY = !c->zf && (c->nf == c->vf);                /* bgt.w loc_FE980 */
    lift_charge_bcc(x, 0xFE976, outY);
    if (outY)
    {
      invalid = 1;
      goto sub_FE8EC_settle;
    }
  }

sub_FE8EC_valid:
  invalid = 0;

sub_FE8EC_settle:
  if (invalid)
  {
    setw(&c->d[0], alu_movew(c, 1));                    /* loc_FE980: move.w #1,d0 */
    lift_charge(x, 0xFE980);
  }
  else
  {
    setw(&c->d[0], alu_movew(c, 0));                    /* loc_FE97A: clr.w d0 */
    lift_charge(x, 0xFE97A);
    lift_charge(x, 0xFE97C);                              /* bra.w loc_FE984 */
  }

  c->d[0] = saved[14];                                  /* movem.l (sp)+,d0-a6 */
  c->d[1] = saved[13];
  c->d[2] = saved[12];
  c->d[3] = saved[11];
  c->d[4] = saved[10];
  c->d[5] = saved[9];
  c->d[6] = saved[8];
  c->d[7] = saved[7];
  c->a[0] = saved[6];
  c->a[1] = saved[5];
  c->a[2] = saved[4];
  c->a[3] = saved[3];
  c->a[4] = saved[2];
  c->a[5] = saved[1];
  c->a[6] = saved[0];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xFE984);

  lift_charge(x, 0xFE988);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_EE58 (called from ROM:$EDBA; all of d0-a6 saved/restored via the
 * movem — the real output is the N flag at return (from the final
 * move.w #1/#$FFFF,d0), plus $FFFFD40A/$FFFFD406 globals)
 *   in: none (reads $FFFFD404 team-side flag)
 * Walks up to 26 variable-length line-stat records off the
 * $1E(team-block)-relative array (home $FFFFC6CE or away $FFFFCA32 per
 * $FFFFD404), each record advanced by its own leading length word.
 * Skips records whose masked byte-5 nibble is <=1, or whose
 * $66(team-block,d1*2) slot isn't the $FFFE/$FFFF sentinel. For
 * surviving records, sums 8 packed nibbles (bytes 1-3 low+high, byte 5
 * low+high, bytes 6-7 high only) into d3 (forced to $7FFF if this is
 * the record at the current $FFFFD40A index — the "keep current"
 * override), and tracks the running max into d6/d5 (value/index).
 * Bails the whole scan early (without touching $FFFFD40A) if
 * $FFFFC2FA bit0 is set. On a length-word == 2 terminator or loop end:
 * if no record ever won (d5 still negative), returns with N set
 * (d0=$FFFF) and $FFFFD40A untouched; otherwise stores the winning
 * index to $FFFFD40A, sets $FFFFD406 (0, or 6 if $FFFFD404 != 0), and
 * returns with N clear (d0=1). a3 is recomputed
 * ($FFFFB04A + $FFFFD406<<7) but discarded by the movem restore — dead
 * in the original source, preserved for cycle-fidelity only.
 */
void sub_EE58(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xEE58);

  uint32_t a0 = 0xFFFFC6CEu;                            /* move.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xEE5C);
  alu_tstw(c, lift_r16(x, 0xFFFFD404u));               /* tst.w (abs) */
  lift_charge(x, 0xEE62);
  int away = !c->zf;                                     /* beq.w loc_EE70 */
  lift_charge_bcc(x, 0xEE66, c->zf);   /* charge uses beq's own taken condition */
  if (away)
  {
    a0 = 0xFFFFCA32u;                                    /* move.l #$FFFFCA32,a0 */
    lift_charge(x, 0xEE6A);
  }

  /* loc_EE70 */
  uint32_t d1 = 0;                                      /* move.w #0,d1 */
  lift_charge(x, 0xEE70);
  uint32_t d6 = 0xFFFFu;                                /* move.w #$FFFF,d6 */
  lift_charge(x, 0xEE74);
  int32_t d5 = -1;                                      /* move.w #$FFFF,d5 */
  lift_charge(x, 0xEE78);
  uint32_t a2 = lift_r32(x, a0 + 0x1E);                 /* move.l $1E(a0),a2 */
  lift_charge(x, 0xEE7C);
  a2 += SW(lift_r16(x, a2));                            /* add.w (a2),a2 */
  lift_charge(x, 0xEE80);

  int aborted = 0;
  for (;;)
  {
    /* loc_EE82 */
    alu_cmpw(c, 2, lift_r16(x, a2));                    /* cmp.w #2,(a2) */
    lift_charge(x, 0xEE82);
    int terminated = c->zf;                               /* beq.w loc_EF4C */
    lift_charge_bcc(x, 0xEE86, terminated);
    if (terminated) break;

    a2 += SW(lift_r16(x, a2));                          /* add.w (a2),a2 */
    lift_charge(x, 0xEE8A);
    uint32_t d7 = W(d1);                                 /* move.w d1,d7 */
    lift_charge(x, 0xEE8C);
    d7 = alu_aslw(c, d7, 1);                             /* asl.w #1,d7 */
    lift_charge(x, 0xEE8E);

    uint32_t d0 = alu_moveb(c, lift_r8(x, a2 + 5));      /* move.b 5(a2),d0 */
    lift_charge(x, 0xEE90);
    d0 = alu_andw(c, 0xF, d0);                           /* and.w #$F,d0 */
    lift_charge(x, 0xEE94);
    alu_cmpb(c, 1, d0);                                  /* cmp.b #1,d0 */
    lift_charge(x, 0xEE98);
    int tooFew = c->zf || (c->nf != c->vf);                /* ble.w loc_EF40 */
    lift_charge_bcc(x, 0xEE9C, tooFew);
    if (tooFew) goto sub_EE58_next;

    alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 0);            /* btst #0,(abs) */
    lift_charge(x, 0xEEA0);
    int bail = !c->zf;                                    /* bne.w loc_EF56 */
    lift_charge_bcc(x, 0xEEA6, bail);
    if (bail) { aborted = 1; break; }

    alu_cmpw(c, 0xFFFE, lift_r16(x, a0 + 0x66 + W(d7))); /* cmp.w #$FFFE,$66(a0,d7.w) */
    lift_charge(x, 0xEEAA);
    int sentinelA = c->zf;                                 /* beq.w loc_EEBE */
    lift_charge_bcc(x, 0xEEB0, sentinelA);

    if (!sentinelA)
    {
      alu_cmpw(c, 0xFFFF, lift_r16(x, a0 + 0x66 + W(d7))); /* cmp.w #$FFFF,$66(a0,d7.w) */
      lift_charge(x, 0xEEB4);
      int sentinelB = !c->zf;                              /* bne.w loc_EF40 */
      lift_charge_bcc(x, 0xEEBA, sentinelB);
      if (sentinelB) goto sub_EE58_next;
    }

    /* loc_EEBE: sum 8 packed nibbles into d3 */
    {
      uint32_t d3 = 0;                                   /* clr.w d3 */
      lift_charge(x, 0xEEBE);
      d0 = alu_movew(c, 0);                               /* clr.w d0 */
      lift_charge(x, 0xEEC0);

      d0 = alu_moveb(c, lift_r8(x, a2 + 1));              /* move.b 1(a2),d0 */
      lift_charge(x, 0xEEC2);
      d0 = alu_andw(c, 0xF, d0);                          /* and.w #$F,d0 */
      lift_charge(x, 0xEEC6);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEECA);

      d0 = alu_moveb(c, lift_r8(x, a2 + 2));              /* move.b 2(a2),d0 */
      lift_charge(x, 0xEECC);
      d0 = alu_andw(c, 0xF, d0);                          /* and.w #$F,d0 */
      lift_charge(x, 0xEED0);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEED4);

      d0 = alu_moveb(c, lift_r8(x, a2 + 2));              /* move.b 2(a2),d0 */
      lift_charge(x, 0xEED6);
      d0 = alu_andw(c, 0xF0, d0);                         /* and.w #$F0,d0 */
      lift_charge(x, 0xEEDA);
      d0 = alu_lsrw(c, d0, 4);                            /* lsr.w #4,d0 */
      lift_charge(x, 0xEEDE);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEEE0);

      d0 = alu_moveb(c, lift_r8(x, a2 + 3));              /* move.b 3(a2),d0 */
      lift_charge(x, 0xEEE2);
      d0 = alu_andw(c, 0xF, d0);                          /* and.w #$F,d0 */
      lift_charge(x, 0xEEE6);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEEEA);

      d0 = alu_moveb(c, lift_r8(x, a2 + 3));              /* move.b 3(a2),d0 */
      lift_charge(x, 0xEEEC);
      d0 = alu_andw(c, 0xF0, d0);                         /* and.w #$F0,d0 */
      lift_charge(x, 0xEEF0);
      d0 = alu_lsrw(c, d0, 4);                            /* lsr.w #4,d0 */
      lift_charge(x, 0xEEF4);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEEF6);

      d0 = alu_moveb(c, lift_r8(x, a2 + 5));              /* move.b 5(a2),d0 */
      lift_charge(x, 0xEEF8);
      d0 = alu_andw(c, 0xF, d0);                          /* and.w #$F,d0 */
      lift_charge(x, 0xEEFC);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEF00);

      d0 = alu_moveb(c, lift_r8(x, a2 + 5));              /* move.b 5(a2),d0 */
      lift_charge(x, 0xEF02);
      d0 = alu_andw(c, 0xF0, d0);                         /* and.w #$F0,d0 */
      lift_charge(x, 0xEF06);
      d0 = alu_lsrw(c, d0, 4);                            /* lsr.w #4,d0 */
      lift_charge(x, 0xEF0A);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEF0C);

      d0 = alu_moveb(c, lift_r8(x, a2 + 6));              /* move.b 6(a2),d0 */
      lift_charge(x, 0xEF0E);
      d0 = alu_andw(c, 0xF0, d0);                         /* and.w #$F0,d0 */
      lift_charge(x, 0xEF12);
      d0 = alu_lsrw(c, d0, 4);                            /* lsr.w #4,d0 */
      lift_charge(x, 0xEF16);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEF18);

      d0 = alu_moveb(c, lift_r8(x, a2 + 7));              /* move.b 7(a2),d0 */
      lift_charge(x, 0xEF1A);
      d0 = alu_andw(c, 0xF0, d0);                         /* and.w #$F0,d0 */
      lift_charge(x, 0xEF1E);
      d0 = alu_lsrw(c, d0, 4);                            /* lsr.w #4,d0 */
      lift_charge(x, 0xEF22);
      d3 = alu_addw(c, d0, d3);                           /* add.w d0,d3 */
      lift_charge(x, 0xEF24);

      alu_cmpw(c, lift_r16(x, 0xFFFFD40Au), W(d1));       /* cmp.w (abs),d1 */
      lift_charge(x, 0xEF26);
      int isCurrent = !c->zf;                               /* bne.w loc_EF32 */
      lift_charge_bcc(x, 0xEF2A, isCurrent);
      if (!isCurrent)
      {
        d3 = alu_movew(c, 0x7FFF);                        /* move.w #$7FFF,d3 */
        lift_charge(x, 0xEF2E);
      }

      /* loc_EF32 */
      alu_cmpw(c, d6, W(d3));                              /* cmp.w d6,d3 */
      lift_charge(x, 0xEF32);
      int worse = (c->nf != c->vf);                          /* blt.w loc_EF40 */
      lift_charge_bcc(x, 0xEF34, worse);
      if (!worse)
      {
        d6 = W(d3);                                        /* move.w d3,d6 */
        lift_charge(x, 0xEF38);
        d5 = (int16_t)(uint16_t)W(d1);                      /* move.w d1,d5 */
        lift_charge(x, 0xEF3A);
        lift_charge(x, 0xEF3C);                              /* bra.w *+4 (no-op) */
      }
    }

sub_EE58_next:
    /* loc_EF40 */
    a2 += 8;                                              /* addq.l #8,a2 */
    lift_charge(x, 0xEF40);
    d1 = alu_addw(c, 1, d1);                              /* addq.w #1,d1 */
    lift_charge(x, 0xEF42);
    alu_cmpw(c, 0x1A, W(d1));                             /* cmp.w #$1A,d1 */
    lift_charge(x, 0xEF44);
    int more = (c->nf != c->vf);                            /* blt.w loc_EE82 */
    lift_charge_bcc(x, 0xEF48, more);
    if (!more) break;
  }

  if (!aborted)
  {
    /* loc_EF4C */
    alu_tstw(c, W(d5));                                   /* tst.w d5 */
    lift_charge(x, 0xEF4C);
    int notFound = c->nf;                                    /* bmi.w loc_EF80 */
    lift_charge_bcc(x, 0xEF4E, notFound);

    if (notFound)
    {
      setw(&c->d[0], alu_movew(c, 0xFFFF));                /* move.w #$FFFF,d0 */
      lift_charge(x, 0xEF80);
      lift_charge(x, 0xEF84);                                /* bra.w loc_EF8C */
      goto sub_EE58_done;
    }

    lift_w16(x, 0xFFFFD40Au, alu_movew(c, W(d5)));         /* move.w d5,(abs) */
    lift_charge(x, 0xEF52);
  }

  /* loc_EF56 */
  lift_w16(x, 0xFFFFD406u, alu_movew(c, 0));              /* move.w #0,(abs) */
  lift_charge(x, 0xEF56);
  alu_tstw(c, lift_r16(x, 0xFFFFD404u));                  /* tst.w (abs) */
  lift_charge(x, 0xEF5C);
  int homeAlt = !c->zf;                                     /* beq.w loc_EF6A */
  lift_charge_bcc(x, 0xEF60, c->zf);   /* charge uses beq's own taken condition */
  if (homeAlt)
  {
    lift_w16(x, 0xFFFFD406u, alu_movew(c, 6));            /* move.w #6,(abs) */
    lift_charge(x, 0xEF64);
  }

  /* loc_EF6A: dead a3 recompute (movem-restored, kept for cycle fidelity) */
  {
    uint32_t d0 = lift_r16(x, 0xFFFFD406u);               /* move.w (abs),d0 */
    lift_charge(x, 0xEF6A);
    d0 = alu_aslw(c, d0, 7);                              /* asl.w #7,d0 */
    lift_charge(x, 0xEF6E);
    uint32_t a3 = 0xFFFFB04Au;                            /* move.l #$FFFFB04A,a3 */
    lift_charge(x, 0xEF70);
    a3 += SW(d0);                                         /* add.w d0,a3 */
    lift_charge(x, 0xEF76);
    (void)a3;
    uint32_t d3 = lift_r16(x, 0xFFFFD40Au);               /* move.w (abs),d3 */
    lift_charge(x, 0xEF78);
    (void)d3;
    lift_charge(x, 0xEF7C);                                 /* bra.w loc_EF88 */
  }

  setw(&c->d[0], alu_movew(c, 1));                        /* loc_EF88: move.w #1,d0 */
  lift_charge(x, 0xEF88);

sub_EE58_done:
  c->d[0] = saved[14];                                    /* movem.l (sp)+,d0-a6 */
  c->d[1] = saved[13];
  c->d[2] = saved[12];
  c->d[3] = saved[11];
  c->d[4] = saved[10];
  c->d[5] = saved[9];
  c->d[6] = saved[8];
  c->d[7] = saved[7];
  c->a[0] = saved[6];
  c->a[1] = saved[5];
  c->a[2] = saved[4];
  c->a[3] = saved[3];
  c->a[4] = saved[2];
  c->a[5] = saved[1];
  c->a[6] = saved[0];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xEF8C);

  lift_charge(x, 0xEF90);                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE756 (called from ROM:$ECEC and sub_FC4C0+14; all of d0-a6
 * scratch via the movem)
 *   in: none
 * Rejection-samples Rng_NextScaled(7) until it returns <=6, stores the
 * result to $FFFFDA12 (the bank index sub_FE7FC's off_FE788 table
 * lookup uses), clears $FFFFC2FC bit3, resets $FFFFDA14 to $FFFF and
 * $FFFFDA16 to 0, then calls sub_FE7FC to pick the first candidate.
 */
void sub_FE756(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE756);

  for (;;)
  {
    /* loc_FE75A */
    setw(&c->d[0], alu_movew(c, 7));                    /* move.w #7,d0 */
    lift_charge(x, 0xFE75A);
    lift_call(x, 0xFE75E, 6, Rng_NextScaled);           /* jsr sub_11086 */
    if (x->declined) return;

    alu_cmpw(c, 6, W(c->d[0]));                         /* cmp.w #6,d0 */
    lift_charge(x, 0xFE764);
    int retry = !c->zf && (c->nf == c->vf);               /* bgt.s loc_FE75A */
    lift_charge_bcc(x, 0xFE768, retry);
    if (!retry) break;
  }

  lift_w16(x, 0xFFFFDA12u, alu_movew(c, W(c->d[0])));   /* move.w d0,(abs) */
  lift_charge(x, 0xFE76A);
  lift_w8(x, 0xFFFFC2FCu, alu_bclr(c, lift_r8(x, 0xFFFFC2FCu), 3));  /* bclr #3,(abs) */
  lift_charge(x, 0xFE76E);
  lift_w16(x, 0xFFFFDA14u, alu_movew(c, 0xFFFF));       /* move.w #$FFFF,(abs) */
  lift_charge(x, 0xFE774);
  lift_w16(x, 0xFFFFDA16u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFE77A);

  lift_call(x, 0xFE77E, 4, sub_FE7FC);                  /* bsr.w sub_FE7FC */
  if (x->declined) return;

  c->d[0] = saved[14];                                  /* movem.l (sp)+,d0-a6 */
  c->d[1] = saved[13];
  c->d[2] = saved[12];
  c->d[3] = saved[11];
  c->d[4] = saved[10];
  c->d[5] = saved[9];
  c->d[6] = saved[8];
  c->d[7] = saved[7];
  c->a[0] = saved[6];
  c->a[1] = saved[5];
  c->a[2] = saved[4];
  c->a[3] = saved[3];
  c->a[4] = saved[2];
  c->a[5] = saved[1];
  c->a[6] = saved[0];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xFE782);

  lift_charge(x, 0xFE786);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FC570 (called twice from sub_FC47C's loc_FC4AC; no movem — a0/a1/
 * d0/d1 are all live outputs, discarded by the caller)
 *   in: none (reads $FFFFC330/$FFFFC332 and $FFFFD594's home/away flag)
 * Picks the ROM record at the $30E pointer table (indexed by
 * $FFFFC330, or $FFFFC332 if $FFFFD594 != 0), walks its trailing
 * 6-byte tail (base+6, or +4 for the sibling table used at $FC5FE — a
 * fixed +6 here), and unpacks each (byte-1) into a descending word
 * slot of the $FFFFD586-based buffer (or $FFFFD594-based if the away
 * flag was set).
 */
void sub_FC570(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = 0xFFFFD586u;                                /* move.l #$FFFFD586,a0 */
  lift_charge(x, 0xFC570);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC330u)));  /* move.w (abs),d0 */
  lift_charge(x, 0xFC576);

  alu_tstw(c, lift_r16(x, 0xFFFFD594u));                /* tst.w (abs) */
  lift_charge(x, 0xFC57A);
  int isAway = !c->zf;                                    /* beq.w loc_FC58C */
  lift_charge_bcc(x, 0xFC57E, c->zf);   /* charge uses beq's own taken condition */

  if (isAway)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC332u)));  /* move.w (abs),d0 */
    lift_charge(x, 0xFC582);
    c->a[0] = 0xFFFFD594u;                              /* move.l #$FFFFD594,a0 */
    lift_charge(x, 0xFC586);
  }

  /* loc_FC58C */
  c->a[1] = 0x30Eu;                                     /* move.l #$30E,a1 */
  lift_charge(x, 0xFC58C);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));           /* asl.w #2,d0 */
  lift_charge(x, 0xFC592);
  c->a[1] = lift_r32(x, c->a[1] + SEW(c->d[0]));          /* move.l (a1,d0.w),a1 */
  lift_charge(x, 0xFC594);
  c->a[1] += SW(lift_r16(x, c->a[1] + 6));              /* add.w 6(a1),a1 */
  lift_charge(x, 0xFC598);

  setw(&c->d[0], alu_movew(c, 5));                      /* move.w #5,d0 */
  lift_charge(x, 0xFC59C);

  for (;;)
  {
    /* loc_FC5A0 */
    setw(&c->d[1], alu_movew(c, 0));                    /* clr.w d1 */
    lift_charge(x, 0xFC5A0);
    setw(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,d1 */
    c->a[1] += 1;
    lift_charge(x, 0xFC5A2);
    setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));         /* subq.w #1,d1 */
    lift_charge(x, 0xFC5A4);
    c->a[0] -= 2;                                        /* move.w d1,-(a0) */
    lift_w16(x, c->a[0], alu_movew(c, W(c->d[1])));
    lift_charge(x, 0xFC5A6);

    uint32_t nd0 = W(W(c->d[0]) - 1);                    /* dbf d0,loc_FC5A0 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFC5A8, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0xFC5AC);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FC47C (called from sub_F739E+10F8; no movem — a0/d0 are live
 * outputs, discarded by whichever caller sits above sub_F739E)
 *   in: none
 * Clears the $FFFFB04A on-ice-object table (1024 words), resets a
 * cluster of shootout-round globals ($FFFFDED0/$FFFFD578/$FFFFD574/
 * $FFFFD576/$FFFFD586/$FFFFD594, plus $FFFFC2FA bit3), then calls
 * sub_FC570 twice — once for the home side, once with $FFFFD594 forced
 * to 1 for the away side (restored to 0 afterward).
 */
void sub_FC47C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = 0xFFFFB04Au;                                /* move.l #$FFFFB04A,a0 */
  lift_charge(x, 0xFC47C);
  setw(&c->d[0], alu_movew(c, 0x3FF));                  /* move.w #$3FF,d0 */
  lift_charge(x, 0xFC482);

  for (;;)
  {
    /* loc_FC486 */
    lift_w16(x, c->a[0], alu_movew(c, 0));              /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0xFC486);

    uint32_t nd0 = W(W(c->d[0]) - 1);                    /* dbf d0,loc_FC486 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xFC488, taken, !taken);
    if (!taken) break;
  }

  lift_w16(x, 0xFFFFDED0u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC48C);
  lift_w16(x, 0xFFFFD578u, alu_movew(c, 1));            /* move.w #1,(abs) */
  lift_charge(x, 0xFC490);
  lift_w8(x, 0xFFFFC2FAu, alu_bclr(c, lift_r8(x, 0xFFFFC2FAu), 3));  /* bclr #3,(abs) */
  lift_charge(x, 0xFC496);
  lift_w16(x, 0xFFFFD574u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC49C);
  lift_w16(x, 0xFFFFD576u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC4A0);
  lift_w16(x, 0xFFFFD586u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC4A4);
  lift_w16(x, 0xFFFFD594u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC4A8);

  /* loc_FC4AC */
  lift_call(x, 0xFC4AC, 4, sub_FC570);                  /* bsr.w sub_FC570 */
  if (x->declined) return;

  lift_w16(x, 0xFFFFD594u, alu_movew(c, 1));            /* move.w #1,(abs) */
  lift_charge(x, 0xFC4B0);
  lift_call(x, 0xFC4B6, 4, sub_FC570);                  /* bsr.w sub_FC570 */
  if (x->declined) return;

  lift_w16(x, 0xFFFFD594u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFC4BA);

  lift_charge(x, 0xFC4BE);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_17CA0 (called from sub_F76CE and sub_F7942+192; d0-a3 saved/
 * restored via the movem)
 *   in: none (a3 is a local scratch param for sub_1803E, not the
 *       caller's on-ice object)
 * Seeds sub_1803E with a3=$FFFFD088, then if $FFFFCEEC|$FFFFCEEA is
 * nonzero: calls sub_17D80, computes a base d0 of 7 (or $B if
 * $FFFFD046 is set), adjusts $FFFFCEF0 through a small state machine
 * keyed on $FFFFD046/$FFFFCEF0's current value, and stores
 * d0-$FFFFCEF0 into $FFFFD04A.
 */
void sub_17CA0(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a3,-(sp): push order a3..a0,d7..d0 (d0 lands lowest/top) */
  uint32_t saved[12] = {
    c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 12; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x17CA0);

  c->a[3] = 0xFFFFD088u;                                /* move.w #$D088,a3 */
  lift_charge(x, 0x17CA4);

  lift_call(x, 0x17CA8, 4, sub_1803E);                  /* bsr.w sub_1803E */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFCEECu)));  /* move.w (abs),d0 */
  lift_charge(x, 0x17CAC);
  setw(&c->d[0], alu_movew(c, W(c->d[0]) | lift_r16(x, 0xFFFFCEEAu)));  /* or.w (abs),d0 */
  lift_charge(x, 0x17CB0);
  int empty = c->zf;                                      /* beq.w loc_17D10 */
  lift_charge_bcc(x, 0x17CB4, empty);

  if (!empty)
  {
    lift_call(x, 0x17CB8, 4, sub_17D80);                /* bsr.w sub_17D80 */
    if (x->declined) return;

    c->d[0] = alu_moveql(c, 7);                         /* moveq #7,d0 */
    lift_charge(x, 0x17CBC);
    alu_tstw(c, lift_r16(x, 0xFFFFD046u));              /* tst.w (abs) */
    lift_charge(x, 0x17CBE);
    int flagged = !c->zf;                                 /* beq.w loc_17CCA */
    lift_charge_bcc(x, 0x17CC2, c->zf);   /* charge uses beq's own taken condition */

    if (flagged)
    {
      setw(&c->d[0], alu_movew(c, 0xB));                /* move.w #$B,d0 */
      lift_charge(x, 0x17CC6);
    }

    /* loc_17CCA */
    alu_tstw(c, lift_r16(x, 0xFFFFD046u));              /* tst.w (abs) */
    lift_charge(x, 0x17CCA);
    int hasFlag = !c->zf;                                 /* bne.w loc_17CE4 */
    lift_charge_bcc(x, 0x17CCE, hasFlag);

    if (!hasFlag)
    {
      alu_cmpw(c, 2, lift_r16(x, 0xFFFFCEF0u));         /* cmp.w #2,(abs) */
      lift_charge(x, 0x17CD2);
      int under2 = (c->nf != c->vf);                      /* blt.w loc_17D08 */
      lift_charge_bcc(x, 0x17CD8, under2);

      if (!under2)
      {
        lift_w16(x, 0xFFFFCEF0u, alu_subw(c, 2, lift_r16(x, 0xFFFFCEF0u)));  /* subq.w #2,(abs) */
        lift_charge(x, 0x17CDC);
        lift_charge(x, 0x17CE0);                          /* bra.w loc_17D08 */
      }
    }
    else
    {
      /* loc_17CE4 */
      alu_cmpw(c, 1, lift_r16(x, 0xFFFFCEF0u));         /* cmp.w #1,(abs) */
      lift_charge(x, 0x17CE4);
      int notOne = !c->zf;                                /* bne.w loc_17CF8 */
      lift_charge_bcc(x, 0x17CEA, notOne);

      if (!notOne)
      {
        lift_w16(x, 0xFFFFCEF0u, alu_movew(c, 3));      /* move.w #3,(abs) */
        lift_charge(x, 0x17CEE);
        lift_charge(x, 0x17CF4);                          /* bra.w loc_17D08 */
      }
      else
      {
        /* loc_17CF8 */
        alu_cmpw(c, 2, lift_r16(x, 0xFFFFCEF0u));       /* cmp.w #2,(abs) */
        lift_charge(x, 0x17CF8);
        int notTwo = !c->zf;                              /* bne.w loc_17D08 */
        lift_charge_bcc(x, 0x17CFE, notTwo);

        if (!notTwo)
        {
          lift_w16(x, 0xFFFFCEF0u, alu_movew(c, 4));    /* move.w #4,(abs) */
          lift_charge(x, 0x17D02);
        }
      }
    }

    /* loc_17D08 */
    setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFCEF0u), W(c->d[0])));  /* sub.w (abs),d0 */
    lift_charge(x, 0x17D08);
    lift_w16(x, 0xFFFFD04Au, alu_movew(c, W(c->d[0])));  /* move.w d0,(abs) */
    lift_charge(x, 0x17D0C);
  }

  /* loc_17D10: movem.l (sp)+,d0-a3: pop order d0..d7,a0..a3 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x17D10);

  lift_charge(x, 0x17D14);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_17D16 (called from sub_F76C8; no movem — a0/d0/d1/d2 are live
 * outputs, discarded by the caller; ends by tail-falling into the
 * already-Done sub_17D80, not an rts)
 *   in: none
 * Draws Rng_NextScaled(32) results as a 5-bit index into the ROM pair
 * table at $5576 (16 bytes/entry), retrying (loc_17D1C) until a byte
 * within the entry matches a clamped $FFFFD04C value (searched up to
 * 16 bytes via the dbeq scan). On a match: stores the match's
 * complement-nibble position to $FFFFCEEE and the entry index to
 * $FFFFCEE8, resets $FFFFCEEC/$FFFFCEEA (7), then either tail-calls
 * sub_17D80 directly ($FFFFD048==2) or first zeroes the +4/+6 words of
 * all eight $FFFFCE66 structs before falling into it.
 */
void sub_17D16(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x20);                        /* moveq #$20,d0 */
  lift_charge(x, 0x17D16);
  lift_call(x, 0x17D18, 4, Rng_NextScaled);             /* bsr.w sub_11086 */
  if (x->declined) return;

  for (;;)
  {
    /* loc_17D1C */
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));         /* addq.w #1,d0 */
    lift_charge(x, 0x17D1C);
    setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));      /* and.w #$1F,d0 */
    lift_charge(x, 0x17D1E);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));         /* asl.w #4,d0 */
    lift_charge(x, 0x17D22);
    c->a[0] = 0x5576u;                                  /* move.l #$5576,a0 */
    lift_charge(x, 0x17D24);
    c->a[0] += SW(W(c->d[0]));                          /* add.w d0,a0 */
    lift_charge(x, 0x17D2A);
    setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));         /* lsr.w #4,d0 */
    lift_charge(x, 0x17D2C);
    c->d[1] = alu_moveql(c, 0xF);                       /* moveq #$F,d1 */
    lift_charge(x, 0x17D2E);
    setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFD04Cu)));  /* move.w (abs),d2 */
    lift_charge(x, 0x17D30);

    alu_cmpw(c, 0x19, W(c->d[2]));                      /* cmp.w #$19,d2 */
    lift_charge(x, 0x17D34);
    int inRange = c->cf || c->zf;                         /* bls.w loc_17D3E */
    lift_charge_bcc(x, 0x17D38, inRange);

    if (!inRange)
    {
      c->d[2] = alu_moveql(c, 0x19);                    /* moveq #$19,d2 */
      lift_charge(x, 0x17D3C);
    }

    /* loc_17D3E */
    for (;;)
    {
      alu_cmpb(c, lift_r8(x, c->a[0]), W(c->d[2]));     /* cmp.b (a0)+,d2 */
      c->a[0] += 1;
      lift_charge(x, 0x17D3E);

      int cond = c->zf, taken = 0, expd = 0;             /* dbeq d1,loc_17D3E */
      if (!cond)
      {
        expd = (W(c->d[1]) == 0);
        setw(&c->d[1], W(W(c->d[1]) - 1));
        taken = !expd;
      }
      lift_charge_dbcc(x, 0x17D40, taken, expd);
      if (!taken) break;
    }

    int found = c->zf;                                    /* bne.s loc_17D1C */
    lift_charge_bcc(x, 0x17D44, !found);
    if (found) break;
  }

  setw(&c->d[1], alu_eorw(c, 0xF, W(c->d[1])));         /* eor.w #$F,d1 */
  lift_charge(x, 0x17D46);
  lift_w16(x, 0xFFFFCEEEu, alu_movew(c, W(c->d[1])));   /* move.w d1,(abs) */
  lift_charge(x, 0x17D4A);
  lift_w16(x, 0xFFFFCEE8u, alu_movew(c, W(c->d[0])));   /* move.w d0,(abs) */
  lift_charge(x, 0x17D4E);
  lift_w16(x, 0xFFFFCEECu, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0x17D52);
  lift_w16(x, 0xFFFFCEEAu, alu_movew(c, 7));            /* move.w #7,(abs) */
  lift_charge(x, 0x17D56);

  alu_cmpw(c, 2, lift_r16(x, 0xFFFFD048u));             /* cmp.w #2,(abs) */
  lift_charge(x, 0x17D5C);
  int direct = c->zf;                                     /* beq.w sub_17D80 */
  lift_charge_bcc(x, 0x17D62, direct);

  if (direct)
  {
    sub_17D80(x);                                       /* tail: its rts pops our caller's return */
    return;
  }

  lift_w16(x, 0xFFFFCEEAu, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0x17D66);
  c->d[0] = alu_moveql(c, 7);                           /* moveq #7,d0 */
  lift_charge(x, 0x17D6A);
  c->a[0] = 0xFFFFCE66u;                                /* move.w #$CE66,a0 */
  lift_charge(x, 0x17D6C);

  for (;;)
  {
    /* loc_17D70 */
    lift_w16(x, c->a[0] + 4, alu_movew(c, 0));          /* clr.w 4(a0) */
    lift_charge(x, 0x17D70);
    lift_w16(x, c->a[0] + 6, alu_movew(c, 0));          /* clr.w 6(a0) */
    lift_charge(x, 0x17D74);
    c->a[0] += 0x10;                                    /* add.w #$10,a0 */
    lift_charge(x, 0x17D78);

    uint32_t nd0 = W(W(c->d[0]) - 1);                    /* dbf d0,loc_17D70 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x17D7C, taken, !taken);
    if (!taken) break;
  }

  sub_17D80(x);                                         /* tail: its rts pops our caller's return */
}

/*
 * sub_18AE8 (called from sub_9112+6, sub_9316+88, and others; d0-d3/a0/
 * a2 saved/restored via movem)
 *   in: a2 = struct pointer (passed straight through to
 *       Piece_AdvanceChain), d0 = hop count-1 (same)
 * Formats a name/number record into the text buffer at
 * $FFFFBFA6-based cursor: Piece_AdvanceChain resolves the record base,
 * a self-relative word chases it to a lookup row whose first byte is
 * written as two ASCII digits (Text_WriteTwoDigits) followed by a
 * space; the original record base is then read again for its own
 * length-prefixed string, copied as "first-initial. " + the remainder
 * after the first space, up to the length-derived end pointer;
 * finally Text_AlignBufferEven pads the buffer to an even length. The
 * two byte-scan loops (space-delimiter search, copy-to-end) are
 * bounded by the record's own length prefix / a guaranteed space
 * delimiter, not hardware polls — same family as the already-Done
 * sub_18BAE/18BC8/18BDC callees, just previously flagged defensively
 * by triage before those callees existed.
 */
void sub_18AE8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[6] = {
    c->a[2], c->a[0], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 6; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x18AE8);

  lift_call(x, 0x18AEC, 4, Piece_AdvanceChain);         /* bsr.w sub_18BC8 */
  if (x->declined) return;

  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);          /* move.l a0,-(sp) */
  lift_charge(x, 0x18AF0);
  c->a[1] = 0xFFFFBFA6u;                                /* move.w #$BFA6,a1 */
  lift_charge(x, 0x18AF2);
  c->a[0] += SEW(lift_r16(x, c->a[0]));                 /* add.w (a0),a0 */
  lift_charge(x, 0x18AF6);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));    /* move.b (a0),d0 */
  lift_charge(x, 0x18AF8);

  lift_call(x, 0x18AFA, 4, Text_WriteTwoDigits);        /* bsr.w sub_18BDC */
  if (x->declined) return;

  lift_w8(x, c->a[1], alu_moveb(c, 0x20));              /* move.b #$20,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x18AFE);

  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;         /* move.l (sp)+,a0 */
  lift_charge(x, 0x18B02);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));   /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0x18B04);
  c->a[2] = c->a[0] + SEW(c->d[0]) - 2;                 /* lea -2(a0,d0.w),a2 */
  lift_charge(x, 0x18B06);

  lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,(a1)+ */
  c->a[0] += 1;
  c->a[1] += 1;
  lift_charge(x, 0x18B0A);
  lift_w16(x, c->a[1], alu_movew(c, 0x2E20));           /* move.w #$2E20,(a1)+ */
  c->a[1] += 2;
  lift_charge(x, 0x18B0C);

  for (;;)
  {
    /* loc_18B10 */
    alu_cmpb(c, 0x20, lift_r8(x, c->a[0]));             /* cmp.b #$20,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x18B10);
    int more = !c->zf;                                    /* bne.s loc_18B10 */
    lift_charge_bcc(x, 0x18B14, more);
    if (!more) break;
  }

  for (;;)
  {
    /* loc_18B16 */
    lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,(a1)+ */
    c->a[0] += 1;
    c->a[1] += 1;
    lift_charge(x, 0x18B16);
    alu_cmpl(c, c->a[0], c->a[2]);                      /* cmp.l a0,a2 */
    lift_charge(x, 0x18B18);
    int more = !c->zf;                                    /* bne.s loc_18B16 */
    lift_charge_bcc(x, 0x18B1A, more);
    if (!more) break;
  }

  lift_call(x, 0x18B1C, 4, Text_AlignBufferEven);       /* bsr.w sub_18BAE */
  if (x->declined) return;

  c->d[0] = saved[5];                                   /* movem.l (sp)+,d0-d3/a0/a2 */
  c->d[1] = saved[4];
  c->d[2] = saved[3];
  c->d[3] = saved[2];
  c->a[0] = saved[1];
  c->a[2] = saved[0];
  c->a[7] += 6 * 4;
  lift_charge_movem(x, 0x18B20);

  lift_charge(x, 0x18B24);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F87EA (called from sub_F8304's loc_F840C; no movem)
 *   in: none
 * Countdown-gated cue-sequence state machine over $FFFFD42E/$FFFFD43C/
 * etc. On most calls, decrements $FFFFD440/$FFFFD43C and returns
 * (locret_F8860) without further effect. Occasionally (5/1061 calls in
 * the profiled 45k run) it reaches the `jsr loc_F8868` VDP-text/menu
 * chunk — a large routine chained through sub_11B92's inline-data call
 * convention that isn't lifted; that path declines, same mechanism as
 * a div-by-zero decline. The other rare exit (loc_F8862) tail-jumps
 * into the already-Done Sfx_SelectCue.
 */
void sub_F87EA(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFD440u, alu_subw(c, 1, lift_r16(x, 0xFFFFD440u)));  /* subq.w #1,(abs) */
  lift_charge(x, 0xF87EA);
  int stillCounting = !c->nf;                             /* bpl.w locret_F8860 */
  lift_charge_bcc(x, 0xF87EE, stillCounting);
  if (stillCounting)
  {
    lift_charge(x, 0xF8860);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, 0xFFFFD440u, alu_movew(c, 0xFFFF));       /* move.w #$FFFF,(abs) */
  lift_charge(x, 0xF87F2);
  lift_w8(x, 0xFFFFD42Eu, alu_bset(c, lift_r8(x, 0xFFFFD42Eu), 0));  /* bset #0,(abs) */
  lift_charge(x, 0xF87F8);

  alu_btst(c, lift_r8(x, 0xFFFFD42Eu), 0);              /* btst #0,(abs) */
  lift_charge(x, 0xF87FE);
  int active = !c->zf;                                    /* beq.w locret_F8860 */
  lift_charge_bcc(x, 0xF8804, !active);
  if (!active)
  {
    lift_charge(x, 0xF8860);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, 0xFFFFD42Eu), 5);              /* btst #5,(abs) */
  lift_charge(x, 0xF8808);
  int mode5 = !c->zf;                                     /* beq.w loc_F8822 */
  lift_charge_bcc(x, 0xF880E, !mode5);

  if (mode5)
  {
    alu_cmpw(c, 0x52, lift_r16(x, 0xFFFFD43Cu));        /* cmp.w #$52,(abs) */
    lift_charge(x, 0xF8812);
    int over = (c->nf == c->vf);                          /* bge.w loc_F8822 */
    lift_charge_bcc(x, 0xF8818, over);
    if (!over)
    {
      lift_w16(x, 0xFFFFD43Cu, alu_subw(c, 0x50, lift_r16(x, 0xFFFFD43Cu)));  /* sub.w #$50,(abs) */
      lift_charge(x, 0xF881C);
    }
  }

  /* loc_F8822 */
  lift_w16(x, 0xFFFFD43Cu, alu_subw(c, 1, lift_r16(x, 0xFFFFD43Cu)));  /* subq.w #1,(abs) */
  lift_charge(x, 0xF8822);
  int expired = c->nf;                                    /* bmi.w loc_F8862 */
  lift_charge_bcc(x, 0xF8826, expired);

  if (expired)
  {
    /* loc_F8862: jmp sub_F87D2 */
    lift_charge(x, 0xF8862);
    Sfx_SelectCue(x);                                    /* tail: its rts pops our caller's return */
    return;
  }

  alu_cmpw(c, 0x52, lift_r16(x, 0xFFFFD43Cu));          /* cmp.w #$52,(abs) */
  lift_charge(x, 0xF882A);
  int notReady = !c->zf;                                  /* bne.w locret_F8860 */
  lift_charge_bcc(x, 0xF8830, notReady);
  if (notReady)
  {
    lift_charge(x, 0xF8860);                              /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w8(x, 0xFFFFD42Eu, alu_bclr(c, lift_r8(x, 0xFFFFD42Eu), 2));  /* bclr #2,(abs) */
  lift_charge(x, 0xF8834);
  lift_w8(x, 0xFFFFD42Eu, alu_bclr(c, lift_r8(x, 0xFFFFD42Eu), 3));  /* bclr #3,(abs) */
  lift_charge(x, 0xF883A);
  lift_w8(x, 0xFFFFD42Eu, alu_bchg(c, lift_r8(x, 0xFFFFD42Eu), 1));  /* bchg #1,(abs) */
  lift_charge(x, 0xF8840);
  lift_w8(x, 0xFFFFD42Au, 0xFF);                        /* st (abs) */
  lift_charge(x, 0xF8846);
  lift_w8(x, 0xFFFFD42Cu, 0xFF);                        /* st (abs) */
  lift_charge(x, 0xF884A);
  lift_w8(x, 0xFFFFD42Eu, alu_bclr(c, lift_r8(x, 0xFFFFD42Eu), 6));  /* bclr #6,(abs) */
  lift_charge(x, 0xF884E);
  lift_w8(x, 0xFFFFD42Eu, alu_bclr(c, lift_r8(x, 0xFFFFD42Eu), 7));  /* bclr #7,(abs) */
  lift_charge(x, 0xF8854);

  /* jsr loc_F8868 — large VDP-text/menu chunk via sub_11B92's
   * inline-data call convention; not lifted. */
  x->declined = 1;
  return;
}

/*
 * Text_BuildNumberNameRecord (sub_18A90; called from ROM:85C8,
 * sub_8D4E+8, sub_18A6E+18 and others; d0-d3/a0/a2-a3 saved/restored
 * via movem)
 *   in:  a2 = team block base, d0 = roster hop count-1 (both consumed
 *        by Piece_AdvanceChain)
 *   out: a1 = $FFFFBFA4 (the record just built)
 * Builds the "<number> <name>" text record at $FFFFBFA4:
 * Piece_AdvanceChain resolves the roster entry base into a0; a
 * self-relative word at (a0) chases to the row whose first byte is the
 * jersey number, written as two ASCII digits by Text_WriteTwoDigits;
 * a single space is appended via the inline-literal path
 * (Text_AppendInlineString + the `dc.w 4 / dc.b $20,0` literal sitting
 * at $18AB2, in the code stream); finally the saved entry base is
 * popped back into a1 and its own length-prefixed name string is
 * appended with Text_AppendString.
 *
 * The inline literal is why triage.py reports "last insn 'bsr.w' not
 * rts — falls past end marker": execution resumes at $18AB6, after the
 * four literal bytes.  Same shape as the already-lifted sub_18AE8.
 */
void Text_BuildNumberNameRecord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d3/a0/a2-a3,-(sp): a3 pushed first, d0 lands lowest */
  uint32_t saved[7] = {
    c->a[3], c->a[2], c->a[0], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 7; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x18A90);

  lift_call(x, 0x18A94, 4, Piece_AdvanceChain);         /* bsr.w sub_18BC8 */
  if (x->declined) return;

  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);          /* move.l a0,-(sp) */
  lift_charge(x, 0x18A98);
  c->a[3] = 0xFFFFBFA4u;                                /* movea.w #$BFA4,a3 */
  lift_charge(x, 0x18A9A);
  lift_w16(x, c->a[3], alu_movew(c, 4));                /* move.w #4,(a3) */
  lift_charge(x, 0x18A9E);
  c->a[1] = c->a[3] + 2;                                /* lea 2(a3),a1 */
  lift_charge(x, 0x18AA2);
  c->a[0] += SEW(lift_r16(x, c->a[0]));                 /* adda.w (a0),a0 */
  lift_charge(x, 0x18AA6);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));    /* move.b (a0),d0 */
  lift_charge(x, 0x18AA8);

  lift_call(x, 0x18AAA, 4, Text_WriteTwoDigits);        /* bsr.w sub_18BDC */
  if (x->declined) return;

  /* bsr.w sub_11D96 — consumes the literal at $18AB2, resumes at $18AB6 */
  lift_call(x, 0x18AAE, 4, Text_AppendInlineString);
  if (x->declined) return;

  /* loc_18AB6 (past the inline literal) */
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;         /* move.l (sp)+,a1 */
  lift_charge(x, 0x18AB6);

  lift_call(x, 0x18AB8, 4, Text_AppendString);          /* bsr.w sub_11D9E */
  if (x->declined) return;

  c->a[1] = 0xFFFFBFA4u;                                /* movea.w #$BFA4,a1 */
  lift_charge(x, 0x18ABC);

  c->d[0] = saved[6];                                   /* movem.l (sp)+,... */
  c->d[1] = saved[5];
  c->d[2] = saved[4];
  c->d[3] = saved[3];
  c->a[0] = saved[2];
  c->a[2] = saved[1];
  c->a[3] = saved[0];
  c->a[7] += 7 * 4;
  lift_charge_movem(x, 0x18AC0);

  lift_charge(x, 0x18AC4);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_BuildNumberNameForCurrentSlot (sub_18A6E; called from
 * ROM:12628; d0/a2 saved/restored via movem)
 * Picks the home ($FFFFC6CE) or away (+$364) team block based on the
 * sign of the roster selector at $FFFFC470 — negative means away, in
 * which case the selector is masked to its low byte to give the slot
 * index — then defers to Text_BuildNumberNameRecord.
 */
void Text_BuildNumberNameForCurrentSlot(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/a2,-(sp): a2 pushed first, d0 lands lowest */
  uint32_t sv_a2 = c->a[2], sv_d0 = c->d[0];
  c->a[7] -= 4; lift_w32(x, c->a[7], sv_a2);
  c->a[7] -= 4; lift_w32(x, c->a[7], sv_d0);
  lift_charge_movem(x, 0x18A6E);

  c->a[2] = 0xFFFFC6CEu;                                /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x18A72);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC470u)));  /* move.w ($C470).w,d0 */
  lift_charge(x, 0x18A76);
  int away = c->nf;                                     /* bpl.w loc_18A86 */
  lift_charge_bcc(x, 0x18A7A, !away);
  if (away)
  {
    setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));      /* and.w #$FF,d0 */
    lift_charge(x, 0x18A7E);
    c->a[2] += 0x364;                                   /* adda.w #$364,a2 */
    lift_charge(x, 0x18A82);
  }

  /* loc_18A86 */
  lift_call(x, 0x18A86, 4, Text_BuildNumberNameRecord); /* bsr.w sub_18A90 */
  if (x->declined) return;

  c->d[0] = sv_d0;                                      /* movem.l (sp)+,d0/a2 */
  c->a[2] = sv_a2;
  c->a[7] += 8;
  lift_charge_movem(x, 0x18A8A);

  lift_charge(x, 0x18A8E);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Score_ClearLeadAnnouncedLatch (sub_FF87C; called from
 * Score_AnnounceLeadChange at $FF816 and $FF830)
 * Clears bit 5 of $FFFFC2EE — the "lead change already announced this
 * run" latch — so the next differing score fires a fresh announcement.
 */
void Score_ClearLeadAnnouncedLatch(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2EEu, alu_bclr(c, lift_r8(x, 0xFFFFC2EEu), 5));
  lift_charge(x, 0xFF87C);

  lift_charge(x, 0xFF882);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Score_AnnounceLeadChange (sub_FF7E2; called from ROM:$FCA6;
 * d0/a0-a3 saved/restored via movem, plus a word save/restore of
 * $FFFFC2EE across the whole body)
 * Compares the home ($FFFFC6CE) and away (+$364) team blocks' $24
 * fields and, when the lead changes hands, queues the matching
 * announcement through sub_FE556 — event id 1 when the home block ends
 * up leading, 4 when the away block does. Bit 6 of $FFFFC2EE remembers
 * which side was ahead (the latch is cleared via
 * Score_ClearLeadAnnouncedLatch whenever that flips); bit 5 is the
 * once-per-run guard, and the routine bails if it was already set.
 * Bit 1 of $FFFFC2FA suppresses announcements entirely.
 *
 * loc_FF884 is this routine's OWN epilogue chunk (the listing marks it
 * "START OF FUNCTION CHUNK FOR sub_FF7E2"); the four forward branches
 * into it are intra-routine jumps, not far-branches into someone
 * else's body.
 */
void Score_AnnounceLeadChange(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0/a0-a3,-(sp): a3 pushed first, d0 lands lowest */
  uint32_t saved[5] = { c->a[3], c->a[2], c->a[1], c->a[0], c->d[0] };
  for (int i = 0; i < 5; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFF7E2);

  c->a[7] -= 2;                                         /* move.w ($C2EE).w,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, 0xFFFFC2EEu)));
  lift_charge(x, 0xFF7E6);

  int done = 0;

  alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 1);              /* btst #1,($C2FA).w */
  lift_charge(x, 0xFF7EA);
  int suppressed = !c->zf;                              /* bne.w loc_FF884 */
  lift_charge_bcc(x, 0xFF7F0, suppressed);
  if (suppressed) done = 1;

  if (!done)
  {
    c->a[2] = 0xFFFFC6CEu;                              /* movea.w #$C6CE,a2 */
    lift_charge(x, 0xFF7F4);
    c->a[3] = c->a[2] + 0x364;                          /* lea $364(a2),a3 */
    lift_charge(x, 0xFF7F8);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x24)));  /* move.w $24(a2),d0 */
    lift_charge(x, 0xFF7FC);
    setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[3] + 0x24), W(c->d[0])));
    lift_charge(x, 0xFF800);                            /* sub.w $24(a3),d0 */

    int tied = c->zf;                                   /* beq.w loc_FF884 */
    lift_charge_bcc(x, 0xFF804, tied);
    if (tied) done = 1;

    if (!done)
    {
      int home_ahead = !c->nf;                          /* bpl.w loc_FF824 */
      lift_charge_bcc(x, 0xFF808, home_ahead);

      if (!home_ahead)
      {
        alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 6);        /* btst #6,($C2EE).w */
        lift_charge(x, 0xFF80C);
        int already = !c->zf;                           /* bne.w loc_FF83A */
        lift_charge_bcc(x, 0xFF812, already);
        if (!already)
        {
          lift_call(x, 0xFF816, 4, Score_ClearLeadAnnouncedLatch);
          if (x->declined) return;
          lift_w8(x, 0xFFFFC2EEu, alu_bset(c, lift_r8(x, 0xFFFFC2EEu), 6));
          lift_charge(x, 0xFF81A);                      /* bset #6,($C2EE).w */
          lift_charge_bcc(x, 0xFF820, 1);               /* bra.w loc_FF83A */
        }
      }
      else
      {
        /* loc_FF824 */
        uint32_t t = c->a[2]; c->a[2] = c->a[3]; c->a[3] = t;   /* exg a2,a3 */
        lift_charge(x, 0xFF824);
        alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 6);        /* btst #6,($C2EE).w */
        lift_charge(x, 0xFF826);
        int skip = c->zf;                               /* beq.w loc_FF83A */
        lift_charge_bcc(x, 0xFF82C, skip);
        if (!skip)
        {
          lift_call(x, 0xFF830, 4, Score_ClearLeadAnnouncedLatch);
          if (x->declined) return;
          lift_w8(x, 0xFFFFC2EEu, alu_bclr(c, lift_r8(x, 0xFFFFC2EEu), 6));
          lift_charge(x, 0xFF834);                      /* bclr #6,($C2EE).w */
        }
      }

      /* loc_FF83A */
      lift_w8(x, 0xFFFFC2EEu, alu_bset(c, lift_r8(x, 0xFFFFC2EEu), 5));
      lift_charge(x, 0xFF83A);                          /* bset #5,($C2EE).w */
      int announced = !c->zf;                           /* bne.w loc_FF884 */
      lift_charge_bcc(x, 0xFF840, announced);
      if (announced) done = 1;

      if (!done)
      {
        alu_cmpl(c, 0xFFFFC6CEu, c->a[3]);              /* cmpa.w #$C6CE,a3 */
        lift_charge(x, 0xFF844);
        int away = !c->zf;                              /* bne.w loc_FF868 */
        lift_charge_bcc(x, 0xFF848, away);

        if (!away)
        {
          lift_w16(x, 0xFFFFD6CAu,                      /* move.w ($C330).w,($D6CA).w */
                   alu_movew(c, lift_r16(x, 0xFFFFC330u)));
          lift_charge(x, 0xFF84C);
          lift_w16(x, 0xFFFFD6CCu, alu_movew(c, 1));    /* move.w #1,($D6CC).w */
          lift_charge(x, 0xFF852);
          lift_call(x, 0xFF858, 6, sub_FE556);          /* jsr sub_FE556 */
          if (x->declined) return;
        }
        else
        {
          /* loc_FF868 */
          lift_w16(x, 0xFFFFD6CAu,
                   alu_movew(c, lift_r16(x, 0xFFFFC330u)));
          lift_charge(x, 0xFF868);
          lift_w16(x, 0xFFFFD6CCu, alu_movew(c, 4));    /* move.w #4,($D6CC).w */
          lift_charge(x, 0xFF86E);
          lift_call(x, 0xFF874, 6, sub_FE556);          /* jsr sub_FE556 */
          if (x->declined) return;
          lift_charge_bcc(x, 0xFF87A, 1);               /* bra.s loc_FF85E */
        }

        /* loc_FF85E */
        lift_w8(x, 0xFFFFC2FEu, alu_bset(c, lift_r8(x, 0xFFFFC2FEu), 6));
        lift_charge(x, 0xFF85E);                        /* bset #6,($C2FE).w */
        lift_charge_bcc(x, 0xFF864, 1);                 /* bra.w loc_FF884 */
      }
    }
  }

  /* loc_FF884 — this routine's own epilogue chunk */
  lift_w16(x, 0xFFFFC2EEu, alu_movew(c, lift_r16(x, c->a[7])));
  c->a[7] += 2;                                         /* move.w (sp)+,($C2EE).w */
  lift_charge(x, 0xFF884);

  c->d[0] = saved[4];                                   /* movem.l (sp)+,d0/a0-a3 */
  c->a[0] = saved[3];
  c->a[1] = saved[2];
  c->a[2] = saved[1];
  c->a[3] = saved[0];
  c->a[7] += 5 * 4;
  lift_charge_movem(x, 0xFF888);

  lift_charge(x, 0xFF88C);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sram_SyncAltTeamRecord_body — the shared body at loc_F9C26, entered
 * either by sub_F9C20 (bclr #6 = read) or sub_F9C56 (bset #6 = write).
 * Structurally identical to Sram_SyncTeamRecord's body but for a second
 * 16-byte-per-slot region based at SRAM offset $D20 instead of $B60.
 *   in: a0 = RAM buffer, d1 = slot index
 */
static void Sram_SyncAltTeamRecord_body(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d1/a0-a1,-(sp) */
  c->a[7] -= 16;
  lift_w32(x, c->a[7] + 0, c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0xF9C26);

  c->d[0] = alu_movel(c, W(c->d[1]));                               /* move.l d1,d0 */
  lift_charge(x, 0xF9C2A);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));                       /* asl.w #4,d0 */
  lift_charge(x, 0xF9C2C);
  c->d[0] = alu_addl(c, 0xD20, c->d[0]);                            /* add.l #$D20,d0 */
  lift_charge(x, 0xF9C2E);
  c->d[1] = alu_moveql(c, 0x10);                                    /* moveq #$10,d1 */
  lift_charge(x, 0xF9C34);
  alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);                          /* btst #6,(abs) */
  lift_charge(x, 0xF9C36);
  int beqTaken = c->zf;
  lift_charge_bcc(x, 0xF9C3C, beqTaken);                            /* beq.w loc_F9C4A */
  if (!beqTaken)
  {
    lift_call(x, 0xF9C40, 6, SRAM_WriteBytes);                      /* jsr SRAM_WriteBytes */
    if (x->declined) return;
    lift_charge(x, 0xF9C46);                                        /* bra.w loc_F9C50 */
  }
  else
  {
    lift_call(x, 0xF9C4A, 6, SRAM_ReadBytes);                       /* jsr SRAM_ReadBytes */
    if (x->declined) return;
  }

  /* loc_F9C50: movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7] + 0);
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0xF9C50);

  lift_charge(x, 0xF9C54);                                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* sub_F9C20 — read entry for the $D20 region. */
void Sram_SyncAltTeamRecord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bclr #6,(abs) */
  lift_charge(x, 0xF9C20);
  Sram_SyncAltTeamRecord_body(x);
}

/* sub_F9C56 — the WRITE twin of sub_F9C20; same body, bit 6 set. */
void Sram_SyncAltTeamRecordWrite(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 6));  /* bset #6,(abs) */
  lift_charge(x, 0xF9C56);
  lift_charge_bcc(x, 0xF9C5C, 1);                                    /* bra.s loc_F9C26 */
  Sram_SyncAltTeamRecord_body(x);
}

/* --- Roster / stat-table reset helpers (postgame + season menu) --- */

void Team_ClearLineTable(lift_ctx *);   /* forward: defined below */
void Roster_PartitionListedPlayers(lift_ctx *);  /* forward: defined below */

/*
 * Team_ClearLineTable (sub_FF8C8)
 *   in:  a0 = team block base ($FFFFC6CE home / $FFFFCA32 away)
 *   out: 26 words of $FFFE written at team+$66, terminated by $FFFF;
 *        d0 = -1, a0 = team+$66+52
 */
void Team_ClearLineTable(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0x19));                  /* move.w #$19,d0 */
  lift_charge(x, 0xFF8C8);
  c->a[0] = (c->a[0] + 0x66) & 0xFFFFFFFFu;            /* adda.w #$66,a0: no CCR */
  lift_charge(x, 0xFF8CC);

  for (;;)
  {
    lift_w16(x, c->a[0], 0xFFFE);                      /* move.w #$FFFE,(a0)+ */
    alu_movew(c, 0xFFFE);
    c->a[0] += 2;
    lift_charge(x, 0xFF8D0);

    setw(&c->d[0], W(c->d[0]) - 1);                    /* dbf d0: no CCR */
    if (W(c->d[0]) != 0xFFFF) { lift_charge_dbcc(x, 0xFF8D4, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFF8D4, 0, 1);
    break;
  }

  lift_w16(x, c->a[0], 0xFFFF);                        /* move.w #$FFFF,(a0) */
  alu_movew(c, 0xFFFF);
  lift_charge(x, 0xFF8D8);

  lift_charge(x, 0xFF8DC);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Game_ResetPeriodStats (sub_FF88E; called from sub_7B30)
 * Clears the three counters at $FFFFC3E6/E8/EA and the 17-long block at
 * $FFFFC3A4, then resets both teams' line tables.
 */
void Game_ResetPeriodStats(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  sp -= 8;                                             /* movem.l d0/a0,-(sp) */
  lift_w16(x, sp + 0, (c->d[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[0] & 0xFFFF);
  lift_w16(x, sp + 4, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 6, c->a[0] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0xFF88E);

  lift_w16(x, 0xFFFFC3EAu, 0); alu_movew(c, 0);        /* clr.w ($FFFFC3EA).w */
  lift_charge(x, 0xFF892);
  lift_w16(x, 0xFFFFC3E8u, 0); alu_movew(c, 0);        /* clr.w ($FFFFC3E8).w */
  lift_charge(x, 0xFF896);
  lift_w16(x, 0xFFFFC3E6u, 0); alu_movew(c, 0);        /* clr.w ($FFFFC3E6).w */
  lift_charge(x, 0xFF89A);

  setw(&c->d[0], alu_movew(c, 0x10));                  /* move.w #$10,d0 */
  lift_charge(x, 0xFF89E);
  c->a[0] = 0xFFFFC3A4u;                               /* movea.l #$FFFFC3A4,a0 */
  lift_charge(x, 0xFF8A2);

  for (;;)
  {
    lift_w32(x, c->a[0], 0); alu_movel(c, 0);          /* clr.l (a0)+ */
    c->a[0] += 4;
    lift_charge(x, 0xFF8A8);

    setw(&c->d[0], W(c->d[0]) - 1);                    /* dbf d0 */
    if (W(c->d[0]) != 0xFFFF) { lift_charge_dbcc(x, 0xFF8AA, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFF8AA, 0, 1);
    break;
  }

  c->a[0] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFF8AE);
  lift_call(x, 0xFF8B4, 4, Team_ClearLineTable);       /* bsr.w sub_FF8C8 */
  if (x->declined) return;

  c->a[0] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a0 */
  lift_charge(x, 0xFF8B8);
  lift_call(x, 0xFF8BE, 4, Team_ClearLineTable);       /* bsr.w sub_FF8C8 */
  if (x->declined) return;

  c->d[0] = saved_d0;                                  /* movem.l (sp)+,d0/a0 */
  c->a[0] = saved_a0;
  c->a[7] = sp + 8;
  lift_charge_movem(x, 0xFF8C2);

  lift_charge(x, 0xFF8C6);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_PartitionListedPlayers (sub_FAFE4)
 *   in:  a0 = team block base, a1 = destination buffer
 * a0's roster pointer ($1E(a0)) plus the word at +6 gives a 6-byte header
 * of 1-based player indexes. Player indexes 0..25 are then split: the six
 * that appear in the header land at (a1)+, the rest at (a1+6)+.
 */
void Roster_PartitionListedPlayers(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = lift_r32(x, c->a[0] + 0x1E) & 0xFFFFFFFFu; /* movea.l $1E(a0),a0 */
  lift_charge(x, 0xFAFE4);
  c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0] + 6))) & 0xFFFFFFFFu; /* adda.w 6(a0),a0 */
  lift_charge(x, 0xFAFE8);
  c->a[2] = c->a[1];                                   /* movea.l a1,a2 */
  lift_charge(x, 0xFAFEC);
  c->a[2] = (c->a[2] + 6) & 0xFFFFFFFFu;               /* addq.w #6,a2: no CCR */
  lift_charge(x, 0xFAFEE);
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0xFAFF0);

  for (;;)
  {
    int hit = 0;
    setw(&c->d[1], alu_movew(c, 5));                   /* move.w #5,d1 */
    lift_charge(x, 0xFAFF2);

    for (;;)
    {
      setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1])))); /* move.b (a0,d1.w),d3 */
      lift_charge(x, 0xFAFF6);
      setb(&c->d[3], alu_subb(c, 1, c->d[3] & 0xFF));  /* subq.b #1,d3 */
      lift_charge(x, 0xFAFFA);
      alu_cmpb(c, c->d[3] & 0xFF, c->d[0] & 0xFF);     /* cmp.b d3,d0 */
      lift_charge(x, 0xFAFFC);
      lift_charge_bcc(x, 0xFAFFE, c->zf);              /* beq.w loc_FB00C */
      if (c->zf) { hit = 1; break; }

      setw(&c->d[1], W(c->d[1]) - 1);                  /* dbf d1: no CCR */
      if (W(c->d[1]) != 0xFFFF) { lift_charge_dbcc(x, 0xFB002, 1, 0); continue; }
      lift_charge_dbcc(x, 0xFB002, 0, 1);
      break;
    }

    if (!hit)
    {
      lift_w8(x, c->a[2], c->d[0] & 0xFF);             /* move.b d0,(a2)+ */
      alu_moveb(c, c->d[0] & 0xFF);
      c->a[2] += 1;
      lift_charge(x, 0xFB006);
      lift_charge_bcc(x, 0xFB008, 1);                  /* bra.w loc_FB00E */
    }
    else
    {
      lift_w8(x, c->a[1], c->d[0] & 0xFF);             /* move.b d0,(a1)+ */
      alu_moveb(c, c->d[0] & 0xFF);
      c->a[1] += 1;
      lift_charge(x, 0xFB00C);
    }

    /* loc_FB00E */
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0xFB00E);
    alu_cmpw(c, 0x1A, W(c->d[0]));                     /* cmpi.w #$1A,d0 */
    lift_charge(x, 0xFB010);
    {
      int lt = (!!c->nf) != (!!c->vf);
      lift_charge_bcc(x, 0xFB014, lt);                 /* blt.s loc_FAFF2 */
      if (lt) continue;
    }
    break;
  }

  lift_charge(x, 0xFB016);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_PartitionBothTeams (sub_FAFBA)
 * Runs Roster_PartitionListedPlayers for the home block into $FFFFD4FA and
 * the away block into $FFFFD514. All registers restored by the epilogue.
 */
void Roster_PartitionBothTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];

  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFAFBA);

  c->a[1] = 0xFFFFD4FAu;                               /* movea.l #$FFFFD4FA,a1 */
  lift_charge(x, 0xFAFBE);
  c->a[0] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFAFC4);
  lift_call(x, 0xFAFCA, 4, Roster_PartitionListedPlayers);
  if (x->declined) return;

  c->a[1] = 0xFFFFD514u;                               /* movea.l #$FFFFD514,a1 */
  lift_charge(x, 0xFAFCE);
  c->a[0] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a0 */
  lift_charge(x, 0xFAFD4);
  lift_call(x, 0xFAFDA, 4, Roster_PartitionListedPlayers);
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFAFDE);

  lift_charge(x, 0xFAFE2);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Name-entry / menu helpers (season menu + postgame) --- */

#define ALPHABET_TABLE 0xFB95Cu   /* ROM: "ABCDEFGHIJKLMNOPQRSTUVWXYZ.12 -" */

void Text_InitialCharToIndex(lift_ctx *);   /* forward: defined below */

/*
 * Text_InitialCharToIndex (sub_FB750)
 *   in:  d0.b = character
 *   out: d0.w = its index in ALPHABET_TABLE, or $1E if not found
 * d1-d3/a0-a6 are restored by the epilogue.
 */
void Text_InitialCharToIndex(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[10];
  int i;

  saved[0] = c->d[1]; saved[1] = c->d[2]; saved[2] = c->d[3];
  for (i = 0; i < 7; i++) saved[3 + i] = c->a[i];

  sp -= 40;                                            /* movem.l d1-d3/a0-a6,-(sp) */
  for (i = 0; i < 10; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFB750);

  c->a[0] = ALPHABET_TABLE;                            /* movea.l #table,a0 */
  lift_charge(x, 0xFB754);
  setb(&c->d[1], alu_moveb(c, c->d[0] & 0xFF));        /* move.b d0,d1 */
  lift_charge(x, 0xFB75A);
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0xFB75C);
  setw(&c->d[3], alu_movew(c, 0x1E));                  /* move.w #$1E,d3 */
  lift_charge(x, 0xFB75E);

  for (;;)
  {
    alu_cmpb(c, lift_r8(x, c->a[0] + SEW(c->d[0])), c->d[1] & 0xFF); /* cmp.b (a0,d0.w),d1 */
    lift_charge(x, 0xFB762);
    lift_charge_bcc(x, 0xFB766, c->zf);                /* beq.w loc_FB774 */
    if (c->zf) goto done;

    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0xFB76A);

    setw(&c->d[3], W(c->d[3]) - 1);                    /* dbf d3: no CCR */
    if (W(c->d[3]) != 0xFFFF) { lift_charge_dbcc(x, 0xFB76C, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFB76C, 0, 1);
    break;
  }

  setw(&c->d[0], alu_movew(c, 0x1E));                  /* move.w #$1E,d0 */
  lift_charge(x, 0xFB770);

done:
  /* loc_FB774: movem.l (sp)+,d1-d3/a0-a6 */
  c->d[1] = saved[0]; c->d[2] = saved[1]; c->d[3] = saved[2];
  for (i = 0; i < 7; i++) c->a[i] = saved[3 + i];
  c->a[7] = sp + 40;
  lift_charge_movem(x, 0xFB774);

  lift_charge(x, 0xFB778);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_NormalizeInitialChar (sub_FB722)
 * Round-trips the character at $FFFFD4DA through ALPHABET_TABLE, replacing
 * it with the canonical table entry (and leaving d5 = its index, 0 when the
 * character is not in the table). d0-d4/a0-a6 are restored.
 */
void Text_NormalizeInitialChar(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[12];
  int i;

  for (i = 0; i < 5; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[5 + i] = c->a[i];

  sp -= 48;                                            /* movem.l d0-d4/a0-a6,-(sp) */
  for (i = 0; i < 12; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFB722);

  setb(&c->d[0], alu_moveb(c, lift_r8(x, 0xFFFFD4DAu))); /* move.b ($FFFFD4DA).w,d0 */
  lift_charge(x, 0xFB726);

  lift_call(x, 0xFB72A, 4, Text_InitialCharToIndex);   /* bsr.w sub_FB750 */
  if (x->declined) return;

  alu_cmpw(c, 0x1E, W(c->d[0]));                       /* cmpi.w #$1E,d0 */
  lift_charge(x, 0xFB72E);
  {
    int lt = (!!c->nf) != (!!c->vf);
    lift_charge_bcc(x, 0xFB732, lt);                   /* blt.w loc_FB73C */
    if (!lt)
    {
      setw(&c->d[5], alu_movew(c, 0));                 /* clr.w d5 */
      lift_charge(x, 0xFB736);
      lift_charge_bcc(x, 0xFB738, 1);                  /* bra.w loc_FB74A */
    }
    else
    {
      setw(&c->d[5], alu_movew(c, W(c->d[0])));        /* move.w d0,d5 */
      lift_charge(x, 0xFB73C);
      c->a[0] = ALPHABET_TABLE;                        /* movea.l #table,a0 */
      lift_charge(x, 0xFB73E);
      {
        uint32_t v = lift_r8(x, c->a[0] + SEW(c->d[5]));
        lift_w8(x, 0xFFFFD4DAu, v);                    /* move.b (a0,d5.w),(abs).w */
        alu_moveb(c, v);
      }
      lift_charge(x, 0xFB744);
    }
  }

  /* loc_FB74A: movem.l (sp)+,d0-d4/a0-a6 */
  for (i = 0; i < 5; i++) c->d[i] = saved[i];
  for (i = 0; i < 7; i++) c->a[i] = saved[5 + i];
  c->a[7] = sp + 48;
  lift_charge_movem(x, 0xFB74A);

  lift_charge(x, 0xFB74E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_LoadSelectedEntry (sub_FAF8C)
 * Reads the ($FFFFD4F4 - 1)'th byte of the active team's partition buffer
 * ($FFFFD4FA home / $FFFFD514 away, chosen by $FFFFD4F6) into d0, sign
 * extended, and mirrors it to $FFFFD52E.
 */
void Roster_LoadSelectedEntry(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_d1 = c->d[1], saved_a0 = c->a[0];

  sp -= 8;                                             /* movem.l d1/a0,-(sp) */
  lift_w16(x, sp + 0, (c->d[1] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[1] & 0xFFFF);
  lift_w16(x, sp + 4, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 6, c->a[0] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0xFAF8C);

  c->a[0] = 0xFFFFD4FAu;                               /* movea.l #$FFFFD4FA,a0 */
  lift_charge(x, 0xFAF90);
  alu_tstw(c, lift_r16(x, 0xFFFFD4F6u));               /* tst.w ($FFFFD4F6).w */
  lift_charge(x, 0xFAF96);
  lift_charge_bcc(x, 0xFAF9A, c->zf);                  /* beq.w loc_FAFA4 */
  if (!c->zf)
  {
    c->a[0] = 0xFFFFD514u;                             /* movea.l #$FFFFD514,a0 */
    lift_charge(x, 0xFAF9E);
  }

  /* loc_FAFA4 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD4F4u))); /* move.w (abs).w,d1 */
  lift_charge(x, 0xFAFA4);
  setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));          /* subq.w #1,d1 */
  lift_charge(x, 0xFAFA8);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1])))); /* move.b (a0,d1.w),d0 */
  lift_charge(x, 0xFAFAA);
  setw(&c->d[0], alu_extw(c, c->d[0]));                /* ext.w d0 */
  lift_charge(x, 0xFAFAE);
  lift_w16(x, 0xFFFFD52Eu, W(c->d[0]));                /* move.w d0,($FFFFD52E).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xFAFB0);

  c->d[1] = saved_d1;                                  /* movem.l (sp)+,d1/a0 */
  c->a[0] = saved_a0;
  c->a[7] = sp + 8;
  lift_charge_movem(x, 0xFAFB4);

  lift_charge(x, 0xFAFB8);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_TrimTrailingSpaces (sub_FAF66)
 *   in:  a2 = { word length, bytes... }
 * Strips trailing spaces (zeroing them), then rounds the length up to the
 * next even value. a3 is restored; d0 holds the last byte examined.
 */
void Text_TrimTrailingSpaces(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_a3 = c->a[3];
  int guard = 0;

  sp -= 4;                                             /* movem.l a3,-(sp) */
  lift_w16(x, sp + 0, (c->a[3] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[3] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0xFAF66);

  c->a[3] = c->a[2];                                   /* movea.l a2,a3 */
  lift_charge(x, 0xFAF6A);
  c->a[3] = (c->a[3] + SEW(lift_r16(x, c->a[2]))) & 0xFFFFFFFFu; /* adda.w (a2),a3 */
  lift_charge(x, 0xFAF6C);

  for (;;)
  {
    if (++guard > 0x10000) { x->declined = 1; return; }

    c->a[3] -= 1;                                      /* move.b -(a3),d0 */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3])));
    lift_charge(x, 0xFAF6E);
    alu_cmpb(c, 0x20, c->d[0] & 0xFF);                 /* cmpi.b #$20,d0 */
    lift_charge(x, 0xFAF70);
    lift_charge_bcc(x, 0xFAF74, !c->zf);               /* bne.w loc_FAF80 */
    if (!c->zf) break;

    lift_w8(x, c->a[3], 0); alu_moveb(c, 0);           /* move.b #0,(a3) */
    lift_charge(x, 0xFAF78);
    lift_w16(x, c->a[2], alu_subw(c, 1, lift_r16(x, c->a[2]))); /* subq.w #1,(a2) */
    lift_charge(x, 0xFAF7C);
    lift_charge_bcc(x, 0xFAF7E, 1);                    /* bra.s loc_FAF6E */
  }

  /* loc_FAF80 */
  lift_w16(x, c->a[2], alu_addw(c, 1, lift_r16(x, c->a[2]))); /* addq.w #1,(a2) */
  lift_charge(x, 0xFAF80);
  lift_w16(x, c->a[2], alu_andw(c, 0xFE, lift_r16(x, c->a[2]))); /* andi.w #$FE,(a2) */
  lift_charge(x, 0xFAF82);

  c->a[3] = saved_a3;                                  /* movem.l (sp)+,a3 */
  c->a[7] = sp + 4;
  lift_charge_movem(x, 0xFAF86);

  lift_charge(x, 0xFAF8A);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_SaveRowIndex (sub_FDD9C) — d3 = ($FFFFD5AE >> 4) + 6, then a bra to
 * the bare rts that immediately follows.
 */
void Menu_SaveRowIndex(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFD5AEu))); /* move.w (abs).w,d3 */
  lift_charge(x, 0xFDD9C);
  setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 4));          /* lsr.w #4,d3 */
  lift_charge(x, 0xFDDA0);
  setw(&c->d[3], alu_addw(c, 6, W(c->d[3])));          /* addq.w #6,d3 */
  lift_charge(x, 0xFDDA2);
  lift_charge_bcc(x, 0xFDDA4, 1);                      /* bra.w locret_FDDA8 */

  lift_charge(x, 0xFDDA8);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Nullsub_6 (sub_FA934) — a bare rts. */
void Nullsub_6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge(x, 0xFA934);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Small object/team selectors (batch 3) --- */

/*
 * Object_ResetSlotTimers (sub_13098)
 * Walks the 26 word-slots at $66(a2) downward; any slot that is not the
 * $FFFD/$FFFC sentinel pair gets $1000 stored at $32(a2,d0.w).
 */
void Object_ResetSlotTimers(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_moveql(c, 0x32);                       /* moveq #$32,d0 */
  lift_charge(x, 0x13098);

  for (;;)
  {
    int store = 1;
    alu_cmpw(c, 0xFFFD, lift_r16(x, c->a[2] + SEW(c->d[0]) + 0x66)); /* cmpi.w */
    lift_charge(x, 0x1309A);
    lift_charge_bcc(x, 0x130A0, !c->zf);               /* bne.w loc_130B2 */
    if (c->zf)
    {
      alu_cmpw(c, 0xFFFC, lift_r16(x, c->a[2] + SEW(c->d[0]) + 0x66)); /* cmpi.w */
      lift_charge(x, 0x130A4);
      lift_charge_bcc(x, 0x130AA, !c->zf);             /* bne.w loc_130B2 */
      if (c->zf)
      {
        lift_charge_bcc(x, 0x130AE, 1);                /* bra.w loc_130B8 */
        store = 0;
      }
    }

    if (store)
    {
      lift_w16(x, c->a[2] + SEW(c->d[0]) + 0x32, 0x1000); /* move.w #$1000,... */
      alu_movew(c, 0x1000);
      lift_charge(x, 0x130B2);
    }

    /* loc_130B8 */
    setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));        /* subq.w #2,d0 */
    lift_charge(x, 0x130B8);
    lift_charge_bcc(x, 0x130BA, !c->nf);               /* bpl.s loc_1309A */
    if (!c->nf) continue;
    break;
  }

  lift_charge(x, 0x130BC);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_StoreClampedTimer (sub_1577A)
 * Stores d0 (clamped to >= 0) into $32(a2,d1.w).
 */
void Object_StoreClampedTimer(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_tstw(c, W(c->d[0]));                             /* tst.w d0 */
  lift_charge(x, 0x1577A);
  lift_charge_bcc(x, 0x1577C, !c->nf);                 /* bpl.w loc_15782 */
  if (c->nf)
  {
    setw(&c->d[0], alu_movew(c, 0));                   /* clr.w d0 */
    lift_charge(x, 0x15780);
  }

  lift_w16(x, c->a[2] + SEW(c->d[1]) + 0x32, W(c->d[0])); /* move.w d0,$32(a2,d1.w) */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0x15782);

  lift_charge(x, 0x15786);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_SelectByPossessionFlag (sub_7E0E)
 * a2 = home team block, advanced to the away block unless the side
 * indicated by rink-flip bit 1 ($FFFFC2EC) has the value 1 in its
 * possession word ($FFFFC32A flipped / $FFFFC328 normal).
 */
void Team_SelectByPossessionFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = SEW(0xC6CE);                               /* movea.w #$C6CE,a2: sign-extends */
  lift_charge(x, 0x7E0E);
  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 1);             /* btst #1,($FFFFC2EC).w */
  lift_charge(x, 0x7E12);
  lift_charge_bcc(x, 0x7E18, c->zf);                   /* beq.w loc_7E26 */
  if (!c->zf)
  {
    alu_cmpw(c, 1, lift_r16(x, 0xFFFFC32Au));          /* cmpi.w #1,($FFFFC32A).w */
    lift_charge(x, 0x7E1C);
    lift_charge_bcc(x, 0x7E22, 1);                     /* bra.w loc_7E2C */
  }
  else
  {
    alu_cmpw(c, 1, lift_r16(x, 0xFFFFC328u));          /* cmpi.w #1,($FFFFC328).w */
    lift_charge(x, 0x7E26);
  }

  /* loc_7E2C */
  lift_charge_bcc(x, 0x7E2C, c->zf);                   /* beq.w locret_7E34 */
  if (!c->zf)
  {
    c->a[2] = (c->a[2] + 0x364) & 0xFFFFFFFFu;         /* adda.w #$364,a2: no CCR */
    lift_charge(x, 0x7E30);
  }

  lift_charge(x, 0x7E34);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_WrapCursorWord (sub_FA2A2)
 *   in: a0 -> cursor word; $FFFFD4F8 = item count
 * Wraps the cursor to 0 when it has run past the last item, and to the
 * last item when it has gone negative. d0 is restored word-wise.
 */
void Menu_WrapCursorWord(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];

  sp -= 2;                                             /* move.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  alu_movew(c, W(c->d[0]));
  c->a[7] = sp;
  lift_charge(x, 0xFA2A2);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD4F8u))); /* move.w (abs).w,d0 */
  lift_charge(x, 0xFA2A4);
  setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));          /* addq.w #1,d0 */
  lift_charge(x, 0xFA2A8);
  alu_cmpw(c, lift_r16(x, c->a[0]), W(c->d[0]));       /* cmp.w (a0),d0 */
  lift_charge(x, 0xFA2AA);
  lift_charge_bcc(x, 0xFA2AC, !c->zf);                 /* bne.w loc_FA2B2 */
  if (c->zf)
  {
    lift_w16(x, c->a[0], 0); alu_movew(c, 0);          /* clr.w (a0) */
    lift_charge(x, 0xFA2B0);
  }

  /* loc_FA2B2 */
  alu_tstw(c, lift_r16(x, c->a[0]));                   /* tst.w (a0) */
  lift_charge(x, 0xFA2B2);
  lift_charge_bcc(x, 0xFA2B4, !c->nf);                 /* bpl.w loc_FA2BC */
  if (c->nf)
  {
    setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));        /* subq.w #1,d0 */
    lift_charge(x, 0xFA2B8);
    lift_w16(x, c->a[0], W(c->d[0]));                  /* move.w d0,(a0) */
    alu_movew(c, W(c->d[0]));
    lift_charge(x, 0xFA2BA);
  }

  /* loc_FA2BC: move.w (sp)+,d0 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));
  c->a[7] += 2;
  lift_charge(x, 0xFA2BC);

  lift_charge(x, 0xFA2BE);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_SelectByStatWord (sub_FEE4A)
 * a2 = the team block whose $28 word does NOT equal d2 (home first).
 */
void Team_SelectByStatWord(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xFEE4A);
  alu_cmpw(c, lift_r16(x, c->a[2] + 0x28), W(c->d[2])); /* cmp.w $28(a2),d2 */
  lift_charge(x, 0xFEE50);
  lift_charge_bcc(x, 0xFEE54, c->zf);                  /* beq.w locret_FEE5E */
  if (!c->zf)
  {
    c->a[2] = TEAM_HOME + TEAM_SIZE;                   /* movea.l #$FFFFCA32,a2 */
    lift_charge(x, 0xFEE58);
  }

  lift_charge(x, 0xFEE5E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stack_UnwindNineRegs (sub_8D82) — a shared epilogue trampoline:
 * movem.l (sp)+,d0-d4/a0-a1/a4-a5 then rts.
 */
void Stack_UnwindNineRegs(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  int i;

  for (i = 0; i < 5; i++) c->d[i] = lift_r32(x, sp + i * 4);
  c->a[0] = lift_r32(x, sp + 20);
  c->a[1] = lift_r32(x, sp + 24);
  c->a[4] = lift_r32(x, sp + 28);
  c->a[5] = lift_r32(x, sp + 32);
  c->a[7] = sp + 36;
  lift_charge_movem(x, 0x8D82);

  lift_charge(x, 0x8D86);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Camera clamp + on-ice event handlers (batch 4) --- */

void Anim_SetScript(lift_ctx *);              /* anim.c ($1073A) */
void Object_AdvanceStateMod8(lift_ctx *);     /* ($10646) */

/*
 * Iter_AdvanceObjectCursor (sub_A600)
 * Steps a4 by $62 through the object array and wraps it to $FFFF0000 when
 * it reaches the $FFFFAF54 end marker.
 */
void Iter_AdvanceObjectCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[4] = (c->a[4] + 0x62) & 0xFFFFFFFFu;            /* adda.w #$62,a4: no CCR */
  lift_charge(x, 0xA600);
  alu_cmpl(c, 0xFFFFAF54u, c->a[4]);                   /* cmpa.l #$FFFFAF54,a4 */
  lift_charge(x, 0xA604);
  lift_charge_bcc(x, 0xA60A, !c->zf);                  /* bne.w locret_A9D4 */
  if (!c->zf)
  {
    lift_charge(x, 0xA9D4);                            /* locret_A9D4: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[4] = 0xFFFF0000u;                               /* movea.l #$FFFF0000,a4 */
  lift_charge(x, 0xA60E);
  lift_charge(x, 0xA614);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Camera_ApplyTrackedTarget (sub_A616)
 * Mirrors the tracked object's world position (negated when rink-flip bit 7
 * of $FFFFC2F4 is set) into the follower at (a5), then clamps it to the
 * camera limits and stores it at $FFFFBD1C / $FFFFBD18.
 */
void Camera_ApplyTrackedTarget(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[3];
  int i;

  for (i = 0; i < 3; i++) saved[i] = c->d[i];
  sp -= 12;                                            /* movem.l d0-d2,-(sp) */
  for (i = 0; i < 3; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xA616);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3])));  /* move.w (a3),d0 */
  lift_charge(x, 0xA61A);
  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 7);             /* btst #7,($FFFFC2F4).w */
  lift_charge(x, 0xA61C);
  lift_charge_bcc(x, 0xA622, c->zf);                   /* beq.w loc_A628 */
  if (!c->zf)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));           /* neg.w d0 */
    lift_charge(x, 0xA626);
  }

  /* loc_A628 */
  lift_w16(x, c->a[5], W(c->d[0]));                    /* move.w d0,(a5) */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xA628);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 0x14))); /* move.w $14(a3),d1 */
  lift_charge(x, 0xA62A);
  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 7);             /* btst #7,($FFFFC2F4).w */
  lift_charge(x, 0xA62E);
  lift_charge_bcc(x, 0xA634, c->zf);                   /* beq.w loc_A63A */
  if (!c->zf)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));           /* neg.w d1 */
    lift_charge(x, 0xA638);
  }

  /* loc_A63A */
  lift_w16(x, c->a[5] + 0x14, W(c->d[1]));             /* move.w d1,$14(a5) */
  alu_movew(c, W(c->d[1]));
  lift_charge(x, 0xA63A);

  alu_cmpw(c, 0x3C, W(c->d[0]));                       /* cmpi.w #$3C,d0 */
  lift_charge(x, 0xA63E);
  {
    int lt = (!!c->nf) != (!!c->vf);
    lift_charge_bcc(x, 0xA642, lt);                    /* blt.w loc_A64A */
    if (!lt) { setw(&c->d[0], alu_movew(c, 0x3C)); lift_charge(x, 0xA646); }
  }
  alu_cmpw(c, 0xFFC4, W(c->d[0]));                     /* cmpi.w #$FFC4,d0 */
  lift_charge(x, 0xA64A);
  {
    int gt = !c->zf && ((!!c->nf) == (!!c->vf));
    lift_charge_bcc(x, 0xA64E, gt);                    /* bgt.w loc_A656 */
    if (!gt) { setw(&c->d[0], alu_movew(c, 0xFFC4)); lift_charge(x, 0xA652); }
  }
  /* loc_A656 */
  lift_w16(x, 0xFFFFBD1Cu, W(c->d[0]));                /* move.w d0,($FFFFBD1C).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xA656);

  alu_cmpw(c, 0x100, W(c->d[1]));                      /* cmpi.w #$100,d1 */
  lift_charge(x, 0xA65A);
  {
    int lt = (!!c->nf) != (!!c->vf);
    lift_charge_bcc(x, 0xA65E, lt);                    /* blt.w loc_A666 */
    if (!lt) { setw(&c->d[1], alu_movew(c, 0x100)); lift_charge(x, 0xA662); }
  }
  alu_cmpw(c, 0xFF38, W(c->d[1]));                     /* cmpi.w #$FF38,d1 */
  lift_charge(x, 0xA666);
  {
    int gt = !c->zf && ((!!c->nf) == (!!c->vf));
    lift_charge_bcc(x, 0xA66A, gt);                    /* bgt.w loc_A672 */
    if (!gt) { setw(&c->d[1], alu_movew(c, 0xFF38)); lift_charge(x, 0xA66E); }
  }
  /* loc_A672 */
  lift_w16(x, 0xFFFFBD18u, W(c->d[1]));                /* move.w d1,($FFFFBD18).w */
  alu_movew(c, W(c->d[1]));
  lift_charge(x, 0xA672);

  for (i = 0; i < 3; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d2 */
  c->a[7] = sp + 12;
  lift_charge_movem(x, 0xA676);

  lift_charge(x, 0xA67A);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Player_ZoneWhistleCheck (sub_CA0C)
 * Bit 3 of $62(a3) gates a whistle: d4 = 0 when the player's camera zone
 * matches $FFFFC320, else 2; both paths tail into sub_BFBC.
 */
void Player_ZoneWhistleCheck(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 3);          /* btst #3,$62(a3) */
  lift_charge(x, 0xCA0C);
  lift_charge_bcc(x, 0xCA12, c->zf);                   /* beq.w locret_C880 */
  if (c->zf)
  {
    lift_charge(x, 0xC880);                            /* locret_C880: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[4], alu_movew(c, 0));                     /* clr.w d4 */
  lift_charge(x, 0xCA16);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x52))); /* move.w $52(a3),d0 */
  lift_charge(x, 0xCA18);
  alu_cmpw(c, lift_r16(x, 0xFFFFC320u), W(c->d[0]));   /* cmp.w ($FFFFC320).w,d0 */
  lift_charge(x, 0xCA1C);
  lift_charge_bcc(x, 0xCA20, c->zf);                   /* beq.w sub_BFBC */
  if (!c->zf)
  {
    c->d[4] = alu_moveql(c, 2);                        /* moveq #2,d4 */
    lift_charge(x, 0xCA24);
    lift_charge_bcc(x, 0xCA26, 1);                     /* bra.w sub_BFBC */
  }
  sub_BFBC(x);
}

/*
 * Player_CountGoalOrAssist (sub_CA2A)
 * Adds $10 (home) or 1 (away, bit 6 of $62(a3)) to the byte counter at
 * $FFFFC3EA, flags $34(a3) and clears the object's sprite frame.
 */
void Player_CountGoalOrAssist(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 5);          /* btst #5,$62(a3) */
  lift_charge(x, 0xCA2A);
  lift_charge_bcc(x, 0xCA30, !c->zf);                  /* bne.w locret_C880 */
  if (!c->zf)
  {
    lift_charge(x, 0xC880);                            /* locret_C880: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->d[0] = alu_moveql(c, 0x10);                       /* moveq #$10,d0 */
  lift_charge(x, 0xCA34);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);          /* btst #6,$62(a3) */
  lift_charge(x, 0xCA36);
  lift_charge_bcc(x, 0xCA3C, c->zf);                   /* beq.w loc_CA42 */
  if (!c->zf)
  {
    c->d[0] = alu_moveql(c, 1);                        /* moveq #1,d0 */
    lift_charge(x, 0xCA40);
  }

  /* loc_CA42 */
  lift_w8(x, 0xFFFFC3EAu, alu_addb(c, c->d[0] & 0xFF, lift_r8(x, 0xFFFFC3EAu))); /* add.b d0,(abs).w */
  lift_charge(x, 0xCA42);
  lift_w8(x, c->a[3] + 0x34, 0xFF);                    /* st $34(a3): no CCR */
  lift_charge(x, 0xCA46);
  lift_w16(x, c->a[3] + 6, 0); alu_movew(c, 0);        /* clr.w 6(a3) */
  lift_charge(x, 0xCA4A);

  lift_charge(x, 0xCA4E);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Player_FaceoffOrReset (sub_C882)
 * Bit 1 of $62(a3) selects between placing the player for a faceoff
 * (position from the camera zone, anim script $F6E via Anim_SetScript) and
 * the plain reset path (state $1000, tail into Object_AdvanceStateMod8).
 */
void Player_FaceoffOrReset(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 5);          /* btst #5,$62(a3) */
  lift_charge(x, 0xC882);
  lift_charge_bcc(x, 0xC888, !c->zf);                  /* bne.s locret_C880 */
  if (!c->zf)
  {
    lift_charge(x, 0xC880);                            /* locret_C880: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w8(x, c->a[3] + 0x62, alu_bclr(c, lift_r8(x, c->a[3] + 0x62), 1)); /* bclr #1,$62(a3) */
  lift_charge(x, 0xC88A);
  lift_charge_bcc(x, 0xC890, c->zf);                   /* beq.w loc_C8CA */
  if (!c->zf)
  {
    lift_w16(x, c->a[3] + 0x28, 0); alu_movew(c, 0);   /* clr.w $28(a3) */
    lift_charge(x, 0xC894);
    lift_w16(x, c->a[3] + 0x2A, 0); alu_movew(c, 0);   /* clr.w $2A(a3) */
    lift_charge(x, 0xC898);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x52))); /* move.w $52(a3),d0 */
    lift_charge(x, 0xC89C);
    setw(&c->d[0], alu_subw(c, 6, W(c->d[0])));        /* subq.w #6,d0 */
    lift_charge(x, 0xC8A0);
    lift_charge_bcc(x, 0xC8A2, !!c->nf);               /* bmi.w loc_C8A8 */
    if (!c->nf)
    {
      setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));      /* addq.w #1,d0 */
      lift_charge(x, 0xC8A6);
    }

    /* loc_C8A8 */
    lift_charge_muls(x, 0xC8A8, 0xE);                  /* muls.w #$E,d0 */
    c->d[0] = alu_muls(c, 0xE, c->d[0]);
    lift_w16(x, c->a[3] + 0x14, W(c->d[0]));           /* move.w d0,$14(a3) */
    alu_movew(c, W(c->d[0]));
    lift_charge(x, 0xC8AC);
    lift_w16(x, c->a[3], 0x88); alu_movew(c, 0x88);    /* move.w #$88,(a3) */
    lift_charge(x, 0xC8B0);
    lift_w16(x, c->a[3], alu_negw(c, lift_r16(x, c->a[3]))); /* neg.w (a3) */
    lift_charge(x, 0xC8B4);
    lift_w16(x, c->a[3] + 0x54, 2); alu_movew(c, 2);   /* move.w #2,$54(a3) */
    lift_charge(x, 0xC8B6);
    lift_w8(x, c->a[3] + 0x62, alu_bset(c, lift_r8(x, c->a[3] + 0x62), 5)); /* bset #5,$62(a3) */
    lift_charge(x, 0xC8BC);
    setw(&c->d[1], alu_movew(c, 0xF6E));               /* move.w #$F6E,d1 */
    lift_charge(x, 0xC8C2);
    lift_charge_bcc(x, 0xC8C6, 1);                     /* bra.w sub_1073A */
    Anim_SetScript(x);
    return;
  }

  /* loc_C8CA */
  lift_w16(x, c->a[3] + 0x54, 4); alu_movew(c, 4);     /* move.w #4,$54(a3) */
  lift_charge(x, 0xC8CA);
  lift_w8(x, c->a[3] + 0x62, alu_bclr(c, lift_r8(x, c->a[3] + 0x62), 2)); /* bclr #2,$62(a3) */
  lift_charge(x, 0xC8D0);
  lift_w8(x, c->a[3] + 0x63, alu_bclr(c, lift_r8(x, c->a[3] + 0x63), 5)); /* bclr #5,$63(a3) */
  lift_charge(x, 0xC8D6);
  lift_w8(x, c->a[3] + 0x63, alu_bclr(c, lift_r8(x, c->a[3] + 0x63), 2)); /* bclr #2,$63(a3) */
  lift_charge(x, 0xC8DC);
  lift_w16(x, c->a[3] + 0x58, 0); alu_movew(c, 0);     /* clr.w $58(a3) */
  lift_charge(x, 0xC8E2);
  lift_w16(x, c->a[3] + 0x28, 0x1000); alu_movew(c, 0x1000); /* move.w #$1000,$28(a3) */
  lift_charge(x, 0xC8E6);
  lift_charge_bcc(x, 0xC8EC, 1);                       /* bra.w sub_10646 */
  Object_AdvanceStateMod8(x);
}

/* --- Attribute-table lookups (batch 5) --- */

void Attr_AddTableEntry(lift_ctx *);   /* forward: defined below */

/*
 * Attr_AddTableEntry (sub_98CE)
 * Follows the word pointer at $98E6+d1 and adds the (a1,d0.w) word to d2.
 * A null pointer bails through the shared bare rts at $A9D4.
 */
void Attr_AddTableEntry(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = 0x98E6u;                                   /* movea.l #$98E6,a1 */
  lift_charge(x, 0x98CE);
  alu_tstw(c, lift_r16(x, c->a[1] + SEW(c->d[1])));    /* tst.w (a1,d1.w) */
  lift_charge(x, 0x98D4);
  lift_charge_bcc(x, 0x98D8, c->zf);                   /* beq.w locret_A9D4 */
  if (c->zf)
  {
    lift_charge(x, 0xA9D4);                            /* locret_A9D4: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[1] = SEW(lift_r16(x, c->a[1] + SEW(c->d[1])));  /* movea.w (a1,d1.w),a1 */
  lift_charge(x, 0x98DC);
  setw(&c->d[2], alu_addw(c, lift_r16(x, c->a[1] + SEW(c->d[0])), W(c->d[2]))); /* add.w (a1,d0.w),d2 */
  lift_charge(x, 0x98E0);

  lift_charge(x, 0x98E4);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Attr_SumPlayerRating (sub_988E)
 * d7 == 0: sums the two bytes of the (a2) attribute block named by the
 * $988FA descriptor pair for index d1 (offsets biased by d0); a negative
 * second offset bails early.
 * d7 != 0: sums two indirect table entries through Attr_AddTableEntry and
 * halves the result.
 */
void Attr_SumPlayerRating(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_tstw(c, W(c->d[7]));                             /* tst.w d7 */
  lift_charge(x, 0x988E);
  lift_charge_bcc(x, 0x9890, !c->zf);                  /* bne.w loc_98BA */
  if (!c->zf)
  {
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));        /* asl.w #2,d1 */
    lift_charge(x, 0x98BA);
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0]))); /* add.w d0,d0 */
    lift_charge(x, 0x98BC);
    setw(&c->d[2], alu_movew(c, 0));                   /* clr.w d2 */
    lift_charge(x, 0x98BE);
    lift_call(x, 0x98C0, 4, Attr_AddTableEntry);       /* bsr.w sub_98CE */
    if (x->declined) return;
    setw(&c->d[1], alu_addw(c, 2, W(c->d[1])));        /* addq.w #2,d1 */
    lift_charge(x, 0x98C4);
    lift_call(x, 0x98C6, 4, Attr_AddTableEntry);       /* bsr.w sub_98CE */
    if (x->declined) return;
    setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));        /* lsr.w #1,d0 */
    lift_charge(x, 0x98CA);
    lift_charge(x, 0x98CC);                            /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[1] = 0x98FAu;                                   /* lea unk_98FA(pc),a1 */
  lift_charge(x, 0x9894);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));          /* asl.w #2,d1 */
  lift_charge(x, 0x9898);
  c->a[1] = (c->a[1] + SEW(c->d[1])) & 0xFFFFFFFFu;    /* adda.w d1,a1: no CCR */
  lift_charge(x, 0x989A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1),d1 */
  lift_charge(x, 0x989C);
  setw(&c->d[1], alu_addw(c, W(c->d[0]), W(c->d[1]))); /* add.w d0,d1 */
  lift_charge(x, 0x989E);
  setw(&c->d[2], alu_movew(c, 0));                     /* clr.w d2 */
  lift_charge(x, 0x98A0);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[1])))); /* move.b (a2,d1.w),d2 */
  lift_charge(x, 0x98A2);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1] + 2))); /* move.w 2(a1),d1 */
  lift_charge(x, 0x98A6);
  lift_charge_bcc(x, 0x98AA, !!c->nf);                 /* bmi.w locret_A9D4 */
  if (c->nf)
  {
    lift_charge(x, 0xA9D4);                            /* locret_A9D4: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[1], alu_addw(c, W(c->d[0]), W(c->d[1]))); /* add.w d0,d1 */
  lift_charge(x, 0x98AE);
  setw(&c->d[3], alu_movew(c, 0));                     /* clr.w d3 */
  lift_charge(x, 0x98B0);
  setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[1])))); /* move.b (a2,d1.w),d3 */
  lift_charge(x, 0x98B2);
  setw(&c->d[2], alu_addw(c, W(c->d[3]), W(c->d[2]))); /* add.w d3,d2 */
  lift_charge(x, 0x98B6);

  lift_charge(x, 0x98B8);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Team_SelectActiveBlock (sub_FAF50)
 * a0 = the team block selected by $FFFFD4F6 (0 = home).
 */
void Team_SelectActiveBlock(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFAF50);
  alu_tstw(c, lift_r16(x, 0xFFFFD4F6u));               /* tst.w ($FFFFD4F6).w */
  lift_charge(x, 0xFAF56);
  lift_charge_bcc(x, 0xFAF5A, c->zf);                  /* beq.w locret_FAF64 */
  if (!c->zf)
  {
    c->a[0] = TEAM_HOME + TEAM_SIZE;                   /* movea.l #$FFFFCA32,a0 */
    lift_charge(x, 0xFAF5E);
  }

  lift_charge(x, 0xFAF64);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_MeasureEntryName (sub_FB8EC)
 * Counts the characters of the name buffer at $FFFFD4DA into $FFFFD4E8,
 * stopping at '-' , at a NUL, or after 12 characters.
 */
void Text_MeasureEntryName(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFB8EC);

  setw(&c->d[3], alu_movew(c, 0xB));                   /* move.w #$B,d3 */
  lift_charge(x, 0xFB8F0);
  c->a[0] = 0xFFFFD4DAu;                               /* movea.l #$FFFFD4DA,a0 */
  lift_charge(x, 0xFB8F4);
  lift_w16(x, 0xFFFFD4E8u, 0); alu_movew(c, 0);        /* clr.w ($FFFFD4E8).w */
  lift_charge(x, 0xFB8FA);

  for (;;)
  {
    alu_cmpb(c, 0x2D, lift_r8(x, c->a[0]));            /* cmpi.b #$2D,(a0) */
    lift_charge(x, 0xFB8FE);
    lift_charge_bcc(x, 0xFB902, c->zf);                /* beq.w loc_FB914 */
    if (c->zf) break;

    alu_tstb(c, lift_r8(x, c->a[0]));                  /* tst.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xFB906);
    lift_charge_bcc(x, 0xFB908, c->zf);                /* beq.w loc_FB914 */
    if (c->zf) break;

    lift_w16(x, 0xFFFFD4E8u, alu_addw(c, 1, lift_r16(x, 0xFFFFD4E8u))); /* addq.w #1,(abs).w */
    lift_charge(x, 0xFB90C);

    setw(&c->d[3], W(c->d[3]) - 1);                    /* dbf d3: no CCR */
    if (W(c->d[3]) != 0xFFFF) { lift_charge_dbcc(x, 0xFB910, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFB910, 0, 1);
    break;
  }

  /* loc_FB914 */
  for (i = 0; i < 8; i++) c->d[i] = saved[i];
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFB914);

  lift_charge(x, 0xFB918);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_AppendIndexedString (sub_FA880)
 * Resolves string index d0 through the pointer table at $30E (entry + the
 * relative offset at +4, then the word at that address) and appends it via
 * Text_AppendString, with the caller's a1 stashed in a3.
 */
void Text_AppendIndexedString(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[5];
  int i;

  saved[0] = c->d[0];
  for (i = 0; i < 4; i++) saved[1 + i] = c->a[i];
  sp -= 20;                                            /* movem.l d0/a0-a3,-(sp) */
  for (i = 0; i < 5; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFA880);

  setw(&c->d[0], alu_extw(c, c->d[0]));                /* ext.w d0 */
  lift_charge(x, 0xFA884);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));          /* asl.w #2,d0 */
  lift_charge(x, 0xFA886);
  c->a[0] = 0x30Eu;                                    /* movea.l #$30E,a0 */
  lift_charge(x, 0xFA888);
  c->a[0] = lift_r32(x, c->a[0] + SEW(c->d[0]));       /* movea.l (a0,d0.w),a0 */
  lift_charge(x, 0xFA88E);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 4))); /* move.w 4(a0),d0 */
  lift_charge(x, 0xFA892);
  c->d[0] = alu_extl(c, c->d[0]);                      /* ext.l d0 */
  lift_charge(x, 0xFA896);
  c->a[0] = (c->a[0] + c->d[0]) & 0xFFFFFFFFu;         /* adda.l d0,a0: no CCR */
  lift_charge(x, 0xFA898);
  c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0]))) & 0xFFFFFFFFu; /* adda.w (a0),a0 */
  lift_charge(x, 0xFA89A);
  c->a[3] = c->a[1];                                   /* movea.l a1,a3 */
  lift_charge(x, 0xFA89C);
  c->a[1] = c->a[0];                                   /* movea.l a0,a1 */
  lift_charge(x, 0xFA89E);

  lift_call(x, 0xFA8A0, 6, Text_AppendString);         /* jsr sub_11D9E */
  if (x->declined) return;

  c->d[0] = saved[0];                                  /* movem.l (sp)+,d0/a0-a3 */
  for (i = 0; i < 4; i++) c->a[i] = saved[1 + i];
  c->a[7] = sp + 20;
  lift_charge_movem(x, 0xFA8A6);

  lift_charge(x, 0xFA8AA);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_StepBoundedCursor (sub_FBBDE)
 * When the cursor at $FFFFD4EA has reached the limit ($FFFFD044, or
 * $FFFFD042 while $FFFFD4EE is set) it is nudged by the sign of the
 * caller's d0 and d0 is cleared. The pushed word is restored sign-extended.
 */
void Menu_StepBoundedCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];

  sp -= 2;                                             /* movem.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  c->a[7] = sp;
  lift_charge_movem(x, 0xFBBDE);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD044u))); /* move.w (abs).w,d0 */
  lift_charge(x, 0xFBBE2);
  alu_tstw(c, lift_r16(x, 0xFFFFD4EEu));               /* tst.w ($FFFFD4EE).w */
  lift_charge(x, 0xFBBE6);
  lift_charge_bcc(x, 0xFBBEA, c->zf);                  /* beq.w loc_FBBF2 */
  if (!c->zf)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD042u))); /* move.w (abs).w,d0 */
    lift_charge(x, 0xFBBEE);
  }

  /* loc_FBBF2 */
  alu_cmpw(c, lift_r16(x, 0xFFFFD4EAu), W(c->d[0]));   /* cmp.w ($FFFFD4EA).w,d0 */
  lift_charge(x, 0xFBBF2);
  lift_charge_bcc(x, 0xFBBF6, !c->zf);                 /* bne.w loc_FBC0E */
  if (c->zf)
  {
    setw(&c->d[0], alu_movew(c, 1));                   /* move.w #1,d0 */
    lift_charge(x, 0xFBBFA);
    alu_tstw(c, lift_r16(x, c->a[7]));                 /* tst.w (sp) */
    lift_charge(x, 0xFBBFE);
    lift_charge_bcc(x, 0xFBC00, !c->nf);               /* bpl.w loc_FBC08 */
    if (c->nf)
    {
      setw(&c->d[0], alu_movew(c, 0xFFFF));            /* move.w #$FFFF,d0 */
      lift_charge(x, 0xFBC04);
    }
    /* loc_FBC08 */
    lift_w16(x, 0xFFFFD4EAu, alu_addw(c, W(c->d[0]), lift_r16(x, 0xFFFFD4EAu))); /* add.w d0,(abs).w */
    lift_charge(x, 0xFBC08);
    setw(&c->d[0], alu_movew(c, 0));                   /* clr.w d0 */
    lift_charge(x, 0xFBC0C);
  }

  /* loc_FBC0E: movem.w (sp)+,d0 — sign-extends into the full register */
  c->d[0] = SEW(lift_r16(x, c->a[7]));
  c->a[7] += 2;
  lift_charge_movem(x, 0xFBC0E);

  lift_charge(x, 0xFBC12);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Roster-block scanners (batch 6) --- */

void Roster_CountLeadingNibbles(lift_ctx *);   /* forward: defined below */
void Roster_CountLineEntries(lift_ctx *);      /* forward: defined below */

/*
 * Roster_CountLeadingNibbles (sub_9F40)
 * d0 = how many nibble-shifts it takes for the word at the $A(a0) sub-block
 * of the team's roster pointer to become zero (1..4).
 */
void Roster_CountLeadingNibbles(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_d1 = c->d[1], saved_a0 = c->a[0];

  sp -= 8;                                             /* movem.l d1/a0,-(sp) */
  lift_w16(x, sp + 0, (c->d[1] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[1] & 0xFFFF);
  lift_w16(x, sp + 4, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 6, c->a[0] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0x9F40);

  c->a[0] = lift_r32(x, c->a[2] + 0x1E);               /* movea.l $1E(a2),a0 */
  lift_charge(x, 0x9F44);
  c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0] + 0xA))) & 0xFFFFFFFFu; /* adda.w $A(a0),a0 */
  lift_charge(x, 0x9F48);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0),d1 */
  lift_charge(x, 0x9F4C);
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0x9F4E);

  for (;;)
  {
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0x9F50);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));        /* asl.w #4,d1 */
    lift_charge(x, 0x9F52);
    lift_charge_bcc(x, 0x9F54, !c->zf);                /* bne.s loc_9F50 */
    if (c->zf) break;
  }

  c->d[1] = saved_d1;                                  /* movem.l (sp)+,d1/a0 */
  c->a[0] = saved_a0;
  c->a[7] = sp + 8;
  lift_charge_movem(x, 0x9F56);

  lift_charge(x, 0x9F5A);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_RatingWithNibbleBonus (sub_9F5C)
 * d0 = Roster_CountLeadingNibbles + the high nibble of byte 3 of the 8(a0)
 * sub-block.
 */
void Roster_RatingWithNibbleBonus(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_a0 = c->a[0];

  sp -= 4;                                             /* movem.l a0,-(sp) */
  lift_w16(x, sp + 0, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[0] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0x9F5C);

  lift_call(x, 0x9F60, 2, Roster_CountLeadingNibbles); /* bsr.s sub_9F40 */
  if (x->declined) return;

  sp = c->a[7] - 2;                                    /* move.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  alu_movew(c, W(c->d[0]));
  c->a[7] = sp;
  lift_charge(x, 0x9F62);

  c->a[0] = lift_r32(x, c->a[2] + 0x1E);               /* movea.l $1E(a2),a0 */
  lift_charge(x, 0x9F64);
  c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0] + 8))) & 0xFFFFFFFFu; /* adda.w 8(a0),a0 */
  lift_charge(x, 0x9F68);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 3))); /* move.b 3(a0),d0 */
  lift_charge(x, 0x9F6C);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));          /* lsr.w #4,d0 */
  lift_charge(x, 0x9F70);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));        /* andi.w #$F,d0 */
  lift_charge(x, 0x9F72);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[0]))); /* add.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0x9F76);

  c->a[0] = saved_a0;                                  /* movem.l (sp)+,a0 */
  c->a[7] += 4;
  lift_charge_movem(x, 0x9F78);

  lift_charge(x, 0x9F7C);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_CountLineEntries (sub_9F9A)
 * Walks the chained records of the team's roster pointer until one whose
 * +8 word is 2, returning the count in d0.
 */
void Roster_CountLineEntries(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved_a0 = c->a[0];
  int guard = 0;

  sp -= 4;                                             /* movem.l a0,-(sp) */
  lift_w16(x, sp + 0, (c->a[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[0] & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0x9F9A);

  c->a[0] = lift_r32(x, c->a[2] + 0x1E);               /* movea.l $1E(a2),a0 */
  lift_charge(x, 0x9F9E);
  c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0]))) & 0xFFFFFFFFu; /* adda.w (a0),a0 */
  lift_charge(x, 0x9FA2);
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0x9FA4);

  for (;;)
  {
    if (++guard > 0x10000) { x->declined = 1; return; }

    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0x9FA6);
    c->a[0] = (c->a[0] + SEW(lift_r16(x, c->a[0]))) & 0xFFFFFFFFu; /* adda.w (a0),a0 */
    lift_charge(x, 0x9FA8);
    c->a[0] = (c->a[0] + 8) & 0xFFFFFFFFu;             /* addq.w #8,a0: no CCR */
    lift_charge(x, 0x9FAA);
    alu_cmpw(c, 2, lift_r16(x, c->a[0]));              /* cmpi.w #2,(a0) */
    lift_charge(x, 0x9FAC);
    lift_charge_bcc(x, 0x9FB0, !c->zf);                /* bne.s loc_9FA6 */
    if (c->zf) break;
  }

  c->a[0] = saved_a0;                                  /* movem.l (sp)+,a0 */
  c->a[7] = sp + 4;
  lift_charge_movem(x, 0x9FB2);

  lift_charge(x, 0x9FB6);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_CacheBothLineCounts (sub_F9FEA)
 * Stores Roster_CountLineEntries for the home block at $FFFFD44C and for the
 * away block at $FFFFD44E.
 */
void Roster_CacheBothLineCounts(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9FEA);

  c->a[2] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xF9FEE);
  lift_call(x, 0xF9FF4, 6, Roster_CountLineEntries);   /* jsr sub_9F9A */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD44Cu, W(c->d[0]));                /* move.w d0,($FFFFD44C).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xF9FFA);

  c->a[2] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a2 */
  lift_charge(x, 0xF9FFE);
  lift_call(x, 0xFA004, 6, Roster_CountLineEntries);   /* jsr sub_9F9A */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD44Eu, W(c->d[0]));                /* move.w d0,($FFFFD44E).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xFA00A);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFA00E);

  lift_charge(x, 0xFA012);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stats_SelectLeaderSlot (sub_185E0)
 * Finds the largest long in the 52-entry table at $FFFFCF36, zeroes it, and
 * returns its index in d0 with a2 pointing at the owning team block.
 */
void Stats_SelectLeaderSlot(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[4];
  int i;

  saved[0] = c->d[1]; saved[1] = c->d[2]; saved[2] = c->a[1]; saved[3] = c->a[4];
  sp -= 16;                                            /* movem.l d1-d2/a1/a4,-(sp) */
  for (i = 0; i < 4; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0x185E0);

  c->a[4] = SEW(0xCF36);                               /* movea.w #$CF36,a4 */
  lift_charge(x, 0x185E4);
  c->d[0] = alu_movel(c, 0);                           /* clr.l d0 */
  lift_charge(x, 0x185E8);
  c->d[2] = alu_moveql(c, 0x33);                       /* moveq #$33,d2 */
  lift_charge(x, 0x185EA);

  for (;;)
  {
    alu_cmpl(c, lift_r32(x, c->a[4]), c->d[0]);        /* cmp.l (a4)+,d0 */
    c->a[4] += 4;
    lift_charge(x, 0x185EC);
    {
      int ge = (!!c->nf) == (!!c->vf);
      lift_charge_bcc(x, 0x185EE, ge);                 /* bge.w loc_185F8 */
      if (!ge)
      {
        c->a[1] = (c->a[4] - 4) & 0xFFFFFFFFu;         /* lea -4(a4),a1: no CCR */
        lift_charge(x, 0x185F2);
        c->d[0] = alu_movel(c, lift_r32(x, c->a[1]));  /* move.l (a1),d0 */
        lift_charge(x, 0x185F6);
      }
    }

    /* loc_185F8 */
    setw(&c->d[2], W(c->d[2]) - 1);                    /* dbf d2: no CCR */
    if (W(c->d[2]) != 0xFFFF) { lift_charge_dbcc(x, 0x185F8, 1, 0); continue; }
    lift_charge_dbcc(x, 0x185F8, 0, 1);
    break;
  }

  lift_w32(x, c->a[1], 0); alu_movel(c, 0);            /* clr.l (a1) */
  lift_charge(x, 0x185FC);
  setw(&c->d[0], alu_movew(c, W(c->a[1])));            /* move.w a1,d0 */
  lift_charge(x, 0x185FE);
  setw(&c->d[0], alu_subw(c, 0xCF36, W(c->d[0])));     /* subi.w #$CF36,d0 */
  lift_charge(x, 0x18600);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));          /* lsr.w #2,d0 */
  lift_charge(x, 0x18604);
  c->a[2] = SEW(0xC6CE);                               /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x18606);
  alu_cmpw(c, 0x1A, W(c->d[0]));                       /* cmpi.w #$1A,d0 */
  lift_charge(x, 0x1860A);
  {
    int lt = (!!c->nf) != (!!c->vf);
    lift_charge_bcc(x, 0x1860E, lt);                   /* blt.w loc_1861A */
    if (!lt)
    {
      setw(&c->d[0], alu_subw(c, 0x1A, W(c->d[0])));   /* subi.w #$1A,d0 */
      lift_charge(x, 0x18612);
      c->a[2] = (c->a[2] + 0x364) & 0xFFFFFFFFu;       /* adda.w #$364,a2: no CCR */
      lift_charge(x, 0x18616);
    }
  }

  /* loc_1861A */
  c->d[1] = saved[0]; c->d[2] = saved[1];
  c->a[1] = saved[2]; c->a[4] = saved[3];
  c->a[7] = sp + 16;
  lift_charge_movem(x, 0x1861A);

  lift_charge(x, 0x1861E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Roster digits, line matching, stat leaders (batch 7) --- */

void Sram_SyncScoreRecord(lift_ctx *);       /* ($F9B94) */
void Sram_SyncScoreRecordWrite(lift_ctx *);  /* ($F9BDA) */

/*
 * Roster_FormatJerseyNumber (sub_FAC90)
 * Walks d0 records into the active team's roster chain and writes the
 * player's two jersey digits (leading zero blanked) after a #4 word at (a1).
 */
void Roster_FormatJerseyNumber(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  if (W(c->d[0]) > 0x1000) { x->declined = 1; return; }  /* dbf would run away */

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFAC90);

  c->a[0] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFAC94);
  alu_tstw(c, lift_r16(x, 0xFFFFD4F6u));               /* tst.w ($FFFFD4F6).w */
  lift_charge(x, 0xFAC9A);
  lift_charge_bcc(x, 0xFAC9E, c->zf);                  /* beq.w loc_FACA8 */
  if (!c->zf)
  {
    c->a[0] = TEAM_HOME + TEAM_SIZE;                   /* movea.l #$FFFFCA32,a0 */
    lift_charge(x, 0xFACA2);
  }

  /* loc_FACA8 */
  c->a[0] = lift_r32(x, c->a[0] + 0x1E);               /* movea.l $1E(a0),a0 */
  lift_charge(x, 0xFACA8);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0),d1 */
  lift_charge(x, 0xFACAC);
  c->d[1] = alu_extl(c, c->d[1]);                      /* ext.l d1 */
  lift_charge(x, 0xFACAE);
  c->a[0] = (c->a[0] + c->d[1]) & 0xFFFFFFFFu;         /* adda.l d1,a0 */
  lift_charge(x, 0xFACB0);

  for (;;)
  {
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0]))); /* move.w (a0),d1 */
    lift_charge(x, 0xFACB2);
    c->d[1] = alu_extl(c, c->d[1]);                    /* ext.l d1 */
    lift_charge(x, 0xFACB4);
    c->a[0] = (c->a[0] + c->d[1]) & 0xFFFFFFFFu;       /* adda.l d1,a0 */
    lift_charge(x, 0xFACB6);
    c->a[0] = (c->a[0] + 8) & 0xFFFFFFFFu;             /* addq.l #8,a0 */
    lift_charge(x, 0xFACB8);

    setw(&c->d[0], W(c->d[0]) - 1);                    /* dbf d0: no CCR */
    if (W(c->d[0]) != 0xFFFF) { lift_charge_dbcc(x, 0xFACBA, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFACBA, 0, 1);
    break;
  }

  c->a[0] = (c->a[0] - 8) & 0xFFFFFFFFu;               /* subq.l #8,a0 */
  lift_charge(x, 0xFACBE);
  lift_w16(x, c->a[1], 4); alu_movew(c, 4);            /* move.w #4,(a1)+ */
  c->a[1] += 2;
  lift_charge(x, 0xFACC0);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0),d0 */
  lift_charge(x, 0xFACC4);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));          /* lsr.w #4,d0 */
  lift_charge(x, 0xFACC6);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));        /* andi.w #$F,d0 */
  lift_charge(x, 0xFACC8);
  lift_charge_bcc(x, 0xFACCC, !c->zf);                 /* bne.w loc_FACD8 */
  if (c->zf)
  {
    setb(&c->d[0], alu_moveb(c, 0x20));                /* move.b #$20,d0 */
    lift_charge(x, 0xFACD0);
    lift_charge_bcc(x, 0xFACD4, 1);                    /* bra.w loc_FACDC */
  }
  else
  {
    setb(&c->d[0], alu_addb(c, 0x30, c->d[0] & 0xFF)); /* addi.b #$30,d0 */
    lift_charge(x, 0xFACD8);
  }

  /* loc_FACDC */
  lift_w8(x, c->a[1], c->d[0] & 0xFF);                 /* move.b d0,(a1)+ */
  alu_moveb(c, c->d[0] & 0xFF);
  c->a[1] += 1;
  lift_charge(x, 0xFACDC);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0),d0 */
  lift_charge(x, 0xFACDE);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));        /* andi.w #$F,d0 */
  lift_charge(x, 0xFACE0);
  setb(&c->d[0], alu_addb(c, 0x30, c->d[0] & 0xFF));   /* addi.b #$30,d0 */
  lift_charge(x, 0xFACE4);
  lift_w8(x, c->a[1], c->d[0] & 0xFF);                 /* move.b d0,(a1)+ */
  alu_moveb(c, c->d[0] & 0xFF);
  c->a[1] += 1;
  lift_charge(x, 0xFACE8);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFACEA);

  lift_charge(x, 0xFACEE);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_CacheBothNibbleCounts (sub_F9FC0)
 * Caches Roster_CountLeadingNibbles for both team blocks at $FFFFD448 /
 * $FFFFD44A.
 */
void Roster_CacheBothNibbleCounts(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9FC0);

  c->a[2] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xF9FC4);
  lift_call(x, 0xF9FCA, 6, Roster_CountLeadingNibbles); /* jsr sub_9F40 */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD448u, W(c->d[0]));                /* move.w d0,($FFFFD448).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xF9FD0);

  c->a[2] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a2 */
  lift_charge(x, 0xF9FD4);
  lift_call(x, 0xF9FDA, 6, Roster_CountLeadingNibbles); /* jsr sub_9F40 */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD44Au, W(c->d[0]));                /* move.w d0,($FFFFD44A).w */
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0xF9FE0);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xF9FE4);

  lift_charge(x, 0xF9FE8);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stats_TrackTeamLeader (sub_F9F50)
 * Scans the 26 players of team a2, computing each one's stat (direct byte at
 * +$B4, or the +$E8 minus +$B4 difference past index d5), and keeps the best
 * in the record at (a0) with its name bytes and Sram_SyncScoreRecordWrite.
 */
void Stats_TrackTeamLeader(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_movel(c, 0);                           /* clr.l d0 */
  lift_charge(x, 0xF9F50);
  setw(&c->d[2], alu_movew(c, 0x19));                  /* move.w #$19,d2 */
  lift_charge(x, 0xF9F52);

  for (;;)
  {
    lift_call(x, 0xF9F56, 4, Sram_SyncScoreRecord);    /* bsr.w sub_F9B94 */
    if (x->declined) return;

    alu_cmpw(c, W(c->d[5]), W(c->d[0]));               /* cmp.w d5,d0 */
    lift_charge(x, 0xF9F5A);
    {
      int lt = (!!c->nf) != (!!c->vf);
      lift_charge_bcc(x, 0xF9F5C, lt);                 /* blt.w loc_F9F70 */
      if (!lt)
      {
        uint32_t sp = c->a[7] - 2;                     /* move.w d0,-(sp) */
        lift_w16(x, sp, W(c->d[0]));
        alu_movew(c, W(c->d[0]));
        c->a[7] = sp;
        lift_charge(x, 0xF9F60);
        setw(&c->d[0], alu_addw(c, 0xB4, W(c->d[0]))); /* addi.w #$B4,d0 */
        lift_charge(x, 0xF9F62);
        setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0])))); /* move.b (a2,d0.w),d4 */
        lift_charge(x, 0xF9F66);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7]))); /* move.w (sp)+,d0 */
        c->a[7] += 2;
        lift_charge(x, 0xF9F6A);
        lift_charge_bcc(x, 0xF9F6C, 1);                /* bra.w loc_F9F84 */
      }
      else
      {
        uint32_t sp = c->a[7] - 2;                     /* move.w d0,-(sp) */
        lift_w16(x, sp, W(c->d[0]));
        alu_movew(c, W(c->d[0]));
        c->a[7] = sp;
        lift_charge(x, 0xF9F70);
        setw(&c->d[0], alu_addw(c, 0xE8, W(c->d[0]))); /* addi.w #$E8,d0 */
        lift_charge(x, 0xF9F72);
        setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0])))); /* move.b (a2,d0.w),d4 */
        lift_charge(x, 0xF9F76);
        setw(&c->d[0], alu_addw(c, 0xFFCC, W(c->d[0]))); /* addi.w #-$34,d0 */
        lift_charge(x, 0xF9F7A);
        setb(&c->d[4], alu_subb(c, lift_r8(x, c->a[2] + SEW(c->d[0])), c->d[4] & 0xFF)); /* sub.b (a2,d0.w),d4 */
        lift_charge(x, 0xF9F7E);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7]))); /* move.w (sp)+,d0 */
        c->a[7] += 2;
        lift_charge(x, 0xF9F82);
      }
    }

    /* loc_F9F84 */
    setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0]))); /* move.b (a0),d3 */
    lift_charge(x, 0xF9F84);
    alu_cmpb(c, c->d[3] & 0xFF, c->d[4] & 0xFF);       /* cmp.b d3,d4 */
    lift_charge(x, 0xF9F86);
    {
      int le = c->zf || ((!!c->nf) != (!!c->vf));
      lift_charge_bcc(x, 0xF9F88, le);                 /* ble.w loc_F9FB8 */
      if (!le)
      {
        lift_w8(x, c->a[0], c->d[4] & 0xFF);           /* move.b d4,(a0) */
        alu_moveb(c, c->d[4] & 0xFF);
        lift_charge(x, 0xF9F8C);
        {
          uint32_t v = lift_r8(x, 0xFFFFD043u);
          lift_w8(x, c->a[0] + 1, v); alu_moveb(c, v); /* move.b (abs).w,1(a0) */
        }
        lift_charge(x, 0xF9F8E);
        {
          uint32_t v = lift_r8(x, 0xFFFFD045u);
          lift_w8(x, c->a[0] + 3, v); alu_moveb(c, v); /* move.b (abs).w,3(a0) */
        }
        lift_charge(x, 0xF9F94);
        alu_cmpl(c, TEAM_HOME, c->a[2]);               /* cmpa.l #$FFFFC6CE,a2 */
        lift_charge(x, 0xF9F9A);
        lift_charge_bcc(x, 0xF9FA0, c->zf);            /* beq.w loc_F9FB0 */
        if (!c->zf)
        {
          uint32_t v = lift_r8(x, 0xFFFFD045u);
          lift_w8(x, c->a[0] + 1, v); alu_moveb(c, v); /* move.b (abs).w,1(a0) */
          lift_charge(x, 0xF9FA4);
          v = lift_r8(x, 0xFFFFD043u);
          lift_w8(x, c->a[0] + 3, v); alu_moveb(c, v); /* move.b (abs).w,3(a0) */
          lift_charge(x, 0xF9FAA);
        }

        /* loc_F9FB0 */
        lift_w8(x, c->a[0] + 2, c->d[6] & 0xFF);       /* move.b d6,2(a0) */
        alu_moveb(c, c->d[6] & 0xFF);
        lift_charge(x, 0xF9FB0);
        lift_call(x, 0xF9FB4, 4, Sram_SyncScoreRecordWrite); /* bsr.w sub_F9BDA */
        if (x->declined) return;
      }
    }

    /* loc_F9FB8 */
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0xF9FB8);
    setw(&c->d[2], W(c->d[2]) - 1);                    /* dbf d2: no CCR */
    if (W(c->d[2]) != 0xFFFF) { lift_charge_dbcc(x, 0xF9FBA, 1, 0); continue; }
    lift_charge_dbcc(x, 0xF9FBA, 0, 1);
    break;
  }

  lift_charge(x, 0xF9FBE);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Lineup_MatchShiftTotals (sub_12A2E)
 * Sums the on-ice shift deltas of a2's line list ($9A) and compares them
 * against a3's; bails through the shared far rts at $15464 unless a3's line
 * is behind, in which case d0 carries the difference.
 */
void Lineup_MatchShiftTotals(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int guard = 0;

  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0x12A2E);
  setw(&c->d[3], alu_movew(c, 0));                     /* clr.w d3 */
  lift_charge(x, 0x12A30);
  c->a[0] = (c->a[2] + 0x9A) & 0xFFFFFFFFu;            /* lea $9A(a2),a0: no CCR */
  lift_charge(x, 0x12A32);

  for (;;)
  {
    if (++guard > 0x10000) { x->declined = 1; return; }

    setw(&c->d[2], alu_movew(c, 0));                   /* clr.w d2 */
    lift_charge(x, 0x12A36);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0]))); /* move.b (a0)+,d2 */
    c->a[0] += 1;
    lift_charge(x, 0x12A38);
    lift_charge_bcc(x, 0x12A3A, !!c->nf);              /* bmi.w loc_12A50 */
    if (c->nf) break;

    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + SEW(c->d[2]) + 0x66))); /* move.w $66(a2,d2.w),d2 */
    lift_charge(x, 0x12A3E);
    alu_btst(c, c->d[2], 14);                          /* btst #$E,d2 */
    lift_charge(x, 0x12A42);
    lift_charge_bcc(x, 0x12A46, !c->zf);               /* bne.s loc_12A36 */
    if (!c->zf) continue;

    setw(&c->d[2], alu_subw(c, W(c->d[3]), W(c->d[2]))); /* sub.w d3,d2 */
    lift_charge(x, 0x12A48);
    setw(&c->d[0], alu_addw(c, W(c->d[2]), W(c->d[0]))); /* add.w d2,d0 */
    lift_charge(x, 0x12A4A);
    setw(&c->d[3], alu_movew(c, W(c->d[2])));          /* move.w d2,d3 */
    lift_charge(x, 0x12A4C);
    lift_charge_bcc(x, 0x12A4E, 1);                    /* bra.s loc_12A36 */
  }

  /* loc_12A50 */
  alu_cmpw(c, 6, lift_r16(x, c->a[3] + 0x24));         /* cmpi.w #6,$24(a3) */
  lift_charge(x, 0x12A50);
  lift_charge_bcc(x, 0x12A56, c->zf);                  /* beq.w locret_15464 */
  if (c->zf)
  {
    lift_charge(x, 0x15464);                           /* locret_15464: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[0], alu_subw(c, W(c->d[3]), W(c->d[0]))); /* sub.w d3,d0 */
  lift_charge(x, 0x12A5A);
  c->a[0] = (c->a[3] + 0x9A) & 0xFFFFFFFFu;            /* lea $9A(a3),a0 */
  lift_charge(x, 0x12A5C);
  setw(&c->d[2], alu_movew(c, 0));                     /* clr.w d2 */
  lift_charge(x, 0x12A60);

  guard = 0;
  for (;;)
  {
    if (++guard > 0x10000) { x->declined = 1; return; }

    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0]))); /* move.b (a0)+,d2 */
    c->a[0] += 1;
    lift_charge(x, 0x12A62);
    lift_charge_bcc(x, 0x12A64, !!c->nf);              /* bmi.w locret_15464 */
    if (c->nf)
    {
      lift_charge(x, 0x15464);                         /* locret_15464: rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    alu_btst(c, lift_r8(x, c->a[3] + SEW(c->d[2]) + 0x66), 6); /* btst #6,$66(a3,d2.w) */
    lift_charge(x, 0x12A68);
    lift_charge_bcc(x, 0x12A6E, !c->zf);               /* bne.s loc_12A62 */
    if (!c->zf) continue;
    break;
  }

  alu_cmpw(c, lift_r16(x, c->a[3] + SEW(c->d[2]) + 0x66), W(c->d[0])); /* cmp.w $66(a3,d2.w),d0 */
  lift_charge(x, 0x12A70);
  {
    int lt = (!!c->nf) != (!!c->vf);
    lift_charge_bcc(x, 0x12A74, lt);                   /* blt.w locret_15464 */
    if (lt)
    {
      lift_charge(x, 0x15464);                         /* locret_15464: rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  setw(&c->d[0], alu_addw(c, W(c->d[3]), W(c->d[0]))); /* add.w d3,d0 */
  lift_charge(x, 0x12A78);
  lift_charge(x, 0x12A7A);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Season stat records (batch 8) --- */

void Sram_SyncTeamRecord(lift_ctx *);        /* ($F9BE2) */
void Sram_SyncTeamRecordWrite(lift_ctx *);   /* ($F9C18) */

/*
 * Stats_PickTeamRecordHolder (sub_FEC5E)
 * Scans the 28 team records at $FFFFCF36, returning in d0 the index of the
 * team with the highest $8 byte (0 counting as $50) and its value in d7.
 */
void Stats_PickTeamRecordHolder(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[14];
  int i;

  for (i = 0; i < 7; i++) saved[i] = c->d[1 + i];
  for (i = 0; i < 7; i++) saved[7 + i] = c->a[i];
  sp -= 56;                                            /* movem.l d1-a6,-(sp) */
  for (i = 0; i < 14; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFEC5E);

  setw(&c->d[1], alu_movew(c, 0x1B));                  /* move.w #$1B,d1 */
  lift_charge(x, 0xFEC62);
  c->d[1] = alu_extl(c, c->d[1]);                      /* ext.l d1 */
  lift_charge(x, 0xFEC66);
  setw(&c->d[7], alu_movew(c, 0));                     /* clr.w d7 */
  lift_charge(x, 0xFEC68);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFEC6A);

  for (;;)
  {
    lift_call(x, 0xFEC70, 6, Sram_SyncTeamRecord);     /* jsr sub_F9BE2 */
    if (x->declined) return;

    setw(&c->d[2], alu_movew(c, 0));                   /* clr.w d2 */
    lift_charge(x, 0xFEC76);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + 8))); /* move.b 8(a0),d2 */
    lift_charge(x, 0xFEC78);
    lift_charge_bcc(x, 0xFEC7C, !c->zf);               /* bne.w loc_FEC84 */
    if (c->zf)
    {
      setw(&c->d[2], alu_movew(c, 0x50));              /* move.w #$50,d2 */
      lift_charge(x, 0xFEC80);
    }

    /* loc_FEC84 */
    alu_cmpw(c, W(c->d[7]), W(c->d[2]));               /* cmp.w d7,d2 */
    lift_charge(x, 0xFEC84);
    {
      int le = c->zf || ((!!c->nf) != (!!c->vf));
      lift_charge_bcc(x, 0xFEC86, le);                 /* ble.w loc_FEC8E */
      if (!le)
      {
        setw(&c->d[7], alu_movew(c, W(c->d[2])));      /* move.w d2,d7 */
        lift_charge(x, 0xFEC8A);
        setw(&c->d[0], alu_movew(c, W(c->d[1])));      /* move.w d1,d0 */
        lift_charge(x, 0xFEC8C);
      }
    }

    /* loc_FEC8E */
    setw(&c->d[1], W(c->d[1]) - 1);                    /* dbf d1: no CCR */
    if (W(c->d[1]) != 0xFFFF) { lift_charge_dbcc(x, 0xFEC8E, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFEC8E, 0, 1);
    break;
  }

  for (i = 0; i < 7; i++) c->d[1 + i] = saved[i];      /* movem.l (sp)+,d1-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[7 + i];
  c->a[7] = sp + 56;
  lift_charge_movem(x, 0xFEC92);

  lift_charge(x, 0xFEC96);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stats_UpdateSeasonLeaders (sub_F9EAA)
 * Compares one team's points / plus-minus / goals-against records against
 * the leaders stored at (a0), rewriting each beaten slot (value plus the
 * d4/d5/d2 name bytes) and, if anything changed (d7), writing the block
 * back to SRAM via Sram_SyncTeamRecordWrite.
 */
void Stats_UpdateSeasonLeaders(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9EAA);

  lift_call(x, 0xF9EAE, 4, Sram_SyncTeamRecord);       /* bsr.w sub_F9BE2 */
  if (x->declined) return;

  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1] + 0xC))); /* move.w $C(a1),d3 */
  lift_charge(x, 0xF9EB2);
  alu_cmpb(c, lift_r8(x, c->a[0]), c->d[3] & 0xFF);    /* cmp.b (a0),d3 */
  lift_charge(x, 0xF9EB6);
  {
    int le = c->zf || ((!!c->nf) != (!!c->vf));
    lift_charge_bcc(x, 0xF9EB8, le);                   /* ble.w loc_F9ECC */
    if (!le)
    {
      lift_w8(x, c->a[0], c->d[3] & 0xFF); alu_moveb(c, c->d[3] & 0xFF);
      lift_charge(x, 0xF9EBC);                         /* move.b d3,(a0) */
      lift_w8(x, c->a[0] + 1, c->d[4] & 0xFF); alu_moveb(c, c->d[4] & 0xFF);
      lift_charge(x, 0xF9EBE);                         /* move.b d4,1(a0) */
      lift_w8(x, c->a[0] + 3, c->d[5] & 0xFF); alu_moveb(c, c->d[5] & 0xFF);
      lift_charge(x, 0xF9EC2);                         /* move.b d5,3(a0) */
      lift_w8(x, c->a[0] + 2, c->d[2] & 0xFF); alu_moveb(c, c->d[2] & 0xFF);
      lift_charge(x, 0xF9EC6);                         /* move.b d2,2(a0) */
      setb(&c->d[7], 0xFF);                            /* st d7: no CCR */
      lift_charge(x, 0xF9ECA);
    }
  }

  /* loc_F9ECC */
  setw(&c->d[6], alu_subw(c, 1, W(c->d[6])));          /* subq.w #1,d6 */
  lift_charge(x, 0xF9ECC);
  setw(&c->d[3], alu_movew(c, 0));                     /* clr.w d3 */
  lift_charge(x, 0xF9ECE);

  for (;;)
  {
    setw(&c->d[0], alu_movew(c, W(c->d[6])));          /* move.w d6,d0 */
    lift_charge(x, 0xF9ED0);
    setw(&c->d[0], alu_addw(c, 0xE8, W(c->d[0])));     /* addi.w #$E8,d0 */
    lift_charge(x, 0xF9ED2);
    {
      uint32_t v = lift_r8(x, c->a[1] + SEW(c->d[0]));
      lift_w8(x, 0xFFFFBF12u, v); alu_moveb(c, v);     /* move.b (a1,d0.w),(abs).w */
    }
    lift_charge(x, 0xF9ED6);
    setw(&c->d[0], alu_addw(c, 0xFFCC, W(c->d[0])));   /* addi.w #-$34,d0 */
    lift_charge(x, 0xF9EDC);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[0])))); /* move.b (a1,d0.w),d0 */
    lift_charge(x, 0xF9EE0);
    lift_w8(x, 0xFFFFBF12u, alu_subb(c, c->d[0] & 0xFF, lift_r8(x, 0xFFFFBF12u))); /* sub.b d0,(abs).w */
    lift_charge(x, 0xF9EE4);
    alu_cmpb(c, lift_r8(x, 0xFFFFBF12u), c->d[3] & 0xFF); /* cmp.b (abs).w,d3 */
    lift_charge(x, 0xF9EE8);
    {
      int ge = (!!c->nf) == (!!c->vf);
      lift_charge_bcc(x, 0xF9EEC, ge);                 /* bge.w loc_F9EF4 */
      if (!ge)
      {
        setb(&c->d[3], alu_moveb(c, lift_r8(x, 0xFFFFBF12u))); /* move.b (abs).w,d3 */
        lift_charge(x, 0xF9EF0);
      }
    }

    /* loc_F9EF4 */
    setw(&c->d[6], W(c->d[6]) - 1);                    /* dbf d6: no CCR */
    if (W(c->d[6]) != 0xFFFF) { lift_charge_dbcc(x, 0xF9EF4, 1, 0); continue; }
    lift_charge_dbcc(x, 0xF9EF4, 0, 1);
    break;
  }

  alu_cmpb(c, lift_r8(x, c->a[0] + 4), c->d[3] & 0xFF); /* cmp.b 4(a0),d3 */
  lift_charge(x, 0xF9EF8);
  {
    int le = c->zf || ((!!c->nf) != (!!c->vf));
    lift_charge_bcc(x, 0xF9EFC, le);                   /* ble.w loc_F9F12 */
    if (!le)
    {
      setb(&c->d[7], 0xFF);                            /* st d7 */
      lift_charge(x, 0xF9F00);
      lift_w8(x, c->a[0] + 4, c->d[3] & 0xFF); alu_moveb(c, c->d[3] & 0xFF);
      lift_charge(x, 0xF9F02);                         /* move.b d3,4(a0) */
      lift_w8(x, c->a[0] + 5, c->d[4] & 0xFF); alu_moveb(c, c->d[4] & 0xFF);
      lift_charge(x, 0xF9F06);                         /* move.b d4,5(a0) */
      lift_w8(x, c->a[0] + 7, c->d[5] & 0xFF); alu_moveb(c, c->d[5] & 0xFF);
      lift_charge(x, 0xF9F0A);                         /* move.b d5,7(a0) */
      lift_w8(x, c->a[0] + 6, c->d[2] & 0xFF); alu_moveb(c, c->d[2] & 0xFF);
      lift_charge(x, 0xF9F0E);                         /* move.b d2,6(a0) */
    }
  }

  /* loc_F9F12 */
  alu_cmpw(c, lift_r16(x, 0xFFFFC330u), W(c->d[1]));   /* cmp.w ($FFFFC330).w,d1 */
  lift_charge(x, 0xF9F12);
  lift_charge_bcc(x, 0xF9F16, !c->zf);                 /* bne.w loc_F9F40 */
  if (c->zf)
  {
    setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + 8))); /* move.b 8(a0),d3 */
    lift_charge(x, 0xF9F1A);
    setw(&c->d[3], alu_andw(c, 0xFF, W(c->d[3])));     /* andi.w #$FF,d3 */
    lift_charge(x, 0xF9F1E);
    alu_cmpw(c, lift_r16(x, 0xFFFFC314u), W(c->d[3])); /* cmp.w ($FFFFC314).w,d3 */
    lift_charge(x, 0xF9F22);
    {
      int ge = (!!c->nf) == (!!c->vf);
      lift_charge_bcc(x, 0xF9F26, ge);                 /* bge.w loc_F9F40 */
      if (!ge)
      {
        setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFC314u))); /* move.w (abs).w,d3 */
        lift_charge(x, 0xF9F2A);
        setb(&c->d[7], 0xFF);                          /* st d7 */
        lift_charge(x, 0xF9F2E);
        lift_w8(x, c->a[0] + 8, c->d[3] & 0xFF); alu_moveb(c, c->d[3] & 0xFF);
        lift_charge(x, 0xF9F30);                       /* move.b d3,8(a0) */
        lift_w8(x, c->a[0] + 9, c->d[4] & 0xFF); alu_moveb(c, c->d[4] & 0xFF);
        lift_charge(x, 0xF9F34);                       /* move.b d4,9(a0) */
        lift_w8(x, c->a[0] + 0xB, c->d[5] & 0xFF); alu_moveb(c, c->d[5] & 0xFF);
        lift_charge(x, 0xF9F38);                       /* move.b d5,$B(a0) */
        lift_w8(x, c->a[0] + 0xA, c->d[2] & 0xFF); alu_moveb(c, c->d[2] & 0xFF);
        lift_charge(x, 0xF9F3C);                       /* move.b d2,$A(a0) */
      }
    }
  }

  /* loc_F9F40 */
  alu_tstw(c, W(c->d[7]));                             /* tst.w d7 */
  lift_charge(x, 0xF9F40);
  lift_charge_bcc(x, 0xF9F42, c->zf);                  /* beq.w loc_F9F4A */
  if (!c->zf)
  {
    lift_call(x, 0xF9F46, 4, Sram_SyncTeamRecordWrite); /* bsr.w sub_F9C18 */
    if (x->declined) return;
  }

  /* loc_F9F4A */
  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xF9F4A);

  lift_charge(x, 0xF9F4E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Sram_SyncAltTeamRecord(lift_ctx *);       /* ($F9C20) */
void Sram_SyncAltTeamRecordWrite(lift_ctx *);  /* ($F9C56) */

/*
 * Stats_RecordGameResult (sub_F9DDA)
 * Bumps the games-played counter at $A/$B(a0) (capped at $2328), then the
 * win / tie / loss counter for this result, and updates the best-score and
 * biggest-margin records before writing the block back to SRAM.
 */
void Stats_RecordGameResult(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9DDA);
  {
  uint32_t frame = sp;

  lift_call(x, 0xF9DDE, 4, Sram_SyncAltTeamRecord);    /* bsr.w sub_F9C20 */
  if (x->declined) return;

  setb(&c->d[7], 0xFF);                                /* st d7: no CCR */
  lift_charge(x, 0xF9DE2);

  sp = c->a[7] - 2;                                    /* movem.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9DE4);

  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0xF9DE8);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 0xA))); /* move.b $A(a0),d0 */
  lift_charge(x, 0xF9DEA);
  setw(&c->d[0], alu_lslw(c, W(c->d[0]), 8));          /* lsl.w #8,d0 */
  lift_charge(x, 0xF9DEE);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 0xB))); /* move.b $B(a0),d0 */
  lift_charge(x, 0xF9DF0);
  alu_cmpw(c, 0x2328, W(c->d[0]));                     /* cmpi.w #$2328,d0 */
  lift_charge(x, 0xF9DF4);
  {
    int ge = (!!c->nf) == (!!c->vf);
    lift_charge_bcc(x, 0xF9DF8, ge);                   /* bge.w loc_F9E4C */
    if (!ge)
    {
      setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));      /* addq.w #1,d0 */
      lift_charge(x, 0xF9DFC);
      lift_w8(x, c->a[0] + 0xB, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
      lift_charge(x, 0xF9DFE);                         /* move.b d0,$B(a0) */
      setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 8));      /* lsr.w #8,d0 */
      lift_charge(x, 0xF9E02);
      lift_w8(x, c->a[0] + 0xA, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
      lift_charge(x, 0xF9E04);                         /* move.b d0,$A(a0) */

      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0xC))); /* move.w $C(a1),d0 */
      lift_charge(x, 0xF9E08);
      alu_cmpw(c, lift_r16(x, c->a[2] + 0xC), W(c->d[0])); /* cmp.w $C(a2),d0 */
      lift_charge(x, 0xF9E0C);
      {
        int gt = !c->zf && ((!!c->nf) == (!!c->vf));
        int lt = (!!c->nf) != (!!c->vf);
        lift_charge_bcc(x, 0xF9E10, gt);               /* bgt.w loc_F9E34 */
        if (gt)
        {
          /* loc_F9E34: bump the win counter at 8/9(a0) */
          setw(&c->d[0], alu_movew(c, 0));             /* clr.w d0 */
          lift_charge(x, 0xF9E34);
          setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 8)));
          lift_charge(x, 0xF9E36);                     /* move.b 8(a0),d0 */
          setw(&c->d[0], alu_lslw(c, W(c->d[0]), 8));  /* lsl.w #8,d0 */
          lift_charge(x, 0xF9E3A);
          setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 9)));
          lift_charge(x, 0xF9E3C);                     /* move.b 9(a0),d0 */
          setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));  /* addq.w #1,d0 */
          lift_charge(x, 0xF9E40);
          lift_w8(x, c->a[0] + 9, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
          lift_charge(x, 0xF9E42);                     /* move.b d0,9(a0) */
          setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 8));  /* lsr.w #8,d0 */
          lift_charge(x, 0xF9E46);
          lift_w8(x, c->a[0] + 8, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
          lift_charge(x, 0xF9E48);                     /* move.b d0,8(a0) */
        }
        else
        {
          lift_charge_bcc(x, 0xF9E14, lt);             /* blt.w loc_F9E4C */
          if (!lt)
          {
            /* tie: bump the counter at $C/$D(a0) */
            setw(&c->d[0], alu_movew(c, 0));           /* clr.w d0 */
            lift_charge(x, 0xF9E18);
            setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 0xC)));
            lift_charge(x, 0xF9E1A);                   /* move.b $C(a0),d0 */
            setw(&c->d[0], alu_lslw(c, W(c->d[0]), 8)); /* lsl.w #8,d0 */
            lift_charge(x, 0xF9E1E);
            setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 0xD)));
            lift_charge(x, 0xF9E20);                   /* move.b $D(a0),d0 */
            setw(&c->d[0], alu_addw(c, 1, W(c->d[0]))); /* addq.w #1,d0 */
            lift_charge(x, 0xF9E24);
            lift_w8(x, c->a[0] + 0xD, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
            lift_charge(x, 0xF9E26);                   /* move.b d0,$D(a0) */
            setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 8)); /* lsr.w #8,d0 */
            lift_charge(x, 0xF9E2A);
            lift_w8(x, c->a[0] + 0xC, c->d[0] & 0xFF); alu_moveb(c, c->d[0] & 0xFF);
            lift_charge(x, 0xF9E2C);                   /* move.b d0,$C(a0) */
            lift_charge_bcc(x, 0xF9E30, 1);            /* bra.w loc_F9E4C */
          }
        }
      }
    }
  }

  /* loc_F9E4C */
  c->d[0] = SEW(lift_r16(x, c->a[7]));                 /* movem.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge_movem(x, 0xF9E4C);
  sp = c->a[7] - 2;                                    /* movem.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9E50);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0xC))); /* move.w $C(a1),d0 */
  lift_charge(x, 0xF9E54);
  alu_cmpw(c, lift_r16(x, c->a[2] + 0xC), W(c->d[0])); /* cmp.w $C(a2),d0 */
  lift_charge(x, 0xF9E58);
  c->d[0] = SEW(lift_r16(x, c->a[7]));                 /* movem.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge_movem(x, 0xF9E5C);

  {
    int le = c->zf || ((!!c->nf) != (!!c->vf));
    lift_charge_bcc(x, 0xF9E60, le);                   /* ble.w loc_F9EA0 */
    if (!le)
    {
      setw(&c->d[5], alu_movew(c, lift_r16(x, c->a[1] + 0xC))); /* move.w $C(a1),d5 */
      lift_charge(x, 0xF9E64);
      alu_cmpb(c, lift_r8(x, c->a[0]), c->d[5] & 0xFF); /* cmp.b (a0),d5 */
      lift_charge(x, 0xF9E68);
      {
        int le2 = c->zf || ((!!c->nf) != (!!c->vf));
        lift_charge_bcc(x, 0xF9E6A, le2);              /* ble.w loc_F9E7E */
        if (!le2)
        {
          setb(&c->d[7], 0xFF);                        /* st d7 */
          lift_charge(x, 0xF9E6E);
          lift_w8(x, c->a[0], c->d[5] & 0xFF); alu_moveb(c, c->d[5] & 0xFF);
          lift_charge(x, 0xF9E70);                     /* move.b d5,(a0) */
          lift_w8(x, c->a[0] + 1, c->d[3] & 0xFF); alu_moveb(c, c->d[3] & 0xFF);
          lift_charge(x, 0xF9E72);                     /* move.b d3,1(a0) */
          lift_w8(x, c->a[0] + 2, c->d[4] & 0xFF); alu_moveb(c, c->d[4] & 0xFF);
          lift_charge(x, 0xF9E76);                     /* move.b d4,2(a0) */
          lift_w8(x, c->a[0] + 3, c->d[2] & 0xFF); alu_moveb(c, c->d[2] & 0xFF);
          lift_charge(x, 0xF9E7A);                     /* move.b d2,3(a0) */
        }
      }

      /* loc_F9E7E */
      setw(&c->d[5], alu_movew(c, lift_r16(x, c->a[2] + 0xC))); /* move.w $C(a2),d5 */
      lift_charge(x, 0xF9E7E);
      setw(&c->d[6], alu_movew(c, lift_r16(x, c->a[2]))); /* move.w (a2),d6 */
      lift_charge(x, 0xF9E82);
      setw(&c->d[6], alu_subw(c, W(c->d[5]), W(c->d[6]))); /* sub.w d5,d6 */
      lift_charge(x, 0xF9E84);
      alu_cmpb(c, lift_r8(x, c->a[0] + 4), c->d[6] & 0xFF); /* cmp.b 4(a0),d6 */
      lift_charge(x, 0xF9E86);
      {
        int le3 = c->zf || ((!!c->nf) != (!!c->vf));
        lift_charge_bcc(x, 0xF9E8A, le3);              /* ble.w loc_F9EA0 */
        if (!le3)
        {
          setb(&c->d[7], 0xFF);                        /* st d7 */
          lift_charge(x, 0xF9E8E);
          lift_w8(x, c->a[0] + 4, c->d[6] & 0xFF); alu_moveb(c, c->d[6] & 0xFF);
          lift_charge(x, 0xF9E90);                     /* move.b d6,4(a0) */
          lift_w8(x, c->a[0] + 5, c->d[3] & 0xFF); alu_moveb(c, c->d[3] & 0xFF);
          lift_charge(x, 0xF9E94);                     /* move.b d3,5(a0) */
          lift_w8(x, c->a[0] + 6, c->d[4] & 0xFF); alu_moveb(c, c->d[4] & 0xFF);
          lift_charge(x, 0xF9E98);                     /* move.b d4,6(a0) */
          lift_w8(x, c->a[0] + 7, c->d[2] & 0xFF); alu_moveb(c, c->d[2] & 0xFF);
          lift_charge(x, 0xF9E9C);                     /* move.b d2,7(a0) */
        }
      }
    }
  }

  /* loc_F9EA0 */
  lift_call(x, 0xF9EA0, 4, Sram_SyncAltTeamRecordWrite); /* bsr.w sub_F9C56 */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = frame + 60;
  lift_charge_movem(x, 0xF9EA4);

  lift_charge(x, 0xF9EA8);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  }
}

void SRAM_RecalcChecksum(lift_ctx *);          /* save.c ($1A206) */
void Roster_CacheBothNibbleCounts(lift_ctx *);
void Roster_CacheBothLineCounts(lift_ctx *);
void Stats_TrackTeamLeader(lift_ctx *);
void Stats_UpdateSeasonLeaders(lift_ctx *);
void Stats_RecordGameResult(lift_ctx *);

/*
 * Stats_CommitSeasonRecords (sub_F9CDE)
 * The end-of-game season bookkeeping pass: refresh the cached roster counts,
 * run the per-team stat-leader scan and the season-leader / game-result
 * updates for both teams, then recompute the SRAM checksum. Bails without
 * touching anything when $FFFFD458 is negative or $FFFFD054 is set.
 */
void Stats_CommitSeasonRecords(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;
  uint32_t saved[15];
  int i;

  alu_tstw(c, lift_r16(x, 0xFFFFD458u));               /* tst.w ($FFFFD458).w */
  lift_charge(x, 0xF9CDE);
  lift_charge_bcc(x, 0xF9CE2, !!c->nf);                /* bmi.w locret_F9DD8 */
  if (c->nf)
  {
    lift_charge(x, 0xF9DD8);                           /* locret_F9DD8: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  alu_tstw(c, lift_r16(x, 0xFFFFD054u));               /* tst.w ($FFFFD054).w */
  lift_charge(x, 0xF9CE6);
  lift_charge_bcc(x, 0xF9CEA, !c->zf);                 /* bne.w locret_F9DD8 */
  if (!c->zf)
  {
    lift_charge(x, 0xF9DD8);                           /* locret_F9DD8: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp = c->a[7] - 60;                                   /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9CEE);

  lift_call(x, 0xF9CF2, 4, Roster_CacheBothNibbleCounts); /* bsr.w sub_F9FC0 */
  if (x->declined) return;
  lift_call(x, 0xF9CF6, 4, Roster_CacheBothLineCounts);   /* bsr.w sub_F9FEA */
  if (x->declined) return;

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC330u))); /* move.w (abs).w,d1 */
  lift_charge(x, 0xF9CFA);
  c->d[1] = alu_extl(c, c->d[1]);                      /* ext.l d1 */
  lift_charge(x, 0xF9CFE);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xF9D00);
  c->a[2] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xF9D06);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFD448u))); /* move.w (abs).w,d5 */
  lift_charge(x, 0xF9D0C);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFC332u))); /* move.w (abs).w,d6 */
  lift_charge(x, 0xF9D10);
  lift_call(x, 0xF9D14, 4, Stats_TrackTeamLeader);     /* bsr.w sub_F9F50 */
  if (x->declined) return;

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC332u)));
  lift_charge(x, 0xF9D18);
  c->d[1] = alu_extl(c, c->d[1]);
  lift_charge(x, 0xF9D1C);
  c->a[2] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a2 */
  lift_charge(x, 0xF9D1E);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFD44Au)));
  lift_charge(x, 0xF9D24);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFC330u)));
  lift_charge(x, 0xF9D28);
  lift_call(x, 0xF9D2C, 4, Stats_TrackTeamLeader);     /* bsr.w sub_F9F50 */
  if (x->declined) return;

  setw(&c->d[7], alu_movew(c, 0));                     /* clr.w d7 */
  lift_charge(x, 0xF9D30);
  c->a[1] = TEAM_HOME;                                 /* movea.l #$FFFFC6CE,a1 */
  lift_charge(x, 0xF9D32);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFD042u)));
  lift_charge(x, 0xF9D38);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFD044u)));
  lift_charge(x, 0xF9D3C);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC330u)));
  lift_charge(x, 0xF9D40);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFC332u)));
  lift_charge(x, 0xF9D44);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFD448u)));
  lift_charge(x, 0xF9D48);
  c->d[1] = alu_extl(c, c->d[1]);                      /* ext.l d1 */
  lift_charge(x, 0xF9D4C);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xF9D4E);
  lift_call(x, 0xF9D54, 4, Stats_UpdateSeasonLeaders); /* bsr.w sub_F9EAA */
  if (x->declined) return;

  c->a[1] = TEAM_HOME + TEAM_SIZE;                     /* movea.l #$FFFFCA32,a1 */
  lift_charge(x, 0xF9D58);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFD044u)));
  lift_charge(x, 0xF9D5E);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFD042u)));
  lift_charge(x, 0xF9D62);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC332u)));
  lift_charge(x, 0xF9D66);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFC330u)));
  lift_charge(x, 0xF9D6A);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFD44Au)));
  lift_charge(x, 0xF9D6E);
  c->d[1] = alu_extl(c, c->d[1]);
  lift_charge(x, 0xF9D72);
  c->a[0] = 0xFFFFCF36u;
  lift_charge(x, 0xF9D74);
  lift_call(x, 0xF9D7A, 4, Stats_UpdateSeasonLeaders); /* bsr.w sub_F9EAA */
  if (x->declined) return;

  c->a[0] = 0xFFFFCF36u;
  lift_charge(x, 0xF9D7E);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD042u)));
  lift_charge(x, 0xF9D84);
  c->d[1] = alu_extl(c, c->d[1]);
  lift_charge(x, 0xF9D88);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFD044u)));
  lift_charge(x, 0xF9D8A);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFC330u)));
  lift_charge(x, 0xF9D8E);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFC332u)));
  lift_charge(x, 0xF9D92);
  c->a[1] = TEAM_HOME;
  lift_charge(x, 0xF9D96);
  c->a[2] = TEAM_HOME + TEAM_SIZE;
  lift_charge(x, 0xF9D9C);
  lift_call(x, 0xF9DA2, 4, Stats_RecordGameResult);    /* bsr.w sub_F9DDA */
  if (x->declined) return;

  c->a[0] = 0xFFFFCF36u;
  lift_charge(x, 0xF9DA6);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD044u)));
  lift_charge(x, 0xF9DAC);
  c->d[1] = alu_extl(c, c->d[1]);
  lift_charge(x, 0xF9DB0);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFD042u)));
  lift_charge(x, 0xF9DB2);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFC332u)));
  lift_charge(x, 0xF9DB6);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFC330u)));
  lift_charge(x, 0xF9DBA);
  c->a[1] = TEAM_HOME + TEAM_SIZE;
  lift_charge(x, 0xF9DBE);
  c->a[2] = TEAM_HOME;
  lift_charge(x, 0xF9DC4);
  lift_call(x, 0xF9DCA, 4, Stats_RecordGameResult);    /* bsr.w sub_F9DDA */
  if (x->declined) return;

  lift_call(x, 0xF9DCE, 6, SRAM_RecalcChecksum);       /* jsr SRAM_RecalcChecksum */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xF9DD4);

  lift_charge(x, 0xF9DD8);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- Line rating average + period-change side swap (batch 11) --- */

void Object_UpdateSelectedSlot_A(lift_ctx *);  /* ($C0BC) */
void Object_UpdateSelectedSlot_B(lift_ctx *);  /* ($C0DA) */

/*
 * Lineup_AverageLineRating (sub_12EB4)
 * Averages the $30(a2) rating words of the players named by line d0's
 * 8-byte entry at $16A(a2), indexed through the $19286 position table.
 */
void Lineup_AverageLineRating(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[9];
  int i;

  for (i = 0; i < 5; i++) saved[i] = c->d[1 + i];
  for (i = 0; i < 4; i++) saved[5 + i] = c->a[i];
  sp -= 36;                                            /* movem.l d1-d5/a0-a3,-(sp) */
  for (i = 0; i < 9; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0x12EB4);

  c->a[1] = (c->a[2] + 0x16A) & 0xFFFFFFFFu;           /* lea $16A(a2),a1: no CCR */
  lift_charge(x, 0x12EB8);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));          /* asl.w #3,d0 */
  lift_charge(x, 0x12EBC);
  c->a[1] = (c->a[1] + SEW(c->d[0])) & 0xFFFFFFFFu;    /* adda.w d0,a1: no CCR */
  lift_charge(x, 0x12EBE);
  c->d[0] = alu_movel(c, 0);                           /* clr.l d0 */
  lift_charge(x, 0x12EC0);
  setw(&c->d[1], alu_movew(c, 0));                     /* clr.w d1 */
  lift_charge(x, 0x12EC2);
  c->a[0] = 0x19286u;                                  /* movea.l #off_19286,a0 */
  lift_charge(x, 0x12EC4);
  setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[2] + 0x24))); /* move.w $24(a2),d4 */
  lift_charge(x, 0x12ECA);
  lift_charge_bcc(x, 0x12ECE, 1);                      /* bra.w loc_12EEA */

  for (;;)
  {
    /* loc_12EEA: dbf d4,loc_12ED2 */
    setw(&c->d[4], W(c->d[4]) - 1);                    /* dbf: no CCR */
    if (W(c->d[4]) == 0xFFFF) { lift_charge_dbcc(x, 0x12EEA, 0, 1); break; }
    lift_charge_dbcc(x, 0x12EEA, 1, 0);

    /* loc_12ED2 */
    setw(&c->d[5], alu_movew(c, 0));                   /* clr.w d5 */
    lift_charge(x, 0x12ED2);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[4])))); /* move.b (a0,d4.w),d5 */
    lift_charge(x, 0x12ED4);
    lift_charge_bcc(x, 0x12ED8, c->zf);                /* beq.w loc_12EEA */
    if (c->zf) continue;

    setw(&c->d[3], alu_movew(c, 0));                   /* clr.w d3 */
    lift_charge(x, 0x12EDC);
    setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[5])))); /* move.b (a1,d5.w),d3 */
    lift_charge(x, 0x12EDE);
    setw(&c->d[3], alu_aslw(c, W(c->d[3]), 1));        /* asl.w #1,d3 */
    lift_charge(x, 0x12EE2);
    setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));        /* addq.w #1,d1 */
    lift_charge(x, 0x12EE4);
    setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[2] + SEW(c->d[3]) + 0x30), W(c->d[0]))); /* add.w $30(a2,d3.w),d0 */
    lift_charge(x, 0x12EE6);
  }

  lift_charge_divu(x, 0x12EEE, W(c->d[1]), c->d[0]);   /* divu.w d1,d0 */
  if (x->declined) return;                             /* zero divisor would trap */
  c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);

  for (i = 0; i < 5; i++) c->d[1 + i] = saved[i];      /* movem.l (sp)+,d1-d5/a0-a3 */
  for (i = 0; i < 4; i++) c->a[i] = saved[5 + i];
  c->a[7] = sp + 36;
  lift_charge_movem(x, 0x12EF0);

  lift_charge(x, 0x12EF4);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Period_SwapSidesAndSlots (sub_FE1D8)
 * At a period change, flips the $FFFFD05A/$D05C side flags (depending on the
 * score comparison and the rink-flip bit) and then, when the clock and mode
 * call for it, re-points the selected object slot via
 * Object_UpdateSelectedSlot_A/B.
 */
void Period_SwapSidesAndSlots(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  sp -= 60;                                            /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++)
  {
    lift_w16(x, sp + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, sp + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = sp;
  lift_charge_movem(x, 0xFE1D8);

  alu_tstw(c, lift_r16(x, 0xFFFFD04Au));               /* tst.w ($FFFFD04A).w */
  lift_charge(x, 0xFE1DC);
  lift_charge_bcc(x, 0xFE1E0, c->zf);                  /* beq.w loc_FE214 */
  if (!c->zf)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC328u))); /* move.w (abs).w,d0 */
    lift_charge(x, 0xFE1E4);
    alu_cmpw(c, lift_r16(x, 0xFFFFC32Au), W(c->d[0])); /* cmp.w ($FFFFC32A).w,d0 */
    lift_charge(x, 0xFE1E8);
    lift_charge_bcc(x, 0xFE1EC, !c->zf);               /* bne.w loc_FE1FA */
    if (c->zf)
    {
      lift_w16(x, 0xFFFFD05Cu, alu_eorw(c, 1, lift_r16(x, 0xFFFFD05Cu))); /* eori.w #1,(abs).w */
      lift_charge(x, 0xFE1F0);
      lift_charge_bcc(x, 0xFE1F6, 1);                  /* bra.w loc_FE20E */
      goto loc_FE20E;
    }

    /* loc_FE1FA */
    alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 1);           /* btst #1,($FFFFC2EC).w */
    lift_charge(x, 0xFE1FA);
    lift_charge_bcc(x, 0xFE200, c->zf);                /* beq.w loc_FE20E */
    if (c->zf) goto loc_FE20E;

    lift_w16(x, 0xFFFFD05Cu, alu_eorw(c, 1, lift_r16(x, 0xFFFFD05Cu))); /* eori.w #1,(abs).w */
    lift_charge(x, 0xFE204);
    lift_charge_bcc(x, 0xFE20A, 1);                    /* bra.w loc_FE214 */
    goto loc_FE214;

  loc_FE20E:
    lift_w16(x, 0xFFFFD05Au, alu_eorw(c, 1, lift_r16(x, 0xFFFFD05Au))); /* eori.w #1,(abs).w */
    lift_charge(x, 0xFE20E);
  }

loc_FE214:
  alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 0);             /* btst #0,($FFFFC2FA).w */
  lift_charge(x, 0xFE214);
  lift_charge_bcc(x, 0xFE21A, !c->zf);                 /* bne.w loc_FE228 */
  if (c->zf)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2F2u), 2);           /* btst #2,($FFFFC2F2).w */
    lift_charge(x, 0xFE21E);
    lift_charge_bcc(x, 0xFE224, c->zf);                /* beq.w loc_FE2C2 */
    if (c->zf) goto loc_FE2C2;
  }

  /* loc_FE228 */
  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 1);             /* btst #1,($FFFFC2EC).w */
  lift_charge(x, 0xFE228);
  lift_charge_bcc(x, 0xFE22E, !c->zf);                 /* bne.w loc_FE28A */
  if (c->zf)
  {
    alu_cmpw(c, 2, lift_r16(x, 0xFFFFD04Au));          /* cmpi.w #2,($FFFFD04A).w */
    lift_charge(x, 0xFE232);
    lift_charge_bcc(x, 0xFE238, !c->zf);               /* bne.w loc_FE24A */
    if (c->zf)
    {
      alu_cmpw(c, 5, lift_r16(x, 0xFFFFD406u));        /* cmpi.w #5,($FFFFD406).w */
      lift_charge(x, 0xFE23C);
      {
        int gt = !c->zf && ((!!c->nf) == (!!c->vf));
        lift_charge_bcc(x, 0xFE242, gt);               /* bgt.w loc_FE2C2 */
        if (gt) goto loc_FE2C2;
      }
      lift_charge_bcc(x, 0xFE246, 1);                  /* bra.w loc_FE254 */
    }
    else
    {
      /* loc_FE24A */
      alu_cmpw(c, 5, lift_r16(x, 0xFFFFD406u));        /* cmpi.w #5,($FFFFD406).w */
      lift_charge(x, 0xFE24A);
      {
        int le = c->zf || ((!!c->nf) != (!!c->vf));
        lift_charge_bcc(x, 0xFE250, le);               /* ble.w loc_FE2C2 */
        if (le) goto loc_FE2C2;
      }
    }

    /* loc_FE254 */
    setw(&c->d[0], alu_movew(c, 0));                   /* move.w #0,d0 */
    lift_charge(x, 0xFE254);
    alu_cmpw(c, 2, lift_r16(x, 0xFFFFD04Au));          /* cmpi.w #2,($FFFFD04A).w */
    lift_charge(x, 0xFE258);
    lift_charge_bcc(x, 0xFE25E, !c->zf);               /* bne.w loc_FE266 */
    if (c->zf)
    {
      setw(&c->d[0], alu_movew(c, 6));                 /* move.w #6,d0 */
      lift_charge(x, 0xFE262);
    }

    /* loc_FE266 */
    alu_tstw(c, lift_r16(x, 0xFFFFD05Au));             /* tst.w ($FFFFD05A).w */
    lift_charge(x, 0xFE266);
    lift_charge_bcc(x, 0xFE26A, !c->zf);               /* bne.w loc_FE280 */
    if (c->zf)
    {
      setw(&c->d[0], alu_movew(c, 5));                 /* move.w #5,d0 */
      lift_charge(x, 0xFE26E);
      alu_cmpw(c, 2, lift_r16(x, 0xFFFFD04Au));        /* cmpi.w #2,($FFFFD04A).w */
      lift_charge(x, 0xFE272);
      lift_charge_bcc(x, 0xFE278, !c->zf);             /* bne.w loc_FE280 */
      if (c->zf)
      {
        setw(&c->d[0], alu_movew(c, 0xB));             /* move.w #$B,d0 */
        lift_charge(x, 0xFE27C);
      }
    }

    /* loc_FE280 */
    lift_call(x, 0xFE280, 6, Object_UpdateSelectedSlot_A); /* jsr sub_C0BC */
    if (x->declined) return;
    lift_charge_bcc(x, 0xFE286, 1);                    /* bra.w loc_FE2C2 */
    goto loc_FE2C2;
  }

  /* loc_FE28A */
  alu_cmpw(c, 2, lift_r16(x, 0xFFFFD04Au));            /* cmpi.w #2,($FFFFD04A).w */
  lift_charge(x, 0xFE28A);
  lift_charge_bcc(x, 0xFE290, !c->zf);                 /* bne.w loc_FE2A2 */
  if (c->zf)
  {
    alu_cmpw(c, 5, lift_r16(x, 0xFFFFD406u));          /* cmpi.w #5,($FFFFD406).w */
    lift_charge(x, 0xFE294);
    {
      int le = c->zf || ((!!c->nf) != (!!c->vf));
      lift_charge_bcc(x, 0xFE29A, le);                 /* ble.w loc_FE2C2 */
      if (le) goto loc_FE2C2;
    }
    lift_charge_bcc(x, 0xFE29E, 1);                    /* bra.w loc_FE2AC */
  }
  else
  {
    /* loc_FE2A2 */
    alu_cmpw(c, 5, lift_r16(x, 0xFFFFD406u));          /* cmpi.w #5,($FFFFD406).w */
    lift_charge(x, 0xFE2A2);
    {
      int gt = !c->zf && ((!!c->nf) == (!!c->vf));
      lift_charge_bcc(x, 0xFE2A8, gt);                 /* bgt.w loc_FE2C2 */
      if (gt) goto loc_FE2C2;
    }
  }

  /* loc_FE2AC */
  setw(&c->d[0], alu_movew(c, 6));                     /* move.w #6,d0 */
  lift_charge(x, 0xFE2AC);
  alu_tstw(c, lift_r16(x, 0xFFFFD05Cu));               /* tst.w ($FFFFD05C).w */
  lift_charge(x, 0xFE2B0);
  lift_charge_bcc(x, 0xFE2B4, !c->zf);                 /* bne.w loc_FE2BC */
  if (c->zf)
  {
    setw(&c->d[0], alu_movew(c, 0xB));                 /* move.w #$B,d0 */
    lift_charge(x, 0xFE2B8);
  }

  /* loc_FE2BC */
  lift_call(x, 0xFE2BC, 6, Object_UpdateSelectedSlot_B); /* jsr sub_C0DA */
  if (x->declined) return;

loc_FE2C2:
  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFE2C2);

  lift_charge(x, 0xFE2C6);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Lineup_AverageLineRating(lift_ctx *);   /* ($12EB4) */

/*
 * Lineup_PickBestLine (sub_F790)
 * Chooses which line to put on for a2 against a1, scoring candidates with
 * Lineup_AverageLineRating. Tied period scores ($24) take the $F818/$F87C
 * preference tables (offset by the goal-difference comparison) and settle on
 * the first line rating above $C00; otherwise it compares the two lines
 * around the current index and stores the winner at $16(a2).
 */
void Lineup_PickBestLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 16;
  uint32_t saved[4];
  int i;

  saved[0] = c->d[0]; saved[1] = c->d[1]; saved[2] = c->d[2]; saved[3] = c->a[0];
  for (i = 0; i < 4; i++)                              /* movem.l d0-d2/a0,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xF790);

  c->d[0] = alu_moveql(c, 3);                          /* moveq #3,d0 */
  lift_charge(x, 0xF794);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[2] + 0x24))); /* move.w $24(a2),d1 */
  lift_charge(x, 0xF796);
  setw(&c->d[1], alu_subw(c, lift_r16(x, c->a[1] + 0x24), W(c->d[1]))); /* sub.w $24(a1),d1 */
  lift_charge(x, 0xF79A);
  lift_charge_bcc(x, 0xF79E, c->zf);                   /* beq.w loc_F7CA */
  if (!c->zf)
  {
    lift_charge_bcc(x, 0xF7A2, !c->nf);                /* bpl.w loc_F7A8 */
    if (c->nf)
    {
      setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));      /* addq.w #2,d0 */
      lift_charge(x, 0xF7A6);
    }

    /* loc_F7A8 */
    {
      uint32_t sp = c->a[7] - 2;                       /* move.w d0,-(sp) */
      lift_w16(x, sp, W(c->d[0]));
      alu_movew(c, W(c->d[0]));
      c->a[7] = sp;
    }
    lift_charge(x, 0xF7A8);
    lift_call(x, 0xF7AA, 4, Lineup_AverageLineRating); /* bsr.w sub_12EB4 */
    if (x->declined) return;
    setw(&c->d[1], alu_movew(c, W(c->d[0])));          /* move.w d0,d1 */
    lift_charge(x, 0xF7AE);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7]))); /* move.w (sp),d0 */
    lift_charge(x, 0xF7B0);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0xF7B2);
    lift_call(x, 0xF7B4, 4, Lineup_AverageLineRating); /* bsr.w sub_12EB4 */
    if (x->declined) return;
    alu_cmpw(c, W(c->d[0]), W(c->d[1]));               /* cmp.w d0,d1 */
    lift_charge(x, 0xF7B8);
    {
      int ge = (!!c->nf) == (!!c->vf);
      lift_charge_bcc(x, 0xF7BA, ge);                  /* bge.w loc_F7C0 */
      if (!ge)
      {
        lift_w16(x, c->a[7], alu_addw(c, 1, lift_r16(x, c->a[7]))); /* addq.w #1,(sp) */
        lift_charge(x, 0xF7BE);
      }
    }
    /* loc_F7C0 */
    lift_w16(x, c->a[2] + 0x16, lift_r16(x, c->a[7])); /* move.w (sp)+,$16(a2) */
    alu_movew(c, lift_r16(x, c->a[7]));
    c->a[7] += 2;
    lift_charge(x, 0xF7C0);
    goto loc_F7C4;
  }

  /* loc_F7CA */
  alu_cmpl(c, SEW(0xC6CE), c->a[2]);                   /* cmpa.w #$C6CE,a2 */
  lift_charge(x, 0xF7CA);
  lift_charge_bcc(x, 0xF7CE, !c->zf);                  /* bne.w loc_F842 */
  if (c->zf)
  {
    c->d[1] = alu_moveql(c, 2);                        /* moveq #2,d1 */
    lift_charge(x, 0xF7D2);
    c->a[0] = 0xF818u;                                 /* lea word_F818(pc),a0 */
    lift_charge(x, 0xF7D4);
    alu_cmpw(c, 2, lift_r16(x, 0xFFFFC466u));          /* cmpi.w #2,($FFFFC466).w */
    lift_charge(x, 0xF7D8);
    lift_charge_bcc(x, 0xF7DE, !c->zf);                /* bne.w loc_F7FA */
    if (c->zf)
    {
      setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0xC))); /* move.w $C(a2),d2 */
      lift_charge(x, 0xF7E2);
      alu_cmpw(c, lift_r16(x, c->a[1] + 0xC), W(c->d[2])); /* cmp.w $C(a1),d2 */
      lift_charge(x, 0xF7E6);
      lift_charge_bcc(x, 0xF7EA, c->zf);               /* beq.w loc_F7FA */
      if (!c->zf)
      {
        c->a[0] = (c->a[0] + 0xE) & 0xFFFFFFFFu;       /* adda.w #$E,a0: no CCR */
        lift_charge(x, 0xF7EE);
        {
          int gt = !c->zf && ((!!c->nf) == (!!c->vf));
          lift_charge_bcc(x, 0xF7F2, gt);              /* bgt.w loc_F7FA */
          if (!gt)
          {
            c->a[0] = (c->a[0] + 0xE) & 0xFFFFFFFFu;   /* adda.w #$E,a0 */
            lift_charge(x, 0xF7F6);
          }
        }
      }
    }

    /* loc_F7FA */
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1] + 0x16))); /* move.w $16(a1),d1 */
    lift_charge(x, 0xF7FA);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 1));        /* asl.w #1,d1 */
    lift_charge(x, 0xF7FE);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[1])))); /* move.w (a0,d1.w),d0 */
    lift_charge(x, 0xF800);
    lift_call(x, 0xF804, 4, Lineup_AverageLineRating); /* bsr.w sub_12EB4 */
    if (x->declined) return;
    alu_cmpw(c, 0xC00, W(c->d[0]));                    /* cmpi.w #$C00,d0 */
    lift_charge(x, 0xF808);
    {
      int ls = c->cf || c->zf;
      lift_charge_bcc(x, 0xF80C, ls);                  /* bls.w loc_F842 */
      if (!ls)
      {
        uint32_t v = lift_r16(x, c->a[0] + SEW(c->d[1]));
        lift_w16(x, c->a[2] + 0x16, v); alu_movew(c, v); /* move.w (a0,d1.w),$16(a2) */
        lift_charge(x, 0xF810);
        lift_charge_bcc(x, 0xF816, 1);                 /* bra.s loc_F7C4 */
        goto loc_F7C4;
      }
    }
  }

  /* loc_F842 */
  c->d[1] = alu_moveql(c, 2);                          /* moveq #2,d1 */
  lift_charge(x, 0xF842);
  c->a[0] = 0xF87Cu;                                   /* lea word_F87C(pc),a0 */
  lift_charge(x, 0xF844);
  alu_cmpw(c, 2, lift_r16(x, 0xFFFFC466u));            /* cmpi.w #2,($FFFFC466).w */
  lift_charge(x, 0xF848);
  lift_charge_bcc(x, 0xF84E, !c->zf);                  /* bne.w loc_F866 */
  if (c->zf)
  {
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0xC))); /* move.w $C(a2),d2 */
    lift_charge(x, 0xF852);
    alu_cmpw(c, lift_r16(x, c->a[1] + 0xC), W(c->d[2])); /* cmp.w $C(a1),d2 */
    lift_charge(x, 0xF856);
    lift_charge_bcc(x, 0xF85A, c->zf);                 /* beq.w loc_F866 */
    if (!c->zf)
    {
      c->a[0] = (c->a[0] + 6) & 0xFFFFFFFFu;           /* addq.w #6,a0: no CCR */
      lift_charge(x, 0xF85E);
      {
        int gt = !c->zf && ((!!c->nf) == (!!c->vf));
        lift_charge_bcc(x, 0xF860, gt);                /* bgt.w loc_F866 */
        if (!gt)
        {
          c->a[0] = (c->a[0] + 6) & 0xFFFFFFFFu;       /* addq.w #6,a0 */
          lift_charge(x, 0xF864);
        }
      }
    }
  }

  for (;;)
  {
    /* loc_F866 */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0]))); /* move.w (a0)+,d0 */
    c->a[0] += 2;
    lift_charge(x, 0xF866);
    lift_call(x, 0xF868, 4, Lineup_AverageLineRating); /* bsr.w sub_12EB4 */
    if (x->declined) return;
    alu_cmpw(c, 0xC00, W(c->d[0]));                    /* cmpi.w #$C00,d0 */
    lift_charge(x, 0xF86C);
    {
      int hi = !c->cf && !c->zf;                       /* dbhi d1,loc_F866 */
      if (hi) { lift_charge_dbcc(x, 0xF870, 0, 0); break; }
      setw(&c->d[1], W(c->d[1]) - 1);                  /* counter: no CCR */
      if (W(c->d[1]) != 0xFFFF) { lift_charge_dbcc(x, 0xF870, 1, 0); continue; }
      lift_charge_dbcc(x, 0xF870, 0, 1);
      break;
    }
  }

  c->a[0] -= 2;                                        /* move.w -(a0),$16(a2) */
  {
    uint32_t v = lift_r16(x, c->a[0]);
    lift_w16(x, c->a[2] + 0x16, v); alu_movew(c, v);
  }
  lift_charge(x, 0xF874);
  lift_charge_bcc(x, 0xF878, 1);                       /* bra.w loc_F7C4 */

loc_F7C4:
  c->d[0] = saved[0]; c->d[1] = saved[1]; c->d[2] = saved[2]; c->a[0] = saved[3];
  c->a[7] = frame + 16;                                /* movem.l (sp)+,d0-d2/a0 */
  lift_charge_movem(x, 0xF7C4);

  lift_charge(x, 0xF7C8);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}


/*
 * Season_ClearTeamFromRecords (sub_FBA76)
 * Wipes team $FFFFD4EA out of the saved season records: the 728 four-byte
 * slots at SRAM $0, then the 28 sixteen-byte blocks at $B60 (six name bytes
 * each), then zeroes that team's own $D20 block, rewriting only the blocks it
 * actually touched and recomputing the checksum at the end.
 */
void Season_ClearTeamFromRecords(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 60;
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  for (i = 0; i < 15; i++)                             /* movem.l d0-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xFBA76);

  setw(&c->d[7], alu_movew(c, 0x2D7));                 /* move.w #$2D7,d7 */
  lift_charge(x, 0xFBA7A);
  c->d[0] = alu_moveql(c, 0);                          /* moveq #0,d0 */
  lift_charge(x, 0xFBA7E);
  c->d[1] = alu_moveql(c, 4);                          /* moveq #4,d1 */
  lift_charge(x, 0xFBA80);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFBA82);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFD4EAu))); /* move.w (abs).w,d6 */
  lift_charge(x, 0xFBA88);

  for (;;)
  {
    setw(&c->d[5], alu_movew(c, 0));                   /* clr.w d5 */
    lift_charge(x, 0xFBA8C);
    lift_call(x, 0xFBA8E, 6, SRAM_ReadBytes);          /* jsr SRAM_ReadBytes */
    if (x->declined) return;

    alu_cmpb(c, lift_r8(x, c->a[0] + 0x1), c->d[6] & 0xFF); /* cmp.b $1(a0),d6 */
    lift_charge(x, 0xFBA94);
    lift_charge_bcc(x, 0xFBA98, !c->zf);                 /* bne.w loc_FBAA2 */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x1, 0); alu_moveb(c, 0);        /* clr.b $1(a0) */
      lift_charge(x, 0xFBA9C);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBAA0);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0x3), c->d[6] & 0xFF); /* cmp.b $3(a0),d6 */
    lift_charge(x, 0xFBAA2);
    lift_charge_bcc(x, 0xFBAA6, !c->zf);                 /* bne.w loc_FBAB0 */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x3, 0); alu_moveb(c, 0);        /* clr.b $3(a0) */
      lift_charge(x, 0xFBAAA);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBAAE);
    }

    /* loc_FBAB0 */
    alu_tstw(c, W(c->d[5]));                           /* tst.w d5 */
    lift_charge(x, 0xFBAB0);
    lift_charge_bcc(x, 0xFBAB2, c->zf);                /* beq.w loc_FBABC */
    if (!c->zf)
    {
      lift_call(x, 0xFBAB6, 6, SRAM_WriteBytes);       /* jsr SRAM_WriteBytes */
      if (x->declined) return;
    }

    /* loc_FBABC */
    c->d[0] = alu_addl(c, 4, c->d[0]);                 /* addq.l #4,d0 */
    lift_charge(x, 0xFBABC);
    setw(&c->d[7], W(c->d[7]) - 1);                    /* dbf d7: no CCR */
    if (W(c->d[7]) != 0xFFFF) { lift_charge_dbcc(x, 0xFBABE, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFBABE, 0, 1);
    break;
  }

  setw(&c->d[7], alu_movew(c, 0x1B));                  /* move.w #$1B,d7 */
  lift_charge(x, 0xFBAC2);
  c->d[0] = alu_movel(c, 0xB60);                       /* move.l #$B60,d0 */
  lift_charge(x, 0xFBAC6);
  c->d[1] = alu_moveql(c, 0x10);                       /* moveq #$10,d1 */
  lift_charge(x, 0xFBACC);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFBACE);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFFFD4EAu)));
  lift_charge(x, 0xFBAD4);

  for (;;)
  {
    setw(&c->d[5], alu_movew(c, 0));                   /* clr.w d5 */
    lift_charge(x, 0xFBAD8);
    lift_call(x, 0xFBADA, 6, SRAM_ReadBytes);          /* jsr SRAM_ReadBytes */
    if (x->declined) return;

    alu_cmpb(c, lift_r8(x, c->a[0] + 0x1), c->d[6] & 0xFF); /* cmp.b $1(a0),d6 */
    lift_charge(x, 0xFBAE0);
    lift_charge_bcc(x, 0xFBAE4, !c->zf);                 /* bne.w loc_FBAEE */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x1, 0); alu_moveb(c, 0);        /* clr.b $1(a0) */
      lift_charge(x, 0xFBAE8);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBAEC);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0x3), c->d[6] & 0xFF); /* cmp.b $3(a0),d6 */
    lift_charge(x, 0xFBAEE);
    lift_charge_bcc(x, 0xFBAF2, !c->zf);                 /* bne.w loc_FBAFC */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x3, 0); alu_moveb(c, 0);        /* clr.b $3(a0) */
      lift_charge(x, 0xFBAF6);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBAFA);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0x5), c->d[6] & 0xFF); /* cmp.b $5(a0),d6 */
    lift_charge(x, 0xFBAFC);
    lift_charge_bcc(x, 0xFBB00, !c->zf);                 /* bne.w loc_FBB0A */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x5, 0); alu_moveb(c, 0);        /* clr.b $5(a0) */
      lift_charge(x, 0xFBB04);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBB08);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0x7), c->d[6] & 0xFF); /* cmp.b $7(a0),d6 */
    lift_charge(x, 0xFBB0A);
    lift_charge_bcc(x, 0xFBB0E, !c->zf);                 /* bne.w loc_FBB18 */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x7, 0); alu_moveb(c, 0);        /* clr.b $7(a0) */
      lift_charge(x, 0xFBB12);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBB16);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0x9), c->d[6] & 0xFF); /* cmp.b $9(a0),d6 */
    lift_charge(x, 0xFBB18);
    lift_charge_bcc(x, 0xFBB1C, !c->zf);                 /* bne.w loc_FBB26 */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0x9, 0); alu_moveb(c, 0);        /* clr.b $9(a0) */
      lift_charge(x, 0xFBB20);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBB24);
    }
    alu_cmpb(c, lift_r8(x, c->a[0] + 0xB), c->d[6] & 0xFF); /* cmp.b $B(a0),d6 */
    lift_charge(x, 0xFBB26);
    lift_charge_bcc(x, 0xFBB2A, !c->zf);                 /* bne.w loc_FBB34 */
    if (c->zf)
    {
      lift_w8(x, c->a[0] + 0xB, 0); alu_moveb(c, 0);        /* clr.b $B(a0) */
      lift_charge(x, 0xFBB2E);
      setb(&c->d[5], 0xFF);                            /* st d5: no CCR */
      lift_charge(x, 0xFBB32);
    }

    /* loc_FBB34 */
    alu_tstw(c, W(c->d[5]));                           /* tst.w d5 */
    lift_charge(x, 0xFBB34);
    lift_charge_bcc(x, 0xFBB36, c->zf);                /* beq.w loc_FBB40 */
    if (!c->zf)
    {
      lift_call(x, 0xFBB3A, 6, SRAM_WriteBytes);       /* jsr SRAM_WriteBytes */
      if (x->declined) return;
    }

    /* loc_FBB40 */
    c->d[0] = alu_addl(c, 0x10, c->d[0]);              /* addi.l #$10,d0 */
    lift_charge(x, 0xFBB40);
    setw(&c->d[7], W(c->d[7]) - 1);                    /* dbf d7: no CCR */
    if (W(c->d[7]) != 0xFFFF) { lift_charge_dbcc(x, 0xFBB46, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFBB46, 0, 1);
    break;
  }

  c->d[0] = alu_movel(c, 0xD20);                       /* move.l #$D20,d0 */
  lift_charge(x, 0xFBB4A);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFD4EAu))); /* move.w (abs).w,d3 */
  lift_charge(x, 0xFBB50);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 4));          /* asl.w #4,d3 */
  lift_charge(x, 0xFBB54);
  c->d[3] = alu_extl(c, c->d[3]);                      /* ext.l d3 */
  lift_charge(x, 0xFBB56);
  c->d[0] = alu_addl(c, c->d[3], c->d[0]);             /* add.l d3,d0 */
  lift_charge(x, 0xFBB58);
  c->d[1] = alu_moveql(c, 0x10);                       /* moveq #$10,d1 */
  lift_charge(x, 0xFBB5A);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFBB5C);
  lift_call(x, 0xFBB62, 6, SRAM_ReadBytes);            /* jsr SRAM_ReadBytes */
  if (x->declined) return;

  {
    uint32_t sp = c->a[7] - 4;                         /* move.l a0,-(sp) */
    lift_w16(x, sp + 0, (c->a[0] >> 16) & 0xFFFF);
    lift_w16(x, sp + 2, c->a[0] & 0xFFFF);
    alu_movel(c, c->a[0]);
    c->a[7] = sp;
  }
  lift_charge(x, 0xFBB68);
  setw(&c->d[3], alu_movew(c, 0xF));                   /* move.w #$F,d3 */
  lift_charge(x, 0xFBB6A);

  for (;;)
  {
    lift_w8(x, c->a[0], 0); alu_moveb(c, 0);           /* clr.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xFBB6E);
    setw(&c->d[3], W(c->d[3]) - 1);                    /* dbf d3: no CCR */
    if (W(c->d[3]) != 0xFFFF) { lift_charge_dbcc(x, 0xFBB70, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFBB70, 0, 1);
    break;
  }

  c->a[0] = lift_r32(x, c->a[7]);                      /* movea.l (sp)+,a0 */
  c->a[7] += 4;
  lift_charge(x, 0xFBB74);
  lift_call(x, 0xFBB76, 6, SRAM_WriteBytes);           /* jsr SRAM_WriteBytes */
  if (x->declined) return;
  lift_call(x, 0xFBB7C, 6, SRAM_RecalcChecksum);       /* jsr SRAM_RecalcChecksum */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = frame + 60;
  lift_charge_movem(x, 0xFBB82);

  lift_charge(x, 0xFBB86);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Sram_SyncFixedD45ABlockWrite(lift_ctx *);  /* ($F9C5E) */
void Season_ClearTeamFromRecords(lift_ctx *);   /* ($FBA76) */

/*
 * Season_CommitTeamName (sub_FB9D4)
 * Compares the just-entered name at $FFFFD4DA against the stored 12-byte
 * name for team $FFFFD4EA (treating '-' as NUL). If it changed, the new name
 * is written, synced to SRAM, and the team's old records are wiped. Finally
 * the team index is published to $FFFFD042 or $FFFFD044.
 */
void Season_CommitTeamName(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 60;
  uint32_t saved[15];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  for (i = 0; i < 15; i++)                             /* movem.l d0-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xFB9D4);

  c->a[0] = 0xFFFFD45Au;                               /* movea.l #$FFFFD45A,a0 */
  lift_charge(x, 0xFB9D8);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD4EAu))); /* move.w (abs).w,d0 */
  lift_charge(x, 0xFB9DE);
  lift_charge_mulu(x, 0xFB9E2, 0xC);                   /* mulu.w #$C,d0 */
  c->d[0] = alu_mulu(c, 0xC, c->d[0]);
  c->a[0] = (c->a[0] + SEW(c->d[0])) & 0xFFFFFFFFu;    /* adda.w d0,a0: no CCR */
  lift_charge(x, 0xFB9E6);
  c->a[1] = 0xFFFFD4DAu;                               /* movea.l #$FFFFD4DA,a1 */
  lift_charge(x, 0xFB9E8);
  setw(&c->d[4], alu_movew(c, 0xB));                   /* move.w #$B,d4 */
  lift_charge(x, 0xFB9EE);

  {
    int changed = 0;
    for (;;)
    {
      alu_cmpb(c, 0x2D, lift_r8(x, c->a[1]));          /* cmpi.b #$2D,(a1) */
      lift_charge(x, 0xFB9F2);
      lift_charge_bcc(x, 0xFB9F6, !c->zf);             /* bne.w loc_FB9FE */
      if (c->zf)
      {
        lift_w8(x, c->a[1], 0); alu_moveb(c, 0);       /* move.b #0,(a1) */
        lift_charge(x, 0xFB9FA);
      }

      /* loc_FB9FE */
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1]))); /* move.b (a1)+,d1 */
      c->a[1] += 1;
      lift_charge(x, 0xFB9FE);
      alu_cmpb(c, lift_r8(x, c->a[0]), c->d[1] & 0xFF); /* cmp.b (a0)+,d1 */
      c->a[0] += 1;
      lift_charge(x, 0xFBA00);
      lift_charge_bcc(x, 0xFBA02, !c->zf);             /* bne.w loc_FBA0E */
      if (!c->zf) { changed = 1; break; }

      setw(&c->d[4], W(c->d[4]) - 1);                  /* dbf d4: no CCR */
      if (W(c->d[4]) != 0xFFFF) { lift_charge_dbcc(x, 0xFBA06, 1, 0); continue; }
      lift_charge_dbcc(x, 0xFBA06, 0, 1);
      lift_charge_bcc(x, 0xFBA0A, 1);                  /* bra.w loc_FBA4C */
      break;
    }

    if (changed)
    {
      /* loc_FBA0E: rewrite the stored name from the entry buffer */
      c->a[0] = 0xFFFFD45Au;                           /* movea.l #$FFFFD45A,a0 */
      lift_charge(x, 0xFBA0E);
      setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD4EAu)));
      lift_charge(x, 0xFBA14);
      lift_charge_mulu(x, 0xFBA18, 0xC);               /* mulu.w #$C,d0 */
      c->d[0] = alu_mulu(c, 0xC, c->d[0]);
      c->a[0] = (c->a[0] + SEW(c->d[0])) & 0xFFFFFFFFu; /* adda.w d0,a0 */
      lift_charge(x, 0xFBA1C);
      c->a[1] = 0xFFFFD4DAu;                           /* movea.l #$FFFFD4DA,a1 */
      lift_charge(x, 0xFBA1E);
      setw(&c->d[4], alu_movew(c, 0xB));               /* move.w #$B,d4 */
      lift_charge(x, 0xFBA24);

      for (;;)
      {
        setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1]))); /* move.b (a1)+,d1 */
        c->a[1] += 1;
        lift_charge(x, 0xFBA28);
        alu_cmpb(c, 0x2D, c->d[1] & 0xFF);             /* cmpi.b #$2D,d1 */
        lift_charge(x, 0xFBA2A);
        lift_charge_bcc(x, 0xFBA2E, !c->zf);           /* bne.w loc_FBA36 */
        if (c->zf)
        {
          setb(&c->d[1], alu_moveb(c, 0));             /* move.b #0,d1 */
          lift_charge(x, 0xFBA32);
        }

        /* loc_FBA36 */
        lift_w8(x, c->a[0], c->d[1] & 0xFF);           /* move.b d1,(a0)+ */
        alu_moveb(c, c->d[1] & 0xFF);
        c->a[0] += 1;
        lift_charge(x, 0xFBA36);

        setw(&c->d[4], W(c->d[4]) - 1);                /* dbf d4: no CCR */
        if (W(c->d[4]) != 0xFFFF) { lift_charge_dbcc(x, 0xFBA38, 1, 0); continue; }
        lift_charge_dbcc(x, 0xFBA38, 0, 1);
        break;
      }

      lift_call(x, 0xFBA3C, 4, Sram_SyncFixedD45ABlockWrite); /* bsr.w sub_F9C5E */
      if (x->declined) return;
      lift_call(x, 0xFBA40, 6, Season_ClearTeamFromRecords);  /* jsr sub_FBA76 */
      if (x->declined) return;
      lift_call(x, 0xFBA46, 6, SRAM_RecalcChecksum);          /* jsr SRAM_RecalcChecksum */
      if (x->declined) return;
    }
  }

  /* loc_FBA4C */
  c->a[5] = 0xFFFFD042u;                               /* movea.l #$FFFFD042,a5 */
  lift_charge(x, 0xFBA4C);
  alu_tstw(c, lift_r16(x, 0xFFFFD4EEu));               /* tst.w ($FFFFD4EE).w */
  lift_charge(x, 0xFBA52);
  lift_charge_bcc(x, 0xFBA56, c->zf);                  /* beq.w loc_FBA60 */
  if (!c->zf)
  {
    c->a[5] = 0xFFFFD044u;                             /* movea.l #$FFFFD044,a5 */
    lift_charge(x, 0xFBA5A);
  }

  /* loc_FBA60 */
  alu_tstb(c, lift_r8(x, 0xFFFFD4DAu));                /* tst.b ($FFFFD4DA).w */
  lift_charge(x, 0xFBA60);
  lift_charge_bcc(x, 0xFBA64, !c->zf);                 /* bne.w loc_FBA6C */
  if (c->zf)
  {
    lift_w16(x, 0xFFFFD4EAu, 0); alu_movew(c, 0);      /* clr.w ($FFFFD4EA).w */
    lift_charge(x, 0xFBA68);
  }

  /* loc_FBA6C */
  {
    uint32_t v = lift_r16(x, 0xFFFFD4EAu);
    lift_w16(x, c->a[5], v); alu_movew(c, v);          /* move.w ($FFFFD4EA).w,(a5) */
  }
  lift_charge(x, 0xFBA6C);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = frame + 60;
  lift_charge_movem(x, 0xFBA70);

  lift_charge(x, 0xFBA74);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Buf_CopyBytes(lift_ctx *);           /* ($F997A) */
void Text_TrimTrailingSpaces(lift_ctx *); /* ($FAF66) */

/*
 * Text_EmitTeamName (sub_FA014)
 * Builds the display string for team d2 at (a1): a non-zero d2 spreads the
 * 12 stored name bytes (NUL rendered as space) into the buffer and stamps a
 * #$E length word, while d2 == 0 copies the default word_FA07C string via
 * Buf_CopyBytes. Both paths finish with Text_TrimTrailingSpaces.
 */
void Text_EmitTeamName(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 32;
  uint32_t saved[8];
  int i;

  for (i = 0; i < 4; i++) saved[i] = c->d[i];
  for (i = 0; i < 4; i++) saved[4 + i] = c->a[i];
  for (i = 0; i < 8; i++)                              /* movem.l d0-d3/a0-a3,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xFA014);

  c->a[2] = c->a[1];                                   /* movea.l a1,a2 */
  lift_charge(x, 0xFA018);
  alu_tstw(c, W(c->d[2]));                             /* tst.w d2 */
  lift_charge(x, 0xFA01A);
  lift_charge_bcc(x, 0xFA01C, c->zf);                  /* beq.w loc_FA060 */
  if (!c->zf)
  {
    setw(&c->d[0], alu_movew(c, 0xB));                 /* move.w #$B,d0 */
    lift_charge(x, 0xFA020);
    setw(&c->d[3], alu_movew(c, 0));                   /* clr.w d3 */
    lift_charge(x, 0xFA024);
    c->a[0] = 0xFFFFD45Au;                             /* movea.l #$FFFFD45A,a0 */
    lift_charge(x, 0xFA026);
    lift_charge_mulu(x, 0xFA02C, 0xC);                 /* mulu.w #$C,d2 */
    c->d[2] = alu_mulu(c, 0xC, c->d[2]);
    c->a[0] = (c->a[0] + c->d[2]) & 0xFFFFFFFFu;       /* adda.l d2,a0: no CCR */
    lift_charge(x, 0xFA030);

    for (;;)
    {
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3])))); /* move.b (a0,d3.w),d1 */
      lift_charge(x, 0xFA032);
      lift_charge_bcc(x, 0xFA036, !c->zf);             /* bne.w loc_FA03E */
      if (c->zf)
      {
        setb(&c->d[1], alu_moveb(c, 0x20));            /* move.b #$20,d1 */
        lift_charge(x, 0xFA03A);
      }

      /* loc_FA03E */
      lift_w8(x, c->a[1] + 2, c->d[1] & 0xFF);         /* move.b d1,2(a1) */
      alu_moveb(c, c->d[1] & 0xFF);
      lift_charge(x, 0xFA03E);
      alu_tstb(c, lift_r8(x, c->a[1]));                /* tst.b (a1)+ */
      c->a[1] += 1;
      lift_charge(x, 0xFA042);
      setw(&c->d[3], alu_addw(c, 1, W(c->d[3])));      /* addq.w #1,d3 */
      lift_charge(x, 0xFA044);

      setw(&c->d[0], W(c->d[0]) - 1);                  /* dbf d0: no CCR */
      if (W(c->d[0]) != 0xFFFF) { lift_charge_dbcc(x, 0xFA046, 1, 0); continue; }
      lift_charge_dbcc(x, 0xFA046, 0, 1);
      break;
    }

    lift_w16(x, c->a[2], 0xE); alu_movew(c, 0xE);      /* move.w #$E,(a2) */
    lift_charge(x, 0xFA04A);
    alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 7);           /* btst #7,($FFFFC2F8).w */
    lift_charge(x, 0xFA04E);
    lift_charge_bcc(x, 0xFA054, c->zf);                /* beq.w loc_FA05C */
    if (!c->zf)
    {
      lift_call(x, 0xFA058, 4, Text_TrimTrailingSpaces); /* bsr.w sub_FAF66 */
      if (x->declined) return;
    }
    /* loc_FA05C */
    lift_charge_bcc(x, 0xFA05C, 1);                    /* bra.w loc_FA076 */
  }
  else
  {
    /* loc_FA060 */
    c->a[3] = c->a[1];                                 /* movea.l a1,a3 */
    lift_charge(x, 0xFA060);
    {
      uint32_t sp = c->a[7] - 4;                       /* move.l a3,-(sp) */
      lift_w16(x, sp + 0, (c->a[3] >> 16) & 0xFFFF);
      lift_w16(x, sp + 2, c->a[3] & 0xFFFF);
      alu_movel(c, c->a[3]);
      c->a[7] = sp;
    }
    lift_charge(x, 0xFA062);
    c->a[1] = 0xFA07Cu;                                /* movea.l #word_FA07C,a1 */
    lift_charge(x, 0xFA064);
    lift_call(x, 0xFA06A, 6, Buf_CopyBytes);           /* jsr sub_F997A */
    if (x->declined) return;
    c->a[2] = lift_r32(x, c->a[7]);                    /* movea.l (sp)+,a2 */
    c->a[7] += 4;
    lift_charge(x, 0xFA070);
    lift_call(x, 0xFA072, 4, Text_TrimTrailingSpaces); /* bsr.w sub_FAF66 */
    if (x->declined) return;
  }

  /* loc_FA076 */
  for (i = 0; i < 4; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d3/a0-a3 */
  for (i = 0; i < 4; i++) c->a[i] = saved[4 + i];
  c->a[7] = frame + 32;
  lift_charge_movem(x, 0xFA076);

  lift_charge(x, 0xFA07A);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* --- "by <team>" / "<team> vs. <team>" caption builders (batch 16) --- */

void Text_EmitTeamName(lift_ctx *);          /* ($FA014) */
void Text_AppendIndexedString(lift_ctx *);   /* ($FA880) */
void Text_CaptionOneTeam(lift_ctx *);        /* forward ($F9AE4) */
void Text_CaptionTwoTeams(lift_ctx *);       /* forward ($F9B2A) */

/* Shared prologue of sub_F9AE4 / sub_F9B2A: when a0 is null, look the record
 * up through Sram_SyncScoreRecord with d0/d1 swapped and sign-extended. */
static void Caption_ResolveRecordPtr(lift_ctx *x, unsigned int movem_a,
                                     unsigned int push_a, unsigned int mv_a,
                                     unsigned int pop_a, unsigned int ext1_a,
                                     unsigned int ext0_a, unsigned int lea_a,
                                     unsigned int bsr_a, unsigned int lea2_a,
                                     unsigned int rest_a)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 56;
  uint32_t saved[14];
  uint32_t sp;
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 6; i++) saved[8 + i] = c->a[1 + i];
  for (i = 0; i < 14; i++)                             /* movem.l d0-d7/a1-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, movem_a);

  sp = c->a[7] - 2;                                    /* move.w d0,-(sp) */
  lift_w16(x, sp, W(c->d[0]));
  alu_movew(c, W(c->d[0]));
  c->a[7] = sp;
  lift_charge(x, push_a);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));            /* move.w d1,d0 */
  lift_charge(x, mv_a);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[7])));  /* move.w (sp)+,d1 */
  c->a[7] += 2;
  lift_charge(x, pop_a);
  c->d[1] = alu_extl(c, c->d[1]);                      /* ext.l d1 */
  lift_charge(x, ext1_a);
  c->d[0] = alu_extl(c, c->d[0]);                      /* ext.l d0 */
  lift_charge(x, ext0_a);
  c->a[0] = 0xFFFF0000u;                               /* movea.l #$FFFF0000,a0 */
  lift_charge(x, lea_a);
  lift_call(x, bsr_a, 4, Sram_SyncScoreRecord);        /* bsr.w sub_F9B94 */
  if (x->declined) return;
  c->a[0] = 0xFFFF0000u;                               /* movea.l #$FFFF0000,a0 */
  lift_charge(x, lea2_a);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d7/a1-a6 */
  for (i = 0; i < 6; i++) c->a[1 + i] = saved[8 + i];
  c->a[7] = frame + 56;
  lift_charge_movem(x, rest_a);
}

/*
 * Text_CaptionOneTeam (sub_F9AE4)
 * Emits the name of the team in byte 1 of the record at a0 (resolving a null
 * a0 through the score record first).
 */
void Text_CaptionOneTeam(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 40;
  uint32_t saved[10];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  saved[8] = c->a[1]; saved[9] = c->a[2];
  for (i = 0; i < 10; i++)                             /* movem.l d0-d7/a1-a2,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xF9AE4);

  alu_cmpl(c, 0, c->a[0]);                             /* cmpa.l #0,a0 */
  lift_charge(x, 0xF9AE8);
  lift_charge_bcc(x, 0xF9AEE, !c->zf);                 /* bne.w loc_F9B14 */
  if (c->zf)
  {
    Caption_ResolveRecordPtr(x, 0xF9AF2, 0xF9AF6, 0xF9AF8, 0xF9AFA, 0xF9AFC,
                             0xF9AFE, 0xF9B00, 0xF9B06, 0xF9B0A, 0xF9B10);
    if (x->declined) return;
  }

  /* loc_F9B14 */
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + 1))); /* move.b 1(a0),d2 */
  lift_charge(x, 0xF9B14);
  setw(&c->d[2], alu_extw(c, c->d[2]));                /* ext.w d2 */
  lift_charge(x, 0xF9B18);
  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 7)); /* bset #7,(abs).w */
  lift_charge(x, 0xF9B1A);
  lift_call(x, 0xF9B20, 4, Text_EmitTeamName);         /* bsr.w sub_FA014 */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d7/a1-a2 */
  c->a[1] = saved[8]; c->a[2] = saved[9];
  c->a[7] = frame + 40;
  lift_charge_movem(x, 0xF9B24);

  lift_charge(x, 0xF9B28);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_CaptionTwoTeams (sub_F9B2A)
 * Emits "<team 3(a0)>" + the word_F9B90 separator + the indexed string for
 * byte 2 of the record.
 */
void Text_CaptionTwoTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 60;
  uint32_t saved[15];
  uint32_t sp;
  uint32_t inner_d0, inner_a1;
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  for (i = 0; i < 15; i++)                             /* movem.l d0-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xF9B2A);

  alu_cmpl(c, 0, c->a[0]);                             /* cmpa.l #0,a0 */
  lift_charge(x, 0xF9B2E);
  lift_charge_bcc(x, 0xF9B34, !c->zf);                 /* bne.w loc_F9B5A */
  if (c->zf)
  {
    Caption_ResolveRecordPtr(x, 0xF9B38, 0xF9B3C, 0xF9B3E, 0xF9B40, 0xF9B42,
                             0xF9B44, 0xF9B46, 0xF9B4C, 0xF9B50, 0xF9B56);
    if (x->declined) return;
  }

  /* loc_F9B5A */
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + 3))); /* move.b 3(a0),d2 */
  lift_charge(x, 0xF9B5A);
  setw(&c->d[2], alu_extw(c, c->d[2]));                /* ext.w d2 */
  lift_charge(x, 0xF9B5E);

  inner_d0 = c->d[0]; inner_a1 = c->a[1];
  sp = c->a[7] - 8;                                    /* movem.l d0/a1,-(sp) */
  lift_w16(x, sp + 0, (inner_d0 >> 16) & 0xFFFF); lift_w16(x, sp + 2, inner_d0 & 0xFFFF);
  lift_w16(x, sp + 4, (inner_a1 >> 16) & 0xFFFF); lift_w16(x, sp + 6, inner_a1 & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, 0xF9B60);

  lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8u), 7)); /* bset #7,(abs).w */
  lift_charge(x, 0xF9B64);
  lift_call(x, 0xF9B6A, 4, Text_EmitTeamName);         /* bsr.w sub_FA014 */
  if (x->declined) return;
  c->a[3] = c->a[1];                                   /* movea.l a1,a3 */
  lift_charge(x, 0xF9B6E);
  c->a[1] = 0xF9B90u;                                  /* movea.l #word_F9B90,a1 */
  lift_charge(x, 0xF9B70);
  lift_call(x, 0xF9B76, 6, Text_AppendString);         /* jsr sub_11D9E */
  if (x->declined) return;

  c->d[0] = inner_d0;                                  /* movem.l (sp)+,d0/a1 */
  c->a[1] = inner_a1;
  c->a[7] = sp + 8;
  lift_charge_movem(x, 0xF9B7C);

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 2))); /* move.b 2(a0),d0 */
  lift_charge(x, 0xF9B80);
  lift_call(x, 0xF9B84, 6, Text_AppendIndexedString);  /* jsr sub_FA880 */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = frame + 60;
  lift_charge_movem(x, 0xF9B8A);

  lift_charge(x, 0xF9B8E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Shared body of sub_F9A64 / sub_F9AAC: copy a literal prefix, build the
 * caption into $FFFFBF20, then append it. */
static void Caption_Build(lift_ctx *x, unsigned int movem_a, unsigned int pusha1_a,
                          unsigned int mva3_a, unsigned int lit_a, uint32_t lit,
                          unsigned int pushd_a, unsigned int copy_a,
                          unsigned int popd_a, unsigned int buf_a,
                          unsigned int call_a, void (*inner)(lift_ctx *),
                          unsigned int popa3_a)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 56;
  uint32_t saved[14];
  uint32_t sp, saved_d0, saved_d1;
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 6; i++) saved[8 + i] = c->a[1 + i];
  for (i = 0; i < 14; i++)                             /* movem.l d0-d7/a1-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, movem_a);

  sp = c->a[7] - 4;                                    /* move.l a1,-(sp) */
  lift_w16(x, sp + 0, (c->a[1] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[1] & 0xFFFF);
  alu_movel(c, c->a[1]);
  c->a[7] = sp;
  lift_charge(x, pusha1_a);
  c->a[3] = c->a[1];                                   /* movea.l a1,a3 */
  lift_charge(x, mva3_a);
  c->a[1] = lit;                                       /* movea.l #literal,a1 */
  lift_charge(x, lit_a);

  saved_d0 = c->d[0]; saved_d1 = c->d[1];
  sp = c->a[7] - 8;                                    /* movem.l d0-d1,-(sp) */
  lift_w16(x, sp + 0, (saved_d0 >> 16) & 0xFFFF); lift_w16(x, sp + 2, saved_d0 & 0xFFFF);
  lift_w16(x, sp + 4, (saved_d1 >> 16) & 0xFFFF); lift_w16(x, sp + 6, saved_d1 & 0xFFFF);
  c->a[7] = sp;
  lift_charge_movem(x, pushd_a);

  lift_call(x, copy_a, 4, Buf_CopyBytes);              /* bsr.w sub_F997A */
  if (x->declined) return;

  c->d[0] = saved_d0; c->d[1] = saved_d1;              /* movem.l (sp)+,d0-d1 */
  c->a[7] = sp + 8;
  lift_charge_movem(x, popd_a);

  c->a[1] = 0xFFFFBF20u;                               /* movea.l #$FFFFBF20,a1 */
  lift_charge(x, buf_a);
  lift_call(x, call_a, 4, inner);                      /* bsr.w inner */
  if (x->declined) return;

  c->a[3] = lift_r32(x, c->a[7]);                      /* movea.l (sp)+,a3 */
  c->a[7] += 4;
  lift_charge(x, popa3_a);
}

/*
 * Text_CaptionByTeam (sub_F9A64) — "by <team>", with the $FFFFBF20 caption
 * appended unless it came back as the bare length-2 empty string.
 */
void Text_CaptionByTeam(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 56;
  uint32_t saved[14];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 6; i++) saved[8 + i] = c->a[1 + i];

  Caption_Build(x, 0xF9A64, 0xF9A68, 0xF9A6A, 0xF9A6C, 0xF9AA6u, 0xF9A72,
                0xF9A76, 0xF9A7A, 0xF9A7E, 0xF9A84, Text_CaptionOneTeam, 0xF9A88);
  if (x->declined) return;

  alu_cmpw(c, 2, lift_r16(x, c->a[1]));                /* cmpi.w #2,(a1) */
  lift_charge(x, 0xF9A8A);
  lift_charge_bcc(x, 0xF9A8E, !c->zf);                 /* bne.w loc_F9A9A */
  if (c->zf)
  {
    lift_w16(x, c->a[3], 2); alu_movew(c, 2);          /* move.w #2,(a3) */
    lift_charge(x, 0xF9A92);
    lift_charge_bcc(x, 0xF9A96, 1);                    /* bra.w loc_F9AA0 */
  }
  else
  {
    lift_call(x, 0xF9A9A, 6, Text_AppendString);       /* jsr sub_11D9E */
    if (x->declined) return;
  }

  /* loc_F9AA0 */
  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d7/a1-a6 */
  for (i = 0; i < 6; i++) c->a[1 + i] = saved[8 + i];
  c->a[7] = frame + 56;
  lift_charge_movem(x, 0xF9AA0);

  lift_charge(x, 0xF9AA4);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* Text_CaptionVsTeams (sub_F9AAC) — "vs. <team> ... <team>". */
void Text_CaptionVsTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 56;
  uint32_t saved[14];
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 6; i++) saved[8 + i] = c->a[1 + i];

  Caption_Build(x, 0xF9AAC, 0xF9AB0, 0xF9AB2, 0xF9AB4, 0xF9ADEu, 0xF9ABA,
                0xF9ABE, 0xF9AC2, 0xF9AC6, 0xF9ACC, Text_CaptionTwoTeams, 0xF9AD0);
  if (x->declined) return;

  lift_call(x, 0xF9AD2, 6, Text_AppendString);         /* jsr sub_11D9E */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-d7/a1-a6 */
  for (i = 0; i < 6; i++) c->a[1 + i] = saved[8 + i];
  c->a[7] = frame + 56;
  lift_charge_movem(x, 0xF9AD8);

  lift_charge(x, 0xF9ADC);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Standings_SortDivision (sub_FC282)
 * Loads the standings block from SRAM, seeds the 7-entry order list at
 * $FFFFD532 (or $FFFFD53A when $FFFFC2F8 bit 6 is set) with 1..7, then
 * bubble-sorts it by each team's record byte — offset +0 normally, +4 under
 * that same bit — repeating until a pass makes no swap.
 */
void Standings_SortDivision(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t frame = c->a[7] - 60;
  uint32_t saved[15];
  uint32_t list_sp;
  int i;

  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  for (i = 0; i < 15; i++)                             /* movem.l d0-a6,-(sp) */
  {
    lift_w16(x, frame + i * 4 + 0, (saved[i] >> 16) & 0xFFFF);
    lift_w16(x, frame + i * 4 + 2, saved[i] & 0xFFFF);
  }
  c->a[7] = frame;
  lift_charge_movem(x, 0xFC282);

  c->d[0] = alu_movel(c, 0xD20);                       /* move.l #$D20,d0 */
  lift_charge(x, 0xFC286);
  c->d[1] = alu_movel(c, 0x80);                        /* move.l #$80,d1 */
  lift_charge(x, 0xFC28C);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFC292);
  lift_call(x, 0xFC298, 6, SRAM_ReadBytes);            /* jsr SRAM_ReadBytes */
  if (x->declined) return;

  c->a[1] = 0xFFFFD532u;                               /* movea.l #$FFFFD532,a1 */
  lift_charge(x, 0xFC29E);
  alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);             /* btst #6,($FFFFC2F8).w */
  lift_charge(x, 0xFC2A4);
  lift_charge_bcc(x, 0xFC2AA, c->zf);                  /* beq.w loc_FC2B4 */
  if (!c->zf)
  {
    c->a[1] = 0xFFFFD53Au;                             /* movea.l #$FFFFD53A,a1 */
    lift_charge(x, 0xFC2AE);
  }

  /* loc_FC2B4 */
  list_sp = c->a[7] - 4;                               /* move.l a1,-(sp) */
  lift_w16(x, list_sp + 0, (c->a[1] >> 16) & 0xFFFF);
  lift_w16(x, list_sp + 2, c->a[1] & 0xFFFF);
  alu_movel(c, c->a[1]);
  c->a[7] = list_sp;
  lift_charge(x, 0xFC2B4);
  setw(&c->d[0], alu_movew(c, 1));                     /* move.w #1,d0 */
  lift_charge(x, 0xFC2B6);
  setw(&c->d[7], alu_movew(c, 6));                     /* move.w #6,d7 */
  lift_charge(x, 0xFC2BA);

  for (;;)
  {
    lift_w8(x, c->a[1], c->d[0] & 0xFF);               /* move.b d0,(a1)+ */
    alu_moveb(c, c->d[0] & 0xFF);
    c->a[1] += 1;
    lift_charge(x, 0xFC2BE);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0xFC2C0);
    setw(&c->d[7], W(c->d[7]) - 1);                    /* dbf d7: no CCR */
    if (W(c->d[7]) != 0xFFFF) { lift_charge_dbcc(x, 0xFC2C2, 1, 0); continue; }
    lift_charge_dbcc(x, 0xFC2C2, 0, 1);
    break;
  }

  c->a[1] = lift_r32(x, c->a[7]);                      /* movea.l (sp),a1 */
  lift_charge(x, 0xFC2C6);
  c->a[0] = 0xFFFFCF36u;                               /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFC2C8);

  for (;;)
  {
    /* loc_FC2CE */
    c->a[1] = lift_r32(x, c->a[7]);                    /* movea.l (sp),a1 */
    lift_charge(x, 0xFC2CE);
    setw(&c->d[7], alu_movew(c, 5));                   /* move.w #5,d7 */
    lift_charge(x, 0xFC2D0);
    setw(&c->d[6], alu_movew(c, 0));                   /* clr.w d6 */
    lift_charge(x, 0xFC2D4);

    for (;;)
    {
      /* loc_FC2D6 */
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1]))); /* move.b (a1)+,d1 */
      c->a[1] += 1;
      lift_charge(x, 0xFC2D6);
      setw(&c->d[1], alu_extw(c, c->d[1]));            /* ext.w d1 */
      lift_charge(x, 0xFC2D8);
      setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[1]))); /* move.b (a1),d2 */
      lift_charge(x, 0xFC2DA);
      setw(&c->d[2], alu_extw(c, c->d[2]));            /* ext.w d2 */
      lift_charge(x, 0xFC2DC);
      setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));      /* asl.w #4,d1 */
      lift_charge(x, 0xFC2DE);
      setw(&c->d[2], alu_aslw(c, W(c->d[2]), 4));      /* asl.w #4,d2 */
      lift_charge(x, 0xFC2E0);
      setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1])))); /* move.b (a0,d1.w),d0 */
      lift_charge(x, 0xFC2E2);
      setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2])))); /* move.b (a0,d2.w),d3 */
      lift_charge(x, 0xFC2E6);
      alu_btst(c, lift_r8(x, 0xFFFFC2F8u), 6);         /* btst #6,($FFFFC2F8).w */
      lift_charge(x, 0xFC2EA);
      lift_charge_bcc(x, 0xFC2F0, c->zf);              /* beq.w loc_FC2FC */
      if (!c->zf)
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1]) + 4))); /* move.b 4(a0,d1.w),d0 */
        lift_charge(x, 0xFC2F4);
        setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2]) + 4))); /* move.b 4(a0,d2.w),d3 */
        lift_charge(x, 0xFC2F8);
      }

      /* loc_FC2FC */
      alu_cmpb(c, c->d[3] & 0xFF, c->d[0] & 0xFF);     /* cmp.b d3,d0 */
      lift_charge(x, 0xFC2FC);
      {
        int ge = (!!c->nf) == (!!c->vf);
        lift_charge_bcc(x, 0xFC2FE, ge);               /* bge.w loc_FC310 */
        if (!ge)
        {
          setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1]))); /* move.b (a1),d0 */
          lift_charge(x, 0xFC302);
          setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1] - 1))); /* move.b -1(a1),d1 */
          lift_charge(x, 0xFC304);
          lift_w8(x, c->a[1] - 1, c->d[0] & 0xFF);     /* move.b d0,-1(a1) */
          alu_moveb(c, c->d[0] & 0xFF);
          lift_charge(x, 0xFC308);
          lift_w8(x, c->a[1], c->d[1] & 0xFF);         /* move.b d1,(a1) */
          alu_moveb(c, c->d[1] & 0xFF);
          lift_charge(x, 0xFC30C);
          setb(&c->d[6], 0xFF);                        /* st d6: no CCR */
          lift_charge(x, 0xFC30E);
        }
      }

      /* loc_FC310 */
      setw(&c->d[7], W(c->d[7]) - 1);                  /* dbf d7: no CCR */
      if (W(c->d[7]) != 0xFFFF) { lift_charge_dbcc(x, 0xFC310, 1, 0); continue; }
      lift_charge_dbcc(x, 0xFC310, 0, 1);
      break;
    }

    alu_tstw(c, W(c->d[6]));                           /* tst.w d6 */
    lift_charge(x, 0xFC314);
    lift_charge_bcc(x, 0xFC316, !c->zf);               /* bne.s loc_FC2CE */
    if (!c->zf) continue;
    break;
  }

  c->a[1] = lift_r32(x, c->a[7]);                      /* movea.l (sp)+,a1 */
  c->a[7] += 4;
  lift_charge(x, 0xFC318);

  for (i = 0; i < 8; i++) c->d[i] = saved[i];          /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = saved[8 + i];
  c->a[7] = frame + 60;
  lift_charge_movem(x, 0xFC31A);

  lift_charge(x, 0xFC31E);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_FormatClock (sub_11CA2)
 * Formats d0 seconds as " m:ss" backwards into the buffer ending at
 * $FFFFBFC2 (minutes tens suppressed when zero), pads to an even length and
 * writes the resulting length word ahead of the text. d0 is restored.
 */
void Text_FormatClock(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;

  c->a[1] = SEW(0xBFC2);                               /* movea.w #$BFC2,a1 */
  lift_charge(x, 0x11CA2);

  sp = c->a[7] - 4;                                    /* move.l d0,-(sp) */
  lift_w16(x, sp + 0, (c->d[0] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->d[0] & 0xFFFF);
  alu_movel(c, c->d[0]);
  c->a[7] = sp;
  lift_charge(x, 0x11CA6);
  sp = c->a[7] - 4;                                    /* move.l a1,-(sp) */
  lift_w16(x, sp + 0, (c->a[1] >> 16) & 0xFFFF); lift_w16(x, sp + 2, c->a[1] & 0xFFFF);
  alu_movel(c, c->a[1]);
  c->a[7] = sp;
  lift_charge(x, 0x11CA8);

  c->d[0] = alu_extl(c, c->d[0]);                      /* ext.l d0 */
  lift_charge(x, 0x11CAA);
  lift_charge_divu(x, 0x11CAC, 0xA, c->d[0]);          /* divu.w #$A,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0xA, c->d[0]);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CB0);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));       /* addi.w #$30,d0 */
  lift_charge(x, 0x11CB2);
  c->a[1] -= 1;                                        /* move.b d0,-(a1) */
  lift_w8(x, c->a[1], c->d[0] & 0xFF);
  alu_moveb(c, c->d[0] & 0xFF);
  lift_charge(x, 0x11CB6);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CB8);

  c->d[0] = alu_extl(c, c->d[0]);                      /* ext.l d0 */
  lift_charge(x, 0x11CBA);
  lift_charge_divu(x, 0x11CBC, 6, c->d[0]);            /* divu.w #6,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 6, c->d[0]);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CC0);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));       /* addi.w #$30,d0 */
  lift_charge(x, 0x11CC2);
  c->a[1] -= 1;                                        /* move.b d0,-(a1) */
  lift_w8(x, c->a[1], c->d[0] & 0xFF);
  alu_moveb(c, c->d[0] & 0xFF);
  lift_charge(x, 0x11CC6);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CC8);

  c->a[1] -= 1;                                        /* move.b #$3A,-(a1) */
  lift_w8(x, c->a[1], 0x3A);
  alu_moveb(c, 0x3A);
  lift_charge(x, 0x11CCA);

  c->d[0] = alu_extl(c, c->d[0]);                      /* ext.l d0 */
  lift_charge(x, 0x11CCE);
  lift_charge_divu(x, 0x11CD0, 0xA, c->d[0]);          /* divu.w #$A,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0xA, c->d[0]);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CD4);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));       /* addi.w #$30,d0 */
  lift_charge(x, 0x11CD6);
  c->a[1] -= 1;                                        /* move.b d0,-(a1) */
  lift_w8(x, c->a[1], c->d[0] & 0xFF);
  alu_moveb(c, c->d[0] & 0xFF);
  lift_charge(x, 0x11CDA);
  c->d[0] = alu_swap(c, c->d[0]);                      /* swap d0 */
  lift_charge(x, 0x11CDC);

  c->a[1] -= 1;                                        /* move.b #$20,-(a1) */
  lift_w8(x, c->a[1], 0x20);
  alu_moveb(c, 0x20);
  lift_charge(x, 0x11CDE);

  alu_tstw(c, W(c->d[0]));                             /* tst.w d0 */
  lift_charge(x, 0x11CE2);
  lift_charge_bcc(x, 0x11CE4, c->zf);                  /* beq.w loc_11CEE */
  if (!c->zf)
  {
    setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));     /* addi.w #$30,d0 */
    lift_charge(x, 0x11CE8);
    lift_w8(x, c->a[1], c->d[0] & 0xFF);               /* move.b d0,(a1) */
    alu_moveb(c, c->d[0] & 0xFF);
    lift_charge(x, 0x11CEC);
  }

  /* loc_11CEE — pops the saved a1, so d0 becomes the buffer end pointer */
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));        /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x11CEE);
  c->d[0] = alu_subl(c, c->a[1], c->d[0]);             /* sub.l a1,d0 */
  lift_charge(x, 0x11CF0);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));          /* addq.w #2,d0 */
  lift_charge(x, 0x11CF2);
  alu_btst(c, c->d[0], 0);                             /* btst #0,d0 */
  lift_charge(x, 0x11CF4);
  lift_charge_bcc(x, 0x11CF8, c->zf);                  /* beq.w loc_11D00 */
  if (!c->zf)
  {
    c->a[1] -= 1;                                      /* clr.b -(a1) */
    lift_w8(x, c->a[1], 0);
    alu_moveb(c, 0);
    lift_charge(x, 0x11CFC);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));        /* addq.w #1,d0 */
    lift_charge(x, 0x11CFE);
  }

  /* loc_11D00 */
  c->a[1] -= 2;                                        /* move.w d0,-(a1) */
  lift_w16(x, c->a[1], W(c->d[0]));
  alu_movew(c, W(c->d[0]));
  lift_charge(x, 0x11D00);

  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));        /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x11D02);

  lift_charge(x, 0x11D04);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Roster_CountLeadingNibbles(lift_ctx *);  /* ($9F40) */

/*
 * Draft_ScorePlayers (sub_1867E)
 * Walks the 26 players of team a2 writing a long "value" per player into the
 * (a4) array: the goal-difference base, plus weighted goal/assist/games
 * counts for the players inside the nibble-count cutoff, or the two flat
 * bonuses for a low games-per-appearance ratio outside it.
 */
void Draft_ScorePlayers(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = SEW(c->a[2]);                              /* movea.w a2,a1: sign-extends */
  lift_charge(x, 0x1867E);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[2] + 0xC))); /* move.w $C(a2),d3 */
  lift_charge(x, 0x18680);
  setw(&c->d[3], alu_subw(c, lift_r16(x, c->a[3] + 0xC), W(c->d[3]))); /* sub.w $C(a3),d3 */
  lift_charge(x, 0x18684);
  c->d[3] = alu_extl(c, c->d[3]);                      /* ext.l d3 */
  lift_charge(x, 0x18688);
  c->d[4] = alu_moveql(c, 0x19);                       /* moveq #$19,d4 */
  lift_charge(x, 0x1868A);
  lift_call(x, 0x1868C, 6, Roster_CountLeadingNibbles); /* jsr sub_9F40 */
  if (x->declined) return;
  setw(&c->d[0], alu_negw(c, W(c->d[0])));             /* neg.w d0 */
  lift_charge(x, 0x18692);
  setw(&c->d[0], alu_addw(c, W(c->d[4]), W(c->d[0]))); /* add.w d4,d0 */
  lift_charge(x, 0x18694);

  for (;;)
  {
    /* loc_18696 */
    lift_w32(x, c->a[4], c->d[3]);                     /* move.l d3,(a4) */
    alu_movel(c, c->d[3]);
    lift_charge(x, 0x18696);
    alu_cmpw(c, W(c->d[0]), W(c->d[4]));               /* cmp.w d0,d4 */
    lift_charge(x, 0x18698);
    {
      int hi = !c->cf && !c->zf;
      lift_charge_bcc(x, 0x1869A, hi);                 /* bhi.w loc_186D8 */
      if (!hi)
      {
        setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
        lift_charge(x, 0x1869E);
        setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[2] + 0xB4))); /* move.b $B4(a2),d1 */
        lift_charge(x, 0x186A0);
        lift_charge_mulu(x, 0x186A4, 0x2AF8);          /* mulu.w #$2AF8,d1 */
        c->d[1] = alu_mulu(c, 0x2AF8, c->d[1]);
        lift_w32(x, c->a[4], alu_addl(c, c->d[1], lift_r32(x, c->a[4]))); /* add.l d1,(a4) */
        lift_charge(x, 0x186A8);

        setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
        lift_charge(x, 0x186AA);
        setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[2] + 0xCE))); /* move.b $CE(a2),d1 */
        lift_charge(x, 0x186AC);
        lift_charge_mulu(x, 0x186B0, 0x2774);          /* mulu.w #$2774,d1 */
        c->d[1] = alu_mulu(c, 0x2774, c->d[1]);
        lift_w32(x, c->a[4], alu_addl(c, c->d[1], lift_r32(x, c->a[4]))); /* add.l d1,(a4) */
        lift_charge(x, 0x186B4);

        setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
        lift_charge(x, 0x186B6);
        setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[2] + 0xE8))); /* move.b $E8(a2),d1 */
        lift_charge(x, 0x186B8);
        lift_charge_mulu(x, 0x186BC, 0xA);             /* mulu.w #$A,d1 */
        c->d[1] = alu_mulu(c, 0xA, c->d[1]);
        alu_tstw(c, W(c->d[3]));                       /* tst.w d3 */
        lift_charge(x, 0x186C0);
        lift_charge_bcc(x, 0x186C2, !c->zf);           /* bne.w loc_186D2 */
        if (c->zf)
        {
          lift_charge_mulu(x, 0x186C6, 0x64);          /* mulu.w #$64,d1 */
          c->d[1] = alu_mulu(c, 0x64, c->d[1]);
          lift_w32(x, c->a[4], alu_addl(c, c->d[1], lift_r32(x, c->a[4]))); /* add.l d1,(a4) */
          lift_charge(x, 0x186CA);
          c->d[1] = alu_movel(c, 0);                   /* clr.l d1 */
          lift_charge(x, 0x186CC);
          setw(&c->d[1], alu_addw(c, lift_r16(x, c->a[1] + 0x136), W(c->d[1]))); /* add.w $136(a1),d1 */
          lift_charge(x, 0x186CE);
        }

        /* loc_186D2 */
        lift_w32(x, c->a[4], alu_addl(c, c->d[1], lift_r32(x, c->a[4]))); /* add.l d1,(a4) */
        lift_charge(x, 0x186D2);
        lift_charge_bcc(x, 0x186D4, 1);                /* bra.w loc_18710 */
        goto loc_18710;
      }
    }

    /* loc_186D8 */
    alu_cmpw(c, lift_r16(x, c->a[1] + 0x136), W(c->d[5])); /* cmp.w $136(a1),d5 */
    lift_charge(x, 0x186D8);
    {
      int hi = !c->cf && !c->zf;
      lift_charge_bcc(x, 0x186DC, hi);                 /* bhi.w loc_18710 */
      if (hi) goto loc_18710;
    }
    setw(&c->d[1], alu_movew(c, 0));                   /* clr.w d1 */
    lift_charge(x, 0x186E0);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[2] + 0xB4))); /* move.b $B4(a2),d1 */
    lift_charge(x, 0x186E2);
    lift_charge_mulu(x, 0x186E6, 0x64);                /* mulu.w #$64,d1 */
    c->d[1] = alu_mulu(c, 0x64, c->d[1]);
    setw(&c->d[2], alu_movew(c, 0));                   /* clr.w d2 */
    lift_charge(x, 0x186EA);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[2] + 0xE8))); /* move.b $E8(a2),d2 */
    lift_charge(x, 0x186EC);
    lift_charge_bcc(x, 0x186F0, c->zf);                /* beq.w loc_18710 */
    if (c->zf) goto loc_18710;

    lift_charge_divu(x, 0x186F4, W(c->d[2]), c->d[1]); /* divu.w d2,d1 */
    if (x->declined) return;
    c->d[1] = alu_divu(c, W(c->d[2]), c->d[1]);
    alu_cmpw(c, 4, W(c->d[1]));                        /* cmpi.w #4,d1 */
    lift_charge(x, 0x186F6);
    {
      int hi = !c->cf && !c->zf;
      lift_charge_bcc(x, 0x186FA, hi);                 /* bhi.w loc_18710 */
      if (hi) goto loc_18710;
    }
    lift_w32(x, c->a[4], alu_addl(c, 0x7D00, lift_r32(x, c->a[4]))); /* addi.l #$7D00,(a4) */
    lift_charge(x, 0x186FE);
    alu_tstw(c, W(c->d[1]));                           /* tst.w d1 */
    lift_charge(x, 0x18704);
    lift_charge_bcc(x, 0x18706, !c->zf);               /* bne.w loc_18710 */
    if (c->zf)
    {
      lift_w32(x, c->a[4], alu_addl(c, 0x124F8, lift_r32(x, c->a[4]))); /* addi.l #$124F8,(a4) */
      lift_charge(x, 0x1870A);
    }

  loc_18710:
    c->a[1] = (c->a[1] + 2) & 0xFFFFFFFFu;             /* addq.w #2,a1: no CCR */
    lift_charge(x, 0x18710);
    c->a[2] = (c->a[2] + 1) & 0xFFFFFFFFu;             /* addq.w #1,a2 */
    lift_charge(x, 0x18712);
    c->a[4] = (c->a[4] + 4) & 0xFFFFFFFFu;             /* addq.w #4,a4 */
    lift_charge(x, 0x18714);

    setw(&c->d[4], W(c->d[4]) - 1);                    /* dbf d4: no CCR */
    if (W(c->d[4]) != 0xFFFF) { lift_charge_dbcc(x, 0x18716, 1, 0); continue; }
    lift_charge_dbcc(x, 0x18716, 0, 1);
    break;
  }

  lift_charge(x, 0x1871A);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_C868 (called from sub_C710+12E; a3 = on-ice object slot)
 *   in:  a3 = object base
 *   out: d0 = sign-extended $60(a3); a0/d1.. per Object_QueueFrameFromTable
 * Latches the pending animation selector byte at +$60 into the word-sized
 * request field +$34, queues the matching frame through
 * Object_QueueFrameFromTable, then marks both +$60 and +$61 as consumed
 * ($FF) so the selector is not re-applied on the next tick.
 */
void sub_C868(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3] + 0x60)));   /* move.b $60(a3),d0 */
  lift_charge(x, 0xC868);
  setw(&c->d[0], alu_extw(c, c->d[0]));                        /* ext.w d0 */
  lift_charge(x, 0xC86C);
  lift_w16(x, c->a[3] + 0x34, alu_movew(c, W(c->d[0])));       /* move.w d0,$34(a3) */
  lift_charge(x, 0xC86E);

  lift_call(x, 0xC872, 6, Object_QueueFrameFromTable);         /* jsr sub_15A88 */
  if (x->declined) return;

  lift_w8(x, c->a[3] + 0x61, 0xFF);                            /* st $61(a3) */
  lift_charge(x, 0xC878);
  lift_w8(x, c->a[3] + 0x60, 0xFF);                            /* st $60(a3) */
  lift_charge(x, 0xC87C);

  lift_charge(x, 0xC880);                                      /* locret_C880: rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_13378 (called from sub_12002+D8)
 *   out: d3 = -1 on the early bail, else whatever Fn_13384 leaves
 * Seeds the slot cursor d3 with -1 and bails to the shared far rts when
 * the scan bound ($FFFFC468).w has already passed $1E0; otherwise falls
 * straight through into sub_13384's decrement loop (composed here as a
 * direct call to Fn_13384 — its rts returns to this routine's caller).
 */
void sub_13378(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[3] = alu_moveql(c, -1);                     /* moveq #-1,d3 */
  lift_charge(x, 0x13378);

  alu_cmpw(c, 0x1E0, lift_r16(x, 0xFFFFC468u));    /* cmp.w #$1E0,($FFFFC468).w */
  lift_charge(x, 0x1337A);

  int gt = !c->zf && (c->nf == c->vf);
  lift_charge_bcc(x, 0x13380, gt);                 /* bgt.w locret_15464 */
  if (gt)
  {
    lift_charge(x, 0x15464);                       /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  Fn_13384(x);                                     /* falls through into sub_13384 */
}

/*
 * sub_18B6E (called from ROM:8660, ROM:FA9B0 and others; d0-d3/a0/a2
 * saved/restored via movem)
 *   in: a2 = struct pointer, d0 = hop count-1 (both consumed by
 *       Piece_AdvanceChain)
 * Sibling of sub_18AE8: formats the SURNAME half of a length-prefixed
 * name record into the $FFFFBFA6 text buffer. Piece_AdvanceChain
 * resolves the record base into a0; the leading space is emitted, the
 * length word gives the end pointer (lea -2(a0,d0.w),a2), a scan skips
 * everything up to and including the first space, and the remainder is
 * copied until either the end pointer is reached or a NUL is hit. The
 * buffer is then space-padded out to $FFFFBFB2 and Text_AlignBufferEven
 * pads it to an even length. Both scans are bounded by the record's own
 * length prefix / its guaranteed space delimiter, not hardware polls,
 * but each is guarded here against a malformed record.
 */
static void text_build_surname_tail(lift_ctx *x);

void sub_18B6E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[6] = {
    c->a[2], c->a[0], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 6; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x18B6E);

  lift_call(x, 0x18B72, 4, Piece_AdvanceChain);         /* bsr.w sub_18BC8 */
  if (x->declined) return;

  c->a[1] = 0xFFFFBFA6u;                                /* move.w #$BFA6,a1 */
  lift_charge(x, 0x18B76);
  lift_w8(x, c->a[1], alu_moveb(c, 0x20));              /* move.b #$20,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x18B7A);

  text_build_surname_tail(x);                           /* falls through into loc_18B7E */
}

/*
 * text_build_surname_tail (loc_18B7E) — the scan-and-copy half shared by
 * sub_18B6E (falls through, after emitting the leading space) and
 * Text_BuildSurnameNoPad (`bra.w` from $18B6A, which emits no space).
 * Both entries have already pushed the same 6-register movem frame that
 * this tail's epilogue pops.
 */
static void text_build_surname_tail(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));   /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0x18B7E);
  c->a[2] = c->a[0] + SEW(c->d[0]) - 2;                 /* lea -2(a0,d0.w),a2 */
  lift_charge(x, 0x18B80);

  for (int guard = 0; ; guard++)
  {
    /* loc_18B84 */
    if (guard > 4096) { x->declined = 1; return; }
    alu_cmpb(c, 0x20, lift_r8(x, c->a[0]));             /* cmp.b #$20,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x18B84);
    int more = !c->zf;                                   /* bne.s loc_18B84 */
    lift_charge_bcc(x, 0x18B88, more);
    if (!more) break;
  }

  for (int guard = 0; ; guard++)
  {
    /* loc_18B8A */
    if (guard > 4096) { x->declined = 1; return; }
    lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,(a1)+ */
    c->a[0] += 1;
    c->a[1] += 1;
    lift_charge(x, 0x18B8A);
    alu_cmpl(c, c->a[0], c->a[2]);                      /* cmp.l a0,a2 */
    lift_charge(x, 0x18B8C);
    int atEnd = c->zf;
    lift_charge_bcc(x, 0x18B8E, atEnd);                 /* beq.w loc_18B9E */
    if (atEnd) break;

    alu_tstb(c, lift_r8(x, c->a[0]));                   /* tst.b (a0) */
    lift_charge(x, 0x18B92);
    int more = !c->zf;
    lift_charge_bcc(x, 0x18B94, more);                  /* bne.s loc_18B8A */
    if (!more)
    {
      lift_charge(x, 0x18B96);                          /* bra.w loc_18B9E */
      break;
    }
  }

  for (;;)
  {
    /* loc_18B9E */
    alu_cmpl(c, SEW(0xBFB2), c->a[1]);                  /* cmp.w #$BFB2,a1 */
    lift_charge(x, 0x18B9E);
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0x18BA2, lt);                    /* blt.s loc_18B9A */
    if (!lt) break;

    /* loc_18B9A */
    lift_w8(x, c->a[1], alu_moveb(c, 0x20));            /* move.b #$20,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0x18B9A);
  }

  lift_call(x, 0x18BA4, 4, Text_AlignBufferEven);       /* bsr.w sub_18BAE */
  if (x->declined) return;

  {
    uint32_t sp = c->a[7];                              /* movem.l (sp)+,d0-d3/a0/a2 */
    c->d[0] = lift_r32(x, sp +  0);
    c->d[1] = lift_r32(x, sp +  4);
    c->d[2] = lift_r32(x, sp +  8);
    c->d[3] = lift_r32(x, sp + 12);
    c->a[0] = lift_r32(x, sp + 16);
    c->a[2] = lift_r32(x, sp + 20);
    c->a[7] = sp + 6 * 4;
  }
  lift_charge_movem(x, 0x18BA8);

  lift_charge(x, 0x18BAC);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_A68C(lift_ctx *x);
void sub_A88C(lift_ctx *x);

/*
 * sub_A4F6 (called from sub_9FD0:loc_A02A and sub_9FD0+2BC; a4 = the
 * replay/state record being applied)
 * Normalizes a4 before handing off to sub_A68C, the on-ice-state
 * unpacker. If a4 is the sentinel $FFFF0000 it is either replaced with
 * the fixed record at $FFFFAF54 (when bit4 of $FFFFC2EC is set) or left
 * alone; otherwise a4 is probed $62 lower against the current record
 * pointer ($FFFFB036) — on a mismatch it tail-branches into sub_A68C
 * with the biased pointer, and on a match the bias is undone first.
 * The bsr'd path clears d7 before returning.
 */
void sub_A4F6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpl(c, 0xFFFF0000u, c->a[4]);               /* cmp.l #$FFFF0000,a4 */
  lift_charge(x, 0xA4F6);
  int ne = !c->zf;
  lift_charge_bcc(x, 0xA4FC, ne);                  /* bne.w loc_A510 */

  int skipBias = 0;
  if (!ne)
  {
    alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 4);       /* btst #4,($FFFFC2EC).w */
    lift_charge(x, 0xA500);
    int clear = c->zf;
    lift_charge_bcc(x, 0xA506, clear);             /* beq.w loc_A520 */
    if (clear) { skipBias = 1; }
    else
    {
      c->a[4] = 0xFFFFAF54u;                       /* move.l #$FFFFAF54,a4 */
      lift_charge(x, 0xA50A);
    }
  }

  if (!skipBias)
  {
    /* loc_A510 */
    c->a[4] -= 0x62;                               /* suba.w #$62,a4 */
    lift_charge(x, 0xA510);
    alu_cmpl(c, lift_r32(x, 0xFFFFB036u), c->a[4]);  /* cmp.l ($FFFFB036).w,a4 */
    lift_charge(x, 0xA514);
    int mismatch = !c->zf;
    lift_charge_bcc(x, 0xA518, mismatch);          /* bne.w sub_A68C */
    if (mismatch) { sub_A68C(x); return; }

    c->a[4] += 0x62;                               /* add.w #$62,a4 */
    lift_charge(x, 0xA51C);
  }

  /* loc_A520 */
  lift_call(x, 0xA520, 4, sub_A68C);               /* bsr.w sub_A68C */
  if (x->declined) return;

  setw(&c->d[7], alu_movew(c, 0));                 /* clr.w d7 */
  lift_charge(x, 0xA524);

  lift_charge(x, 0xA526);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_A67C (called from sub_A528+8E)
 * Three-instruction prologue to sub_A68C: when bit4 of $FFFFC2F4 (the
 * rink-flip / mirrored-view flag) is set it also sets bit7 of the same
 * byte, marking the unpack as a mirrored one. Both paths fall into
 * sub_A68C, whose rts returns to this routine's caller.
 */
void sub_A67C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);         /* btst #4,($FFFFC2F4).w */
  lift_charge(x, 0xA67C);
  int clear = c->zf;
  lift_charge_bcc(x, 0xA682, clear);               /* beq.w sub_A68C */
  if (!clear)
  {
    lift_w8(x, 0xFFFFC2F4u, alu_bset(c, lift_r8(x, 0xFFFFC2F4u), 7));  /* bset #7 */
    lift_charge(x, 0xA686);
  }

  sub_A68C(x);                                     /* falls through */
}

/*
 * sub_A68C (called from sub_9FD0+3F2, sub_A4F6, sub_A67C; a4 = packed
 * state record, a5 = tracked-camera object; d0-d2/a0/a6 saved via movem)
 * Unpacks a compressed on-ice snapshot at (a4) into the 16 object slots
 * from $FFFFB04A ($80 stride) plus the global game words. Per slot it
 * decodes a 10-bit signed X from bits of the record's second word, a
 * 10-bit signed Y from the long shifted right 10, and a frame index
 * looked up through the ROM table at $F600E; with the mirror flag
 * ($FFFFC2F4 bit4) set the coordinates are negated and several rink
 * landmark ranges are special-cased ($18A goal-mouth clamp, $28D-$291
 * and $162-$178 windows). It then walks 6 more slot pairs writing packed
 * nibble fields into +$6F/+$34, restores the global counters ($FFFFB762
 * /$B7E2/$C016/$BE78/$B89A/$BEE2/$C3EA/$BE86), and finally either
 * re-applies the tracked camera (Camera_ApplyTrackedTarget) or restores
 * and clamps the raw camera position (sub_A88C).
 */
void sub_A68C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[5] = { c->a[6], c->a[0], c->d[2], c->d[1], c->d[0] };
  for (int i = 0; i < 5; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xA68C);

  c->a[6] = 0x000F600Eu;                           /* move.l #unk_F600E,a6 */
  lift_charge(x, 0xA690);
  c->a[0] = c->a[4];                               /* move.l a4,a0 */
  lift_charge(x, 0xA696);
  c->d[1] = alu_moveql(c, 0xF);                    /* moveq #$F,d1 */
  lift_charge(x, 0xA698);
  c->a[3] = 0xFFFFB04Au;                           /* move.w #$B04A,a3 */
  lift_charge(x, 0xA69A);

  for (;;)
  {
    /* loc_A69E */
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0] + 2)));   /* move.w 2(a0),d2 */
    lift_charge(x, 0xA69E);
    setw(&c->d[2], alu_andw(c, 0x3FF, W(c->d[2])));           /* and.w #$3FF,d2 */
    lift_charge(x, 0xA6A2);
    alu_btst(c, c->d[2], 9);                                  /* btst #9,d2 */
    lift_charge(x, 0xA6A6);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA6AA, clear);                      /* beq.w loc_A6B2 */
      if (!clear)
      {
        setw(&c->d[2], alu_orw(c, 0xFC00, W(c->d[2])));       /* or.w #$FC00,d2 */
        lift_charge(x, 0xA6AE);
      }
    }

    /* loc_A6B2 */
    alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                  /* btst #4,($FFFFC2F4).w */
    lift_charge(x, 0xA6B2);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA6B8, clear);                      /* beq.w loc_A6BE */
      if (!clear)
      {
        setw(&c->d[2], alu_negw(c, W(c->d[2])));              /* neg.w d2 */
        lift_charge(x, 0xA6BC);
      }
    }

    /* loc_A6BE */
    lift_w16(x, c->a[3], alu_movew(c, W(c->d[2])));           /* move.w d2,(a3) */
    lift_charge(x, 0xA6BE);
    c->d[2] = alu_movel(c, lift_r32(x, c->a[0]));             /* move.l (a0),d2 */
    lift_charge(x, 0xA6C0);
    c->d[2] = alu_asrl(c, c->d[2], 4);                        /* asr.l #4,d2 */
    lift_charge(x, 0xA6C2);
    setw(&c->d[2], alu_asrw(c, W(c->d[2]), 6));               /* asr.w #6,d2 */
    lift_charge(x, 0xA6C4);
    alu_btst(c, c->d[2], 9);                                  /* btst #9,d2 */
    lift_charge(x, 0xA6C6);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA6CA, clear);                      /* beq.w loc_A6D2 */
      if (!clear)
      {
        setw(&c->d[2], alu_orw(c, 0xFC00, W(c->d[2])));       /* or.w #$FC00,d2 */
        lift_charge(x, 0xA6CE);
      }
    }

    /* loc_A6D2 */
    alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                  /* btst #4,($FFFFC2F4).w */
    lift_charge(x, 0xA6D2);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA6D8, clear);                      /* beq.w loc_A6E8 */
      if (!clear)
      {
        setw(&c->d[2], alu_negw(c, W(c->d[2])));              /* neg.w d2 */
        lift_charge(x, 0xA6DC);
        alu_cmpw(c, 0, W(c->d[1]));                           /* cmp.w #0,d1 */
        lift_charge(x, 0xA6DE);
        int nz = !c->zf;
        lift_charge_bcc(x, 0xA6E2, nz);                       /* bne.w loc_A6E8 */
        if (!nz)
        {
          setw(&c->d[2], alu_addw(c, 2, W(c->d[2])));         /* addq.w #2,d2 */
          lift_charge(x, 0xA6E6);
        }
      }
    }

    /* loc_A6E8 */
    lift_w16(x, c->a[3] + 0x14, alu_movew(c, W(c->d[2])));    /* move.w d2,$14(a3) */
    lift_charge(x, 0xA6E8);
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0])));       /* move.w (a0),d2 */
    lift_charge(x, 0xA6EC);
    setw(&c->d[2], alu_asrw(c, W(c->d[2]), 4));               /* asr.w #4,d2 */
    lift_charge(x, 0xA6EE);
    setw(&c->d[2], alu_andw(c, 0x3FF, W(c->d[2])));           /* and.w #$3FF,d2 */
    lift_charge(x, 0xA6F0);
    alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                  /* btst #4,($FFFFC2F4).w */
    lift_charge(x, 0xA6F4);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA6FA, clear);                      /* beq.w loc_A738 */
      if (clear) { goto loc_A738; }
    }

    setw(&c->d[2], alu_aslw(c, W(c->d[2]), 1));               /* asl.w #1,d2 */
    lift_charge(x, 0xA6FE);
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[6] + SEW(c->d[2]))));  /* move.w (a6,d2.w),d2 */
    lift_charge(x, 0xA700);
    alu_cmpw(c, 0xF, lift_r16(x, c->a[3] + 0x52));            /* cmp.w #$F,$52(a3) */
    lift_charge(x, 0xA704);
    {
      int ne = !c->zf;
      lift_charge_bcc(x, 0xA70A, ne);                         /* bne.w loc_A71C */
      if (!ne)
      {
        alu_cmpw(c, 0x18A, W(c->d[2]));                       /* cmp.w #$18A,d2 */
        lift_charge(x, 0xA70E);
        int eq = c->zf;
        lift_charge_bcc(x, 0xA712, eq);                       /* beq.w loc_A71C */
        if (!eq)
        {
          lift_w16(x, c->a[3] + 0x14, alu_movew(c, 0xFC00));  /* move.w #$FC00,$14(a3) */
          lift_charge(x, 0xA716);
        }
      }
    }

    /* loc_A71C */
    alu_cmpw(c, 1, W(c->d[2]));                               /* cmp.w #1,d2 */
    lift_charge(x, 0xA71C);
    {
      int lt = (c->nf != c->vf);
      lift_charge_bcc(x, 0xA720, lt);                         /* blt.w loc_A730 */
      if (!lt)
      {
        alu_cmpw(c, 0x34E, W(c->d[2]));                       /* cmp.w #$34E,d2 */
        lift_charge(x, 0xA724);
        int ge = (c->nf == c->vf);
        lift_charge_bcc(x, 0xA728, ge);                       /* bge.w loc_A730 */
        if (!ge)
        {
          lift_charge(x, 0xA72C);                             /* bra.w loc_A738 */
          goto loc_A738;
        }
      }
    }

    /* loc_A730 */
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0])));       /* move.w (a0),d2 */
    lift_charge(x, 0xA730);
    setw(&c->d[2], alu_asrw(c, W(c->d[2]), 4));               /* asr.w #4,d2 */
    lift_charge(x, 0xA732);
    setw(&c->d[2], alu_andw(c, 0x3FF, W(c->d[2])));           /* and.w #$3FF,d2 */
    lift_charge(x, 0xA734);

  loc_A738:
    lift_w16(x, c->a[3] + 6, alu_movew(c, W(c->d[2])));       /* move.w d2,6(a3) */
    lift_charge(x, 0xA738);
    alu_cmpw(c, 0x28D, W(c->d[2]));                           /* cmp.w #$28D,d2 */
    lift_charge(x, 0xA73C);
    {
      int lt = (c->nf != c->vf);
      lift_charge_bcc(x, 0xA740, lt);                         /* blt.w loc_A75C */
      if (!lt)
      {
        alu_cmpw(c, 0x292, W(c->d[2]));                       /* cmp.w #$292,d2 */
        lift_charge(x, 0xA744);
        int ge = (c->nf == c->vf);
        lift_charge_bcc(x, 0xA748, ge);                       /* bge.w loc_A75C */
        if (!ge)
        {
          alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);            /* btst #4,($FFFFC2F4).w */
          lift_charge(x, 0xA74C);
          int clear = c->zf;
          lift_charge_bcc(x, 0xA752, clear);                  /* beq.w loc_A75C */
          if (!clear)
          {
            lift_w16(x, c->a[3] + 0x14, alu_movew(c, 0x190)); /* move.w #$190,$14(a3) */
            lift_charge(x, 0xA756);
          }
        }
      }
    }

    /* loc_A75C */
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0])));       /* move.w (a0),d2 */
    lift_charge(x, 0xA75C);
    setw(&c->d[2], alu_asrw(c, W(c->d[2]), 3));               /* asr.w #3,d2 */
    lift_charge(x, 0xA75E);
    setw(&c->d[2], alu_andw(c, 0x1800, W(c->d[2])));          /* and.w #$1800,d2 */
    lift_charge(x, 0xA760);
    lift_w16(x, c->a[3] + 4,
             alu_andw(c, 0xE7FF, lift_r16(x, c->a[3] + 4)));  /* and.w #$E7FF,4(a3) */
    lift_charge(x, 0xA764);
    lift_w16(x, c->a[3] + 4,
             alu_orw(c, W(c->d[2]), lift_r16(x, c->a[3] + 4)));  /* or.w d2,4(a3) */
    lift_charge(x, 0xA76A);
    alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                  /* btst #4,($FFFFC2F4).w */
    lift_charge(x, 0xA76E);
    {
      int clear = c->zf;
      lift_charge_bcc(x, 0xA774, clear);                      /* beq.w loc_A792 */
      if (!clear)
      {
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 6)));  /* move.w 6(a3),d2 */
        lift_charge(x, 0xA778);
        alu_cmpw(c, 0x162, W(c->d[2]));                       /* cmp.w #$162,d2 */
        lift_charge(x, 0xA77C);
        int lt = (c->nf != c->vf);
        lift_charge_bcc(x, 0xA780, lt);                       /* blt.w loc_A792 */
        if (!lt)
        {
          alu_cmpw(c, 0x179, W(c->d[2]));                     /* cmp.w #$179,d2 */
          lift_charge(x, 0xA784);
          int ge = (c->nf == c->vf);
          lift_charge_bcc(x, 0xA788, ge);                     /* bge.w loc_A792 */
          if (!ge)
          {
            lift_w8(x, c->a[3] + 4,
                    alu_bchg(c, lift_r8(x, c->a[3] + 4), 3)); /* bchg #3,4(a3) */
            lift_charge(x, 0xA78C);
          }
        }
      }
    }

    /* loc_A792 */
    c->a[0] += 4;                                             /* addq.w #4,a0 */
    lift_charge(x, 0xA792);
    c->a[3] += 0x80;                                          /* add.w #$80,a3 */
    lift_charge(x, 0xA794);
    {
      uint32_t nd1 = W(W(c->d[1]) - 1);                       /* dbf d1,loc_A69E */
      setw(&c->d[1], nd1);
      int taken = (nd1 != 0xFFFF);
      lift_charge_dbcc(x, 0xA798, taken, !taken);
      if (!taken) break;
    }
  }

  c->d[2] = alu_moveql(c, 5);                                 /* moveq #5,d2 */
  lift_charge(x, 0xA79C);
  c->a[3] = 0xFFFFB04Au;                                      /* move.w #$B04A,a3 */
  lift_charge(x, 0xA79E);

  for (;;)
  {
    /* loc_A7A2 */
    lift_w8(x, c->a[3] + 0x6F, alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,$6F(a3) */
    c->a[0] += 1;
    lift_charge(x, 0xA7A2);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));        /* move.b (a0),d0 */
    lift_charge(x, 0xA7A6);
    setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));             /* and.w #$F,d0 */
    lift_charge(x, 0xA7A8);
    alu_cmpw(c, 0xF, W(c->d[0]));                             /* cmp.w #$F,d0 */
    lift_charge(x, 0xA7AC);
    {
      int ne = !c->zf;
      lift_charge_bcc(x, 0xA7B0, ne);                         /* bne.w loc_A7B6 */
      if (!ne)
      {
        c->d[0] = alu_moveql(c, -1);                          /* moveq #-1,d0 */
        lift_charge(x, 0xA7B4);
      }
    }

    /* loc_A7B6 */
    lift_w16(x, c->a[3] + 0x34, alu_movew(c, W(c->d[0])));    /* move.w d0,$34(a3) */
    lift_charge(x, 0xA7B6);
    c->a[3] += 0x80;                                          /* add.w #$80,a3 */
    lift_charge(x, 0xA7BA);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));        /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0xA7BE);
    setb(&c->d[0], alu_lsrb(c, c->d[0], 4));                  /* lsr.b #4,d0 */
    lift_charge(x, 0xA7C0);
    lift_w8(x, c->a[3] + 0x6F, alu_moveb(c, c->d[0]));        /* move.b d0,$6F(a3) */
    lift_charge(x, 0xA7C2);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));        /* move.b (a0),d0 */
    lift_charge(x, 0xA7C6);
    setb(&c->d[0], alu_aslb(c, c->d[0], 4));                  /* asl.b #4,d0 */
    lift_charge(x, 0xA7C8);
    lift_w8(x, c->a[3] + 0x6F,
            alu_moveb(c, (c->d[0] | lift_r8(x, c->a[3] + 0x6F)) & 0xFF));  /* or.b d0,$6F(a3) */
    lift_charge(x, 0xA7CA);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));        /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0xA7CE);
    setb(&c->d[0], alu_lsrb(c, c->d[0], 4));                  /* lsr.b #4,d0 */
    lift_charge(x, 0xA7D0);
    setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));             /* and.w #$F,d0 */
    lift_charge(x, 0xA7D2);
    alu_cmpw(c, 0xF, W(c->d[0]));                             /* cmp.w #$F,d0 */
    lift_charge(x, 0xA7D6);
    {
      int ne = !c->zf;
      lift_charge_bcc(x, 0xA7DA, ne);                         /* bne.w loc_A7E0 */
      if (!ne)
      {
        c->d[0] = alu_moveql(c, -1);                          /* moveq #-1,d0 */
        lift_charge(x, 0xA7DE);
      }
    }

    /* loc_A7E0 */
    lift_w16(x, c->a[3] + 0x34, alu_movew(c, W(c->d[0])));    /* move.w d0,$34(a3) */
    lift_charge(x, 0xA7E0);
    c->a[3] += 0x80;                                          /* add.w #$80,a3 */
    lift_charge(x, 0xA7E4);
    {
      uint32_t nd2 = W(W(c->d[2]) - 1);                       /* dbf d2,loc_A7A2 */
      setw(&c->d[2], nd2);
      int taken = (nd2 != 0xFFFF);
      lift_charge_dbcc(x, 0xA7E8, taken, !taken);
      if (!taken) break;
    }
  }

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));          /* move.b (a0)+,d0 */
  c->a[0] += 1;
  lift_charge(x, 0xA7EC);
  setw(&c->d[0], alu_extw(c, c->d[0]));                       /* ext.w d0 */
  lift_charge(x, 0xA7EE);
  lift_w16(x, 0xFFFFB762u, alu_movew(c, W(c->d[0])));         /* move.w d0,($FFFFB762).w */
  lift_charge(x, 0xA7F0);

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));          /* move.b (a0)+,d0 */
  c->a[0] += 1;
  lift_charge(x, 0xA7F4);
  setw(&c->d[0], alu_extw(c, c->d[0]));                       /* ext.w d0 */
  lift_charge(x, 0xA7F6);
  lift_w16(x, 0xFFFFB7E2u, alu_movew(c, W(c->d[0])));         /* move.w d0,($FFFFB7E2).w */
  lift_charge(x, 0xA7F8);

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));          /* move.b (a0)+,d0 */
  c->a[0] += 1;
  lift_charge(x, 0xA7FC);
  setw(&c->d[0], alu_extw(c, c->d[0]));                       /* ext.w d0 */
  lift_charge(x, 0xA7FE);
  lift_w16(x, 0xFFFFC016u, alu_movew(c, W(c->d[0])));         /* move.w d0,($FFFFC016).w */
  lift_charge(x, 0xA800);

  setw(&c->d[7], alu_movew(c, 0));                            /* clr.w d7 */
  lift_charge(x, 0xA804);
  setb(&c->d[7], alu_moveb(c, lift_r8(x, c->a[0])));          /* move.b (a0)+,d7 */
  c->a[0] += 1;
  lift_charge(x, 0xA806);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));         /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0xA808);
  setw(&c->d[0], alu_andw(c, 0xFFF, W(c->d[0])));             /* and.w #$FFF,d0 */
  lift_charge(x, 0xA80A);
  lift_w16(x, 0xFFFFBE78u,
           alu_andw(c, 0xF000, lift_r16(x, 0xFFFFBE78u)));    /* and.w #$F000,($FFFFBE78).w */
  lift_charge(x, 0xA80E);
  lift_w16(x, 0xFFFFBE78u,
           alu_orw(c, W(c->d[0]), lift_r16(x, 0xFFFFBE78u))); /* or.w d0,($FFFFBE78).w */
  lift_charge(x, 0xA814);
  lift_w16(x, 0xFFFFB89Au, alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,($FFFFB89A).w */
  c->a[0] += 2;
  lift_charge(x, 0xA818);
  lift_w16(x, 0xFFFFBEE2u, alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,($FFFFBEE2).w */
  c->a[0] += 2;
  lift_charge(x, 0xA81C);
  lift_w8(x, 0xFFFFC3EAu, alu_moveb(c, lift_r8(x, c->a[0])));    /* move.b (a0)+,($FFFFC3EA).w */
  c->a[0] += 1;
  lift_charge(x, 0xA820);
  setw(&c->d[0], alu_movew(c, 0));                            /* clr.w d0 */
  lift_charge(x, 0xA824);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));          /* move.b (a0)+,d0 */
  c->a[0] += 1;
  lift_charge(x, 0xA826);
  setw(&c->d[0], alu_lslw(c, W(c->d[0]), 4));                 /* lsl.w #4,d0 */
  lift_charge(x, 0xA828);
  setw(&c->d[0], alu_orw(c, 0xF00F, W(c->d[0])));             /* or.w #$F00F,d0 */
  lift_charge(x, 0xA82A);
  lift_w16(x, 0xFFFFBE86u, alu_movew(c, W(c->d[0])));         /* move.w d0,($FFFFBE86).w */
  lift_charge(x, 0xA82E);

  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 5);                    /* btst #5,($FFFFC2EC).w */
  lift_charge(x, 0xA832);
  {
    int clear = c->zf;
    lift_charge_bcc(x, 0xA838, clear);                        /* beq.w loc_A860 */
    if (!clear)
    {
      c->a[3] = SEW(c->a[5]);                                 /* move.w a5,a3 */
      lift_charge(x, 0xA83C);
      alu_btst(c, lift_r8(x, 0xFFFFC2F0u), 5);                /* btst #5,($FFFFC2F0).w */
      lift_charge(x, 0xA83E);
      int clr5 = c->zf;
      lift_charge_bcc(x, 0xA844, clr5);                       /* beq.w loc_A858 */
      if (!clr5)
      {
        lift_w16(x, c->a[5] + 0x18, alu_movew(c, 0));         /* clr.w $18(a5) */
        lift_charge(x, 0xA848);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[5] + 0x16)));  /* move.w $16(a5),d0 */
        lift_charge(x, 0xA84C);
        setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));           /* asl.w #7,d0 */
        lift_charge(x, 0xA850);
        c->a[3] = 0xFFFFB04Au;                                /* move.w #$B04A,a3 */
        lift_charge(x, 0xA852);
        c->a[3] += SEW(c->d[0]);                              /* add.w d0,a3 */
        lift_charge(x, 0xA856);
      }

      /* loc_A858 */
      lift_call(x, 0xA858, 4, Camera_ApplyTrackedTarget);     /* bsr.w sub_A616 */
      if (x->declined) return;

      lift_charge(x, 0xA85C);                                 /* bra.w loc_A880 */
      goto loc_A880;
    }
  }

  /* loc_A860 */
  lift_w16(x, 0xFFFFBD1Cu, alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,($FFFFBD1C).w */
  c->a[0] += 2;
  lift_charge(x, 0xA860);
  lift_w16(x, 0xFFFFBD18u, alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,($FFFFBD18).w */
  c->a[0] += 2;
  lift_charge(x, 0xA864);
  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                    /* btst #4,($FFFFC2F4).w */
  lift_charge(x, 0xA868);
  {
    int clear = c->zf;
    lift_charge_bcc(x, 0xA86E, clear);                        /* beq.w loc_A880 */
    if (!clear)
    {
      lift_w16(x, 0xFFFFBD1Cu, alu_negw(c, lift_r16(x, 0xFFFFBD1Cu)));  /* neg.w */
      lift_charge(x, 0xA872);
      lift_w16(x, 0xFFFFBD18u, alu_negw(c, lift_r16(x, 0xFFFFBD18u)));  /* neg.w */
      lift_charge(x, 0xA876);
      lift_call(x, 0xA87A, 6, sub_A88C);                      /* jsr sub_A88C */
      if (x->declined) return;
    }
  }

loc_A880:
  lift_w8(x, 0xFFFFC2F4u, alu_bclr(c, lift_r8(x, 0xFFFFC2F4u), 7));  /* bclr #7,($FFFFC2F4).w */
  lift_charge(x, 0xA880);

  c->d[0] = saved[4];                                         /* movem.l (sp)+,d0-d2/a0/a6 */
  c->d[1] = saved[3];
  c->d[2] = saved[2];
  c->a[0] = saved[1];
  c->a[6] = saved[0];
  c->a[7] += 5 * 4;
  lift_charge_movem(x, 0xA886);

  lift_charge(x, 0xA88A);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_A88C (called from sub_A68C+1EE)
 * Clamps the restored camera position to the rink bounds: X
 * ($FFFFBD1C) into [$FFC4, $3C] and Y ($FFFFBD18) into [$FF38, $100].
 * Each axis is written only when it lies outside the window, and the
 * clamp value written is whichever bound was tested last. d0 is saved
 * and restored through the stack.
 */
void sub_A88C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 2;                                    /* move.w d0,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[0])));
  lift_charge(x, 0xA88C);

  setw(&c->d[0], alu_movew(c, 0x3C));              /* move.w #$3C,d0 */
  lift_charge(x, 0xA88E);
  alu_cmpw(c, lift_r16(x, 0xFFFFBD1Cu), W(c->d[0]));  /* cmp.w ($FFFFBD1C).w,d0 */
  lift_charge(x, 0xA892);
  {
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xA896, lt);                /* blt.w loc_A8A6 */
    int store = lt;
    if (!lt)
    {
      setw(&c->d[0], alu_movew(c, 0xFFC4));        /* move.w #$FFC4,d0 */
      lift_charge(x, 0xA89A);
      alu_cmpw(c, lift_r16(x, 0xFFFFBD1Cu), W(c->d[0]));  /* cmp.w ($FFFFBD1C).w,d0 */
      lift_charge(x, 0xA89E);
      int le = c->zf || (c->nf != c->vf);
      lift_charge_bcc(x, 0xA8A2, le);              /* ble.w loc_A8AA */
      store = !le;
    }
    if (store)
    {
      /* loc_A8A6 */
      lift_w16(x, 0xFFFFBD1Cu, alu_movew(c, W(c->d[0])));  /* move.w d0,($FFFFBD1C).w */
      lift_charge(x, 0xA8A6);
    }
  }

  /* loc_A8AA */
  setw(&c->d[0], alu_movew(c, 0x100));             /* move.w #$100,d0 */
  lift_charge(x, 0xA8AA);
  alu_cmpw(c, lift_r16(x, 0xFFFFBD18u), W(c->d[0]));  /* cmp.w ($FFFFBD18).w,d0 */
  lift_charge(x, 0xA8AE);
  {
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xA8B2, lt);                /* blt.w loc_A8C2 */
    int store = lt;
    if (!lt)
    {
      setw(&c->d[0], alu_movew(c, 0xFF38));        /* move.w #$FF38,d0 */
      lift_charge(x, 0xA8B6);
      alu_cmpw(c, lift_r16(x, 0xFFFFBD18u), W(c->d[0]));  /* cmp.w ($FFFFBD18).w,d0 */
      lift_charge(x, 0xA8BA);
      int le = c->zf || (c->nf != c->vf);
      lift_charge_bcc(x, 0xA8BE, le);              /* ble.w loc_A8C6 */
      store = !le;
    }
    if (store)
    {
      /* loc_A8C2 */
      lift_w16(x, 0xFFFFBD18u, alu_movew(c, W(c->d[0])));  /* move.w d0,($FFFFBD18).w */
      lift_charge(x, 0xA8C2);
    }
  }

  /* loc_A8C6 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));  /* move.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0xA8C6);

  lift_charge(x, 0xA8C8);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FC184 (called from ROM:FBCE4 and ROM:FBD2C; all of d0-a6 saved and
 * restored by the outer movem, so only memory and a7 are observable)
 * Builds and sorts the 8-team stat-leaders table used by the season
 * standings screens. Pass one walks the 8 $10-byte records at
 * $FFFFCF36, packing each record's +8/+9 big-endian word (attempts) and
 * +$A/+$B word (chances) into $FFFFD562, storing the percentage
 * (attempts * 100 / chances, or 0 when chances is zero) as a byte at
 * $FFFFD54A, and copying +$C/+$D into $FFFFD552. Pass two seeds an
 * index permutation 1..7 at $FFFFD542 and bubble-sorts it descending by
 * percentage, breaking ties first on the $FFFFD552 word and then on the
 * $FFFFD562 word; the sweep repeats until a pass makes no swap.
 */
void sub_FC184(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFC184);

  c->a[0] = 0xFFFFCF36u;                            /* move.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFC188);
  c->a[1] = 0xFFFFD54Au;                            /* move.l #$FFFFD54A,a1 */
  lift_charge(x, 0xFC18E);
  c->a[6] = 0xFFFFD552u;                            /* move.l #$FFFFD552,a6 */
  lift_charge(x, 0xFC194);
  c->a[5] = 0xFFFFD562u;                            /* move.l #$FFFFD562,a5 */
  lift_charge(x, 0xFC19A);
  setw(&c->d[7], alu_movew(c, 7));                  /* move.w #7,d7 */
  lift_charge(x, 0xFC1A0);

  for (;;)
  {
    /* loc_FC1A4 */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 8)));   /* move.b 8(a0),d0 */
    lift_charge(x, 0xFC1A4);
    setw(&c->d[0], alu_lslw(c, W(c->d[0]), 8));              /* lsl.w #8,d0 */
    lift_charge(x, 0xFC1A8);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 9)));   /* move.b 9(a0),d0 */
    lift_charge(x, 0xFC1AA);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + 0xA))); /* move.b $A(a0),d1 */
    lift_charge(x, 0xFC1AE);
    setw(&c->d[1], alu_lslw(c, W(c->d[1]), 8));              /* lsl.w #8,d1 */
    lift_charge(x, 0xFC1B2);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + 0xB))); /* move.b $B(a0),d1 */
    lift_charge(x, 0xFC1B4);
    lift_w16(x, c->a[5], alu_movew(c, W(c->d[1])));          /* move.w d1,(a5)+ */
    c->a[5] += 2;
    lift_charge(x, 0xFC1B8);
    alu_tstw(c, W(c->d[1]));                                 /* tst.w d1 */
    lift_charge(x, 0xFC1BA);
    {
      int nz = !c->zf;
      lift_charge_bcc(x, 0xFC1BC, nz);                       /* bne.w loc_FC1C6 */
      if (!nz)
      {
        setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
        lift_charge(x, 0xFC1C0);
        lift_charge(x, 0xFC1C2);                             /* bra.w loc_FC1CC */
      }
      else
      {
        /* loc_FC1C6 */
        c->d[0] = alu_mulu(c, 0x64, c->d[0]);                /* mulu.w #$64,d0 */
        lift_charge_mulu(x, 0xFC1C6, 0x64);
        lift_charge_divu(x, 0xFC1CA, W(c->d[1]), c->d[0]);   /* divu.w d1,d0 */
        if (x->declined) return;                             /* zero divisor would trap */
        c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);
      }
    }

    /* loc_FC1CC */
    lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));              /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xFC1CC);
    lift_w8(x, c->a[6], alu_moveb(c, lift_r8(x, c->a[0] + 0xC)));  /* move.b $C(a0),(a6)+ */
    c->a[6] += 1;
    lift_charge(x, 0xFC1CE);
    lift_w8(x, c->a[6], alu_moveb(c, lift_r8(x, c->a[0] + 0xD)));  /* move.b $D(a0),(a6)+ */
    c->a[6] += 1;
    lift_charge(x, 0xFC1D2);
    c->a[0] += 0x10;                                         /* add.w #$10,a0 */
    lift_charge(x, 0xFC1D6);
    {
      uint32_t nd7 = W(W(c->d[7]) - 1);                      /* dbf d7,loc_FC1A4 */
      setw(&c->d[7], nd7);
      int taken = (nd7 != 0xFFFF);
      lift_charge_dbcc(x, 0xFC1DA, taken, !taken);
      if (!taken) break;
    }
  }

  c->a[1] = 0xFFFFD542u;                            /* move.l #$FFFFD542,a1 */
  lift_charge(x, 0xFC1DE);
  c->a[7] -= 4;                                     /* move.l a1,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[1]));
  lift_charge(x, 0xFC1E4);
  setw(&c->d[0], alu_movew(c, 1));                  /* move.w #1,d0 */
  lift_charge(x, 0xFC1E6);
  setw(&c->d[7], alu_movew(c, 6));                  /* move.w #6,d7 */
  lift_charge(x, 0xFC1EA);

  for (;;)
  {
    /* loc_FC1EE */
    lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));     /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xFC1EE);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));     /* addq.w #1,d0 */
    lift_charge(x, 0xFC1F0);
    uint32_t nd7 = W(W(c->d[7]) - 1);               /* dbf d7,loc_FC1EE */
    setw(&c->d[7], nd7);
    int taken = (nd7 != 0xFFFF);
    lift_charge_dbcc(x, 0xFC1F2, taken, !taken);
    if (!taken) break;
  }

  c->a[1] = lift_r32(x, c->a[7]);                   /* move.l (sp),a1 */
  lift_charge(x, 0xFC1F6);
  c->a[0] = 0xFFFFD54Au;                            /* move.l #$FFFFD54A,a0 */
  lift_charge(x, 0xFC1F8);
  c->a[6] = 0xFFFFD552u;                            /* move.l #$FFFFD552,a6 */
  lift_charge(x, 0xFC1FE);
  c->a[5] = 0xFFFFD562u;                            /* move.l #$FFFFD562,a5 */
  lift_charge(x, 0xFC204);

  for (int sweeps = 0; ; sweeps++)
  {
    /* loc_FC20A */
    if (sweeps > 4096) { x->declined = 1; return; }

    c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp),a1 */
    lift_charge(x, 0xFC20A);
    setw(&c->d[7], alu_movew(c, 5));                /* move.w #5,d7 */
    lift_charge(x, 0xFC20C);
    setw(&c->d[6], alu_movew(c, 0));                /* clr.w d6 */
    lift_charge(x, 0xFC210);

    for (;;)
    {
      /* loc_FC212 */
      int swap = 0, done = 0;

      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1])));     /* move.b (a1)+,d1 */
      c->a[1] += 1;
      lift_charge(x, 0xFC212);
      setw(&c->d[1], alu_extw(c, c->d[1]));                  /* ext.w d1 */
      lift_charge(x, 0xFC214);
      setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[1])));     /* move.b (a1),d2 */
      lift_charge(x, 0xFC216);
      setw(&c->d[2], alu_extw(c, c->d[2]));                  /* ext.w d2 */
      lift_charge(x, 0xFC218);
      setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[1]))));  /* move.b (a0,d1.w),d0 */
      lift_charge(x, 0xFC21A);
      setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2]))));  /* move.b (a0,d2.w),d3 */
      lift_charge(x, 0xFC21E);
      alu_cmpb(c, c->d[3], c->d[0]);                         /* cmp.b d3,d0 */
      lift_charge(x, 0xFC222);
      {
        int gt = !c->zf && (c->nf == c->vf);
        lift_charge_bcc(x, 0xFC224, gt);                     /* bgt.w loc_FC272 */
        if (gt) { done = 1; }
      }
      if (!done)
      {
        int lt = (c->nf != c->vf);
        lift_charge_bcc(x, 0xFC228, lt);                     /* blt.w loc_FC264 */
        if (lt) { swap = 1; }
      }

      if (!done && !swap)
      {
        uint32_t in[3] = { c->d[3], c->d[2], c->d[1] };      /* movem.l d1-d3,-(sp) */
        for (int i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], in[i]); }
        lift_charge_movem(x, 0xFC22C);

        setw(&c->d[1], alu_addw(c, W(c->d[1]), W(c->d[1]))); /* add.w d1,d1 */
        lift_charge(x, 0xFC230);
        setw(&c->d[2], alu_addw(c, W(c->d[2]), W(c->d[2]))); /* add.w d2,d2 */
        lift_charge(x, 0xFC232);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[6] + SEW(c->d[1]))));  /* move.w (a6,d1.w),d0 */
        lift_charge(x, 0xFC234);
        setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[6] + SEW(c->d[2]))));  /* move.w (a6,d2.w),d3 */
        lift_charge(x, 0xFC238);
        alu_cmpw(c, W(c->d[3]), W(c->d[0]));                 /* cmp.w d3,d0 */
        lift_charge(x, 0xFC23C);

        c->d[1] = in[2]; c->d[2] = in[1]; c->d[3] = in[0];   /* movem.l (sp)+,d1-d3 */
        c->a[7] += 12;
        lift_charge_movem(x, 0xFC23E);

        int gt = !c->zf && (c->nf == c->vf);
        lift_charge_bcc(x, 0xFC242, gt);                     /* bgt.w loc_FC272 */
        if (gt) { done = 1; }
        if (!done)
        {
          int lt = (c->nf != c->vf);
          lift_charge_bcc(x, 0xFC246, lt);                   /* blt.w loc_FC264 */
          if (lt) { swap = 1; }
        }
      }

      if (!done && !swap)
      {
        uint32_t in[3] = { c->d[3], c->d[2], c->d[1] };      /* movem.l d1-d3,-(sp) */
        for (int i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], in[i]); }
        lift_charge_movem(x, 0xFC24A);

        setw(&c->d[1], alu_addw(c, W(c->d[1]), W(c->d[1]))); /* add.w d1,d1 */
        lift_charge(x, 0xFC24E);
        setw(&c->d[2], alu_addw(c, W(c->d[2]), W(c->d[2]))); /* add.w d2,d2 */
        lift_charge(x, 0xFC250);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[5] + SEW(c->d[1]))));  /* move.w (a5,d1.w),d0 */
        lift_charge(x, 0xFC252);
        setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[5] + SEW(c->d[2]))));  /* move.w (a5,d2.w),d3 */
        lift_charge(x, 0xFC256);
        alu_cmpw(c, W(c->d[3]), W(c->d[0]));                 /* cmp.w d3,d0 */
        lift_charge(x, 0xFC25A);

        c->d[1] = in[2]; c->d[2] = in[1]; c->d[3] = in[0];   /* movem.l (sp)+,d1-d3 */
        c->a[7] += 12;
        lift_charge_movem(x, 0xFC25C);

        int ge = (c->nf == c->vf);
        lift_charge_bcc(x, 0xFC260, ge);                     /* bge.w loc_FC272 */
        if (ge) { done = 1; }
      }

      if (!done)
      {
        /* loc_FC264 */
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1),d0 */
        lift_charge(x, 0xFC264);
        setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1] - 1)));  /* move.b -1(a1),d1 */
        lift_charge(x, 0xFC266);
        lift_w8(x, c->a[1] - 1, alu_moveb(c, c->d[0]));      /* move.b d0,-1(a1) */
        lift_charge(x, 0xFC26A);
        lift_w8(x, c->a[1], alu_moveb(c, c->d[1]));          /* move.b d1,(a1) */
        lift_charge(x, 0xFC26E);
        setb(&c->d[6], 0xFF);                                /* st d6: no flags */
        lift_charge(x, 0xFC270);
      }

      /* loc_FC272 */
      uint32_t nd7 = W(W(c->d[7]) - 1);                      /* dbf d7,loc_FC212 */
      setw(&c->d[7], nd7);
      int taken = (nd7 != 0xFFFF);
      lift_charge_dbcc(x, 0xFC272, taken, !taken);
      if (!taken) break;
    }

    alu_tstw(c, W(c->d[6]));                        /* tst.w d6 */
    lift_charge(x, 0xFC276);
    int again = !c->zf;
    lift_charge_bcc(x, 0xFC278, again);             /* bne.s loc_FC20A */
    if (!again) break;
  }

  c->a[1] = lift_r32(x, c->a[7]);                   /* move.l (sp)+,a1 */
  c->a[7] += 4;
  lift_charge(x, 0xFC27A);

  c->d[0] = saved[14];                              /* movem.l (sp)+,d0-a6 */
  c->d[1] = saved[13];
  c->d[2] = saved[12];
  c->d[3] = saved[11];
  c->d[4] = saved[10];
  c->d[5] = saved[9];
  c->d[6] = saved[8];
  c->d[7] = saved[7];
  c->a[0] = saved[6];
  c->a[1] = saved[5];
  c->a[2] = saved[4];
  c->a[3] = saved[3];
  c->a[4] = saved[2];
  c->a[5] = saved[1];
  c->a[6] = saved[0];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xFC27C);

  lift_charge(x, 0xFC280);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_1833A(lift_ctx *x);

/*
 * sub_182A2 (called from sub_7CB0's $178xx chunk)
 * Season stat-accumulator flush. The per-team stats live packed as
 * variable-width bitfields inside longs at $FFFFD070 (a2 = $FFFFD092,
 * indexed backwards through -$22(a2,d5.w)); the field widths cycle
 * $C/$E/$A/$A (the 4-byte table at $18336) across 104 counters.
 * sub_1833A first unpacks all 104 fields into the word array at
 * $FFFFCD96; this routine then adds the current period's deltas from
 * the team block (+$B4 of $FFFFC6CE, or the away block at +$364 when
 * ($FFFFCEEE)-indexed team id at $FFFFCEF4 does not match $28(a2)) into
 * those words, and repacks each — clamped to its field's maximum
 * ((1<<width)-1) — back into the bitfield longs. Bails via the shared
 * far rts when ($FFFFD048).w is below 1 (no season in progress).
 */
void sub_182A2(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, 1, lift_r16(x, 0xFFFFD048u));        /* cmp.w #1,($FFFFD048).w */
  lift_charge(x, 0x182A2);
  {
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0x182A8, lt);               /* blt.w locret_15464 */
    if (lt)
    {
      lift_charge(x, 0x15464);                     /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_call(x, 0x182AC, 4, sub_1833A);             /* bsr.w sub_1833A */
  if (x->declined) return;

  c->a[0] = 0xFFFFCEF4u;                           /* move.w #$CEF4,a0 */
  lift_charge(x, 0x182B0);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFCEEEu)));  /* move.w ($FFFFCEEE).w,d2 */
  lift_charge(x, 0x182B4);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2]))));  /* move.b (a0,d2.w),d2 */
  lift_charge(x, 0x182B8);
  c->a[2] = 0xFFFFC6CEu;                           /* move.w #$C6CE,a2 */
  lift_charge(x, 0x182BC);
  alu_cmpw(c, lift_r16(x, c->a[2] + 0x28), W(c->d[2]));  /* cmp.w $28(a2),d2 */
  lift_charge(x, 0x182C0);
  {
    int eq = c->zf;
    lift_charge_bcc(x, 0x182C4, eq);               /* beq.w loc_182CC */
    if (!eq)
    {
      c->a[2] += 0x364;                            /* add.w #$364,a2 */
      lift_charge(x, 0x182C8);
    }
  }

  /* loc_182CC */
  c->a[2] += 0xB4;                                 /* add.w #$B4,a2 */
  lift_charge(x, 0x182CC);
  c->d[0] = alu_moveql(c, 0x67);                   /* moveq #$67,d0 */
  lift_charge(x, 0x182D0);
  c->a[1] = 0xFFFFCD96u;                           /* move.w #$CD96,a1 */
  lift_charge(x, 0x182D2);

  for (;;)
  {
    /* loc_182D6 */
    setw(&c->d[1], alu_movew(c, 0));               /* clr.w d1 */
    lift_charge(x, 0x182D6);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[2])));  /* move.b (a2)+,d1 */
    c->a[2] += 1;
    lift_charge(x, 0x182D8);
    lift_w16(x, c->a[1], alu_addw(c, W(c->d[1]), lift_r16(x, c->a[1])));  /* add.w d1,(a1)+ */
    c->a[1] += 2;
    lift_charge(x, 0x182DA);
    uint32_t nd0 = W(W(c->d[0]) - 1);              /* dbf d0,loc_182D6 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x182DC, taken, !taken);
    if (!taken) break;
  }

  c->a[0] = 0xFFFFCD96u;                           /* move.w #$CD96,a0 */
  lift_charge(x, 0x182E0);
  c->a[1] = 0x00018336u;                           /* move.l #byte_18336,a1 */
  lift_charge(x, 0x182E4);
  c->a[2] = 0xFFFFD092u;                           /* move.w #$D092,a2 */
  lift_charge(x, 0x182EA);
  c->d[0] = alu_moveql(c, 0x67);                   /* moveq #$67,d0 */
  lift_charge(x, 0x182EE);
  setw(&c->d[4], alu_movew(c, 0));                 /* clr.w d4 */
  lift_charge(x, 0x182F0);

  for (;;)
  {
    /* loc_182F2 */
    c->d[1] = alu_movel(c, 0);                     /* clr.l d1 */
    lift_charge(x, 0x182F2);
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,d1 */
    c->a[0] += 2;
    lift_charge(x, 0x182F4);
    setw(&c->d[2], alu_movew(c, W(c->d[0])));      /* move.w d0,d2 */
    lift_charge(x, 0x182F6);
    setw(&c->d[2], alu_andw(c, 3, W(c->d[2])));    /* and.w #3,d2 */
    lift_charge(x, 0x182F8);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[2]))));  /* move.b (a1,d2.w),d2 */
    lift_charge(x, 0x182FC);
    c->d[3] = alu_movel(c, 0);                     /* clr.l d3 */
    lift_charge(x, 0x18300);
    c->d[3] = alu_bset(c, c->d[3], (int)(c->d[2] & 31));  /* bset d2,d3 */
    lift_charge_bitop_reg(x, 0x18302, c->d[2]);
    setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));    /* subq.w #1,d3 */
    lift_charge(x, 0x18304);
    alu_cmpw(c, W(c->d[3]), W(c->d[1]));           /* cmp.w d3,d1 */
    lift_charge(x, 0x18306);
    {
      int le = c->zf || (c->nf != c->vf);
      lift_charge_bcc(x, 0x18308, le);             /* ble.w loc_1830E */
      if (!le)
      {
        setw(&c->d[1], alu_movew(c, W(c->d[3])));  /* move.w d3,d1 */
        lift_charge(x, 0x1830C);
      }
    }

    /* loc_1830E */
    c->d[3] = alu_notl(c, c->d[3]);                /* not.l d3 */
    lift_charge(x, 0x1830E);
    setw(&c->d[5], alu_movew(c, W(c->d[4])));      /* move.w d4,d5 */
    lift_charge(x, 0x18310);
    setw(&c->d[5], alu_andw(c, 0xF, W(c->d[5])));  /* and.w #$F,d5 */
    lift_charge(x, 0x18312);
    {
      int cnt = (int)(c->d[5] & 63);
      c->d[3] = alu_roll(c, c->d[3], cnt);         /* rol.l d5,d3 */
      lift_charge_shift_reg(x, 0x18316, cnt);
      c->d[1] = alu_roll(c, c->d[1], cnt);         /* rol.l d5,d1 */
      lift_charge_shift_reg(x, 0x18318, cnt);
    }
    setw(&c->d[5], alu_movew(c, W(c->d[4])));      /* move.w d4,d5 */
    lift_charge(x, 0x1831A);
    setw(&c->d[5], alu_lsrw(c, W(c->d[5]), 4));    /* lsr.w #4,d5 */
    lift_charge(x, 0x1831C);
    setw(&c->d[5], alu_addw(c, W(c->d[5]), W(c->d[5])));  /* add.w d5,d5 */
    lift_charge(x, 0x1831E);
    setw(&c->d[5], alu_negw(c, W(c->d[5])));       /* neg.w d5 */
    lift_charge(x, 0x18320);
    setw(&c->d[5], alu_addw(c, 0x100, W(c->d[5])));  /* add.w #$100,d5 */
    lift_charge(x, 0x18322);
    {
      uint32_t ea = c->a[2] + SEW(c->d[5]) - 0x22;
      lift_w32(x, ea, alu_andl(c, c->d[3], lift_r32(x, ea)));  /* and.l d3,-$22(a2,d5.w) */
      lift_charge(x, 0x18326);
      lift_w32(x, ea, alu_movel(c, c->d[1] | lift_r32(x, ea)));  /* or.l d1,-$22(a2,d5.w) */
      lift_charge(x, 0x1832A);
    }
    setw(&c->d[4], alu_addw(c, W(c->d[2]), W(c->d[4])));  /* add.w d2,d4 */
    lift_charge(x, 0x1832E);

    uint32_t nd0 = W(W(c->d[0]) - 1);              /* dbf d0,loc_182F2 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x18330, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x18334);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1833A (called from sub_9428+4 and sub_182A2+A)
 * Unpacks the 104 variable-width season counters packed into the longs
 * at $FFFFD070 (addressed as -$22(a2,d5.w) with a2 = $FFFFD092 and d5
 * walking backwards two bytes per 16 accumulated bits) into the word
 * array at $FFFFCD96. Field widths cycle $C/$E/$A/$A from the 4-byte
 * table at $18336, selected by the loop counter's low two bits; d4 is
 * the running bit offset, whose low nibble is the shift within the long
 * and whose high nibble picks the long.
 */
void sub_1833A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = 0xFFFFCD96u;                           /* move.w #$CD96,a0 */
  lift_charge(x, 0x1833A);
  c->a[1] = 0x00018336u;                           /* move.l #byte_18336,a1 */
  lift_charge(x, 0x1833E);
  c->a[2] = 0xFFFFD092u;                           /* move.w #$D092,a2 */
  lift_charge(x, 0x18344);
  c->d[0] = alu_moveql(c, 0x67);                   /* moveq #$67,d0 */
  lift_charge(x, 0x18348);
  setw(&c->d[4], alu_movew(c, 0));                 /* clr.w d4 */
  lift_charge(x, 0x1834A);

  for (;;)
  {
    /* loc_1834C */
    setw(&c->d[5], alu_movew(c, W(c->d[4])));      /* move.w d4,d5 */
    lift_charge(x, 0x1834C);
    setw(&c->d[5], alu_lsrw(c, W(c->d[5]), 4));    /* lsr.w #4,d5 */
    lift_charge(x, 0x1834E);
    setw(&c->d[5], alu_addw(c, W(c->d[5]), W(c->d[5])));  /* add.w d5,d5 */
    lift_charge(x, 0x18350);
    setw(&c->d[5], alu_negw(c, W(c->d[5])));       /* neg.w d5 */
    lift_charge(x, 0x18352);
    setw(&c->d[5], alu_addw(c, 0x100, W(c->d[5])));  /* add.w #$100,d5 */
    lift_charge(x, 0x18354);
    c->d[1] = alu_movel(c, lift_r32(x, c->a[2] + SEW(c->d[5]) - 0x22));  /* move.l -$22(a2,d5.w),d1 */
    lift_charge(x, 0x18358);
    setw(&c->d[5], alu_movew(c, W(c->d[4])));      /* move.w d4,d5 */
    lift_charge(x, 0x1835C);
    setw(&c->d[5], alu_andw(c, 0xF, W(c->d[5])));  /* and.w #$F,d5 */
    lift_charge(x, 0x1835E);
    {
      int cnt = (int)(c->d[5] & 63);
      c->d[1] = alu_lsrl(c, c->d[1], cnt);         /* lsr.l d5,d1 */
      lift_charge_shift_reg(x, 0x18362, cnt);
    }
    setw(&c->d[2], alu_movew(c, W(c->d[0])));      /* move.w d0,d2 */
    lift_charge(x, 0x18364);
    setw(&c->d[2], alu_andw(c, 3, W(c->d[2])));    /* and.w #3,d2 */
    lift_charge(x, 0x18366);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[2]))));  /* move.b (a1,d2.w),d2 */
    lift_charge(x, 0x1836A);
    setw(&c->d[4], alu_addw(c, W(c->d[2]), W(c->d[4])));  /* add.w d2,d4 */
    lift_charge(x, 0x1836E);
    setw(&c->d[3], alu_movew(c, 0));               /* clr.w d3 */
    lift_charge(x, 0x18370);
    c->d[3] = alu_bset(c, c->d[3], (int)(c->d[2] & 31));  /* bset d2,d3 */
    lift_charge_bitop_reg(x, 0x18372, c->d[2]);
    setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));    /* subq.w #1,d3 */
    lift_charge(x, 0x18374);
    setw(&c->d[1], alu_andw(c, W(c->d[3]), W(c->d[1])));  /* and.w d3,d1 */
    lift_charge(x, 0x18376);
    lift_w16(x, c->a[0], alu_movew(c, W(c->d[1])));  /* move.w d1,(a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0x18378);

    uint32_t nd0 = W(W(c->d[0]) - 1);              /* dbf d0,loc_1834C */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x1837A, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x1837E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_181F6(lift_ctx *x);
void sub_1820E(lift_ctx *x);
void sub_1821A(lift_ctx *x);
void sub_18236(lift_ctx *x);

/*
 * sub_18192 (called from sub_180FC+44 and sub_18380+100; a3 = the
 * 5-word playoff-odds accumulator being built, $FFFFD088 or $FFFFD176)
 * Zeroes the accumulator (sub_1820E), then folds in one weighted term
 * per playoff-state word — $FFFFCEE8 at weight $20, $CEEA at 8, $CEEC
 * at 4, $CEEE at $10, $CEF0 at 8 and $CEF2 at $4000 — followed by both
 * the +4 and +6 words of each of the 8 $10-byte series records at
 * $FFFFCE66. Each fold is sub_181F6 (scale-and-carry into the running
 * fixed-point accumulator).
 */
void sub_18192(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x18192, 4, sub_1820E);             /* bsr.w sub_1820E */
  if (x->declined) return;

  static const struct { uint32_t src, mv, mq; uint32_t weight; uint32_t bsr; }
  terms[6] = {
    { 0xFFFFCEE8u, 0x18196, 0x1819A, 0x20,   0x1819C },
    { 0xFFFFCEEAu, 0x181A0, 0x181A4, 0x08,   0x181A6 },
    { 0xFFFFCEECu, 0x181AA, 0x181AE, 0x04,   0x181B0 },
    { 0xFFFFCEEEu, 0x181B4, 0x181B8, 0x10,   0x181BA },
    { 0xFFFFCEF0u, 0x181BE, 0x181C2, 0x08,   0x181C4 },
    { 0xFFFFCEF2u, 0x181C8, 0x181CC, 0x4000, 0x181D0 },
  };
  for (int i = 0; i < 6; i++)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, terms[i].src)));  /* move.w (abs).w,d0 */
    lift_charge(x, terms[i].mv);
    if (i == 5)
    {
      setw(&c->d[1], alu_movew(c, terms[i].weight));          /* move.w #$4000,d1 */
    }
    else
    {
      c->d[1] = alu_moveql(c, (int32_t)terms[i].weight);      /* moveq #imm,d1 */
    }
    lift_charge(x, terms[i].mq);
    lift_call(x, terms[i].bsr, 4, sub_181F6);                 /* bsr.w sub_181F6 */
    if (x->declined) return;
  }

  c->d[1] = alu_moveql(c, 5);                      /* moveq #5,d1 */
  lift_charge(x, 0x181D4);
  c->d[2] = alu_moveql(c, 7);                      /* moveq #7,d2 */
  lift_charge(x, 0x181D6);
  c->a[1] = 0xFFFFCE66u;                           /* move.w #$CE66,a1 */
  lift_charge(x, 0x181D8);

  for (;;)
  {
    /* loc_181DC */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 4)));   /* move.w 4(a1),d0 */
    lift_charge(x, 0x181DC);
    lift_call(x, 0x181E0, 4, sub_181F6);                      /* bsr.w sub_181F6 */
    if (x->declined) return;
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 6)));   /* move.w 6(a1),d0 */
    lift_charge(x, 0x181E4);
    lift_call(x, 0x181E8, 4, sub_181F6);                      /* bsr.w sub_181F6 */
    if (x->declined) return;
    c->a[1] += 0x10;                                          /* add.w #$10,a1 */
    lift_charge(x, 0x181EC);

    uint32_t nd2 = W(W(c->d[2]) - 1);                         /* dbf d2,loc_181DC */
    setw(&c->d[2], nd2);
    int taken = (nd2 != 0xFFFF);
    lift_charge_dbcc(x, 0x181F0, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x181F4);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_181F6 (called eight times from sub_18192)
 *   in: d0 = the term's value, d1 = its weight; a3 = accumulator base
 * Folds one weighted term into the multi-word accumulator at (a3):
 * sub_18236 multiplies each of the accumulator's five words by the
 * weight (after the exg, d0 holds the weight) and adds the products in
 * with carry propagation, then sub_1821A adds the term's own value into
 * the low long. d0/d1 are restored by the movem.
 */
void sub_181F6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[2] = { c->d[1], c->d[0] };        /* movem.l d0-d1,-(sp) */
  for (int i = 0; i < 2; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x181F6);

  {
    uint32_t t = c->d[0];                          /* exg d0,d1: no flags */
    c->d[0] = c->d[1];
    c->d[1] = t;
  }
  lift_charge(x, 0x181FA);

  lift_call(x, 0x181FC, 4, sub_18236);             /* bsr.w sub_18236 */
  if (x->declined) return;

  c->d[0] = alu_movel(c, 0);                       /* clr.l d0 */
  lift_charge(x, 0x18200);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));        /* move.w d1,d0 */
  lift_charge(x, 0x18202);

  lift_call(x, 0x18204, 4, sub_1821A);             /* bsr.w sub_1821A */
  if (x->declined) return;

  c->d[0] = saved[1];                              /* movem.l (sp)+,d0-d1 */
  c->d[1] = saved[0];
  c->a[7] += 8;
  lift_charge_movem(x, 0x18208);

  lift_charge(x, 0x1820C);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1820E (called from sub_180FC+68 and sub_18192)
 *   in: a3 = accumulator base
 * Clears the five words of the accumulator at (a3). Leaves a0 one past
 * the last word and d0 = -1.
 */
void sub_1820E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = SEW(c->a[3]);                          /* move.w a3,a0 */
  lift_charge(x, 0x1820E);
  c->d[0] = alu_moveql(c, 4);                      /* moveq #4,d0 */
  lift_charge(x, 0x18210);

  for (;;)
  {
    /* loc_18212 */
    lift_w16(x, c->a[0], alu_movew(c, 0));         /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0x18212);
    uint32_t nd0 = W(W(c->d[0]) - 1);              /* dbf d0,loc_18212 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x18214, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x18218);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_1821A (called from sub_181F6+E)
 *   in: d0 = value to add, a3 = accumulator base
 * Adds d0 into the accumulator's low long at $6(a3) and propagates the
 * carry upward one word at a time: the `dbcc` exits as soon as a word
 * add produces no carry, and otherwise ripples through at most 4 words.
 * d1/a0 are restored by the movem.
 */
void sub_1821A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[2] = { c->a[0], c->d[1] };        /* movem.l d1/a0,-(sp) */
  for (int i = 0; i < 2; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x1821A);

  c->a[0] = c->a[3] + 0xA;                         /* lea $A(a3),a0 */
  lift_charge(x, 0x1821E);
  c->d[1] = alu_moveql(c, 3);                      /* moveq #3,d1 */
  lift_charge(x, 0x18222);

  c->a[0] -= 4;                                    /* add.l d0,-(a0) */
  lift_w32(x, c->a[0], alu_addl(c, c->d[0], lift_r32(x, c->a[0])));
  lift_charge(x, 0x18224);

  lift_charge(x, 0x18226);                         /* bra.w loc_1822C */

  for (;;)
  {
    /* loc_1822C */
    int cc_true = !c->cf;                          /* dbcc d1,loc_1822A (cc = carry clear) */
    int taken = 0, expired = 0;
    if (!cc_true)
    {
      uint32_t nd1 = W(W(c->d[1]) - 1);
      setw(&c->d[1], nd1);
      taken = (nd1 != 0xFFFF);
      expired = !taken;
    }
    lift_charge_dbcc(x, 0x1822C, taken, expired);
    if (!taken) break;

    /* loc_1822A */
    c->a[0] -= 2;                                  /* addq.w #1,-(a0) */
    lift_w16(x, c->a[0], alu_addw(c, 1, lift_r16(x, c->a[0])));
    lift_charge(x, 0x1822A);
  }

  c->d[1] = saved[1];                              /* movem.l (sp)+,d1/a0 */
  c->a[0] = saved[0];
  c->a[7] += 8;
  lift_charge_movem(x, 0x18230);

  lift_charge(x, 0x18234);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_18236 (called from sub_181F6+6)
 *   in: d0 = weight, a3 = accumulator base
 * Multiplies the accumulator in place: the five words at (a3) are
 * snapshotted onto the stack (and zeroed), then each is multiplied by
 * d0 and the 32-bit product added back at its correctly shifted
 * position ($2(a3) + 2*index), with the same `dbcc` carry ripple as
 * sub_1821A. d1-d4/a0 are restored by the movem.
 */
void sub_18236(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[5] = { c->a[0], c->d[4], c->d[3], c->d[2], c->d[1] };
  for (int i = 0; i < 5; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x18236);

  c->a[0] = SEW(c->a[3]);                          /* move.w a3,a0 */
  lift_charge(x, 0x1823A);
  c->d[4] = alu_moveql(c, 4);                      /* moveq #4,d4 */
  lift_charge(x, 0x1823C);

  for (;;)
  {
    /* loc_1823E */
    c->a[7] -= 2;                                  /* move.w (a0),-(sp) */
    lift_w16(x, c->a[7], alu_movew(c, lift_r16(x, c->a[0])));
    lift_charge(x, 0x1823E);
    lift_w16(x, c->a[0], alu_movew(c, 0));         /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0x18240);
    uint32_t nd4 = W(W(c->d[4]) - 1);              /* dbf d4,loc_1823E */
    setw(&c->d[4], nd4);
    int taken = (nd4 != 0xFFFF);
    lift_charge_dbcc(x, 0x18242, taken, !taken);
    if (!taken) break;
  }

  c->d[4] = alu_moveql(c, 4);                      /* moveq #4,d4 */
  lift_charge(x, 0x18246);

  for (;;)
  {
    /* loc_18248 */
    setw(&c->d[1], alu_movew(c, W(c->d[0])));      /* move.w d0,d1 */
    lift_charge(x, 0x18248);
    {
      uint32_t src = lift_r16(x, c->a[7]);         /* mulu.w (sp)+,d1 */
      c->a[7] += 2;
      c->d[1] = alu_mulu(c, src, c->d[1]);
      lift_charge_mulu(x, 0x1824A, src);
    }
    c->a[0] = c->a[3] + 2;                         /* lea 2(a3),a0 */
    lift_charge(x, 0x1824C);
    c->a[0] += SEW(c->d[4]);                       /* add.w d4,a0 */
    lift_charge(x, 0x18250);
    c->a[0] += SEW(c->d[4]);                       /* add.w d4,a0 */
    lift_charge(x, 0x18252);
    setw(&c->d[2], alu_movew(c, W(c->d[4])));      /* move.w d4,d2 */
    lift_charge(x, 0x18254);

    c->a[0] -= 4;                                  /* add.l d1,-(a0) */
    lift_w32(x, c->a[0], alu_addl(c, c->d[1], lift_r32(x, c->a[0])));
    lift_charge(x, 0x18256);

    lift_charge(x, 0x18258);                       /* bra.w loc_1825E */

    for (;;)
    {
      /* loc_1825E */
      int cc_true = !c->cf;                        /* dbcc d2,loc_1825C */
      int taken = 0, expired = 0;
      if (!cc_true)
      {
        uint32_t nd2 = W(W(c->d[2]) - 1);
        setw(&c->d[2], nd2);
        taken = (nd2 != 0xFFFF);
        expired = !taken;
      }
      lift_charge_dbcc(x, 0x1825E, taken, expired);
      if (!taken) break;

      /* loc_1825C */
      c->a[0] -= 2;                                /* addq.w #1,-(a0) */
      lift_w16(x, c->a[0], alu_addw(c, 1, lift_r16(x, c->a[0])));
      lift_charge(x, 0x1825C);
    }

    uint32_t nd4 = W(W(c->d[4]) - 1);              /* dbf d4,loc_18248 */
    setw(&c->d[4], nd4);
    int taken = (nd4 != 0xFFFF);
    lift_charge_dbcc(x, 0x18262, taken, !taken);
    if (!taken) break;
  }

  c->d[1] = saved[4];                              /* movem.l (sp)+,d1-d4/a0 */
  c->d[2] = saved[3];
  c->d[3] = saved[2];
  c->d[4] = saved[1];
  c->a[0] = saved[0];
  c->a[7] += 20;
  lift_charge_movem(x, 0x18266);

  lift_charge(x, 0x1826A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_18380(lift_ctx *x);
void sub_FE696(lift_ctx *x);
void SRAM_WriteBytes(lift_ctx *);      /* save.c */
void SRAM_RecalcChecksum(lift_ctx *);  /* save.c */
void Calc_HalvingAccumulator(lift_ctx *);
void sub_17D80(lift_ctx *);
void sub_1803E(lift_ctx *);

/*
 * sub_FE696 (called from sub_8928:loc_897E and sub_180FC+48)
 * Commits the season/playoff block to SRAM: writes $100 bytes from
 * $FFFFD076 to save offset $1EF6, recomputes the save checksum, clears
 * the mirrored-view flag (bit4 of $FFFFC2EC), and invalidates the
 * cached team id ($FFFFC016 = $FFFF) and record pointer ($FFFFB036 =
 * $FFFF0000). d0/d1/a0 restored by the movem.
 */
void sub_FE696(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[3] = { c->a[0], c->d[1], c->d[0] };
  for (int i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE696);

  c->d[1] = alu_movel(c, 0x100);                   /* move.l #$100,d1 */
  lift_charge(x, 0xFE69A);
  c->d[0] = alu_movel(c, 0x1EF6);                  /* move.l #$1EF6,d0 */
  lift_charge(x, 0xFE6A0);
  c->a[0] = 0xFFFFD076u;                           /* move.l #$FFFFD076,a0 */
  lift_charge(x, 0xFE6A6);

  lift_call(x, 0xFE6AC, 6, SRAM_WriteBytes);       /* jsr SRAM_WriteBytes */
  if (x->declined) return;
  lift_call(x, 0xFE6B2, 6, SRAM_RecalcChecksum);   /* jsr SRAM_RecalcChecksum */
  if (x->declined) return;

  lift_w8(x, 0xFFFFC2ECu, alu_bclr(c, lift_r8(x, 0xFFFFC2ECu), 4));  /* bclr #4 */
  lift_charge(x, 0xFE6B8);
  lift_w16(x, 0xFFFFC016u, alu_movew(c, 0xFFFF));  /* move.w #$FFFF,($FFFFC016).w */
  lift_charge(x, 0xFE6BE);
  lift_w32(x, 0xFFFFB036u, alu_movel(c, 0xFFFF0000u));  /* move.l #$FFFF0000,($FFFFB036).w */
  lift_charge(x, 0xFE6C4);

  c->d[0] = saved[2];                              /* movem.l (sp)+,d0-d1/a0 */
  c->d[1] = saved[1];
  c->a[0] = saved[0];
  c->a[7] += 12;
  lift_charge_movem(x, 0xFE6CC);

  lift_charge(x, 0xFE6D0);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_180FC (called from sub_7CB0:loc_172C8)
 * Season/playoff round advance. Bails via the shared far rts when no
 * season is active ($FFFFD04A zero), when the phase word $FFFFD048 is
 * zero, or when it has already reached 4. Otherwise it drives the
 * bracket forward (sub_18380), refreshes the tracked object state
 * (sub_17D80) and, when the round is still in progress ($FFFFCEEC != 4
 * and $FFFFCEE6 non-negative), rebuilds the odds accumulator at
 * $FFFFD088 (sub_18192) and commits the block to SRAM (sub_FE696) —
 * then, if bit2 of $FFFFC2F0 is set, rebuilds the second accumulator at
 * $FFFFD176 (sub_1803E) and tail-branches back into sub_17D80. The
 * round-over path zeroes $FFFFD088 instead and steps the phase word to
 * 2, or to 3 once $FFFFCEEA reaches 7 (the final series).
 */
void sub_180FC(lift_ctx *x)
{
  rcpu_t *c = x->c;

  static const uint32_t bail_tst[2] = { 0xFFFFD04Au, 0xFFFFD048u };
  static const uint32_t bail_at[2]  = { 0x18100, 0x18108 };
  for (int i = 0; i < 2; i++)
  {
    alu_tstw(c, lift_r16(x, bail_tst[i]));         /* tst.w (abs).w */
    lift_charge(x, i ? 0x18104 : 0x180FC);
    int z = c->zf;
    lift_charge_bcc(x, bail_at[i], z);             /* beq.w locret_15464 */
    if (z)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  alu_cmpw(c, 4, lift_r16(x, 0xFFFFD048u));        /* cmp.w #4,($FFFFD048).w */
  lift_charge(x, 0x1810C);
  {
    int eq = c->zf;
    lift_charge_bcc(x, 0x18112, eq);               /* beq.w locret_15464 */
    if (eq)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_w16(x, 0xFFFFD048u, alu_movew(c, 1));       /* move.w #1,($FFFFD048).w */
  lift_charge(x, 0x18116);
  lift_w16(x, 0xFFFFD074u, alu_movew(c, 1));       /* move.w #1,($FFFFD074).w */
  lift_charge(x, 0x1811C);

  lift_call(x, 0x18122, 4, sub_18380);             /* bsr.w sub_18380 */
  if (x->declined) return;
  lift_call(x, 0x18126, 4, sub_17D80);             /* bsr.w sub_17D80 */
  if (x->declined) return;

  int roundOver;
  alu_cmpw(c, 4, lift_r16(x, 0xFFFFCEECu));        /* cmp.w #4,($FFFFCEEC).w */
  lift_charge(x, 0x1812A);
  roundOver = c->zf;
  lift_charge_bcc(x, 0x18130, roundOver);          /* beq.w loc_18160 */
  if (!roundOver)
  {
    alu_tstw(c, lift_r16(x, 0xFFFFCEE6u));         /* tst.w ($FFFFCEE6).w */
    lift_charge(x, 0x18134);
    roundOver = c->nf;
    lift_charge_bcc(x, 0x18138, roundOver);        /* bmi.w loc_18160 */
  }

  if (!roundOver)
  {
    c->a[3] = 0xFFFFD088u;                         /* move.w #$D088,a3 */
    lift_charge(x, 0x1813C);
    lift_call(x, 0x18140, 4, sub_18192);           /* bsr.w sub_18192 */
    if (x->declined) return;
    lift_call(x, 0x18144, 6, sub_FE696);           /* jsr sub_FE696 */
    if (x->declined) return;

    alu_btst(c, lift_r8(x, 0xFFFFC2F0u), 2);       /* btst #2,($FFFFC2F0).w */
    lift_charge(x, 0x1814A);
    int clear = c->zf;
    lift_charge_bcc(x, 0x18150, clear);            /* beq.w locret_15464 */
    if (clear)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    c->a[3] = 0xFFFFD176u;                         /* move.w #$D176,a3 */
    lift_charge(x, 0x18154);
    lift_call(x, 0x18158, 4, sub_1803E);           /* bsr.w sub_1803E */
    if (x->declined) return;

    lift_charge(x, 0x1815C);                       /* bra.w sub_17D80 */
    sub_17D80(x);
    return;
  }

  /* loc_18160 */
  c->a[3] = 0xFFFFD088u;                           /* move.w #$D088,a3 */
  lift_charge(x, 0x18160);
  lift_call(x, 0x18164, 4, sub_1820E);             /* bsr.w sub_1820E */
  if (x->declined) return;
  lift_call(x, 0x18168, 6, sub_FE696);             /* jsr sub_FE696 */
  if (x->declined) return;

  lift_w16(x, 0xFFFFD048u, alu_movew(c, 2));       /* move.w #2,($FFFFD048).w */
  lift_charge(x, 0x1816E);
  lift_w16(x, 0xFFFFD074u, alu_movew(c, 2));       /* move.w #2,($FFFFD074).w */
  lift_charge(x, 0x18174);
  alu_cmpw(c, 7, lift_r16(x, 0xFFFFCEEAu));        /* cmp.w #7,($FFFFCEEA).w */
  lift_charge(x, 0x1817A);
  {
    int eq = c->zf;
    lift_charge_bcc(x, 0x18180, eq);               /* beq.w locret_15464 */
    if (eq)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_w16(x, 0xFFFFD048u, alu_movew(c, 3));       /* move.w #3,($FFFFD048).w */
  lift_charge(x, 0x18184);
  lift_w16(x, 0xFFFFD074u, alu_movew(c, 3));       /* move.w #3,($FFFFD074).w */
  lift_charge(x, 0x1818A);

  lift_charge(x, 0x18190);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* shared 5-instruction tail of sub_18380's two exit paths: mask the
 * $FFFFCEF2 bracket-result field to d2 bits wide, OR d3 in at that
 * shift, and step the round counter. Called with the two addresses of
 * whichever copy is executing. */
static void Fn_18380_commit(lift_ctx *x, uint32_t a0, uint32_t a1, uint32_t a2,
                            uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
  rcpu_t *c = x->c;

  c->d[1] = alu_moveql(c, 1);                      /* moveq #1,d1 */
  lift_charge(x, a0);
  {
    int cnt = (int)(c->d[2] & 63);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), cnt));  /* asl.w d2,d1 */
    lift_charge_shift_reg(x, a1, cnt);
  }
  setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));      /* subq.w #1,d1 */
  lift_charge(x, a2);
  lift_w16(x, 0xFFFFCEF2u,
           alu_andw(c, W(c->d[1]), lift_r16(x, 0xFFFFCEF2u)));  /* and.w d1,(CEF2).w */
  lift_charge(x, a3);
  {
    int cnt = (int)(c->d[2] & 63);
    setw(&c->d[3], alu_aslw(c, W(c->d[3]), cnt));  /* asl.w d2,d3 */
    lift_charge_shift_reg(x, a4, cnt);
  }
  lift_w16(x, 0xFFFFCEF2u,
           alu_orw(c, W(c->d[3]), lift_r16(x, 0xFFFFCEF2u)));   /* or.w d3,(CEF2).w */
  lift_charge(x, a5);
  lift_w16(x, 0xFFFFCEECu, alu_addw(c, 1, lift_r16(x, 0xFFFFCEECu)));  /* addq.w #1,(CEEC).w */
  lift_charge(x, a6);
}

/*
 * sub_18380 (called from sub_180FC+26)
 * Advances the playoff bracket one game. The 8 series records live at
 * $FFFFCE66 ($10 stride); Calc_HalvingAccumulator returns d1 = the
 * record index limit and d2 = the bit shift for this round's slot in
 * the $FFFFCEF2 result field. It first posts the two current-game
 * scores ($FFFFC6DA / $FFFFCA3E) into the active record's +$A/+$C.
 * With $FFFFCEEA already at 7 (final series) it just tallies each
 * record's winner bit into d3 and commits. Otherwise it credits a win
 * to the leading side of each undecided record, bumps the games-played
 * counter, and — unless the active record has reached 4 wins, or the
 * round counter is already at 3 — simulates the remaining series with
 * Rng_NextScaled coin flips, rebuilds the $FFFFD176 odds accumulator
 * (sub_18192) and sets bit2 of $FFFFC2F0. Both exits clear the
 * per-record scores and commit the round's result bits.
 */
void sub_18380(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFCEE6u)));  /* move.w (CEE6).w,d0 */
  lift_charge(x, 0x18380);
  c->d[0] = alu_mulu(c, 0x10, c->d[0]);            /* mulu.w #$10,d0 */
  lift_charge_mulu(x, 0x18384, 0x10);
  c->a[0] = 0xFFFFCE66u;                           /* move.w #$CE66,a0 */
  lift_charge(x, 0x18388);
  lift_w16(x, c->a[0] + SEW(c->d[0]) + 0xA,
           alu_movew(c, lift_r16(x, 0xFFFFC6DAu)));  /* move.w (C6DA).w,$A(a0,d0.w) */
  lift_charge(x, 0x1838C);
  lift_w16(x, c->a[0] + SEW(c->d[0]) + 0xC,
           alu_movew(c, lift_r16(x, 0xFFFFCA3Eu)));  /* move.w (CA3E).w,$C(a0,d0.w) */
  lift_charge(x, 0x18392);

  lift_call(x, 0x18398, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */
  if (x->declined) return;

  c->a[0] = 0xFFFFCE66u;                           /* move.w #$CE66,a0 */
  lift_charge(x, 0x1839C);
  c->d[3] = alu_moveql(c, 0x10);                   /* moveq #$10,d3 */
  lift_charge(x, 0x183A0);
  c->d[3] = alu_mulu(c, W(c->d[1]), c->d[3]);      /* mulu.w d1,d3 */
  lift_charge_mulu(x, 0x183A2, W(c->d[1]));
  c->a[0] += SEW(c->d[3]);                         /* add.w d3,a0 */
  lift_charge(x, 0x183A4);

  alu_cmpw(c, 7, lift_r16(x, 0xFFFFCEEAu));        /* cmp.w #7,($FFFFCEEA).w */
  lift_charge(x, 0x183A6);
  {
    int final = c->zf;
    lift_charge_bcc(x, 0x183AC, final);            /* beq.w loc_184D0 */
    if (final)
    {
      /* loc_184D0 */
      setw(&c->d[3], alu_movew(c, 0));             /* clr.w d3 */
      lift_charge(x, 0x184D0);

      for (;;)
      {
        /* loc_184D2 */
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0xA)));  /* move.w $A(a0),d0 */
        lift_charge(x, 0x184D2);
        alu_cmpw(c, lift_r16(x, c->a[0] + 0xC), W(c->d[0]));       /* cmp.w $C(a0),d0 */
        lift_charge(x, 0x184D6);
        int hi = !c->cf && !c->zf;
        lift_charge_bcc(x, 0x184DA, hi);           /* bhi.w loc_184E0 */
        if (!hi)
        {
          c->d[3] = alu_bset(c, c->d[3], (int)(c->d[1] & 31));     /* bset d1,d3 */
          lift_charge_bitop_reg(x, 0x184DE, c->d[1]);
        }

        /* loc_184E0 */
        alu_btst(c, lift_r8(x, c->a[0] + 0xE), 0);  /* btst #0,$E(a0) */
        lift_charge(x, 0x184E0);
        int clear = c->zf;
        lift_charge_bcc(x, 0x184E6, clear);         /* beq.w loc_184EC */
        if (!clear)
        {
          c->d[3] = alu_bchg(c, c->d[3], (int)(c->d[1] & 31));     /* bchg d1,d3 */
          lift_charge_bitop_reg(x, 0x184EA, c->d[1]);
        }

        /* loc_184EC */
        c->a[0] -= 0x10;                            /* suba.w #$10,a0 */
        lift_charge(x, 0x184EC);
        uint32_t nd1 = W(W(c->d[1]) - 1);           /* dbf d1,loc_184D2 */
        setw(&c->d[1], nd1);
        int taken = (nd1 != 0xFFFF);
        lift_charge_dbcc(x, 0x184F0, taken, !taken);
        if (!taken) break;
      }

      Fn_18380_commit(x, 0x184F4, 0x184F6, 0x184F8, 0x184FA, 0x184FE, 0x18500, 0x18504);
      lift_charge(x, 0x18508);                      /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  for (;;)
  {
    /* loc_183B0 */
    int decided;
    alu_cmpw(c, 4, lift_r16(x, c->a[0] + 4));       /* cmp.w #4,4(a0) */
    lift_charge(x, 0x183B0);
    decided = c->zf;
    lift_charge_bcc(x, 0x183B6, decided);           /* beq.w loc_183E8 */
    if (!decided)
    {
      alu_cmpw(c, 4, lift_r16(x, c->a[0] + 6));     /* cmp.w #4,6(a0) */
      lift_charge(x, 0x183BA);
      decided = c->zf;
      lift_charge_bcc(x, 0x183C0, decided);         /* beq.w loc_183E8 */
    }

    if (!decided)
    {
      setw(&c->d[3], alu_movew(c, 0));              /* clr.w d3 */
      lift_charge(x, 0x183C4);
      alu_btst(c, lift_r8(x, c->a[0] + 0xE), 0);    /* btst #0,$E(a0) */
      lift_charge(x, 0x183C6);
      int clear = c->zf;
      lift_charge_bcc(x, 0x183CC, clear);           /* beq.w loc_183D4 */
      if (!clear)
      {
        setw(&c->d[3], alu_eorw(c, 2, W(c->d[3]))); /* eor.w #2,d3 */
        lift_charge(x, 0x183D0);
      }

      /* loc_183D4 */
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0xA)));  /* move.w $A(a0),d0 */
      lift_charge(x, 0x183D4);
      setw(&c->d[0], alu_subw(c, lift_r16(x, c->a[0] + 0xC), W(c->d[0])));  /* sub.w $C(a0),d0 */
      lift_charge(x, 0x183D8);
      int pl = !c->nf;
      lift_charge_bcc(x, 0x183DC, pl);              /* bpl.w loc_183E4 */
      if (!pl)
      {
        setw(&c->d[3], alu_eorw(c, 2, W(c->d[3]))); /* eor.w #2,d3 */
        lift_charge(x, 0x183E0);
      }

      /* loc_183E4 */
      {
        uint32_t ea = c->a[0] + SEW(c->d[3]) + 4;
        lift_w16(x, ea, alu_addw(c, 1, lift_r16(x, ea)));  /* addq.w #1,4(a0,d3.w) */
        lift_charge(x, 0x183E4);
      }
    }

    /* loc_183E8 */
    c->a[0] -= 0x10;                                /* suba.w #$10,a0 */
    lift_charge(x, 0x183E8);
    uint32_t nd1 = W(W(c->d[1]) - 1);               /* dbf d1,loc_183B0 */
    setw(&c->d[1], nd1);
    int taken = (nd1 != 0xFFFF);
    lift_charge_dbcc(x, 0x183EC, taken, !taken);
    if (!taken) break;
  }

  lift_w16(x, 0xFFFFCEEAu, alu_addw(c, 1, lift_r16(x, 0xFFFFCEEAu)));  /* addq.w #1,(CEEA).w */
  lift_charge(x, 0x183F0);

  int toTail = 0;
  alu_cmpw(c, 7, lift_r16(x, 0xFFFFCEEAu));         /* cmp.w #7,($FFFFCEEA).w */
  lift_charge(x, 0x183F4);
  {
    int eq = c->zf;
    lift_charge_bcc(x, 0x183FA, eq);                /* beq.w loc_1848A */
    toTail = eq;
  }

  if (!toTail)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFCEE6u)));  /* move.w (CEE6).w,d0 */
    lift_charge(x, 0x183FE);
    c->d[0] = alu_mulu(c, 0x10, c->d[0]);           /* mulu.w #$10,d0 */
    lift_charge_mulu(x, 0x18402, 0x10);
    c->a[0] = 0xFFFFCE66u;                          /* move.w #$CE66,a0 */
    lift_charge(x, 0x18406);
    c->a[0] += SEW(c->d[0]);                        /* add.w d0,a0 */
    lift_charge(x, 0x1840A);

    int atFour;
    alu_cmpw(c, 4, lift_r16(x, c->a[0] + 4));       /* cmp.w #4,4(a0) */
    lift_charge(x, 0x1840C);
    atFour = c->zf;
    lift_charge_bcc(x, 0x18412, atFour);            /* beq.w loc_18420 */
    if (!atFour)
    {
      alu_cmpw(c, 4, lift_r16(x, c->a[0] + 6));     /* cmp.w #4,6(a0) */
      lift_charge(x, 0x18416);
      int ne = !c->zf;
      lift_charge_bcc(x, 0x1841C, ne);              /* bne.w locret_15464 */
      if (ne)
      {
        lift_charge(x, 0x15464);
        c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
        c->a[7] += 4;
        return;
      }
    }

    /* loc_18420 */
    alu_cmpw(c, 3, lift_r16(x, 0xFFFFCEECu));       /* cmp.w #3,($FFFFCEEC).w */
    lift_charge(x, 0x18420);
    int ge = (c->nf == c->vf);
    lift_charge_bcc(x, 0x18426, ge);                /* bge.w loc_1848A */
    toTail = ge;
  }

  if (!toTail)
  {
    lift_call(x, 0x1842A, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */
    if (x->declined) return;

    c->a[0] = 0xFFFFCE66u;                          /* move.w #$CE66,a0 */
    lift_charge(x, 0x1842E);
    c->d[3] = alu_moveql(c, 0x10);                  /* moveq #$10,d3 */
    lift_charge(x, 0x18432);
    c->d[3] = alu_mulu(c, W(c->d[1]), c->d[3]);     /* mulu.w d1,d3 */
    lift_charge_mulu(x, 0x18434, W(c->d[1]));
    c->a[0] += SEW(c->d[3]);                        /* add.w d3,a0 */
    lift_charge(x, 0x18436);

    for (;;)
    {
      for (int guard = 0; ; guard++)
      {
        /* loc_18438 */
        if (guard > 4096) { x->declined = 1; return; }
        int done;
        alu_cmpw(c, lift_r16(x, 0xFFFFCEE6u), W(c->d[1]));  /* cmp.w (CEE6).w,d1 */
        lift_charge(x, 0x18438);
        done = c->zf;
        lift_charge_bcc(x, 0x1843C, done);          /* beq.w loc_18474 */
        if (!done)
        {
          alu_cmpw(c, 4, lift_r16(x, c->a[0] + 4)); /* cmp.w #4,4(a0) */
          lift_charge(x, 0x18440);
          done = c->zf;
          lift_charge_bcc(x, 0x18446, done);        /* beq.w loc_18474 */
        }
        if (!done)
        {
          alu_cmpw(c, 4, lift_r16(x, c->a[0] + 6)); /* cmp.w #4,6(a0) */
          lift_charge(x, 0x1844A);
          done = c->zf;
          lift_charge_bcc(x, 0x18450, done);        /* beq.w loc_18474 */
        }
        if (done) break;

        lift_w16(x, c->a[0] + 4,
                 alu_addw(c, 1, lift_r16(x, c->a[0] + 4)));  /* addq.w #1,4(a0) */
        lift_charge(x, 0x18454);
        c->d[0] = alu_movel(c, 0xC8);               /* move.l #$C8,d0 */
        lift_charge(x, 0x18458);
        lift_call(x, 0x1845E, 6, Rng_NextScaled);   /* jsr sub_11086 */
        if (x->declined) return;
        setw(&c->d[0], alu_andw(c, 1, W(c->d[0]))); /* and.w #1,d0 */
        lift_charge(x, 0x18464);
        int z = c->zf;
        lift_charge_bcc(x, 0x18468, z);             /* beq.s loc_18438 */
        if (z) continue;

        lift_w16(x, c->a[0] + 4,
                 alu_subw(c, 1, lift_r16(x, c->a[0] + 4)));  /* subq.w #1,4(a0) */
        lift_charge(x, 0x1846A);
        lift_w16(x, c->a[0] + 6,
                 alu_addw(c, 1, lift_r16(x, c->a[0] + 6)));  /* addq.w #1,6(a0) */
        lift_charge(x, 0x1846E);
        lift_charge(x, 0x18472);                    /* bra.s loc_18438 */
      }

      /* loc_18474 */
      c->a[0] -= 0x10;                              /* suba.w #$10,a0 */
      lift_charge(x, 0x18474);
      uint32_t nd1 = W(W(c->d[1]) - 1);             /* dbf d1,loc_18438 */
      setw(&c->d[1], nd1);
      int taken = (nd1 != 0xFFFF);
      lift_charge_dbcc(x, 0x18478, taken, !taken);
      if (!taken) break;
    }

    c->a[3] = 0xFFFFD176u;                          /* move.w #$D176,a3 */
    lift_charge(x, 0x1847C);
    lift_call(x, 0x18480, 4, sub_18192);            /* bsr.w sub_18192 */
    if (x->declined) return;
    lift_w8(x, 0xFFFFC2F0u, alu_bset(c, lift_r8(x, 0xFFFFC2F0u), 2));  /* bset #2 */
    lift_charge(x, 0x18484);
  }

  /* loc_1848A */
  lift_w16(x, 0xFFFFCEEAu, alu_movew(c, 0));        /* clr.w ($FFFFCEEA).w */
  lift_charge(x, 0x1848A);
  lift_call(x, 0x1848E, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */
  if (x->declined) return;
  c->a[0] = 0xFFFFCE66u;                            /* move.w #$CE66,a0 */
  lift_charge(x, 0x18492);
  c->d[3] = alu_moveql(c, 0x10);                    /* moveq #$10,d3 */
  lift_charge(x, 0x18496);
  c->d[3] = alu_mulu(c, W(c->d[1]), c->d[3]);       /* mulu.w d1,d3 */
  lift_charge_mulu(x, 0x18498, W(c->d[1]));
  c->a[0] += SEW(c->d[3]);                          /* add.w d3,a0 */
  lift_charge(x, 0x1849A);
  setw(&c->d[3], alu_movew(c, 0));                  /* clr.w d3 */
  lift_charge(x, 0x1849C);

  for (;;)
  {
    /* loc_1849E */
    alu_cmpw(c, 4, lift_r16(x, c->a[0] + 4));       /* cmp.w #4,4(a0) */
    lift_charge(x, 0x1849E);
    int eq = c->zf;
    lift_charge_bcc(x, 0x184A4, eq);                /* beq.w loc_184AA */
    if (!eq)
    {
      c->d[3] = alu_bset(c, c->d[3], (int)(c->d[1] & 31));  /* bset d1,d3 */
      lift_charge_bitop_reg(x, 0x184A8, c->d[1]);
    }

    /* loc_184AA */
    lift_w16(x, c->a[0] + 4, alu_movew(c, 0));      /* clr.w 4(a0) */
    lift_charge(x, 0x184AA);
    lift_w16(x, c->a[0] + 6, alu_movew(c, 0));      /* clr.w 6(a0) */
    lift_charge(x, 0x184AE);
    c->a[0] -= 0x10;                                /* suba.w #$10,a0 */
    lift_charge(x, 0x184B2);
    uint32_t nd1 = W(W(c->d[1]) - 1);               /* dbf d1,loc_1849E */
    setw(&c->d[1], nd1);
    int taken = (nd1 != 0xFFFF);
    lift_charge_dbcc(x, 0x184B6, taken, !taken);
    if (!taken) break;
  }

  Fn_18380_commit(x, 0x184BA, 0x184BC, 0x184BE, 0x184C0, 0x184C4, 0x184C6, 0x184CA);
  lift_charge(x, 0x184CE);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_77E4 (called from sub_7CB0-528)
 * Clears the 228-word season/playoff accumulator block at $FFFFD092 —
 * the same bitfield region sub_1833A unpacks and sub_182A2 repacks. The
 * dbf count is an immediate ($E3), so no wrap guard is needed.
 */
void sub_77E4(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0xE3));              /* move.w #$E3,d0 */
  lift_charge(x, 0x77E4);
  c->a[0] = 0xFFFFD092u;                           /* move.w #$D092,a0 */
  lift_charge(x, 0x77E8);

  for (;;)
  {
    /* loc_77EC */
    lift_w16(x, c->a[0], alu_movew(c, 0));         /* clr.w (a0)+ */
    c->a[0] += 2;
    lift_charge(x, 0x77EC);
    uint32_t nd0 = W(W(c->d[0]) - 1);              /* dbf d0,loc_77EC */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x77EE, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x77F2);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_17572 (called from ROM:17528 and sub_17542+26)
 * Steps the horizontal scroll of the bracket / stats board. ($FFFFD5B0)
 * is the per-frame delta — zero means "not scrolling", and the routine
 * bails to the shared far rts. Otherwise the accumulated offset
 * ($FFFFD5AC) is advanced by it and clamped to +/- $70 per bracket
 * column, the column count coming from ($FFFFCEEC) capped at 3. Out of
 * range, it bails without storing. In range it commits the offset,
 * recomputes the board's screen X into ($FFFFB8AE) — biased by $100 and
 * only when the result lands in [$40,$200], else left at 0 — and, once
 * the offset lands exactly on a $70 column boundary (divs remainder
 * zero), stops the scroll by clearing ($FFFFD5B0).
 */
void sub_17572(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD5B0u)));  /* move.w (D5B0).w,d0 */
  lift_charge(x, 0x17572);
  {
    int z = c->zf;
    lift_charge_bcc(x, 0x17576, z);                /* beq.w locret_15464 */
    if (z)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFD5ACu), W(c->d[0])));  /* add.w (D5AC).w,d0 */
  lift_charge(x, 0x1757A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFCEECu)));  /* move.w (CEEC).w,d1 */
  lift_charge(x, 0x1757E);
  alu_cmpw(c, 3, W(c->d[1]));                      /* cmp.w #3,d1 */
  lift_charge(x, 0x17582);
  {
    int ls = c->cf || c->zf;
    lift_charge_bcc(x, 0x17586, ls);               /* bls.w loc_1758C */
    if (!ls)
    {
      c->d[1] = alu_moveql(c, 3);                  /* moveq #3,d1 */
      lift_charge(x, 0x1758A);
    }
  }

  /* loc_1758C */
  c->d[1] = alu_mulu(c, 0x70, c->d[1]);            /* mulu.w #$70,d1 */
  lift_charge_mulu(x, 0x1758C, 0x70);
  alu_cmpw(c, W(c->d[1]), W(c->d[0]));             /* cmp.w d1,d0 */
  lift_charge(x, 0x17590);
  {
    int gt = !c->zf && (c->nf == c->vf);
    lift_charge_bcc(x, 0x17592, gt);               /* bgt.w locret_15464 */
    if (gt)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  setw(&c->d[1], alu_negw(c, W(c->d[1])));         /* neg.w d1 */
  lift_charge(x, 0x17596);
  alu_cmpw(c, W(c->d[1]), W(c->d[0]));             /* cmp.w d1,d0 */
  lift_charge(x, 0x17598);
  {
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0x1759A, lt);               /* blt.w locret_15464 */
    if (lt)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_w16(x, 0xFFFFD5ACu, alu_movew(c, W(c->d[0])));  /* move.w d0,($FFFFD5AC).w */
  lift_charge(x, 0x1759E);
  lift_w16(x, 0xFFFFB8AEu, alu_movew(c, 0));       /* clr.w ($FFFFB8AE).w */
  lift_charge(x, 0x175A2);
  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0x175A6);
  setw(&c->d[1], alu_addw(c, 0x100, W(c->d[1])));  /* add.w #$100,d1 */
  lift_charge(x, 0x175A8);
  alu_cmpw(c, 0x40, W(c->d[1]));                   /* cmp.w #$40,d1 */
  lift_charge(x, 0x175AC);
  {
    int lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0x175B0, lt);               /* blt.w loc_175C0 */
    if (!lt)
    {
      alu_cmpw(c, 0x200, W(c->d[1]));              /* cmp.w #$200,d1 */
      lift_charge(x, 0x175B4);
      int gt = !c->zf && (c->nf == c->vf);
      lift_charge_bcc(x, 0x175B8, gt);             /* bgt.w loc_175C0 */
      if (!gt)
      {
        lift_w16(x, 0xFFFFB8AEu, alu_movew(c, W(c->d[1])));  /* move.w d1,($FFFFB8AE).w */
        lift_charge(x, 0x175BC);
      }
    }
  }

  /* loc_175C0 */
  c->d[0] = alu_extl(c, c->d[0]);                  /* ext.l d0 */
  lift_charge(x, 0x175C0);
  lift_charge_divs(x, 0x175C2, 0x70, c->d[0]);     /* divs.w #$70,d0 */
  if (x->declined) return;
  c->d[0] = alu_divs(c, 0x70, c->d[0]);
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0x175C6);
  alu_tstw(c, W(c->d[0]));                         /* tst.w d0 */
  lift_charge(x, 0x175C8);
  {
    int nz = !c->zf;
    lift_charge_bcc(x, 0x175CA, nz);               /* bne.w locret_15464 */
    if (nz)
    {
      lift_charge(x, 0x15464);
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_w16(x, 0xFFFFD5B0u, alu_movew(c, 0));       /* clr.w ($FFFFD5B0).w */
  lift_charge(x, 0x175CE);

  lift_charge(x, 0x175D2);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FEE60 (called from sub_B0E8+678 and +728; a3 = on-ice object,
 * d4 selects which counter pair)
 * Bumps a pair of long event counters, gated on bit0 of $FFFFC2EA (the
 * "stats frozen" flag — set means do nothing). d4 picks the pair at
 * $FFFFDEA4 (zero) or $FFFFDEAC (non-zero). The first long always
 * increments; the second only when the pending event code ($FFFFBF12)
 * is neither 8 nor the object's own facing byte $54(a3) — i.e. the
 * event is not self-inflicted. d1-d7/a0 are restored by the movem.
 */
void sub_FEE60(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[8] = { c->a[0], c->d[7], c->d[6], c->d[5],
                        c->d[4], c->d[3], c->d[2], c->d[1] };
  for (int i = 0; i < 8; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFEE60);

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 0);         /* btst #0,($FFFFC2EA).w */
  lift_charge(x, 0xFEE64);
  int frozen = !c->zf;
  lift_charge_bcc(x, 0xFEE6A, frozen);             /* bne.w loc_FEE9A */

  if (!frozen)
  {
    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFBF12u)));  /* move.w (BF12).w,d1 */
    lift_charge(x, 0xFEE6E);
    c->a[0] = 0xFFFFDEA4u;                         /* move.l #$FFFFDEA4,a0 */
    lift_charge(x, 0xFEE72);
    alu_tstw(c, W(c->d[4]));                       /* tst.w d4 */
    lift_charge(x, 0xFEE78);
    int z = c->zf;
    lift_charge_bcc(x, 0xFEE7A, z);                /* beq.w loc_FEE84 */
    if (!z)
    {
      c->a[0] = 0xFFFFDEACu;                       /* move.l #$FFFFDEAC,a0 */
      lift_charge(x, 0xFEE7E);
    }

    /* loc_FEE84 */
    lift_w32(x, c->a[0], alu_addl(c, 1, lift_r32(x, c->a[0])));  /* addq.l #1,(a0) */
    lift_charge(x, 0xFEE84);
    alu_cmpw(c, 8, W(c->d[1]));                    /* cmp.w #8,d1 */
    lift_charge(x, 0xFEE86);
    int skip = c->zf;
    lift_charge_bcc(x, 0xFEE8A, skip);             /* beq.w loc_FEE9A */
    if (!skip)
    {
      alu_cmpw(c, lift_r16(x, c->a[3] + 0x54), W(c->d[1]));  /* cmp.w $54(a3),d1 */
      lift_charge(x, 0xFEE8E);
      skip = c->zf;
      lift_charge_bcc(x, 0xFEE92, skip);           /* beq.w loc_FEE9A */
      if (!skip)
      {
        lift_w32(x, c->a[0] + 4,
                 alu_addl(c, 1, lift_r32(x, c->a[0] + 4)));  /* addq.l #1,4(a0) */
        lift_charge(x, 0xFEE96);
      }
    }
  }

  /* loc_FEE9A */
  c->d[1] = saved[7];                              /* movem.l (sp)+,d1-a0 */
  c->d[2] = saved[6];
  c->d[3] = saved[5];
  c->d[4] = saved[4];
  c->d[5] = saved[3];
  c->d[6] = saved[2];
  c->d[7] = saved[1];
  c->a[0] = saved[0];
  c->a[7] += 32;
  lift_charge_movem(x, 0xFEE9A);

  lift_charge(x, 0xFEE9E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_878E (called from ROM:8316 and ROM:loc_8812 — the Line Editor's
 * entry and its redraw path)
 * Clears the two selection-cursor words the editor uses ($FFFFBD82 and
 * $FFFFBDA2), so nothing is "picked up" when the screen is (re)entered.
 */
void sub_878E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFBD82u, alu_movew(c, 0));       /* clr.w ($FFFFBD82).w */
  lift_charge(x, 0x878E);
  lift_w16(x, 0xFFFFBDA2u, alu_movew(c, 0));       /* clr.w ($FFFFBDA2).w */
  lift_charge(x, 0x8792);

  lift_charge(x, 0x8796);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_9F7E (called from ROM:8394, sub_88C8+10 and others; a2 = team block)
 *   out: d0 = the roster header's high nibble of byte +3
 * Follows the team block's roster pointer ($1E(a2)), steps to the entry
 * its +8 word selects, and returns the top nibble of that entry's byte
 * +3 — the per-line player-count/format field the Line Editor reads.
 * a0 is saved and restored by the movem, so only d0 is an output.
 */
void sub_9F7E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved_a0 = c->a[0];                     /* movem.l a0,-(sp) */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  lift_charge_movem(x, 0x9F7E);

  c->a[0] = lift_r32(x, c->a[2] + 0x1E);           /* move.l $1E(a2),a0 */
  lift_charge(x, 0x9F82);
  c->a[0] += SEW(lift_r16(x, c->a[0] + 8));        /* add.w 8(a0),a0 */
  lift_charge(x, 0x9F86);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 3)));  /* move.b 3(a0),d0 */
  lift_charge(x, 0x9F8A);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));      /* lsr.w #4,d0 */
  lift_charge(x, 0x9F8E);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));    /* and.w #$F,d0 */
  lift_charge(x, 0x9F90);

  c->a[0] = saved_a0;                              /* movem.l (sp)+,a0 */
  c->a[7] += 4;
  lift_charge_movem(x, 0x9F94);

  lift_charge(x, 0x9F98);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_8898 (called from ROM:84C6 and sub_88C8+54; a2 = team block)
 *   in: d0 = the player being assigned, d2 = the target slot index
 * Writes player d0 into slot d2 of the line table at $16A(a2), keeping
 * the line free of duplicates: within d2's own 8-slot line (d1 masked to
 * $FFF8) the six entries at 1(a1,d1.w) are scanned with a `dbeq` for one
 * already holding d0, and if found it is overwritten with whoever
 * currently occupies the destination slot — i.e. the two players are
 * SWAPPED rather than duplicated. If d0 is not already in the line the
 * scan runs out (dbeq expires, Z clear) and the store is skipped.
 * d0-d2/a0-a1 are restored by the movem.
 */
void sub_8898(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[5] = { c->a[1], c->a[0], c->d[2], c->d[1], c->d[0] };
  for (int i = 0; i < 5; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x8898);

  c->a[0] = c->a[2] + 0x16A;                       /* lea $16A(a2),a0 */
  lift_charge(x, 0x889C);
  setw(&c->d[1], alu_movew(c, W(c->d[2])));        /* move.w d2,d1 */
  lift_charge(x, 0x88A0);
  setw(&c->d[1], alu_andw(c, 0xFFF8, W(c->d[1]))); /* and.w #$FFF8,d1 */
  lift_charge(x, 0x88A2);
  c->a[1] = c->a[0] + SEW(c->d[1]);                /* lea (a0,d1.w),a1 */
  lift_charge(x, 0x88A6);
  c->d[1] = alu_moveql(c, 5);                      /* moveq #5,d1 */
  lift_charge(x, 0x88AA);

  for (;;)
  {
    /* loc_88AC */
    alu_cmpb(c, lift_r8(x, c->a[1] + SEW(c->d[1]) + 1), c->d[0]);  /* cmp.b 1(a1,d1.w),d0 */
    lift_charge(x, 0x88AC);

    int cc_true = c->zf;                           /* dbeq d1,loc_88AC */
    int taken = 0, expired = 0;
    if (!cc_true)
    {
      uint32_t nd1 = W(W(c->d[1]) - 1);
      setw(&c->d[1], nd1);
      taken = (nd1 != 0xFFFF);
      expired = !taken;
    }
    lift_charge_dbcc(x, 0x88B0, taken, expired);
    if (!taken) break;
  }

  {
    int notFound = !c->zf;                         /* bne.w loc_88BE */
    lift_charge_bcc(x, 0x88B4, notFound);
    if (!notFound)
    {
      lift_w8(x, c->a[1] + SEW(c->d[1]) + 1,
              alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2]))));  /* move.b (a0,d2.w),1(a1,d1.w) */
      lift_charge(x, 0x88B8);
    }
  }

  /* loc_88BE */
  lift_w8(x, c->a[0] + SEW(c->d[2]), alu_moveb(c, c->d[0]));  /* move.b d0,(a0,d2.w) */
  lift_charge(x, 0x88BE);

  c->d[0] = saved[4];                              /* movem.l (sp)+,d0-d2/a0-a1 */
  c->d[1] = saved[3];
  c->d[2] = saved[2];
  c->a[0] = saved[1];
  c->a[1] = saved[0];
  c->a[7] += 20;
  lift_charge_movem(x, 0x88C2);

  lift_charge(x, 0x88C6);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_88C8 — "Load Team Line" (line-editor menu entry, ROM:$19FF0)
 *
 * The inverse of sub_8928 below. The saved lines live in work RAM at
 * $FFFFD076: one team byte, then the 33 slot assignments packed two to
 * a byte (low nibble first) at $FFFFD077 onward. a0 starts one past the
 * team byte; d4 is the nibble phase, toggled by `bchg #0,d4` so that the
 * even iteration peeks the low nibble without advancing and the odd one
 * consumes the byte and takes the high nibble.
 *
 * Each nibble is a player index relative to a per-team base: d1 =
 * Roster_CountLeadingNibbles (sub_9F40) is added always, and d3 =
 * sub_9F7E is added as well for the first three slots of every group of
 * eight (d2 & 7 <= 2 — the forwards), after which +1 makes it 1-based.
 * The result is written through sub_8898, which handles the
 * duplicate-swap. The slot indices come from the 33-entry table at
 * $898A, terminated by the $FF that trips the `bmi`.
 *
 * d0-d4/a0/a3 are restored by the movem; a1/a2 are the caller's.
 */
void sub_88C8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d4/a0/a3,-(sp) — pushed a3,a0,d4,d3,d2,d1,d0 */
  uint32_t saved[7] = { c->a[3], c->a[0], c->d[4], c->d[3],
                        c->d[2], c->d[1], c->d[0] };
  for (int i = 0; i < 7; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x88C8);

  c->a[0] = SEW(0xD076);                           /* movea.w #$D076,a0 */
  lift_charge(x, 0x88CC);
  c->a[0] += 1;                                    /* addq.w #1,a0 (full An) */
  lift_charge(x, 0x88D0);

  lift_call(x, 0x88D2, 4, Roster_CountLeadingNibbles);  /* bsr.w sub_9F40 */
  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0x88D6);
  lift_call(x, 0x88D8, 4, sub_9F7E);               /* bsr.w sub_9F7E */
  setw(&c->d[3], alu_movew(c, W(c->d[0])));        /* move.w d0,d3 */
  lift_charge(x, 0x88DC);

  c->a[3] = 0x898A;                                /* movea.l #$898A,a3 */
  lift_charge(x, 0x88DE);
  setw(&c->d[4], alu_movew(c, 0));                 /* clr.w d4 */
  lift_charge(x, 0x88E4);
  setw(&c->d[2], alu_movew(c, 0));                 /* clr.w d2 */
  lift_charge(x, 0x88E6);

  for (;;)
  {
    /* loc_88E8 */
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[3])));  /* move.b (a3),d2 */
    lift_charge(x, 0x88E8);

    {
      int mi = c->nf;                              /* bmi.w loc_8922 */
      lift_charge_bcc(x, 0x88EA, mi);
      if (mi) break;
    }

    c->d[4] = alu_bchg(c, c->d[4], 0);             /* bchg #0,d4 */
    lift_charge(x, 0x88EE);

    {
      int ne = !c->zf;                             /* bne.w loc_88FC */
      lift_charge_bcc(x, 0x88F2, ne);
      if (!ne)
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0),d0 */
        lift_charge(x, 0x88F6);
        lift_charge(x, 0x88F8);                    /* bra.w loc_8900 */
      }
      else
      {
        /* loc_88FC */
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,d0 */
        c->a[0] += 1;
        lift_charge(x, 0x88FC);
        setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 4));         /* lsr.w #4,d0 */
        lift_charge(x, 0x88FE);                             /* immediate count: kind 1 */
      }
    }

    /* loc_8900 */
    setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));  /* and.w #$F,d0 */
    lift_charge(x, 0x8900);
    setb(&c->d[0], alu_addb(c, c->d[1] & 0xFF, c->d[0] & 0xFF));  /* add.b d1,d0 */
    lift_charge(x, 0x8904);
    setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));    /* and.w #7,d2 */
    lift_charge(x, 0x8906);
    alu_cmpw(c, 2, W(c->d[2]));                    /* cmp.w #2,d2 */
    lift_charge(x, 0x890A);

    {
      int gt = (!c->zf) && (c->nf == c->vf);       /* bgt.w loc_8914 */
      lift_charge_bcc(x, 0x890E, gt);
      if (!gt)
      {
        setb(&c->d[0], alu_addb(c, c->d[3] & 0xFF, c->d[0] & 0xFF));  /* add.b d3,d0 */
        lift_charge(x, 0x8912);
      }
    }

    /* loc_8914 */
    setb(&c->d[0], alu_addb(c, 1, c->d[0] & 0xFF));   /* addq.b #1,d0 */
    lift_charge(x, 0x8914);
    setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));    /* and.w #$FF,d0 */
    lift_charge(x, 0x8916);
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[3])));  /* move.b (a3)+,d2 */
    c->a[3] += 1;
    lift_charge(x, 0x891A);

    lift_call(x, 0x891C, 4, sub_8898);             /* bsr.w sub_8898 */
    lift_charge(x, 0x8920);                        /* bra.s loc_88E8 */
  }

  /* loc_8922 — movem.l (sp)+,d0-d4/a0/a3 */
  c->d[0] = saved[6];
  c->d[1] = saved[5];
  c->d[2] = saved[4];
  c->d[3] = saved[3];
  c->d[4] = saved[2];
  c->a[0] = saved[1];
  c->a[3] = saved[0];
  c->a[7] += 28;
  lift_charge_movem(x, 0x8922);

  lift_charge(x, 0x8926);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_8928 — "Save Team Line" (line-editor menu entries $19FD8/$1A048)
 *
 * Packs the current team's 33 line slots into the save buffer that
 * sub_88C8 reads back. $FFFFD076 gets the team number ($28(a2)+1) so the
 * menu builder at ROM:$883E can tell whether the buffer belongs to the
 * team being edited (that test is what makes "Load Team Line" appear at
 * all); the nibbles then go to $FFFFD077 onward, low nibble first, again
 * phased by `bchg #0,d4`.
 *
 * Per slot the stored player at $16A(a2) is turned back into a nibble by
 * undoing sub_88C8's arithmetic: -1 for the 1-based bias, -d1
 * (sub_9F40), and -d0 (sub_9F7E) for the first three of each group of
 * eight. The even iteration writes the low nibble in place; the odd one
 * shifts left four and ORs it in, advancing a0. sub_FE696 commits the
 * result to SRAM.
 *
 * d0-d4/a0-a3 are all restored by the movem.
 */
void sub_8928(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d4/a0-a3,-(sp) — pushed a3,a2,a1,a0,d4,d3,d2,d1,d0 */
  uint32_t saved[9] = { c->a[3], c->a[2], c->a[1], c->a[0], c->d[4],
                        c->d[3], c->d[2], c->d[1], c->d[0] };
  for (int i = 0; i < 9; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x8928);

  c->a[0] = SEW(0xD076);                           /* movea.w #$D076,a0 */
  lift_charge(x, 0x892C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x28)));  /* move.w $28(a2),d0 */
  lift_charge(x, 0x8930);
  setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));      /* addq.w #1,d0 */
  lift_charge(x, 0x8934);
  lift_w8(x, c->a[0], alu_moveb(c, c->d[0]));      /* move.b d0,(a0)+ */
  c->a[0] += 1;
  lift_charge(x, 0x8936);

  c->a[1] = c->a[2] + 0x16A;                       /* lea $16A(a2),a1 */
  lift_charge(x, 0x8938);

  lift_call(x, 0x893C, 4, Roster_CountLeadingNibbles);  /* bsr.w sub_9F40 */
  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0x8940);
  lift_call(x, 0x8942, 4, sub_9F7E);               /* bsr.w sub_9F7E — result kept in d0 */

  c->a[3] = 0x898A;                                /* movea.l #$898A,a3 */
  lift_charge(x, 0x8946);
  setw(&c->d[4], alu_movew(c, 0));                 /* clr.w d4 */
  lift_charge(x, 0x894C);
  setw(&c->d[2], alu_movew(c, 0));                 /* clr.w d2 */
  lift_charge(x, 0x894E);

  for (;;)
  {
    /* loc_8950 */
    setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[3])));  /* move.b (a3)+,d2 */
    c->a[3] += 1;
    lift_charge(x, 0x8950);

    {
      int mi = c->nf;                              /* bmi.w loc_897E */
      lift_charge_bcc(x, 0x8952, mi);
      if (mi) break;
    }

    setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[2]))));  /* move.b (a1,d2.w),d3 */
    lift_charge(x, 0x8956);
    setb(&c->d[3], alu_subb(c, 1, c->d[3] & 0xFF));      /* subq.b #1,d3 */
    lift_charge(x, 0x895A);
    setb(&c->d[3], alu_subb(c, c->d[1] & 0xFF, c->d[3] & 0xFF));  /* sub.b d1,d3 */
    lift_charge(x, 0x895C);
    setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));    /* and.w #7,d2 */
    lift_charge(x, 0x895E);
    alu_cmpw(c, 2, W(c->d[2]));                    /* cmp.w #2,d2 */
    lift_charge(x, 0x8962);

    {
      int gt = (!c->zf) && (c->nf == c->vf);       /* bgt.w loc_896C */
      lift_charge_bcc(x, 0x8966, gt);
      if (!gt)
      {
        setb(&c->d[3], alu_subb(c, c->d[0] & 0xFF, c->d[3] & 0xFF));  /* sub.b d0,d3 */
        lift_charge(x, 0x896A);
      }
    }

    /* loc_896C */
    c->d[4] = alu_bchg(c, c->d[4], 0);             /* bchg #0,d4 */
    lift_charge(x, 0x896C);

    {
      int ne = !c->zf;                             /* bne.w loc_8978 */
      lift_charge_bcc(x, 0x8970, ne);
      if (!ne)
      {
        lift_w8(x, c->a[0], alu_moveb(c, c->d[3]));   /* move.b d3,(a0) */
        lift_charge(x, 0x8974);
        lift_charge(x, 0x8976);                       /* bra.s loc_8950 */
      }
      else
      {
        /* loc_8978 */
        setb(&c->d[3], alu_aslb(c, c->d[3] & 0xFF, 4));  /* asl.b #4,d3 */
        lift_charge(x, 0x8978);                          /* immediate count: kind 1 */
        lift_w8(x, c->a[0],                              /* or.b d3,(a0)+ */
                alu_moveb(c, lift_r8(x, c->a[0]) | (c->d[3] & 0xFF)));
        c->a[0] += 1;
        lift_charge(x, 0x897A);
        lift_charge(x, 0x897C);                          /* bra.s loc_8950 */
      }
    }
  }

  /* loc_897E */
  lift_call(x, 0x897E, 6, sub_FE696);              /* jsr sub_FE696 */

  c->d[0] = saved[8];                              /* movem.l (sp)+,d0-d4/a0-a3 */
  c->d[1] = saved[7];
  c->d[2] = saved[6];
  c->d[3] = saved[5];
  c->d[4] = saved[4];
  c->a[0] = saved[3];
  c->a[1] = saved[2];
  c->a[2] = saved[1];
  c->a[3] = saved[0];
  c->a[7] += 36;
  lift_charge_movem(x, 0x8984);

  lift_charge(x, 0x8988);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F6F8C (called from sub_B0E8+5942 and sub_B0E8:loc_10A34; a3 = object)
 * Bleeds the object's two velocity words $28/$2A(a3) toward zero by
 * $7D0 per call, clamping at zero rather than overshooting: a negative
 * component is pushed up by $7D0 and zeroed if that made it
 * non-negative, and a positive one is pulled down by $7D0 and zeroed if
 * that made it negative. Note the fall-through — a component that was
 * negative and stays negative after the add then also takes the
 * subtract path, so it is damped twice on that call.
 */
void sub_F6F8C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* $28(a3) */
  alu_tstw(c, lift_r16(x, c->a[3] + 0x28));        /* tst.w $28(a3) */
  lift_charge(x, 0xF6F8C);
  int pl = !c->nf;
  lift_charge_bcc(x, 0xF6F90, pl);                 /* bpl.w loc_F6FA2 */
  int skip28 = 0;
  if (!pl)
  {
    lift_w16(x, c->a[3] + 0x28,
             alu_addw(c, 0x7D0, lift_r16(x, c->a[3] + 0x28)));  /* add.w #$7D0,$28(a3) */
    lift_charge(x, 0xF6F94);
    int mi = c->nf;
    lift_charge_bcc(x, 0xF6F9A, mi);               /* bmi.w loc_F6FB0 */
    if (mi) { skip28 = 1; }
    else
    {
      lift_w16(x, c->a[3] + 0x28, alu_movew(c, 0));  /* clr.w $28(a3) */
      lift_charge(x, 0xF6F9E);
    }
  }

  if (!skip28)
  {
    /* loc_F6FA2 */
    lift_w16(x, c->a[3] + 0x28,
             alu_subw(c, 0x7D0, lift_r16(x, c->a[3] + 0x28)));  /* sub.w #$7D0,$28(a3) */
    lift_charge(x, 0xF6FA2);
    int pl2 = !c->nf;
    lift_charge_bcc(x, 0xF6FA8, pl2);              /* bpl.w loc_F6FB0 */
    if (!pl2)
    {
      lift_w16(x, c->a[3] + 0x28, alu_movew(c, 0));  /* clr.w $28(a3) */
      lift_charge(x, 0xF6FAC);
    }
  }

  /* loc_F6FB0 — same shape for $2A(a3) */
  alu_tstw(c, lift_r16(x, c->a[3] + 0x2A));        /* tst.w $2A(a3) */
  lift_charge(x, 0xF6FB0);
  int pl3 = !c->nf;
  lift_charge_bcc(x, 0xF6FB4, pl3);                /* bpl.w loc_F6FC6 */
  int done = 0;
  if (!pl3)
  {
    lift_w16(x, c->a[3] + 0x2A,
             alu_addw(c, 0x7D0, lift_r16(x, c->a[3] + 0x2A)));  /* add.w #$7D0,$2A(a3) */
    lift_charge(x, 0xF6FB8);
    int mi = c->nf;
    lift_charge_bcc(x, 0xF6FBE, mi);               /* bmi.w locret_F6FD4 */
    if (mi) { done = 1; }
    else
    {
      lift_w16(x, c->a[3] + 0x2A, alu_movew(c, 0));  /* clr.w $2A(a3) */
      lift_charge(x, 0xF6FC2);
    }
  }

  if (!done)
  {
    /* loc_F6FC6 */
    lift_w16(x, c->a[3] + 0x2A,
             alu_subw(c, 0x7D0, lift_r16(x, c->a[3] + 0x2A)));  /* sub.w #$7D0,$2A(a3) */
    lift_charge(x, 0xF6FC6);
    int pl4 = !c->nf;
    lift_charge_bcc(x, 0xF6FCC, pl4);              /* bpl.w locret_F6FD4 */
    if (!pl4)
    {
      lift_w16(x, c->a[3] + 0x2A, alu_movew(c, 0));  /* clr.w $2A(a3) */
      lift_charge(x, 0xF6FD0);
    }
  }

  /* locret_F6FD4 */
  lift_charge(x, 0xF6FD4);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F66EE (called from sub_C2F2+240; all of d0-a6 movem-saved and
 * restored, so ($FFFFB776) is the only observable output)
 * Computes the shot/pass power meter value at $FFFFB776 from the puck's
 * offset and speed. d0 is |($FFFFB75E)| folded about the rink centre
 * $108 and moved into the high word; d1 is |($FFFFB774)| — zero means
 * "not moving" and the routine writes nothing. d2 is the per-mode
 * divisor ($11, or $16 when ($FFFFD06E) is set). The scaled offset is
 * divided by the speed and then by d2, flooring at 1, and the result
 * feeds two terms: $A0000 / (d0*d2) and 3*d2*d0, the latter clamped to
 * $7FFF as a LONG compare. Their word sum is stored, or $7FFF if it
 * went negative. Every divisor is guarded — d1 by the earlier beq, d2
 * by construction, and the third by lift_charge_divu's zero decline.
 */
void sub_F66EE(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xF66EE);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB75Eu)));  /* move.w (B75E).w,d0 */
  lift_charge(x, 0xF66F2);
  {
    int pl = !c->nf;
    lift_charge_bcc(x, 0xF66F6, pl);               /* bpl.w loc_F66FC */
    if (!pl)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));     /* neg.w d0 */
      lift_charge(x, 0xF66FA);
    }
  }

  /* loc_F66FC */
  setw(&c->d[0], alu_subw(c, 0x108, W(c->d[0])));  /* sub.w #$108,d0 */
  lift_charge(x, 0xF66FC);
  {
    int pl = !c->nf;
    lift_charge_bcc(x, 0xF6700, pl);               /* bpl.w loc_F6706 */
    if (!pl)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));     /* neg.w d0 */
      lift_charge(x, 0xF6704);
    }
  }

  /* loc_F6706 */
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xF6706);
  c->d[0] = alu_andl(c, 0xFFFF0000u, c->d[0]);     /* and.l #$FFFF0000,d0 */
  lift_charge(x, 0xF6708);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB774u)));  /* move.w (B774).w,d1 */
  lift_charge(x, 0xF670E);

  int idle = c->zf;
  lift_charge_bcc(x, 0xF6712, idle);               /* beq.w loc_F6772 */
  if (!idle)
  {
    {
      int pl = !c->nf;
      lift_charge_bcc(x, 0xF6716, pl);             /* bpl.w loc_F671C */
      if (!pl)
      {
        setw(&c->d[1], alu_negw(c, W(c->d[1])));   /* neg.w d1 */
        lift_charge(x, 0xF671A);
      }
    }

    /* loc_F671C */
    setw(&c->d[2], alu_movew(c, 0x11));            /* move.w #$11,d2 */
    lift_charge(x, 0xF671C);
    alu_tstw(c, lift_r16(x, 0xFFFFD06Eu));         /* tst.w ($FFFFD06E).w */
    lift_charge(x, 0xF6720);
    {
      int z = c->zf;
      lift_charge_bcc(x, 0xF6724, z);              /* beq.w loc_F672C */
      if (!z)
      {
        setw(&c->d[2], alu_movew(c, 0x16));        /* move.w #$16,d2 */
        lift_charge(x, 0xF6728);
      }
    }

    /* loc_F672C */
    lift_charge_divu(x, 0xF672C, W(c->d[1]), c->d[0]);  /* divu.w d1,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);
    c->d[0] = alu_andl(c, 0xFFFF, c->d[0]);        /* and.l #$FFFF,d0 */
    lift_charge(x, 0xF672E);
    lift_charge_divu(x, 0xF6734, W(c->d[2]), c->d[0]);  /* divu.w d2,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, W(c->d[2]), c->d[0]);
    alu_tstw(c, W(c->d[0]));                       /* tst.w d0 */
    lift_charge(x, 0xF6736);
    {
      int nz = !c->zf;
      lift_charge_bcc(x, 0xF6738, nz);             /* bne.w loc_F6740 */
      if (!nz)
      {
        setw(&c->d[0], alu_movew(c, 1));           /* move.w #1,d0 */
        lift_charge(x, 0xF673C);
      }
    }

    /* loc_F6740 */
    c->d[1] = alu_movel(c, 0xA0000);               /* move.l #$A0000,d1 */
    lift_charge(x, 0xF6740);
    setw(&c->d[3], alu_movew(c, W(c->d[0])));      /* move.w d0,d3 */
    lift_charge(x, 0xF6746);
    c->d[0] = alu_mulu(c, W(c->d[2]), c->d[0]);    /* mulu.w d2,d0 */
    lift_charge_mulu(x, 0xF6748, W(c->d[2]));
    lift_charge_divu(x, 0xF674A, W(c->d[0]), c->d[1]);  /* divu.w d0,d1 */
    if (x->declined) return;
    c->d[1] = alu_divu(c, W(c->d[0]), c->d[1]);
    setw(&c->d[4], alu_movew(c, W(c->d[2])));      /* move.w d2,d4 */
    lift_charge(x, 0xF674C);
    setw(&c->d[2], alu_addw(c, W(c->d[2]), W(c->d[2])));  /* add.w d2,d2 */
    lift_charge(x, 0xF674E);
    setw(&c->d[2], alu_addw(c, W(c->d[4]), W(c->d[2])));  /* add.w d4,d2 */
    lift_charge(x, 0xF6750);
    c->d[2] = alu_mulu(c, W(c->d[3]), c->d[2]);    /* mulu.w d3,d2 */
    lift_charge_mulu(x, 0xF6752, W(c->d[3]));
    alu_cmpl(c, 0x7FFF, c->d[2]);                  /* cmp.l #$7FFF,d2 */
    lift_charge(x, 0xF6754);
    {
      int lt = (c->nf != c->vf);
      lift_charge_bcc(x, 0xF675A, lt);             /* blt.w loc_F6762 */
      if (!lt)
      {
        setw(&c->d[2], alu_movew(c, 0x7FFF));      /* move.w #$7FFF,d2 */
        lift_charge(x, 0xF675E);
      }
    }

    /* loc_F6762 */
    setw(&c->d[1], alu_addw(c, W(c->d[2]), W(c->d[1])));  /* add.w d2,d1 */
    lift_charge(x, 0xF6762);
    alu_tstw(c, W(c->d[1]));                       /* tst.w d1 */
    lift_charge(x, 0xF6764);
    {
      int pl = !c->nf;
      lift_charge_bcc(x, 0xF6766, pl);             /* bpl.w loc_F676E */
      if (!pl)
      {
        setw(&c->d[1], alu_movew(c, 0x7FFF));      /* move.w #$7FFF,d1 */
        lift_charge(x, 0xF676A);
      }
    }

    /* loc_F676E */
    lift_w16(x, 0xFFFFB776u, alu_movew(c, W(c->d[1])));  /* move.w d1,($FFFFB776).w */
    lift_charge(x, 0xF676E);
  }

  /* loc_F6772 */
  c->d[0] = saved[14]; c->d[1] = saved[13]; c->d[2] = saved[12];
  c->d[3] = saved[11]; c->d[4] = saved[10]; c->d[5] = saved[9];
  c->d[6] = saved[8];  c->d[7] = saved[7];  c->a[0] = saved[6];
  c->a[1] = saved[5];  c->a[2] = saved[4];  c->a[3] = saved[3];
  c->a[4] = saved[2];  c->a[5] = saved[1];  c->a[6] = saved[0];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xF6772);

  lift_charge(x, 0xF6776);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* nullsub_4 ($CBE0; DATA-XREF'd from the $18DCC object-script table) —
 * a bare rts, the table's no-op entry. */
void nullsub_4(lift_ctx *x)
{
  rcpu_t *c = x->c;
  lift_charge(x, 0xCBE0);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FEC98 (called from sub_144AC+52; a2 = team block)
 *   out: d0 = $74(a2) >> 2, zero-extended from a byte
 */
void sub_FEC98(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));                 /* clr.w d0 */
  lift_charge(x, 0xFEC98);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + 0x74)));  /* move.b $74(a2),d0 */
  lift_charge(x, 0xFEC9A);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));      /* lsr.w #2,d0 */
  lift_charge(x, 0xFEC9E);

  lift_charge(x, 0xFECA0);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FECA2 (called from sub_144AC+84; a2 = team block)
 *   out: Z clear if bit1 of $74(a2) is set — the caller branches on it.
 * btst sets Z only; no other state changes.
 */
void sub_FECA2(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, c->a[2] + 0x74), 1);      /* btst #1,$74(a2) */
  lift_charge(x, 0xFECA2);

  lift_charge(x, 0xFECA8);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FCA1E (called from ROM:FC658 — the Record Holders screen's entry)
 * Same two-word cursor reset as sub_878E, for the records screen.
 */
void sub_FCA1E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFBD82u, alu_movew(c, 0));       /* clr.w ($FFFFBD82).w */
  lift_charge(x, 0xFCA1E);
  lift_w16(x, 0xFFFFBDA2u, alu_movew(c, 0));       /* clr.w ($FFFFBDA2).w */
  lift_charge(x, 0xFCA22);

  lift_charge(x, 0xFCA26);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FDD92 (called from sub_FDD1A+3E — the Period Stats screen)
 *   out: d3 = ($FFFFD5AE) >> 4, the period index
 * Ends with bra.w to the bare rts at locret_FDDA8 (charge + pop), the
 * shared tail it uses with sub_FDD9C.
 */
void sub_FDD92(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFD5AEu)));  /* move.w (D5AE).w,d3 */
  lift_charge(x, 0xFDD92);
  setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 4));      /* lsr.w #4,d3 */
  lift_charge(x, 0xFDD96);
  lift_charge(x, 0xFDD98);                         /* bra.w locret_FDDA8 */

  lift_charge(x, 0xFDDA8);                         /* locret_FDDA8: rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TestTeamSideFlag (sub_E274)
 * Returns, in Z only, whether the global "rink sides swapped" bit
 * (bit 6 of $FFFFC2EE) agrees with the object's own away-team bit
 * (bit 6 of $62(a3)).
 *
 * The ROM computes it by differencing two SR reads:
 *   btst #6,($C2EE).w / move sr,d0 / btst #6,$62(a3) / move sr,d1
 *   eor.w d1,d0 / move d0,ccr
 * Nothing between the two reads can change any SR bit except Z (btst
 * writes only Z; an interrupt restores SR on rte), so the eor cancels
 * every unknown upper bit algebraically: the CCR that lands is exactly
 * X=N=V=C=0, Z = bit6($C2EE) XOR bit6($62(a3)). d0/d1 carry the raw SR
 * values but are movem-restored, so no unmodelled SR bit escapes.
 */
void Object_TestTeamSideFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int b_global, b_object;

  c->a[7] -= 8;                                   /* movem.l d0-d1,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_charge_movem(x, 0x0E274);

  b_global = (lift_r8(x, 0xFFC2EE) >> 6) & 1;     /* btst #6,($C2EE).w */
  alu_btst(c, lift_r8(x, 0xFFC2EE), 6);
  lift_charge(x, 0x0E278);
  lift_charge(x, 0x0E27E);                        /* move sr,d0 */
  b_object = (lift_r8(x, c->a[3] + 0x62) >> 6) & 1;  /* btst #6,$62(a3) */
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 6);
  lift_charge(x, 0x0E280);
  lift_charge(x, 0x0E286);                        /* move sr,d1 */
  lift_charge(x, 0x0E288);                        /* eor.w d1,d0 */
  lift_charge(x, 0x0E28A);                        /* move d0,ccr */
  c->xf = 0; c->nf = 0; c->vf = 0; c->cf = 0;
  c->zf = (uint32_t)(b_global ^ b_object);

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0x0E28C);
  lift_charge(x, 0x0E290);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_TestFacingAgainstSide (sub_E264)
 * When the "use the per-object side test" bit (bit 5 of $FFFFC2EE) is
 * set, branch-tail into Object_TestTeamSideFlag (it returns to OUR
 * caller — no return address is pushed). Otherwise the answer is just
 * that bit's own test with Z inverted (`eor #4,ccr` flips Z and touches
 * nothing else).
 */
void Object_TestFacingAgainstSide(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFC2EE), 5);           /* btst #5,($C2EE).w */
  lift_charge(x, 0x0E264);
  lift_charge_bcc(x, 0x0E26A, !c->zf);            /* bne.w sub_E274 */
  if (!c->zf)
  {
    Object_TestTeamSideFlag(x);                   /* branch tail: its rts is ours */
    return;
  }
  c->zf ^= 1;                                     /* eor #4,ccr — Z flip only */
  lift_charge(x, 0x0E26E);
  lift_charge(x, 0x0E272);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 46, the sub_B0E8 branch leaves ---- */

/*
 * Aim_ClampDeltaToSpan (sub_B5D8)
 *   in: d1 = a signed delta, d3 = the amount to take off it,
 *       a3 = the on-ice object
 * When the object's camera zone ($52) already matches the live one
 * ($B7AA), zero d0. Then clamp d1 into [$FEFD, $103] (+/-259, the rink
 * span) and subtract d3.
 */
void Aim_ClampDeltaToSpan(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFB7AA)));  /* move.w ($B7AA).w,d2 */
  lift_charge(x, 0xB5D8);
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x52), W(c->d[2]));   /* cmp.w $52(a3),d2 */
  lift_charge(x, 0xB5DC);
  t = !c->zf;
  lift_charge_bcc(x, 0xB5E0, t);                  /* bne.w loc_B5E6 */
  if (!t)
  {
    setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
    lift_charge(x, 0xB5E4);
  }

  /* loc_B5E6 */
  alu_cmpw(c, 0x103, W(c->d[1]));                 /* cmp.w #$103,d1 */
  lift_charge(x, 0xB5E6);
  t = (c->nf != c->vf);
  lift_charge_bcc(x, 0xB5EA, t);                  /* blt.w loc_B5F2 */
  if (!t)
  {
    setw(&c->d[1], alu_movew(c, 0x103));          /* move.w #$103,d1 */
    lift_charge(x, 0xB5EE);
  }

  /* loc_B5F2 */
  alu_cmpw(c, 0xFEFD, W(c->d[1]));                /* cmp.w #$FEFD,d1 */
  lift_charge(x, 0xB5F2);
  t = (!c->zf && (c->nf == c->vf));
  lift_charge_bcc(x, 0xB5F6, t);                  /* bgt.w loc_B5FE */
  if (!t)
  {
    setw(&c->d[1], alu_movew(c, 0xFEFD));         /* move.w #$FEFD,d1 */
    lift_charge(x, 0xB5FA);
  }

  /* loc_B5FE */
  setw(&c->d[1], alu_subw(c, W(c->d[3]), W(c->d[1])));  /* sub.w d3,d1 */
  lift_charge(x, 0xB5FE);
  lift_charge(x, 0xB600);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Event_LatchFacingAndSetFlags (sub_BC06)
 *   in: a3 = the on-ice object
 * Latch the object's facing ($54, masked to 0-7) into $BEDC. When
 * $C2F2 bit 2 is set, claim the event: clear $C2FA bit 2, set $C2F2
 * bit 5 — and if that bit was ALREADY set, bail out through the far
 * bare rts locret_B868 without touching anything else. Otherwise clear
 * $C2FA bit 5 and arm the $C31C timer at $64. Either way $C2EC bit 2
 * ends up set.
 */
void Event_LatchFacingAndSetFlags(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  {
    uint32_t v = lift_r16(x, c->a[3] + 0x54);     /* move.w $54(a3),($BEDC).w */
    alu_movew(c, v);
    lift_w16(x, 0xFFFFBEDC, v);
    lift_charge(x, 0xBC06);
  }
  lift_w16(x, 0xFFFFBEDC,                          /* and.w #7,($BEDC).w */
           alu_andw(c, 7, lift_r16(x, 0xFFFFBEDC)));
  lift_charge(x, 0xBC0C);

  alu_btst(c, lift_r8(x, 0xFFFFC2F2), 2);         /* btst #2,($C2F2).w — byte */
  lift_charge(x, 0xBC12);
  t = c->zf;
  lift_charge_bcc(x, 0xBC18, t);                  /* beq.w loc_BC38 */
  if (!t)
  {
    lift_w8(x, 0xFFFFC2FA,                        /* bclr #2,($C2FA).w */
            alu_bclr(c, lift_r8(x, 0xFFFFC2FA), 2));
    lift_charge(x, 0xBC1C);
    lift_w8(x, 0xFFFFC2F2,                        /* bset #5,($C2F2).w */
            alu_bset(c, lift_r8(x, 0xFFFFC2F2), 5));
    lift_charge(x, 0xBC22);
    t = !c->zf;                                   /* the bit was already set */
    lift_charge_bcc(x, 0xBC28, t);                /* bne.w locret_B868 */
    if (t)
    {
      lift_charge(x, 0xB868);                     /* the far bare rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    lift_w8(x, 0xFFFFC2FA,                        /* bclr #5,($C2FA).w */
            alu_bclr(c, lift_r8(x, 0xFFFFC2FA), 5));
    lift_charge(x, 0xBC2C);
    lift_w16(x, 0xFFFFC31C, alu_movew(c, 0x64));  /* move.w #$64,($C31C).w */
    lift_charge(x, 0xBC32);
  }

  /* loc_BC38 */
  lift_w8(x, 0xFFFFC2EC,                          /* bset #2,($C2EC).w */
          alu_bset(c, lift_r8(x, 0xFFFFC2EC), 2));
  lift_charge(x, 0xBC38);
  lift_charge(x, 0xBC3E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Camera_CapZoneLevel (sub_FE1AA)
 *   in: a3 = the on-ice object
 * Cap $C31A at 2 — but skip the cap entirely when $C32A is set AND the
 * object's camera zone ($52) differs from the live one ($B7AA). d0 is
 * preserved.
 */
void Camera_CapZoneLevel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[7] -= 4;                                   /* movem.l d0,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge_movem(x, 0xFE1AA);

  alu_tstw(c, lift_r16(x, 0xFFFFC32A));           /* tst.w ($C32A).w */
  lift_charge(x, 0xFE1AE);
  t = c->zf;
  lift_charge_bcc(x, 0xFE1B2, t);                 /* beq.w loc_FE1C2 */
  if (!t)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x52)));  /* move.w $52(a3),d0 */
    lift_charge(x, 0xFE1B6);
    alu_cmpw(c, lift_r16(x, 0xFFFFB7AA), W(c->d[0]));  /* cmp.w ($B7AA).w,d0 */
    lift_charge(x, 0xFE1BA);
    t = !c->zf;
    lift_charge_bcc(x, 0xFE1BE, t);               /* bne.w loc_FE1D2 */
    if (t) goto epilogue;
  }

  /* loc_FE1C2 */
  alu_cmpw(c, 2, lift_r16(x, 0xFFFFC31A));        /* cmp.w #2,($C31A).w */
  lift_charge(x, 0xFE1C2);
  t = (c->zf || (c->nf != c->vf));
  lift_charge_bcc(x, 0xFE1C8, t);                 /* ble.w loc_FE1D2 */
  if (!t)
  {
    lift_w16(x, 0xFFFFC31A, alu_movew(c, 2));     /* move.w #2,($C31A).w */
    lift_charge(x, 0xFE1CC);
  }

epilogue:
  /* loc_FE1D2 */
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge_movem(x, 0xFE1D2);
  lift_charge(x, 0xFE1D6);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_DrawNameForSelectedTeam (sub_18AC6)
 * Point a2 at the home team block ($C6CE), or the away one (+$364) when
 * $C470 is negative — in which case only the low byte of the index is
 * kept — and render the name through sub_18AE8. d0/a2 preserved.
 */
void Roster_DrawNameForSelectedTeam(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int pl;

  c->a[7] -= 8;                                   /* movem.l d0/a2,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->a[2]);
  lift_charge_movem(x, 0x18AC6);

  c->a[2] = 0xFFFFC6CE;                           /* move.w #$C6CE,a2 — movea sign-extends */
  lift_charge(x, 0x18ACA);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFC470)));  /* move.w ($C470).w,d0 */
  lift_charge(x, 0x18ACE);
  pl = !c->nf;
  lift_charge_bcc(x, 0x18AD2, pl);                /* bpl.w loc_18ADE */
  if (!pl)
  {
    setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));  /* and.w #$FF,d0 */
    lift_charge(x, 0x18AD6);
    c->a[2] += 0x364;                             /* add.w #$364,a2 — adda, no CCR */
    lift_charge(x, 0x18ADA);
  }

  /* loc_18ADE */
  lift_call(x, 0x18ADE, 4, sub_18AE8);            /* bsr.w sub_18AE8 */
  if (x->declined) return;

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0/a2 */
  c->a[2] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0x18AE2);
  lift_charge(x, 0x18AE6);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Shot_PlanPuckFlight (sub_F6778; called from sub_BC40:loc_BCA4)
 * The puck object lives at $FFFFB74A; Shot_PickAimPoint has already put
 * the aim point in $D414/$D416 and the shot power in $D418.  Flight
 * duration = sqrt((12 * power) << 16, low word only) and lands in
 * $2C(puck) = $B776; the per-step X/Y deltas are (aim - position)
 * scaled to <<16 and divided by duration/3 (forced to 1 when that
 * quotient is zero), stored to $28/$2A(puck) = $B772/$B774.  Finally
 * a3 = the puck and Anim_StartScript46A kicks its flight animation off
 * with d0 = the duration.  All of d0-a6 are restored by the movem.
 */
void Shot_PlanPuckFlight(lift_ctx *x)
{
  rcpu_t *c = x->c;
  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xF6778);

  setw(&c->d[0], alu_movew(c, 0xC));              /* move.w #$C,d0 */
  lift_charge(x, 0xF677C);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD418)));  /* move.w ($D418).w,d1 */
  lift_charge(x, 0xF6780);
  lift_charge_mulu(x, 0xF6784, W(c->d[1]));       /* mulu.w d1,d0 */
  c->d[0] = alu_mulu(c, W(c->d[1]), W(c->d[0]));
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0xF6786);
  c->d[0] = alu_andl(c, 0xFFFF0000u, c->d[0]);    /* and.l #$FFFF0000,d0 */
  lift_charge(x, 0xF6788);

  lift_call(x, 0xF678E, 6, Math_SqrtU32);         /* jsr sub_110BE */
  if (x->declined) return;

  lift_w16(x, 0xFFFFB776u, alu_movew(c, W(c->d[0])));  /* move.w d0,($B776).w */
  lift_charge(x, 0xF6794);
  c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
  lift_charge(x, 0xF6798);
  setw(&c->d[4], alu_movew(c, 3));                /* move.w #3,d4 */
  lift_charge(x, 0xF679A);
  lift_charge_divu(x, 0xF679E, W(c->d[4]), c->d[0]);   /* divu.w d4,d0 */
  if (x->declined) return;                        /* zero divisor would trap */
  c->d[0] = alu_divu(c, W(c->d[4]), c->d[0]);

  c->d[1] = alu_movel(c, 0);                      /* clr.l d1 */
  lift_charge(x, 0xF67A0);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD414)));  /* move.w ($D414).w,d1 */
  lift_charge(x, 0xF67A2);
  setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB74A), W(c->d[1])));  /* sub.w ($B74A).w,d1 */
  lift_charge(x, 0xF67A6);
  c->d[1] = alu_swap(c, c->d[1]);                 /* swap d1 */
  lift_charge(x, 0xF67AA);
  alu_tstw(c, W(c->d[0]));                        /* tst.w d0 */
  lift_charge(x, 0xF67AC);
  lift_charge_bcc(x, 0xF67AE, !c->zf);            /* bne.w loc_F67B6 */
  if (c->zf)
  {
    setw(&c->d[0], alu_movew(c, 1));              /* move.w #1,d0 */
    lift_charge(x, 0xF67B2);
  }

  /* loc_F67B6 — the divisor is nonzero by construction */
  lift_charge_divs(x, 0xF67B6, W(c->d[0]), c->d[1]);   /* divs.w d0,d1 */
  if (x->declined) return;
  c->d[1] = alu_divs(c, W(c->d[0]), c->d[1]);
  lift_w16(x, 0xFFFFB772u, alu_movew(c, W(c->d[1])));  /* move.w d1,($B772).w */
  lift_charge(x, 0xF67B8);

  c->d[1] = alu_movel(c, 0);                      /* clr.l d1 */
  lift_charge(x, 0xF67BC);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFD416)));  /* move.w ($D416).w,d1 */
  lift_charge(x, 0xF67BE);
  setw(&c->d[1], alu_subw(c, lift_r16(x, 0xFFFFB75E), W(c->d[1])));  /* sub.w ($B75E).w,d1 */
  lift_charge(x, 0xF67C2);
  c->d[1] = alu_swap(c, c->d[1]);                 /* swap d1 */
  lift_charge(x, 0xF67C6);
  lift_charge_divs(x, 0xF67C8, W(c->d[0]), c->d[1]);   /* divs.w d0,d1 */
  if (x->declined) return;
  c->d[1] = alu_divs(c, W(c->d[0]), c->d[1]);
  lift_w16(x, 0xFFFFB774u, alu_movew(c, W(c->d[1])));  /* move.w d1,($B774).w */
  lift_charge(x, 0xF67CA);

  c->a[3] = 0xFFFFB74Au;                          /* move.l #$FFFFB74A,a3 — movea */
  lift_charge(x, 0xF67CE);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB776)));  /* move.w ($B776).w,d0 */
  lift_charge(x, 0xF67D4);
  lift_call(x, 0xF67D8, 6, Anim_StartScript46A);  /* jsr sub_102D2 */
  if (x->declined) return;

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF67DE);

  lift_charge(x, 0xF67E2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Shot_PickAimPoint (sub_F67E4; called from sub_BC40+48)
 *   in: a3 = the shooting player object
 * Chooses the aim point for a shot and writes it to $D414/$D416.
 * Three tables of eight (X,Y) pairs indexed by facing*4 supply the base
 * point: $F691E when the player has no puck-carry state ($34(a3) == 0),
 * otherwise the near-net table $F68DE when |$14(a3)| < $58, or the
 * far/wide table $F68FE beyond that.  $C2F8 bit 2 tracks which table
 * won.  With the far table, a shot from deep (|$14(a3)| >= $8A) and
 * close to the centre line (|X| <= $37) overrides the Y component to
 * $103.  Both components are negated for the away side (bit 7 of
 * $62(a3) clear), then each gets a Rng_NextScaled(10) jitter.  Finally
 * the X aim is pulled back toward the shooter when it overshoots the
 * player by more than $3C.  All of d0-a6 are restored by the movem.
 */
void Shot_PickAimPoint(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int far_table, has_state;
  /* movem.l d0-a6,-(sp): push order a6..a0,d7..d0 (d0 lands lowest/top) */
  uint32_t saved[15] = {
    c->a[6], c->a[5], c->a[4], c->a[3], c->a[2], c->a[1], c->a[0],
    c->d[7], c->d[6], c->d[5], c->d[4], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xF67E4);

  lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8), 2));  /* bclr #2,($C2F8).w */
  lift_charge(x, 0xF67E8);
  alu_tstw(c, lift_r16(x, c->a[3] + 0x34));       /* tst.w $34(a3) */
  lift_charge(x, 0xF67EE);
  has_state = !c->zf;
  lift_charge_bcc(x, 0xF67F2, has_state);         /* bne.w loc_F6800 */
  if (!has_state)
  {
    c->a[0] = 0x000F691Eu;                        /* move.l #unk_F691E,a0 — movea */
    lift_charge(x, 0xF67F6);
    lift_charge_bcc(x, 0xF67FC, 1);               /* bra.w loc_F6830 */
  }
  else
  {
    /* loc_F6800 */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d0 */
    lift_charge(x, 0xF6800);
    alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);   /* btst #7,$62(a3) */
    lift_charge(x, 0xF6804);
    lift_charge_bcc(x, 0xF680A, !c->zf);          /* bne.w loc_F6810 */
    if (c->zf)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));    /* neg.w d0 */
      lift_charge(x, 0xF680E);
    }
    /* loc_F6810 */
    lift_w8(x, 0xFFFFC2F8u, alu_bset(c, lift_r8(x, 0xFFFFC2F8), 2));  /* bset #2,($C2F8).w */
    lift_charge(x, 0xF6810);
    c->a[0] = 0x000F68DEu;                        /* move.l #word_F68DE,a0 — movea */
    lift_charge(x, 0xF6816);
    alu_cmpw(c, 0x58, W(c->d[0]));                /* cmp.w #$58,d0 */
    lift_charge(x, 0xF681C);
    lift_charge_bcc(x, 0xF6820, c->nf != c->vf);  /* blt.w loc_F6830 */
    if (c->nf == c->vf)
    {
      c->a[0] = 0x000F68FEu;                      /* move.l #unk_F68FE,a0 — movea */
      lift_charge(x, 0xF6824);
      lift_w8(x, 0xFFFFC2F8u, alu_bclr(c, lift_r8(x, 0xFFFFC2F8), 2));  /* bclr #2,($C2F8).w */
      lift_charge(x, 0xF682A);
    }
  }

  /* loc_F6830 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x54)));  /* move.w $54(a3),d0 */
  lift_charge(x, 0xF6830);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);     /* btst #7,$62(a3) */
  lift_charge(x, 0xF6834);
  lift_charge_bcc(x, 0xF683A, !c->zf);            /* bne.w loc_F6844 */
  if (c->zf)
  {
    setw(&c->d[0], alu_addw(c, 4, W(c->d[0])));   /* addq.w #4,d0 */
    lift_charge(x, 0xF683E);
    setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));   /* and.w #7,d0 */
    lift_charge(x, 0xF6840);
  }

  /* loc_F6844 */
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0xF6844);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[0]))));      /* move.w (a0,d0.w),d1 */
  lift_charge(x, 0xF6846);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[0]) + 2)));  /* move.w 2(a0,d0.w),d2 */
  lift_charge(x, 0xF684A);
  alu_cmpl(c, 0x000F68FEu, c->a[0]);              /* cmp.l #unk_F68FE,a0 — cmpa */
  lift_charge(x, 0xF684E);
  far_table = c->zf;
  lift_charge_bcc(x, 0xF6854, !far_table);        /* bne.w loc_F687E */
  if (far_table)
  {
    int deep;
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3] + 0x14)));  /* move.w $14(a3),d0 */
    lift_charge(x, 0xF6858);
    lift_charge_bcc(x, 0xF685C, !c->nf);          /* bpl.w loc_F6862 */
    if (c->nf)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));    /* neg.w d0 */
      lift_charge(x, 0xF6860);
    }
    /* loc_F6862 */
    alu_cmpw(c, 0x8A, W(c->d[0]));                /* cmp.w #$8A,d0 */
    lift_charge(x, 0xF6862);
    deep = (c->nf == c->vf);
    lift_charge_bcc(x, 0xF6866, !deep);           /* blt.w loc_F687E */
    if (deep)
    {
      int wide;
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[3])));  /* move.w (a3),d0 */
      lift_charge(x, 0xF686A);
      lift_charge_bcc(x, 0xF686C, !c->nf);        /* bpl.w loc_F6872 */
      if (c->nf)
      {
        setw(&c->d[0], alu_negw(c, W(c->d[0])));  /* neg.w d0 */
        lift_charge(x, 0xF6870);
      }
      /* loc_F6872 */
      alu_cmpw(c, 0x37, W(c->d[0]));              /* cmp.w #$37,d0 */
      lift_charge(x, 0xF6872);
      wide = !c->zf && (c->nf == c->vf);
      lift_charge_bcc(x, 0xF6876, wide);          /* bgt.w loc_F687E */
      if (!wide)
      {
        setw(&c->d[2], alu_movew(c, 0x103));      /* move.w #$103,d2 */
        lift_charge(x, 0xF687A);
      }
    }
  }

  /* loc_F687E */
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);     /* btst #7,$62(a3) */
  lift_charge(x, 0xF687E);
  lift_charge_bcc(x, 0xF6884, !c->zf);            /* bne.w loc_F688C */
  if (c->zf)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));      /* neg.w d1 */
    lift_charge(x, 0xF6888);
    setw(&c->d[2], alu_negw(c, W(c->d[2])));      /* neg.w d2 */
    lift_charge(x, 0xF688A);
  }

  /* loc_F688C */
  setw(&c->d[0], alu_movew(c, 0xA));              /* move.w #$A,d0 */
  lift_charge(x, 0xF688C);
  lift_call(x, 0xF6890, 6, Rng_NextScaled);       /* jsr sub_11086 */
  if (x->declined) return;
  setw(&c->d[1], alu_addw(c, W(c->d[0]), W(c->d[1])));  /* add.w d0,d1 */
  lift_charge(x, 0xF6896);
  setw(&c->d[0], alu_movew(c, 0xA));              /* move.w #$A,d0 */
  lift_charge(x, 0xF6898);
  lift_call(x, 0xF689C, 6, Rng_NextScaled);       /* jsr sub_11086 */
  if (x->declined) return;
  setw(&c->d[2], alu_addw(c, W(c->d[0]), W(c->d[2])));  /* add.w d0,d2 */
  lift_charge(x, 0xF68A2);
  lift_w16(x, 0xFFFFD414u, alu_movew(c, W(c->d[1])));   /* move.w d1,($D414).w */
  lift_charge(x, 0xF68A4);

  alu_tstw(c, lift_r16(x, c->a[3] + 0x34));       /* tst.w $34(a3) */
  lift_charge(x, 0xF68A8);
  lift_charge_bcc(x, 0xF68AC, c->zf);             /* beq.w loc_F68D4 */
  if (!c->zf)
  {
    int neg;
    setw(&c->d[1], alu_subw(c, lift_r16(x, c->a[3]), W(c->d[1])));  /* sub.w (a3),d1 */
    lift_charge(x, 0xF68B0);
    neg = c->nf;
    lift_charge_bcc(x, 0xF68B2, neg);             /* bmi.w loc_F68C8 */
    if (!neg)
    {
      setw(&c->d[1], alu_subw(c, 0x3C, W(c->d[1])));   /* sub.w #$3C,d1 */
      lift_charge(x, 0xF68B6);
      lift_charge_bcc(x, 0xF68BA, !c->nf);        /* bpl.w loc_F68D4 */
      if (c->nf)
      {
        uint32_t v;
        setw(&c->d[1], alu_negw(c, W(c->d[1])));  /* neg.w d1 */
        lift_charge(x, 0xF68BE);
        v = lift_r16(x, 0xFFFFD414);              /* add.w d1,($D414).w */
        lift_w16(x, 0xFFFFD414u, alu_addw(c, W(c->d[1]), v));
        lift_charge(x, 0xF68C0);
        lift_charge_bcc(x, 0xF68C4, 1);           /* bra.w loc_F68D4 */
      }
    }
    else
    {
      /* loc_F68C8 */
      setw(&c->d[1], alu_addw(c, 0x3C, W(c->d[1])));   /* add.w #$3C,d1 */
      lift_charge(x, 0xF68C8);
      lift_charge_bcc(x, 0xF68CC, c->nf);         /* bmi.w loc_F68D4 */
      if (!c->nf)
      {
        uint32_t v = lift_r16(x, 0xFFFFD414);     /* sub.w d1,($D414).w */
        lift_w16(x, 0xFFFFD414u, alu_subw(c, W(c->d[1]), v));
        lift_charge(x, 0xF68D0);
      }
    }
  }

  /* loc_F68D4 */
  lift_w16(x, 0xFFFFD416u, alu_movew(c, W(c->d[2])));   /* move.w d2,($D416).w */
  lift_charge(x, 0xF68D4);

  /* movem.l (sp)+,d0-a6: pop order d0..d7,a0..a6 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[7] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[5] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[6] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF68D8);

  lift_charge(x, 0xF68DC);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Text_FillRows(lift_ctx *);        /* vdp.c */
void Text_DrawString(lift_ctx *);      /* vdp.c */
void Tilemap_DrawRegion(lift_ctx *);   /* vdp.c — sub_1169A */

/*
 * Text_BuildNumberSurname (sub_18B26) — the third sibling of sub_18AE8 /
 * sub_18B6E. Piece_AdvanceChain resolves the roster record into a0; the
 * jersey number lives at the record-relative offset in its first word and
 * is written as two ASCII digits into the $FFFFBFA6 buffer, followed by a
 * space and the surname (everything past the first space in the record,
 * up to the end pointer from the length prefix). Text_AlignBufferEven
 * pads the result. d0-d3/a0/a2 restored; a1 is left past the text (the
 * callers' convention).
 */
void Text_BuildNumberSurname(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[6] = {
    c->a[2], c->a[0], c->d[3], c->d[2], c->d[1], c->d[0]
  };
  for (int i = 0; i < 6; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0x18B26);

  lift_call(x, 0x18B2A, 4, Piece_AdvanceChain);         /* bsr.w sub_18BC8 */
  if (x->declined) return;

  {
    uint32_t v = c->a[0];                               /* move.l a0,-(sp) */
    alu_movel(c, v);
    c->a[7] -= 4;
    lift_w32(x, c->a[7], v);
    lift_charge(x, 0x18B2E);
  }
  c->a[1] = 0xFFFFBFA6u;                                /* move.w #$BFA6,a1 */
  lift_charge(x, 0x18B30);
  c->a[0] += SEW(lift_r16(x, c->a[0]));                 /* add.w (a0),a0 */
  lift_charge(x, 0x18B34);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));    /* move.b (a0),d0 */
  lift_charge(x, 0x18B36);

  lift_call(x, 0x18B38, 4, Text_WriteTwoDigits);        /* bsr.w sub_18BDC */
  if (x->declined) return;

  lift_w8(x, c->a[1], alu_moveb(c, 0x20));              /* move.b #$20,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x18B3C);

  c->a[0] = lift_r32(x, c->a[7]);                       /* move.l (sp)+,a0 */
  c->a[7] += 4;
  lift_charge(x, 0x18B40);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));   /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0x18B42);
  c->a[2] = c->a[0] + SEW(c->d[0]) - 2;                 /* lea -2(a0,d0.w),a2 */
  lift_charge(x, 0x18B44);

  for (int guard = 0; ; guard++)
  {
    /* loc_18B48 — skip the given name */
    if (guard > 4096) { x->declined = 1; return; }
    alu_cmpb(c, 0x20, lift_r8(x, c->a[0]));             /* cmp.b #$20,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x18B48);
    int more = !c->zf;                                   /* bne.s loc_18B48 */
    lift_charge_bcc(x, 0x18B4C, more);
    if (!more) break;
  }

  for (int guard = 0; ; guard++)
  {
    /* loc_18B4E — copy the surname up to the record end */
    if (guard > 4096) { x->declined = 1; return; }
    lift_w8(x, c->a[1], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,(a1)+ */
    c->a[0] += 1;
    c->a[1] += 1;
    lift_charge(x, 0x18B4E);
    alu_cmpl(c, c->a[0], c->a[2]);                      /* cmp.l a0,a2 */
    lift_charge(x, 0x18B50);
    int more = !c->zf;                                   /* bne.s loc_18B4E */
    lift_charge_bcc(x, 0x18B52, more);
    if (!more) break;
  }

  lift_call(x, 0x18B54, 4, Text_AlignBufferEven);       /* bsr.w sub_18BAE */
  if (x->declined) return;

  c->d[0] = saved[5];                                   /* movem.l (sp)+,d0-d3/a0/a2 */
  c->d[1] = saved[4];
  c->d[2] = saved[3];
  c->d[3] = saved[2];
  c->a[0] = saved[1];
  c->a[2] = saved[0];
  c->a[7] += 6 * 4;
  lift_charge_movem(x, 0x18B58);

  lift_charge(x, 0x18B5C);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_DrawTeamLogoForId (sub_FF8DE)
 *   in: $FFFFDEE8 = the team id stored by Board_DrawPlayerNameAndLogo,
 *       a2 = team block ($FFFFC6CE home / +$364 away — only the home
 *       pointer is tested, and it selects the left-hand column $02 vs the
 *       right-hand column $20 and the odd/even half of each id pair)
 * Two one-shot id probes ($FFFFBF5C/$BF5E then $FFFFBF60/$BF62 — the dbf
 * counters start at 0, so each "loop" reads exactly one word). A hit on
 * the first picks the logo table at $F5AF6 and the bias at $FFFFDEE4; a
 * hit on the second picks $F5D1C and $FFFFDEE6. Either way the cursor is
 * parked at column $DEEA / row $19 and the logo's two self-relative
 * chunks (tilemap in a1, patterns in a0) go through Tilemap_DrawRegion.
 * No hit at all just blanks a 8x3 patch with Text_FillRows.
 * d0/a0/a1 restored; d1-d5 and a2 are left as the routine computed them.
 */
void Board_DrawTeamLogoForId(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t saved[3] = { c->a[1], c->a[0], c->d[0] };
  for (int i = 0; i < 3; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFF8DE);

  c->a[0] = 0xFFFFBF5Cu;                                /* move.l #$FFFFBF5C,a0 */
  lift_charge(x, 0xFF8E2);
  lift_w16(x, 0xFFDEEA, alu_movew(c, 2));               /* move.w #2,($DEEA).w */
  lift_charge(x, 0xFF8E8);
  alu_cmpl(c, 0xFFFFC6CEu, c->a[2]);                    /* cmp.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xFF8EE);
  {
    int away = !c->zf;
    lift_charge_bcc(x, 0xFF8F4, away);                  /* bne.w loc_FF904 */
    if (!away)
    {
      c->a[0] = 0xFFFFBF5Eu;                            /* move.l #$FFFFBF5E,a0 */
      lift_charge(x, 0xFF8F8);
      lift_w16(x, 0xFFDEEA, alu_movew(c, 0x20));        /* move.w #$20,($DEEA).w */
      lift_charge(x, 0xFF8FE);
    }
  }

  /* loc_FF904 */
  setw(&c->d[0], alu_movew(c, 0));                      /* move.w #0,d0 */
  lift_charge(x, 0xFF904);

  int hit = 0;                                          /* 0 none, 1 first, 2 second */
  for (;;)
  {
    /* loc_FF908 */
    setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0]))); /* move.w (a0)+,d1 */
    c->a[0] += 2;
    lift_charge(x, 0xFF908);
    alu_cmpw(c, lift_r16(x, 0xFFDEE8), W(c->d[1]));     /* cmp.w ($DEE8).w,d1 */
    lift_charge(x, 0xFF90A);
    int match = c->zf;
    lift_charge_bcc(x, 0xFF90E, match);                 /* beq.w loc_FF960 */
    if (match) { hit = 1; break; }
    setw(&c->d[0], W(c->d[0] - 1));                     /* dbf d0,loc_FF908 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0xFF912, taken, !taken);
      if (!taken) break;
    }
  }

  if (!hit)
  {
    c->a[0] = 0xFFFFBF60u;                              /* move.l #$FFFFBF60,a0 */
    lift_charge(x, 0xFF916);
    alu_cmpl(c, 0xFFFFC6CEu, c->a[2]);                  /* cmp.l #$FFFFC6CE,a2 */
    lift_charge(x, 0xFF91C);
    {
      int away = !c->zf;
      lift_charge_bcc(x, 0xFF922, away);                /* bne.w loc_FF92C */
      if (!away)
      {
        c->a[0] = 0xFFFFBF62u;                          /* move.l #$FFFFBF62,a0 */
        lift_charge(x, 0xFF926);
      }
    }

    /* loc_FF92C */
    setw(&c->d[0], alu_movew(c, 0));                    /* move.w #0,d0 */
    lift_charge(x, 0xFF92C);

    for (;;)
    {
      /* loc_FF930 */
      setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,d1 */
      c->a[0] += 2;
      lift_charge(x, 0xFF930);
      alu_cmpw(c, lift_r16(x, 0xFFDEE8), W(c->d[1]));   /* cmp.w ($DEE8).w,d1 */
      lift_charge(x, 0xFF932);
      int match = c->zf;
      lift_charge_bcc(x, 0xFF936, match);               /* beq.w loc_FF96E */
      if (match) { hit = 2; break; }
      setw(&c->d[0], W(c->d[0] - 1));                   /* dbf d0,loc_FF930 */
      {
        int taken = (W(c->d[0]) != 0xFFFF);
        lift_charge_dbcc(x, 0xFF93A, taken, !taken);
        if (!taken) break;
      }
    }
  }

  if (!hit)
  {
    lift_w16(x, 0xFFB028, alu_movew(c, lift_r16(x, 0xFFDEEA)));  /* move.w ($DEEA).w,($B028).w */
    lift_charge(x, 0xFF93E);
    lift_w16(x, 0xFFB02A, alu_movew(c, 0x19));          /* move.w #$19,($B02A).w */
    lift_charge(x, 0xFF944);
    setw(&c->d[0], alu_movew(c, 8));                    /* move.w #8,d0 */
    lift_charge(x, 0xFF94A);
    setw(&c->d[1], alu_movew(c, 3));                    /* move.w #3,d1 */
    lift_charge(x, 0xFF94E);
    setw(&c->d[2], alu_movew(c, 0x7FF));                /* move.w #$7FF,d2 */
    lift_charge(x, 0xFF952);
    lift_call(x, 0xFF956, 6, Text_FillRows);            /* jsr sub_1197E */
    if (x->declined) return;
    lift_charge(x, 0xFF95C);                            /* bra.w loc_FF9A2 */
  }
  else
  {
    if (hit == 1)
    {
      /* loc_FF960 */
      setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFDEE4)));   /* move.w ($DEE4).w,d4 */
      lift_charge(x, 0xFF960);
      c->a[0] = 0x000F5AF6;                             /* move.l #unk_F5AF6,a0 */
      lift_charge(x, 0xFF964);
      lift_charge(x, 0xFF96A);                          /* bra.w loc_FF978 */
    }
    else
    {
      /* loc_FF96E */
      setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFDEE6)));   /* move.w ($DEE6).w,d4 */
      lift_charge(x, 0xFF96E);
      c->a[0] = 0x000F5D1C;                             /* move.l #unk_F5D1C,a0 */
      lift_charge(x, 0xFF972);
    }

    /* loc_FF978 */
    lift_w16(x, 0xFFB028, alu_movew(c, lift_r16(x, 0xFFDEEA)));  /* move.w ($DEEA).w,($B028).w */
    lift_charge(x, 0xFF978);
    lift_w16(x, 0xFFB02A, alu_movew(c, 0x19));          /* move.w #$19,($B02A).w */
    lift_charge(x, 0xFF97E);
    c->a[1] = c->a[0];                                  /* move.l a0,a1 */
    lift_charge(x, 0xFF984);
    c->a[2] = c->a[0];                                  /* move.l a0,a2 */
    lift_charge(x, 0xFF986);
    c->a[0] += lift_r32(x, c->a[2]);                    /* add.l (a2)+,a0 */
    c->a[2] += 4;
    lift_charge(x, 0xFF988);
    c->a[1] += lift_r32(x, c->a[2]);                    /* add.l (a2)+,a1 */
    c->a[2] += 4;
    lift_charge(x, 0xFF98A);
    c->a[2] = SEW(0x030A);                              /* move.w #$30A,a2 */
    lift_charge(x, 0xFF98C);
    setw(&c->d[0], alu_movew(c, 0));                    /* clr.w d0 */
    lift_charge(x, 0xFF990);
    setw(&c->d[1], alu_movew(c, 0));                    /* clr.w d1 */
    lift_charge(x, 0xFF992);
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1])));      /* move.w (a1),d2 */
    lift_charge(x, 0xFF994);
    setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1] + 2)));  /* move.w 2(a1),d3 */
    lift_charge(x, 0xFF996);
    c->d[5] = alu_moveql(c, 0);                         /* moveq #0,d5 */
    lift_charge(x, 0xFF99A);
    lift_call(x, 0xFF99C, 6, Tilemap_DrawRegion);       /* jsr sub_1169A */
    if (x->declined) return;
  }

  /* loc_FF9A2 */
  c->d[0] = lift_r32(x, c->a[7]);                       /* movem.l (sp)+,d0/a0-a1 */
  c->a[0] = lift_r32(x, c->a[7] + 4);
  c->a[1] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0xFF9A2);

  lift_charge(x, 0xFF9A6);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_DrawPlayerNameAndLogo (sub_FD89A)
 *   in: d0.w = team/player id (stashed at $FFFFDEE8 for the logo pass),
 *       a2 = team block
 * Build "number surname" into the $FFFFBFA6 buffer, right-align it so the
 * text ends at column $29 (trailing "." and space bytes in the record are
 * not counted toward the width), draw it with Text_DrawString, then draw
 * the matching team logo. Everything (d0-d7/a0-a6) is restored.
 */
void Board_DrawPlayerNameAndLogo(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  uint32_t saved[15];
  for (i = 0; i < 8; i++) saved[i] = c->d[i];
  for (i = 0; i < 7; i++) saved[8 + i] = c->a[i];
  c->a[7] -= 60;                                        /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 15; i++) lift_w32(x, c->a[7] + 4 * i, saved[i]);
  lift_charge_movem(x, 0xFD89A);

  lift_w16(x, 0xFFDEE8, alu_movew(c, W(c->d[0])));      /* move.w d0,($DEE8).w */
  lift_charge(x, 0xFD89E);
  {
    uint32_t v = c->a[2];                               /* move.l a2,-(sp) */
    alu_movel(c, v);
    c->a[7] -= 4;
    lift_w32(x, c->a[7], v);
    lift_charge(x, 0xFD8A2);
  }
  lift_call(x, 0xFD8A4, 6, Text_BuildNumberSurname);    /* jsr sub_18B26 */
  if (x->declined) return;

  c->a[0] = c->a[1];                                    /* move.l a1,a0 */
  lift_charge(x, 0xFD8AA);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));   /* move.w (a0),d0 */
  lift_charge(x, 0xFD8AC);
  setw(&c->d[5], alu_movew(c, W(c->d[0])));             /* move.w d0,d5 */
  lift_charge(x, 0xFD8AE);

  for (;;)
  {
    alu_tstb(c, lift_r8(x, c->a[0] + SEW(c->d[5]) - 1));   /* tst.b -1(a0,d5.w) */
    lift_charge(x, 0xFD8B0);
    {
      int nz = !c->zf;
      lift_charge_bcc(x, 0xFD8B4, nz);                  /* bne.w loc_FD8C4 */
      if (nz) break;
    }
    setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));         /* subq.w #1,d0 */
    lift_charge(x, 0xFD8B8);
    alu_tstb(c, lift_r8(x, c->a[0] + SEW(c->d[5]) - 2));   /* tst.b -2(a0,d5.w) */
    lift_charge(x, 0xFD8BA);
    {
      int nz = !c->zf;
      lift_charge_bcc(x, 0xFD8BE, nz);                  /* bne.w loc_FD8C4 */
      if (nz) break;
    }
    setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));         /* subq.w #1,d0 */
    lift_charge(x, 0xFD8C2);
    break;
  }

  /* loc_FD8C4 */
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFB028), W(c->d[0])));  /* add.w ($B028).w,d0 */
  lift_charge(x, 0xFD8C4);
  setw(&c->d[0], alu_subw(c, 0x29, W(c->d[0])));        /* sub.w #$29,d0 */
  lift_charge(x, 0xFD8C8);
  {
    int neg = c->nf;
    lift_charge_bcc(x, 0xFD8CC, neg);                   /* bmi.w loc_FD8D6 */
    if (!neg)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));          /* neg.w d0 */
      lift_charge(x, 0xFD8D0);
      {
        uint32_t v = lift_r16(x, 0xFFB028);             /* add.w d0,($B028).w */
        lift_w16(x, 0xFFB028, alu_addw(c, W(c->d[0]), v));
        lift_charge(x, 0xFD8D2);
      }
    }
  }

  /* loc_FD8D6 */
  c->a[1] = c->a[0];                                    /* move.l a0,a1 */
  lift_charge(x, 0xFD8D6);
  lift_call(x, 0xFD8D8, 6, Text_DrawString);            /* jsr sub_11BA4 */
  if (x->declined) return;

  c->a[2] = lift_r32(x, c->a[7]);                       /* move.l (sp)+,a2 */
  c->a[7] += 4;
  lift_charge(x, 0xFD8DE);
  lift_call(x, 0xFD8E0, 6, Board_DrawTeamLogoForId);    /* jsr sub_FF8DE */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);   /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFD8E6);

  lift_charge(x, 0xFD8EA);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_BuildSurnameNoPad (sub_18B5E) — wave 54. sub_18B6E's twin, minus
 * the leading space: same movem frame, same Piece_AdvanceChain resolve,
 * same $FFFFBFA6 buffer, but it `bra.w`s straight into the shared scan at
 * loc_18B7E instead of emitting `move.b #$20,(a1)+` first. (Blind spot 5:
 * triage only ever saw "far-branches into mid-routine loc_18B7E".)
 */
void Text_BuildSurnameNoPad(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {                                                     /* movem.l d0-d3/a0/a2,-(sp) */
    uint32_t push[6] = { c->a[2], c->a[0], c->d[3], c->d[2], c->d[1], c->d[0] };
    for (int i = 0; i < 6; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], push[i]); }
  }
  lift_charge_movem(x, 0x18B5E);

  lift_call(x, 0x18B62, 4, Piece_AdvanceChain);         /* bsr.w sub_18BC8 */
  if (x->declined) return;

  c->a[1] = 0xFFFFBFA6u;                                /* move.w #$BFA6,a1 — movea.w */
  lift_charge(x, 0x18B66);
  lift_charge_bcc(x, 0x18B6A, 1);                       /* bra.w loc_18B7E */
  text_build_surname_tail(x);
}

/* ===========================================================================
 * Wave 58 — Sfx_TickCueStream, the bonus row the stale-skip housekeeping
 * turned up: its Skip reason ("jsr sub_11738, skip-listed") expired when
 * the sprawl chain lifted on 2026-08-04 (Unpack_BlockDirect $11738), and
 * its only other callee, Sfx_AdvanceCueStreamEntry, was already lifted.
 * =========================================================================== */

void Unpack_BlockDirect(lift_ctx *);   /* vdp.c — sub_11738 */

/*
 * Sfx_TickCueStream (sub_FE2C8)
 * The cue sequence player's per-frame driver. A negative cue id in
 * R_UNK_D6B4 means idle. With no stream armed (R_UNK_D6BA zero) it takes
 * the cue's {sequence, blob} pair from the 8-byte-entry table at $FE364,
 * unpacks the blob's payload (blob + 8) at the tile cursor in R_UNK_D6AC,
 * rewinds the entry cursor and primes the first entry. Otherwise it
 * counts the live entry's delay down by d7 (the frame step) and advances
 * to the next entry when it goes negative.
 */
void Sfx_TickCueStream(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  alu_tstw(c, lift_r16(x, 0xFFD6B4u));            /* tst.w ($D6B4).w */
  lift_charge(x, 0xFE2C8);
  t = c->nf;
  lift_charge_bcc(x, 0xFE2CC, t);                 /* bmi.w locret_FE324 */
  if (t)
  {
    lift_charge(x, 0xFE324);                      /* locret_FE324: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFE2D0);

  alu_tstl(c, lift_r32(x, 0xFFD6BAu));            /* tst.l ($D6BA).w */
  lift_charge(x, 0xFE2D4);
  t = !c->zf;
  lift_charge_bcc(x, 0xFE2D8, t);                 /* bne.w loc_FE310 */
  if (!t)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD6B4u)));  /* move.w ($D6B4).w,d0 */
    lift_charge(x, 0xFE2DC);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 3));   /* asl.w #3,d0 */
    lift_charge(x, 0xFE2E0);
    c->a[0] = 0x000FE364;                         /* move.l #off_FE364,a0 */
    lift_charge(x, 0xFE2E2);
    lift_w32(x, 0xFFD6B6u,                        /* move.l (a0,d0.w),($D6B6).w */
             alu_movel(c, lift_r32(x, c->a[0] + SEW(c->d[0]))));
    lift_charge(x, 0xFE2E8);
    lift_w32(x, 0xFFD6BAu,                        /* move.l 4(a0,d0.w),($D6BA).w */
             alu_movel(c, lift_r32(x, c->a[0] + SEW(c->d[0]) + 4)));
    lift_charge(x, 0xFE2EE);
    c->a[2] = lift_r32(x, c->a[0] + SEW(c->d[0]) + 4);     /* move.l 4(a0,d0.w),a2 — movea */
    lift_charge(x, 0xFE2F4);
    c->a[2] += 8;                                 /* addq.w #8,a2 — An, no CCR */
    lift_charge(x, 0xFE2F8);
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD6ACu)));  /* move.w ($D6AC).w,d4 */
    lift_charge(x, 0xFE2FA);
    lift_call(x, 0xFE2FE, 6, Unpack_BlockDirect); /* jsr sub_11738 */
    if (x->declined) return;
    lift_w16(x, 0xFFD6B0u, alu_movew(c, 0));      /* clr.w ($D6B0).w */
    lift_charge(x, 0xFE304);
    lift_call(x, 0xFE308, 4, Sfx_AdvanceCueStreamEntry);   /* bsr.w sub_FE326 */
    if (x->declined) return;
    lift_charge_bcc(x, 0xFE30C, 1);               /* bra.w loc_FE320 */
  }
  else
  {
    /* loc_FE310 */
    lift_w16(x, 0xFFD6B2u,                        /* sub.w d7,($D6B2).w */
             alu_subw(c, W(c->d[7]), lift_r16(x, 0xFFD6B2u)));
    lift_charge(x, 0xFE310);
    t = !c->nf;
    lift_charge_bcc(x, 0xFE314, t);               /* bpl.w loc_FE320 */
    if (!t)
    {
      lift_w16(x, 0xFFD6B0u,                      /* addq.w #1,($D6B0).w */
               alu_addw(c, 1, lift_r16(x, 0xFFD6B0u)));
      lift_charge(x, 0xFE318);
      lift_call(x, 0xFE31C, 4, Sfx_AdvanceCueStreamEntry); /* bsr.w sub_FE326 */
      if (x->declined) return;
    }
  }

  /* loc_FE320 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);   /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFE320);
  lift_charge(x, 0xFE324);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}
