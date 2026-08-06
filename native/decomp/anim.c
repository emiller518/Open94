/*
 * anim.c — periodic animation/timer table maintenance (lifted).
 */
#include "util68k.h"

#define R_TICK_DELAY   0xFFBF14u  /* frames until the next table pass */
#define R_TICK_STEP    0xFFBD1Eu  /* per-pass step added/subtracted */
#define TABLE_BASE     0xFFB04Au  /* 0xE0 four-byte entries; low word ticked */
#define TABLE_ENTRIES  0xE0

/*
 * TickTimerTable_B04A (sub_FF3F8, called from $FF39E)
 *
 * Runs when the delay counter at $FFBF14 underflows (then pins it at 0, so
 * it fires every frame until re-armed). Walks the 224-entry table at
 * $FFB04A; each entry's second word is ticked by the step at $FFBD1E:
 *   entries with index > $8F: word += step, and is zeroed unless its top
 *     six bits are all set ($FC00 band);
 *   entries with $28 < index <= $8F: word -= step, clamped at zero;
 *   entries with index <= $28: untouched.
 * Exact role TBD (likely scoreboard/crowd animation phases); behaviour
 * preserved bit-for-bit regardless.
 */
void TickTimerTable_B04A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  uint32_t t = alu_subw(c, 1, lift_r16(x, R_TICK_DELAY));
  lift_w16(x, R_TICK_DELAY, t);
  lift_charge(x, 0xFF3F8);
  lift_charge_bcc(x, 0xFF3FC, c->nf);             /* bmi */
  if (!c->nf)
  {
    lift_charge(x, 0xFF400);                      /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, R_TICK_DELAY, 0);                   /* clr.w */
  c->zf = 1; c->nf = 0; c->vf = 0; c->cf = 0;
  lift_charge(x, 0xFF402);

  /* movem.l d0-d2/a0,-(sp): a0, d2, d1, d0 pushed descending */
  uint32_t saved[4] = {c->d[0], c->d[1], c->d[2], c->a[0]};
  for (int r = 3; r >= 0; r--)
  {
    c->a[7] -= 4;
    lift_w32(x, c->a[7], saved[r]);
  }
  lift_charge_movem(x, 0xFF406);

  uint32_t d1 = c->d[1], d2 = c->d[2];
  alu_movew(c, 0xDF);       /* d0 = counter/range key; restored by movem below */
  lift_charge(x, 0xFF40A);
  uint32_t a0 = 0xFFFFB04A;                       /* movea.w: sign-extended, no flags */
  lift_charge(x, 0xFF40E);

  for (int i = TABLE_ENTRIES - 1; i >= 0; i--)
  {
    uint32_t idx = (uint32_t)i;                   /* current d0.w */
    alu_tstl(c, lift_r32(x, a0));
    a0 += 4;
    lift_charge(x, 0xFF412);
    alu_cmpw(c, 0x28, idx);
    lift_charge(x, 0xFF414);
    int skip = (c->zf || c->nf != c->vf);         /* ble */
    lift_charge_bcc(x, 0xFF418, skip);
    if (!skip)
    {
      setw(&d1, alu_movew(c, lift_r16(x, R_TICK_STEP)));
      lift_charge(x, 0xFF41C);
      alu_cmpw(c, 0x8F, idx);
      lift_charge(x, 0xFF420);
      int low = (c->nf != c->vf);                 /* blt */
      lift_charge_bcc(x, 0xFF424, low);
      if (!low)
      {
        setw(&d2, alu_movew(c, lift_r16(x, a0 - 2)));
        lift_charge(x, 0xFF428);
        int zero = c->zf;
        lift_charge_bcc(x, 0xFF42C, zero);        /* beq: untouched slot */
        if (!zero)
        {
          setw(&d2, alu_addw(c, W(d1), W(d2)));
          lift_charge(x, 0xFF430);
          lift_w16(x, a0 - 2, alu_movew(c, W(d2)));
          lift_charge(x, 0xFF432);
          setw(&d2, alu_andw(c, 0xFC00, W(d2)));
          lift_charge(x, 0xFF436);
          alu_cmpw(c, 0xFC00, W(d2));
          lift_charge(x, 0xFF43A);
          int keep = c->zf;
          lift_charge_bcc(x, 0xFF43E, keep);
          if (!keep)
          {
            lift_w16(x, a0 - 2, alu_movew(c, 0));
            lift_charge(x, 0xFF442);
            lift_charge(x, 0xFF448);              /* bra.w */
          }
        }
      }
      else
      {
        uint32_t r = alu_subw(c, W(d1), lift_r16(x, a0 - 2));
        lift_w16(x, a0 - 2, r);
        lift_charge(x, 0xFF44C);
        lift_charge_bcc(x, 0xFF450, !c->nf);      /* bpl */
        if (c->nf)
        {
          lift_w16(x, a0 - 2, 0);
          c->zf = 1; c->nf = 0; c->vf = 0; c->cf = 0;
          lift_charge(x, 0xFF454);
        }
      }
    }
    lift_charge_dbcc(x, 0xFF458, i != 0, i == 0);
  }

  /* movem.l (sp)+ restores d0-d2/a0 */
  c->a[7] += 16;
  lift_charge_movem(x, 0xFF45C);
  lift_charge(x, 0xFF460);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define ANIM_BANK    0x5B1Cu     /* ROM: animation scripts */
#define R_RINK_FLIP2 0xFFC2ECu   /* bit 7: rink drawn flipped */

/*
 * Anim_StepObject (sub_AEE4; called per object per frame from sub_A9D6)
 *   in:  a3 = object, d7 = ticks elapsed this frame
 *
 * Advance one object's animation. $58(a3) is the current script offset
 * into the bank at ROM $5B1C (0 = idle: just clear the two status bits).
 * A script starts with 8 direction-variant offsets (facing from $54(a3),
 * rotated 2 when the rink is flipped and the camera zone $52(a3) < $C,
 * mirrored via attr bit 3) and a flags word at +$10 (bit 0 = looping).
 * A variant is [frame.w, duration.w] pairs, the last pair's duration
 * negative. $5A(a3) is the frame cursor, $5C(a3) the countdown ticked
 * by d7 (a negative countdown re-arms from the current frame's duration
 * without advancing). Running off the end
 * restarts the script — or stops it (clears $58) when not looping —
 * and clears status bits 5/$62, 1/$63, 5/$64. The current frame id is
 * committed to the sprite frame word 6(a3) through a 4-tick cooldown
 * byte at $65(a3) that limits how often the visible frame may change.
 */
void Anim_StepObject(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_movew(c, lift_r16(x, a3 + 0x58));           /* tst.w $58(a3) */
  lift_charge(x, 0xAEE4);
  int active = !c->zf;
  lift_charge_bcc(x, 0xAEE8, active);
  if (!active)
  {
    lift_w8(x, a3 + 0x62, alu_bclr(c, lift_r8(x, a3 + 0x62), 5));
    lift_charge(x, 0xAEEC);
    lift_w8(x, a3 + 0x63, alu_bclr(c, lift_r8(x, a3 + 0x63), 1));
    lift_charge(x, 0xAEF2);
    lift_charge(x, 0xAEF8);                       /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = ANIM_BANK;
  lift_charge(x, 0xAEFA);
  c->a[0] += SEW(lift_r16(x, a3 + 0x58));         /* adda.w: script base */
  lift_charge(x, 0xAF00);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0x10)));  /* flags */
  lift_charge(x, 0xAF04);
  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x54)));       /* facing */
  lift_charge(x, 0xAF08);
  alu_btst(c, lift_r8(x, R_RINK_FLIP2), 7);
  lift_charge(x, 0xAF0C);
  int flip = !c->zf;
  lift_charge_bcc(x, 0xAF12, !flip);
  if (flip)
  {
    alu_cmpw(c, 0xC, lift_r16(x, a3 + 0x52));     /* camera zone */
    lift_charge(x, 0xAF16);
    int far_zone = (c->nf == c->vf);              /* bge */
    lift_charge_bcc(x, 0xAF1C, far_zone);
    if (!far_zone)
    {
      setw(&c->d[0], alu_subw(c, 2, W(c->d[0]))); /* rotate facing */
      lift_charge(x, 0xAF20);
      setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));
      lift_charge(x, 0xAF22);
    }
  }
  alu_btst(c, lift_r8(x, a3 + 4), 3);             /* h-flip attr */
  lift_charge(x, 0xAF26);
  int mirrored = !c->zf;
  lift_charge_bcc(x, 0xAF2C, !mirrored);
  if (mirrored)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));      /* mirror facing */
    lift_charge(x, 0xAF30);
    setw(&c->d[0], alu_addw(c, 8, W(c->d[0])));
    lift_charge(x, 0xAF32);
    setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));
    lift_charge(x, 0xAF34);
  }
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));
  lift_charge(x, 0xAF38);
  c->a[0] += SEW(lift_r16(x, c->a[0] + SW(c->d[0])));  /* variant base */
  lift_charge(x, 0xAF3A);
  setw(&c->d[0], alu_movew(c, lift_r16(x, a3 + 0x5A)));  /* frame cursor */
  lift_charge(x, 0xAF3E);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]))));
  lift_charge(x, 0xAF42);
  alu_movew(c, lift_r16(x, a3 + 0x5C));           /* tst.w countdown */
  lift_charge(x, 0xAF46);
  int hold = c->nf;
  lift_charge_bcc(x, 0xAF4A, hold);
  int expired = 0, set_duration = hold;
  if (!hold)
  {
    uint32_t r = alu_subw(c, W(c->d[7]), lift_r16(x, a3 + 0x5C));
    lift_w16(x, a3 + 0x5C, r);
    lift_charge(x, 0xAF4E);
    expired = c->nf;
    lift_charge_bcc(x, 0xAF52, !expired);
  }
  if (expired)
  {
    lift_w16(x, a3 + 0x5A,                        /* addq.w #4 on memory */
             alu_addw(c, 4, lift_r16(x, a3 + 0x5A)));
    lift_charge(x, 0xAF56);
    setw(&c->d[0], alu_addw(c, 4, W(c->d[0])));
    lift_charge(x, 0xAF5A);
    alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]) - 2));  /* old duration */
    lift_charge(x, 0xAF5C);
    int ended = c->nf;
    lift_charge_bcc(x, 0xAF60, !ended);
    if (ended)                                    /* past the last frame */
    {
      setw(&c->d[0], alu_movew(c, 0));
      lift_charge(x, 0xAF64);
      lift_w16(x, a3 + 0x5A, alu_movew(c, 0));
      lift_charge(x, 0xAF66);
      lift_w8(x, a3 + 0x62, alu_bclr(c, lift_r8(x, a3 + 0x62), 5));
      lift_charge(x, 0xAF6A);
      lift_w8(x, a3 + 0x63, alu_bclr(c, lift_r8(x, a3 + 0x63), 1));
      lift_charge(x, 0xAF70);
      lift_w8(x, a3 + 0x64, alu_bclr(c, lift_r8(x, a3 + 0x64), 5));
      lift_charge(x, 0xAF76);
      alu_btst(c, c->d[1], 0);                    /* looping script? */
      lift_charge(x, 0xAF7C);
      int loops = !c->zf;
      lift_charge_bcc(x, 0xAF80, loops);
      if (!loops)
      {
        lift_w16(x, a3 + 0x58, alu_movew(c, 0));  /* one-shot: stop */
        lift_charge(x, 0xAF84);
      }
    }
    set_duration = 1;
  }
  if (set_duration)                               /* loc_AF88 */
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SW(c->d[0]) + 2)));
    lift_charge(x, 0xAF88);
    int last = c->nf;
    lift_charge_bcc(x, 0xAF8C, !last);
    if (last)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));
      lift_charge(x, 0xAF90);
    }
    lift_w16(x, a3 + 0x5C, alu_movew(c, W(c->d[0])));
    lift_charge(x, 0xAF92);
  }
  /* loc_AF96: commit the frame through the change cooldown */
  {
    uint32_t r = alu_subb(c, c->d[7], lift_r8(x, a3 + 0x65));
    lift_w8(x, a3 + 0x65, r);
    lift_charge(x, 0xAF96);
    int waiting = !c->nf;
    lift_charge_bcc(x, 0xAF9A, waiting);
    if (!waiting)
    {
      lift_w8(x, a3 + 0x65, alu_moveb(c, 0));
      lift_charge(x, 0xAF9E);
      alu_cmpw(c, lift_r16(x, a3 + 6), W(c->d[2]));
      lift_charge(x, 0xAFA2);
      int same = c->zf;
      lift_charge_bcc(x, 0xAFA6, same);
      if (!same)
      {
        lift_w16(x, a3 + 6, alu_movew(c, W(c->d[2])));
        lift_charge(x, 0xAFAA);
        lift_w8(x, a3 + 0x65, alu_moveb(c, 4));
        lift_charge(x, 0xAFAE);
      }
    }
  }
  lift_charge(x, 0xAFB4);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_SetScript (sub_1073A; called from gameplay logic everywhere)
 *   in:  a3 = object, d1 = animation script offset
 * Start an animation unless it is already the one playing: reset the
 * frame cursor, store the script offset, and expire the frame countdown
 * (st on its first byte) so Anim_StepObject advances immediately.
 * The already-playing path leaves via the shared far rts at $15464.
 */
void Anim_SetScript(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_cmpw(c, lift_r16(x, a3 + 0x58), W(c->d[1]));
  lift_charge(x, 0x1073A);
  int same = c->zf;
  lift_charge_bcc(x, 0x1073E, same);
  if (same)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
  }
  else
  {
    lift_w16(x, a3 + 0x5A, alu_movew(c, 0));      /* clr.w: frame cursor */
    lift_charge(x, 0x10742);
    lift_w16(x, a3 + 0x58, alu_movew(c, W(c->d[1])));
    lift_charge(x, 0x10746);
    lift_w8(x, a3 + 0x5C, 0xFF);                  /* st: no flags */
    lift_charge(x, 0x1074A);
    lift_charge(x, 0x1074E);                      /* rts */
  }
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define FRAME_VECTORS 0xA44C8u   /* ROM: sprite frame -> (dx,dy) byte pairs */

/*
 * Object_FrameVector (sub_106E0; called from many gameplay sites)
 *   in:  4(sp) = object pointer (stack argument, callee-popped)
 *   out: d0/d1 = the (dx,dy) vector for the object's current sprite
 *        frame 6(a0), or (0,0) when the frame is <= 0
 * The raw byte pair from $A44C8 is sign-adjusted the same way sprites
 * are drawn: negated per the attr flip bits (3 = H, 4 = V — note dy is
 * negated when bit 4 is CLEAR) and axis-swapped when the rink is
 * flipped. Exits with `move.l (sp)+,(sp)` to pop its own argument —
 * the exit flags come from that move of the return address.
 */
void Object_FrameVector(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];

  sp -= 8;                                        /* movem.l a0-a1,-(sp) */
  lift_w32(x, sp, c->a[0]);
  lift_w32(x, sp + 4, c->a[1]);
  lift_charge_movem(x, 0x106E0);
  c->a[0] = lift_r32(x, sp + 12);                 /* the stack argument */
  lift_charge(x, 0x106E4);
  setw(&c->d[0], alu_movew(c, 0));
  lift_charge(x, 0x106E8);
  setw(&c->d[1], alu_movew(c, 0));
  lift_charge(x, 0x106EA);
  alu_movew(c, lift_r16(x, c->a[0] + 6));         /* tst.w frame */
  lift_charge(x, 0x106EC);
  int none = (c->zf || c->nf);                    /* ble */
  lift_charge_bcc(x, 0x106F0, none);
  if (!none)
  {
    c->a[1] = FRAME_VECTORS;
    lift_charge(x, 0x106F4);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 6)));
    lift_charge(x, 0x106FA);
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));
    lift_charge(x, 0x106FE);
    c->d[1] = (c->d[1] & ~0xFFu) |
              alu_moveb(c, lift_r8(x, c->a[1] + 1 + SW(c->d[0])));
    lift_charge(x, 0x10700);
    setw(&c->d[1], alu_extw(c, c->d[1]));
    lift_charge(x, 0x10704);
    c->d[0] = (c->d[0] & ~0xFFu) |
              alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[0])));
    lift_charge(x, 0x10706);
    setw(&c->d[0], alu_extw(c, c->d[0]));
    lift_charge(x, 0x1070A);
    alu_btst(c, lift_r8(x, c->a[0] + 4), 3);      /* H flip */
    lift_charge(x, 0x1070C);
    int plain = c->zf;
    lift_charge_bcc(x, 0x10712, plain);
    if (!plain)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));
      lift_charge(x, 0x10716);
    }
    alu_btst(c, lift_r8(x, c->a[0] + 4), 4);      /* V flip */
    lift_charge(x, 0x10718);
    int vflip = !c->zf;
    lift_charge_bcc(x, 0x1071E, vflip);
    if (!vflip)
    {
      setw(&c->d[1], alu_negw(c, W(c->d[1])));
      lift_charge(x, 0x10722);
    }
    alu_btst(c, lift_r8(x, R_RINK_FLIP2), 7);
    lift_charge(x, 0x10724);
    int flip = !c->zf;
    lift_charge_bcc(x, 0x1072A, !flip);
    if (flip)
    {
      uint32_t t = c->d[0];                       /* exg d0,d1 */
      c->d[0] = c->d[1];
      c->d[1] = t;
      lift_charge(x, 0x1072E);
      setw(&c->d[1], alu_negw(c, W(c->d[1])));
      lift_charge(x, 0x10730);
    }
  }
  /* loc_10732 */
  c->a[0] = lift_r32(x, sp);                      /* movem.l (sp)+,a0-a1 */
  c->a[1] = lift_r32(x, sp + 4);
  sp += 8;
  lift_charge_movem(x, 0x10732);
  {
    uint32_t ret = lift_r32(x, sp);               /* move.l (sp)+,(sp) */
    sp += 4;
    lift_w32(x, sp, alu_movel(c, ret));
    lift_charge(x, 0x10736);
  }
  lift_charge(x, 0x10738);                        /* rts */
  c->pc = lift_r32(x, sp) & 0xFFFFFF;
  c->a[7] = sp + 4;
}

#define SHARED_RTS_2   0x15464u    /* far rts several routines branch to */
#define R_RINK_FLIP_2  0xFFC2ECu   /* bit 7: rink drawn flipped */

/*
 * Object_ResetOnFrameGate (sub_102EC; DATA XREF from a script/anim
 * dispatch table at $18DE0)
 *   in: a3 = on-ice object
 * If the object's anim frame ($6(a3)) equals $18A, resets its position
 * from the fields $80/$6C bytes earlier (i.e. the previous $80-byte
 * object slot's world X/Y — exact relationship TBD), clears its height
 * ($18), then bumps either world X or world Y by 1 depending on the
 * rink-flip flag. Otherwise, if the anim frame is nonzero, resets world
 * X to 0, world Y to $12C, height to $E, and — when the previous slot's
 * world Y field is negative — flips attr to $8000, negates world Y, and
 * decrements height. Exact role TBD; behaviour preserved bit-for-bit.
 */
void Object_ResetOnFrameGate(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_cmpw(c, 0x18A, lift_r16(x, a3 + 6));         /* cmp.w #$18A,6(a3) */
  lift_charge(x, 0x102EC);
  int match = c->zf;                                /* bne.w loc_10318 */
  lift_charge_bcc(x, 0x102F2, !match);

  if (match)
  {
    lift_w16(x, a3, alu_movew(c, lift_r16(x, a3 - 0x80)));      /* move.w -$80(a3),(a3) */
    lift_charge(x, 0x102F6);
    lift_w16(x, a3 + 0x14, alu_movew(c, lift_r16(x, a3 - 0x6C)));  /* move.w -$6C(a3),$14(a3) */
    lift_charge(x, 0x102FA);
    lift_w16(x, a3 + 0x18, alu_movew(c, 0));         /* clr.w $18(a3) */
    lift_charge(x, 0x10300);

    c->d[0] = alu_moveql(c, 0x14);                   /* moveq #$14,d0 */
    lift_charge(x, 0x10304);
    alu_btst(c, lift_r8(x, R_RINK_FLIP_2), 7);       /* btst #7,(abs) */
    lift_charge(x, 0x10306);
    int skipZero = c->zf;                              /* beq.w loc_10312 */
    lift_charge_bcc(x, 0x1030C, skipZero);
    if (!skipZero)
    {
      c->d[0] = alu_moveql(c, 0);                     /* moveq #0,d0 */
      lift_charge(x, 0x10310);
    }

    /* loc_10312 */
    lift_w16(x, a3 + SEW(c->d[0]), alu_addw(c, 1, lift_r16(x, a3 + SEW(c->d[0]))));  /* addq.w #1,(a3,d0.w) */
    lift_charge(x, 0x10312);
    lift_charge(x, 0x10316);                          /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* loc_10318 */
  alu_movew(c, lift_r16(x, a3 + 6));                 /* tst.w 6(a3) */
  lift_charge(x, 0x10318);
  int zero = c->zf;                                    /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x1031C, zero);
  if (zero)
  {
    lift_charge(x, SHARED_RTS_2);                      /* far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_w16(x, a3, alu_movew(c, 0));                    /* clr.w (a3) */
  lift_charge(x, 0x10320);
  lift_w16(x, a3 + 0x14, alu_movew(c, 0x12C));         /* move.w #$12C,$14(a3) */
  lift_charge(x, 0x10322);
  lift_w16(x, a3 + 0x18, alu_movew(c, 0xE));           /* move.w #$E,$18(a3) */
  lift_charge(x, 0x10328);

  alu_movew(c, lift_r16(x, a3 - 0x6C));                /* tst.w -$6C(a3) */
  lift_charge(x, 0x1032E);
  int nonneg = !c->nf;                                  /* bpl.w locret_10344 */
  lift_charge_bcc(x, 0x10332, nonneg);
  if (!nonneg)
  {
    lift_w16(x, a3 + 4, alu_movew(c, 0x8000));         /* move.w #$8000,4(a3) */
    lift_charge(x, 0x10336);
    lift_w16(x, a3 + 0x14, alu_negw(c, lift_r16(x, a3 + 0x14)));  /* neg.w $14(a3) */
    lift_charge(x, 0x1033C);
    lift_w16(x, a3 + 0x18, alu_subw(c, 1, lift_r16(x, a3 + 0x18)));  /* subq.w #1,$18(a3) */
    lift_charge(x, 0x10340);
  }

  lift_charge(x, 0x10344);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_ForceIdleFacingFlip (sub_102A8; called from sub_FF0C; also a DATA
 * XREF from the $18DE4 script/anim dispatch table)
 *   in: a3 = on-ice object
 * If the frame cursor $5A(a3) is in [8,$18), toggles bit 1 of the facing
 * byte $54(a3) (an 8-way facing flip). Either way, sets bit 2 of $54(a3),
 * resets the frame cursor to 0, and force-sets the frame-countdown byte
 * $5C(a3) to $FF (st). Exact role TBD; behaviour preserved bit-for-bit.
 */
void Anim_ForceIdleFacingFlip(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_cmpw(c, 8, lift_r16(x, a3 + 0x5A));            /* cmp.w #8,$5A(a3) */
  lift_charge(x, 0x102A8);
  int lt = (c->nf != c->vf);                          /* blt.w loc_102C2 */
  lift_charge_bcc(x, 0x102AE, lt);

  if (!lt)
  {
    alu_cmpw(c, 0x18, lift_r16(x, a3 + 0x5A));        /* cmp.w #$18,$5A(a3) */
    lift_charge(x, 0x102B2);
    int ge = (c->nf == c->vf);                         /* bge.w loc_102C2 */
    lift_charge_bcc(x, 0x102B8, ge);
    if (!ge)
    {
      lift_w16(x, a3 + 0x54, alu_movew(c, lift_r16(x, a3 + 0x54) ^ 2));  /* eor.w #2,$54(a3) */
      lift_charge(x, 0x102BC);
    }
  }

  /* loc_102C2 */
  lift_w16(x, a3 + 0x54, alu_movew(c, lift_r16(x, a3 + 0x54) | 4));  /* or.w #4,$54(a3) */
  lift_charge(x, 0x102C2);
  lift_w16(x, a3 + 0x5A, alu_movew(c, 0));            /* clr.w $5A(a3) */
  lift_charge(x, 0x102C8);
  lift_w8(x, a3 + 0x5C, 0xFF);                        /* st $5C(a3) */
  lift_charge(x, 0x102CC);

  lift_charge(x, 0x102D0);                            /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Script_DecodeRotatedField (sub_11B3C; called from a script dispatch
 * jump table at $11AFC)
 *   in:  a1 = script cursor, d3 = remaining command count
 *   out: reads one byte from (a1)+, masks to 3 bits, rotates it into the
 *        top 3 bits of a word (ror.w #3), and stores that word to
 *        $FFFFB02C; d3 decremented
 */
void Script_DecodeRotatedField(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d2 */
  c->a[1] += 1;
  lift_charge(x, 0x11B3C);
  setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));            /* and.w #7,d2 */
  lift_charge(x, 0x11B3E);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));            /* subq.w #1,d3 */
  lift_charge(x, 0x11B42);
  setw(&c->d[2], alu_rorw(c, W(c->d[2]), 3));            /* ror.w #3,d2 */
  lift_charge(x, 0x11B44);
  lift_w16(x, 0xFFFFB02Cu, alu_movew(c, W(c->d[2])));    /* move.w d2,(abs) */
  lift_charge(x, 0x11B46);

  lift_charge(x, 0x11B4A);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_StartScript46A (sub_102D2; called from sub_14620/sub_14C70)
 *   in: a3 = object, d0 = bit to toggle into the facing byte
 * Toggles bit0 of the facing byte $54(a3) with d0's low bit, masks
 * facing to 2 bits, force-expires the frame countdown $5C(a3), then
 * tail-jumps into Anim_SetScript with script offset $46A — its rts
 * returns to this routine's caller.
 */
void Anim_StartScript46A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  setw(&c->d[0], alu_andw(c, 1, W(c->d[0])));           /* and.w #1,d0 */
  lift_charge(x, 0x102D2);
  lift_w16(x, a3 + 0x54, alu_movew(c, lift_r16(x, a3 + 0x54) ^ W(c->d[0])));  /* eor.w d0,$54(a3) */
  lift_charge(x, 0x102D6);
  lift_w16(x, a3 + 0x54, alu_andw(c, 3, lift_r16(x, a3 + 0x54)));  /* and.w #3,$54(a3) */
  lift_charge(x, 0x102DA);
  lift_w8(x, a3 + 0x5C, 0xFF);                            /* st $5C(a3) */
  lift_charge(x, 0x102E0);
  setw(&c->d[1], alu_movew(c, 0x46A));                    /* move.w #$46A,d1 */
  lift_charge(x, 0x102E4);
  lift_charge(x, 0x102E8);                                /* bra.w sub_1073A */

  Anim_SetScript(x);                                       /* tail jump */
}

/*
 * Anim_ResetLineChange (sub_170CA; called from sub_170B0)
 *   in: a2 = team block (uses $22(a2) as the base of a 6-slot, $80-
 *        stride sub-table — the on-ice line, presumably)
 * Clears bit4 of $30(a2), then for each of 6 slots starting at
 * $22(a2): clears $63(slot); if $34(slot) is non-negative, starts
 * animation script $50C (Anim_SetScript), clears $32/$5E(slot), and
 * ANDs $62(slot) with $C2 (clears several status bits).
 */
void Anim_ResetLineChange(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, c->a[2] + 0x30, alu_bclr(c, lift_r8(x, c->a[2] + 0x30), 4));  /* bclr #4,$30(a2) */
  lift_charge(x, 0x170CA);
  c->d[2] = alu_moveql(c, 5);                              /* moveq #5,d2 */
  lift_charge(x, 0x170D0);
  c->a[3] = SEW(lift_r16(x, c->a[2] + 0x22));              /* movea.w $22(a2),a3 */
  lift_charge(x, 0x170D2);

  for (;;)
  {
    /* loc_170D6 */
    lift_w8(x, c->a[3] + 0x63, 0);                          /* clr.b $63(a3) */
    lift_charge(x, 0x170D6);
    alu_movew(c, lift_r16(x, c->a[3] + 0x34));              /* tst.w $34(a3) */
    lift_charge(x, 0x170DA);
    int none = c->nf;                                        /* bmi.w loc_170F8 */
    lift_charge_bcc(x, 0x170DE, none);
    if (!none)
    {
      setw(&c->d[1], alu_movew(c, 0x50C));                  /* move.w #$50C,d1 */
      lift_charge(x, 0x170E2);
      lift_call(x, 0x170E6, 4, Anim_SetScript);             /* bsr.w sub_1073A */
      if (x->declined) return;
      lift_w16(x, c->a[3] + 0x32, alu_movew(c, 0));         /* clr.w $32(a3) */
      lift_charge(x, 0x170EA);
      lift_w8(x, c->a[3] + 0x5E, 0);                         /* clr.b $5E(a3) */
      lift_charge(x, 0x170EE);
      {
        uint32_t r = lift_r8(x, c->a[3] + 0x62) & 0xC2;      /* and.b #$C2,$62(a3) */
        alu_moveb(c, r);
        lift_w8(x, c->a[3] + 0x62, r);
      }
      lift_charge(x, 0x170F2);
    }

    /* loc_170F8 */
    c->a[3] += 0x80;                                          /* add.w #$80,a3 */
    lift_charge(x, 0x170F8);
    uint32_t nd2 = W(W(c->d[2]) - 1);                          /* dbf d2,loc_170D6 */
    setw(&c->d[2], nd2);
    int taken = (nd2 != 0xFFFF);
    lift_charge_dbcc(x, 0x170FC, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x17100);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_ResetLineChangeBothTeams (sub_170B0; called from sub_EF92 and
 * ROM:F9F0)
 * Runs Anim_ResetLineChange for the home team block, then the away
 * team block. d0-d2/a0-a3 saved/restored around the whole body.
 */
void Anim_ResetLineChangeBothTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d[3] = { c->d[0], c->d[1], c->d[2] };
  uint32_t saved_a[4] = { c->a[0], c->a[1], c->a[2], c->a[3] };

  /* movem.l d0-d2/a0-a3,-(sp): a3 pushed first (high addr) ... d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[3]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[2]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d[0]);
  lift_charge_movem(x, 0x170B0);

  c->a[2] = 0xFFFFC6CEu;                                  /* movea.w #$C6CE,a2 */
  lift_charge(x, 0x170B4);
  lift_call(x, 0x170B8, 4, Anim_ResetLineChange);         /* bsr.w sub_170CA */
  if (x->declined) return;
  c->a[2] = 0xFFFFCA32u;                                  /* movea.w #$CA32,a2 */
  lift_charge(x, 0x170BC);
  lift_call(x, 0x170C0, 4, Anim_ResetLineChange);         /* bsr.w sub_170CA */
  if (x->declined) return;

  /* movem.l (sp)+,d0-d2/a0-a3 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x170C4);

  lift_charge(x, 0x170C8);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}
