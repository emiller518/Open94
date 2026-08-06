/*
 * overlay.c — on-ice overlay markers: player stars + jersey-number boards
 * (lifted).
 *
 * Seven $1C-byte overlay objects live at $FFBDB4. Entries 0-5 track on-ice
 * players (the star under the controlled player, plus number boards);
 * entry 6 ($FFBE5C) is a free cursor positioned from the byte coords at
 * $FFBEE2/E3. Overlay objects share the drawable-object layout the render
 * pipeline expects: +0 world X, +4 attr, +6 anim frame, +$12 VRAM tile
 * base, +$14 world Y, +$18 visibility word (st -> hidden, clr -> shown);
 * +2 is a board X-adjust applied to the first two emitted sprite pieces.
 *
 * Which players are tracked comes packed as 4-bit object indices in the
 * word at $FFBE78 (entry 1 instead uses $FFBE86 >> 4): each nibble selects
 * a $80-byte player block at $FFB04A. Nibble $E = leave the overlay as it
 * is but refresh its label; $F = hide it. A tracked player's label word
 * (position code byte $35 | jersey number byte $6F) is cached per entry at
 * $FFBE7A; when it changes, the 3-glyph board (tens, ones, position
 * suffix) is redrawn by queueing tile uploads from the Art_BoardText set.
 */
#include "util68k.h"

#define OVERLAY_TABLE   0xFFFFBDB4u  /* 7 x $1C overlay objects */
#define OVERLAY_CURSOR  0xFFFFBE5Cu  /* table entry 6: the free cursor */
#define PLAYER_TABLE    0xFFFFB04Au  /* $80-byte player blocks */
#define LABEL_CACHE     0xFFFFBE7Au  /* 6 words: last label drawn */
#define R_TRACK_NIBBLES 0xFFBE78u    /* packed 4-bit player indices */
#define R_TRACK_ALT     0xFFBE86u    /* entry-1 index source (>> 4) */
#define R_CURSOR_XB     0xFFBEE2u    /* cursor X byte (*4 = world) */
#define R_CURSOR_YB     0xFFBEE3u
#define R_NO_SUFFIX     0xFFC2FCu    /* bit 7: suppress position suffix */
#define ART_BOARDTEXT   0xAAC52u     /* ROM: board glyph set (Art_BoardText) */
#define GLYPH_SUFFIX    0x1677Au     /* ROM: " DDLCRX" suffix characters */

void Render_DrawObject(lift_ctx *);  /* render.c */
void Sprite_EmitObject(lift_ctx *);  /* render.c */

#define R_CAMERA_Y      0xFFBD18u
#define R_CAMERA_X      0xFFBD1Cu
#define R_RINK_FLIP     0xFFC2ECu    /* bit 7: rink drawn flipped */
#define R_ARROW_ALWAYS1 0xFFC2FAu    /* bit 0 \                        */
#define R_ARROW_ALWAYS2 0xFFC2EEu    /* bit 3  > any set: |X| gate on */
#define R_ARROW_ALWAYS3 0xFFC2F2u    /* bit 2 /                        */
#define DIR_TO_ENTRY    0x113C0u     /* ROM: edge bitmask -> arrow entry */
#define ARROW_TABLE     0x165BCu     /* ROM: 8 x {x.w, y.w, frame+.w, attr.w} */
#define SHARED_RTS      0x15464u     /* far rts several routines branch to */

/*
 * Render_DrawOffscreenArrow (sub_164D6; called up to 4 times from
 * Overlay_DrawTrackedArrows below — 3 via bsr, the 4th by fall-through)
 *   in:  a0 = tracked object, a3 = arrow indicator object, d3 = arrow
 *        base frame index, a5/a6/d6 = render pipeline state
 * When the tracked object is off-screen, draw an edge-of-screen arrow
 * pointing at it. The indicator defaults to hidden (st on its frame
 * byte); bails out entirely if the object itself is hidden, or — when
 * any "gate" flag is set — if the object is far outside the rink
 * (|world X| >= $A6). Otherwise the camera-relative position is tested
 * against the view box (X in [-$74,$74), Y in [-$64,$64)); each edge
 * crossed sets a bit, the bitmask picks an arrow entry via DIR_TO_ENTRY,
 * and the entry supplies clamped screen coords (0 = keep the object's),
 * a frame delta added into d3, and flip attributes. Ends with a bra
 * into Sprite_EmitObject, whose rts returns to this routine's caller.
 * d0-d4/a1 come back modified (Sprite_EmitObject restores its own set).
 */
void Render_DrawOffscreenArrow(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a0 = c->a[0], a3 = c->a[3];

  alu_movew(c, lift_r16(x, a0 + 0x18));           /* tst.w $18(a0) */
  lift_charge(x, 0x164D6);
  int hidden = c->nf;
  lift_charge_bcc(x, 0x164DA, hidden);
  if (hidden)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_w8(x, a3 + 6, 0xFF);                       /* st: default hidden */
  lift_charge(x, 0x164DE);
  setw(&c->d[0], alu_movew(c, lift_r16(x, a0))); /* world X */
  lift_charge(x, 0x164E2);
  alu_btst(c, lift_r8(x, R_ARROW_ALWAYS1), 0);
  lift_charge(x, 0x164E4);
  int gate = !c->zf;
  lift_charge_bcc(x, 0x164EA, gate);
  if (!gate)
  {
    alu_btst(c, lift_r8(x, R_ARROW_ALWAYS2), 3);
    lift_charge(x, 0x164EE);
    gate = !c->zf;
    lift_charge_bcc(x, 0x164F4, gate);
  }
  if (!gate)
  {
    alu_btst(c, lift_r8(x, R_ARROW_ALWAYS3), 2);
    lift_charge(x, 0x164F8);
    gate = !c->zf;
    lift_charge_bcc(x, 0x164FE, !gate);
  }
  if (gate)                                       /* loc_16502: |X| gate */
  {
    c->a[7] -= 2;                                 /* push d0 */
    lift_w16(x, c->a[7], alu_movew(c, W(c->d[0])));
    lift_charge(x, 0x16502);
    alu_movew(c, W(c->d[0]));                     /* tst.w d0 */
    lift_charge(x, 0x16504);
    int pos = !c->nf;
    lift_charge_bcc(x, 0x16506, pos);
    if (!pos)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));
      lift_charge(x, 0x1650A);
    }
    alu_cmpw(c, 0xA6, W(c->d[0]));
    lift_charge(x, 0x1650C);
    int near = (c->nf != c->vf);                  /* blt */
    lift_charge_bcc(x, 0x16510, near);
    if (!near)
    {
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));  /* pop d0 */
      c->a[7] += 2;
      lift_charge(x, 0x16514);
      lift_charge(x, 0x16516);                    /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));
    c->a[7] += 2;
    lift_charge(x, 0x16518);
  }
  /* loc_1651A: camera-relative position (same transform as
   * Render_WorldToScreen's first half) */
  setw(&c->d[1], alu_movew(c, lift_r16(x, a0 + 0x14)));
  lift_charge(x, 0x1651A);
  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);
  lift_charge(x, 0x1651E);
  int normal = c->zf;
  lift_charge_bcc(x, 0x16524, normal);
  if (!normal)
  {
    uint32_t t = c->d[0];                         /* exg d0,d1 */
    c->d[0] = c->d[1];
    c->d[1] = t;
    lift_charge(x, 0x16528);
    setw(&c->d[0], alu_negw(c, W(c->d[0])));
    lift_charge(x, 0x1652A);
    setw(&c->d[1], alu_subw(c, 0xC5, W(c->d[1])));
    lift_charge(x, 0x1652C);
    lift_charge(x, 0x16530);                      /* bra.w */
  }
  else
  {
    setw(&c->d[0], alu_subw(c, lift_r16(x, R_CAMERA_X), W(c->d[0])));
    lift_charge(x, 0x16534);
    setw(&c->d[1], alu_subw(c, lift_r16(x, R_CAMERA_Y), W(c->d[1])));
    lift_charge(x, 0x16538);
  }
  /* loc_1653C: which view-box edges does the object lie beyond? */
  setw(&c->d[2], alu_movew(c, 0));
  lift_charge(x, 0x1653C);
  alu_cmpw(c, 0x74, W(c->d[0]));
  lift_charge(x, 0x1653E);
  int in = (c->nf != c->vf);                      /* blt */
  lift_charge_bcc(x, 0x16542, in);
  if (!in)
  {
    c->d[2] = alu_bset(c, c->d[2], 3);            /* beyond right */
    lift_charge(x, 0x16546);
  }
  alu_cmpw(c, 0xFF8C, W(c->d[0]));
  lift_charge(x, 0x1654A);
  in = (!c->zf && c->nf == c->vf);                /* bgt */
  lift_charge_bcc(x, 0x1654E, in);
  if (!in)
  {
    c->d[2] = alu_bset(c, c->d[2], 2);            /* beyond left */
    lift_charge(x, 0x16552);
  }
  alu_cmpw(c, 0x64, W(c->d[1]));
  lift_charge(x, 0x16556);
  in = (c->nf != c->vf);
  lift_charge_bcc(x, 0x1655A, in);
  if (!in)
  {
    c->d[2] = alu_bset(c, c->d[2], 0);            /* beyond far end */
    lift_charge(x, 0x1655E);
  }
  alu_cmpw(c, 0xFF9C, W(c->d[1]));
  lift_charge(x, 0x16562);
  in = (!c->zf && c->nf == c->vf);
  lift_charge_bcc(x, 0x16566, in);
  if (!in)
  {
    c->d[2] = alu_bset(c, c->d[2], 1);            /* beyond near end */
    lift_charge(x, 0x1656A);
  }
  alu_movew(c, W(c->d[2]));                       /* tst.w d2 */
  lift_charge(x, 0x1656E);
  int onscreen = c->zf;
  lift_charge_bcc(x, 0x16570, onscreen);
  if (onscreen)
  {
    lift_charge(x, SHARED_RTS);                   /* far rts: no arrow */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  c->a[1] = DIR_TO_ENTRY;
  lift_charge(x, 0x16574);
  c->d[2] = (c->d[2] & ~0xFFu) |
            alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[2])));
  lift_charge(x, 0x1657A);
  setw(&c->d[2], alu_aslw(c, W(c->d[2]), 3));     /* entry * 8 bytes */
  lift_charge(x, 0x1657E);
  c->a[1] = ARROW_TABLE;
  lift_charge(x, 0x16580);
  setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[1] + SW(c->d[2]))));
  lift_charge(x, 0x16586);
  int keep = c->zf;                               /* 0 = keep object's X */
  lift_charge_bcc(x, 0x1658A, keep);
  if (!keep)
  {
    setw(&c->d[0], alu_movew(c, W(c->d[4])));     /* clamp to edge */
    lift_charge(x, 0x1658E);
  }
  setw(&c->d[0], alu_addw(c, 0x100, W(c->d[0]))); /* sprite coord space */
  lift_charge(x, 0x16590);
  lift_w16(x, a3, alu_movew(c, W(c->d[0])));
  lift_charge(x, 0x16594);
  setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[1] + 2 + SW(c->d[2]))));
  lift_charge(x, 0x16596);
  keep = c->zf;
  lift_charge_bcc(x, 0x1659A, keep);
  if (!keep)
  {
    setw(&c->d[1], alu_movew(c, W(c->d[4])));
    lift_charge(x, 0x1659E);
  }
  setw(&c->d[1], alu_negw(c, W(c->d[1])));        /* screen Y = $F0 - y */
  lift_charge(x, 0x165A0);
  setw(&c->d[1], alu_addw(c, 0xF0, W(c->d[1])));
  lift_charge(x, 0x165A2);
  lift_w16(x, a3 + 2, alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x165A6);
  setw(&c->d[3], alu_addw(c, lift_r16(x, c->a[1] + 4 + SW(c->d[2])),
                          W(c->d[3])));           /* arrow frame */
  lift_charge(x, 0x165AA);
  lift_w16(x, a3 + 6, alu_movew(c, W(c->d[3])));
  lift_charge(x, 0x165AE);
  lift_w16(x, a3 + 4,                             /* flip attrs */
           alu_movew(c, lift_r16(x, c->a[1] + 6 + SW(c->d[2]))));
  lift_charge(x, 0x165B2);
  lift_charge(x, 0x165B8);                        /* bra.w sub_167AA */
  Sprite_EmitObject(x);      /* tail: its rts pops our caller's return */
}

#define ARROW_OVERLAY_1  0xFFFFBDD0u   /* OVERLAY_TABLE entry 1 */
#define ARROW_OVERLAY_2  0xFFFFBDECu   /* OVERLAY_TABLE entry 2 (BDD0 + $1C) */
#define ARROW_OVERLAY_4  0xFFFFBE24u   /* OVERLAY_TABLE entry 4 */
#define ARROW_OVERLAY_5  0xFFFFBE40u   /* OVERLAY_TABLE entry 5 (BE24 + $1C) */
#define ARROW_OBJECTS    0xFFFFBE88u   /* 4 x $14-byte arrow-indicator objects */
#define R_ARROW_GATE_4   0xFFFFC32Cu   /* nonzero: draw the entry-4 arrow */
#define R_ARROW_GATE_5   0xFFFFC32Eu   /* nonzero: draw the entry-5 arrow */

/*
 * Overlay_DrawTrackedArrows (sub_16480; called from sub_15EC0)
 * Skips entirely when the rink is drawn flipped (R_RINK_FLIP bit 7 set).
 * Otherwise draws off-screen arrows, via Render_DrawOffscreenArrow, for
 * OVERLAY_TABLE entries 1 and 2 unconditionally, then entries 4 and 5
 * gated by R_ARROW_GATE_4/5 — bailing (shared rts) the moment a gate
 * reads zero. Each tracked overlay entry is paired with the next
 * $14-byte slot in the 4-entry ARROW_OBJECTS table; d3 supplies the
 * per-pairing arrow frame base. The last call is a fall-through tail:
 * its rts returns to this routine's own caller.
 */
void Overlay_DrawTrackedArrows(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);
  lift_charge(x, 0x16480);
  int flipped = !c->zf;
  lift_charge_bcc(x, 0x16486, flipped);
  if (flipped)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = ARROW_OVERLAY_1;                      /* movea.w #$BDD0,a0 */
  lift_charge(x, 0x1648A);
  c->a[3] = ARROW_OBJECTS;                        /* movea.w #$BE88,a3 */
  lift_charge(x, 0x1648E);
  setw(&c->d[3], alu_movew(c, 0x180));
  lift_charge(x, 0x16492);
  lift_call(x, 0x16496, 4, Render_DrawOffscreenArrow);

  c->a[0] += 0x1C;                                /* adda.w #$1C,a0: no flags */
  lift_charge(x, 0x1649A);
  c->a[3] += 0x14;                                /* adda.w #$14,a3: no flags */
  lift_charge(x, 0x1649E);
  setw(&c->d[3], alu_movew(c, 0x183));
  lift_charge(x, 0x164A2);
  lift_call(x, 0x164A6, 4, Render_DrawOffscreenArrow);

  alu_movew(c, lift_r16(x, R_ARROW_GATE_4));      /* tst.w */
  lift_charge(x, 0x164AA);
  int noGate4 = c->zf;                            /* beq taken: value == 0 */
  lift_charge_bcc(x, 0x164AE, noGate4);
  if (noGate4)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = ARROW_OVERLAY_4;                      /* movea.w #$BE24,a0 */
  lift_charge(x, 0x164B2);
  c->a[3] += 0x14;
  lift_charge(x, 0x164B6);
  setw(&c->d[3], alu_movew(c, 0x346));
  lift_charge(x, 0x164BA);
  lift_call(x, 0x164BE, 4, Render_DrawOffscreenArrow);

  alu_movew(c, lift_r16(x, R_ARROW_GATE_5));      /* tst.w */
  lift_charge(x, 0x164C2);
  int noGate5 = c->zf;                            /* beq taken: value == 0 */
  lift_charge_bcc(x, 0x164C6, noGate5);
  if (noGate5)
  {
    lift_charge(x, SHARED_RTS);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = ARROW_OVERLAY_5;                      /* movea.w #$BE40,a0 */
  lift_charge(x, 0x164CA);
  c->a[3] += 0x14;
  lift_charge(x, 0x164CE);
  setw(&c->d[3], alu_movew(c, 0x349));
  lift_charge(x, 0x164D2);
  Render_DrawOffscreenArrow(x);   /* fall-through tail: its rts pops our caller's return */
}

/*
 * Overlay_QueueGlyph (sub_1674A; bsr'd twice from Overlay_DrawNumber and
 * entered a third time by fall-through)
 *   in:  d2 = glyph character code, d0 = board slot (0-2), a0 = overlay
 *        object, a5 = tile-DMA queue write ptr
 * Queue one 32-byte glyph tile upload: source looked up in Art_BoardText's
 * directory (entry & $7FF is the tile number, data at +$A), destination
 * VRAM slot = board slot + the object's tile base at $12(a0).
 */
void Overlay_QueueGlyph(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[3] = ART_BOARDTEXT;
  lift_charge(x, 0x1674A);
  c->a[3] += lift_r32(x, c->a[3] + 4);            /* adda.l: directory */
  lift_charge(x, 0x16750);
  setw(&c->d[2], alu_addw(c, W(c->d[2]), W(c->d[2])));
  lift_charge(x, 0x16754);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 4 + SW(c->d[2]))));
  lift_charge(x, 0x16756);
  setw(&c->d[2], alu_andw(c, 0x7FF, W(c->d[2])));
  lift_charge(x, 0x1675A);
  setw(&c->d[2], alu_aslw(c, W(c->d[2]), 5));     /* tile# * 32 bytes */
  lift_charge(x, 0x1675E);
  c->a[3] = ART_BOARDTEXT;
  lift_charge(x, 0x16760);
  c->a[3] = ART_BOARDTEXT + 0xA + SW(c->d[2]);    /* lea: no flags */
  lift_charge(x, 0x16766);
  lift_w32(x, c->a[5], alu_movel(c, c->a[3]));    /* queue: src */
  c->a[5] += 4;
  lift_charge(x, 0x1676A);
  lift_w16(x, c->a[5], alu_movew(c, 0x10));       /* len: 1 tile in words */
  c->a[5] += 2;
  lift_charge(x, 0x1676C);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[0] + 0x12), W(c->d[0])));
  lift_charge(x, 0x16770);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 5));     /* slot -> VRAM addr */
  lift_charge(x, 0x16774);
  lift_w16(x, c->a[5], alu_movew(c, W(c->d[0]))); /* dest */
  c->a[5] += 2;
  lift_charge(x, 0x16776);
  lift_charge(x, 0x16778);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Overlay_DrawNumber (sub_166E6; called from Overlay_TrackTargets)
 *   in:  d1 = label word (high byte: position code, low byte: BCD jersey
 *        number), a0 = overlay object, a5 = tile-DMA queue
 * Queue the board's three glyphs: tens digit (blanked and the board
 * narrowed 4px when zero), ones digit ($F = blank), and the position
 * suffix from " DDLCRX" (blanked, widening the board 4px, when the code
 * is 0 or bit 7 of $FFC2FC suppresses suffixes). The third glyph is
 * emitted by falling through into Overlay_QueueGlyph, whose rts returns
 * to this routine's caller.
 */
void Overlay_DrawNumber(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[4] = GLYPH_SUFFIX;                         /* lea (pc): no flags */
  lift_charge(x, 0x166E6);
  lift_w16(x, c->a[0] + 2, alu_movew(c, 0));      /* clr.w 2(a0) */
  lift_charge(x, 0x166EA);
  setw(&c->d[2], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x166EE);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 4));
  lift_charge(x, 0x166F0);
  setw(&c->d[2], alu_andw(c, 0xF, W(c->d[2])));   /* tens digit */
  lift_charge(x, 0x166F2);
  int tens = !c->zf;
  lift_charge_bcc(x, 0x166F6, tens);
  if (!tens)
  {
    setw(&c->d[2], alu_movew(c, 0xFFF0));         /* blank glyph */
    lift_charge(x, 0x166FA);
    lift_w16(x, c->a[0] + 2,                      /* subq.w #4,2(a0) */
             alu_subw(c, 4, lift_r16(x, c->a[0] + 2)));
    lift_charge(x, 0x166FE);
  }
  setw(&c->d[2], alu_addw(c, 0x30, W(c->d[2])));  /* '0' + digit */
  lift_charge(x, 0x16702);
  setw(&c->d[0], alu_movew(c, 0));                /* slot 0 */
  lift_charge(x, 0x16706);
  lift_call(x, 0x16708, 4, Overlay_QueueGlyph);
  if (x->declined) return;

  setw(&c->d[2], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x1670C);
  setw(&c->d[2], alu_andw(c, 0xF, W(c->d[2])));   /* ones digit */
  lift_charge(x, 0x1670E);
  alu_cmpw(c, 0xF, W(c->d[2]));
  lift_charge(x, 0x16712);
  int blank = c->zf;
  lift_charge_bcc(x, 0x16716, !blank);
  if (blank)
  {
    setw(&c->d[2], alu_movew(c, 0xFFF0));
    lift_charge(x, 0x1671A);
  }
  setw(&c->d[2], alu_addw(c, 0x30, W(c->d[2])));
  lift_charge(x, 0x1671E);
  c->d[0] = alu_moveql(c, 1);                     /* slot 1 */
  lift_charge(x, 0x16722);
  lift_call(x, 0x16724, 4, Overlay_QueueGlyph);
  if (x->declined) return;

  setw(&c->d[2], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0x16728);
  alu_btst(c, lift_r8(x, R_NO_SUFFIX), 7);
  lift_charge(x, 0x1672A);
  int suffix = !c->zf;
  lift_charge_bcc(x, 0x16730, !suffix);
  if (suffix)
  {
    setw(&c->d[2], alu_movew(c, 0));              /* clr.w d2: no suffix */
    lift_charge(x, 0x16734);
  }
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 8));
  lift_charge(x, 0x16736);
  setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));     /* position code */
  lift_charge(x, 0x16738);
  int pos = !c->zf;
  lift_charge_bcc(x, 0x1673C, pos);
  if (!pos)
  {
    lift_w16(x, c->a[0] + 2,                      /* addq.w #4,2(a0) */
             alu_addw(c, 4, lift_r16(x, c->a[0] + 2)));
    lift_charge(x, 0x16740);
  }
  c->d[2] = (c->d[2] & ~0xFFu) |
            alu_moveb(c, lift_r8(x, c->a[4] + SW(c->d[2])));
  lift_charge(x, 0x16744);
  c->d[0] = alu_moveql(c, 2);                     /* slot 2 */
  lift_charge(x, 0x16748);
  Overlay_QueueGlyph(x);       /* fall-through: its rts pops our caller */
}

/*
 * Overlay_TrackTargets (sub_16648; called from Render_DrawOverlays)
 * Reposition all seven overlay objects for this frame: the free cursor
 * from $FFBEE2/E3 (hidden when X is zero), then entries 0-5 from the
 * packed player indices — copying each tracked player's world position,
 * unhiding the overlay, and redrawing its number board when the player's
 * label changed.
 */
void Overlay_TrackTargets(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = OVERLAY_CURSOR;                       /* movea.w sign-extends */
  lift_charge(x, 0x16648);
  lift_w8(x, c->a[0] + 0x18, 0xFF);               /* st: hide, no flags */
  lift_charge(x, 0x1664C);
  c->d[0] = (c->d[0] & ~0xFFu) | alu_moveb(c, lift_r8(x, R_CURSOR_XB));
  lift_charge(x, 0x16650);
  int off = c->zf;
  lift_charge_bcc(x, 0x16654, off);
  if (!off)
  {
    c->d[1] = (c->d[1] & ~0xFFu) | alu_moveb(c, lift_r8(x, R_CURSOR_YB));
    lift_charge(x, 0x16658);
    setw(&c->d[0], alu_extw(c, c->d[0]));
    lift_charge(x, 0x1665C);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));   /* byte coord * 4 */
    lift_charge(x, 0x1665E);
    lift_w16(x, c->a[0], alu_movew(c, W(c->d[0])));
    lift_charge(x, 0x16660);
    setw(&c->d[1], alu_extw(c, c->d[1]));
    lift_charge(x, 0x16662);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));
    lift_charge(x, 0x16664);
    lift_w16(x, c->a[0] + 0x14, alu_movew(c, W(c->d[1])));
    lift_charge(x, 0x16666);
    lift_w16(x, c->a[0] + 0x18, alu_movew(c, 0)); /* clr.w: show */
    lift_charge(x, 0x1666A);
  }

  setw(&c->d[4], alu_movew(c, 5));
  lift_charge(x, 0x1666E);
  c->a[0] = OVERLAY_TABLE;
  lift_charge(x, 0x16672);
  c->a[1] = LABEL_CACHE;
  lift_charge(x, 0x16676);
  c->a[2] = PLAYER_TABLE;
  lift_charge(x, 0x1667A);
  setw(&c->d[3], alu_movew(c, lift_r16(x, R_TRACK_NIBBLES)));
  lift_charge(x, 0x1667E);
  for (;;)
  {
    alu_cmpw(c, 1, W(c->d[4]));
    lift_charge(x, 0x16682);
    int alt = c->zf;                              /* entry 1: other source */
    lift_charge_bcc(x, 0x16686, !alt);
    if (alt)
    {
      setw(&c->d[3], alu_movew(c, lift_r16(x, R_TRACK_ALT)));
      lift_charge(x, 0x1668A);
      setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 4));
      lift_charge(x, 0x1668E);
      lift_charge(x, 0x16690);                    /* bra.w to next insn */
    }
    setw(&c->d[0], alu_movew(c, W(c->d[3])));
    lift_charge(x, 0x16694);
    setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0]))); /* player index */
    lift_charge(x, 0x16696);
    setw(&c->d[1], alu_movew(c, 0xF));            /* default label */
    lift_charge(x, 0x1669A);
    alu_cmpw(c, 0xE, W(c->d[0]));
    lift_charge(x, 0x1669E);
    int keep = c->zf;              /* $E: leave overlay, refresh label */
    lift_charge_bcc(x, 0x166A2, keep);
    int skip_label = 0;
    if (!keep)
    {
      lift_w8(x, c->a[0] + 0x18, 0xFF);           /* st: hide */
      lift_charge(x, 0x166A6);
      alu_cmpw(c, 0xF, W(c->d[0]));
      lift_charge(x, 0x166AA);
      skip_label = c->zf;                         /* $F: stay hidden */
      lift_charge_bcc(x, 0x166AE, skip_label);
      if (!skip_label)
      {
        setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));  /* index * $80 */
        lift_charge(x, 0x166B2);
        lift_w16(x, c->a[0],
                 alu_movew(c, lift_r16(x, c->a[2] + SW(c->d[0]))));
        lift_charge(x, 0x166B4);
        lift_w16(x, c->a[0] + 0x14,
                 alu_movew(c, lift_r16(x, c->a[2] + 0x14 + SW(c->d[0]))));
        lift_charge(x, 0x166B8);
        lift_w16(x, c->a[0] + 0x18, alu_movew(c, 0));  /* show */
        lift_charge(x, 0x166BE);
        c->d[1] = (c->d[1] & ~0xFFu) |            /* position code */
                  alu_moveb(c, lift_r8(x, c->a[2] + 0x35 + SW(c->d[0])));
        lift_charge(x, 0x166C2);
        setw(&c->d[1], alu_aslw(c, W(c->d[1]), 8));
        lift_charge(x, 0x166C6);
        c->d[1] = (c->d[1] & ~0xFFu) |            /* jersey number */
                  alu_moveb(c, lift_r8(x, c->a[2] + 0x6F + SW(c->d[0])));
        lift_charge(x, 0x166C8);
      }
    }
    if (!skip_label)
    {
      alu_cmpw(c, lift_r16(x, c->a[1]), W(c->d[1]));
      lift_charge(x, 0x166CC);
      int same = c->zf;
      lift_charge_bcc(x, 0x166CE, same);
      if (!same)
      {
        lift_w16(x, c->a[1], alu_movew(c, W(c->d[1])));
        lift_charge(x, 0x166D2);
        lift_call(x, 0x166D4, 4, Overlay_DrawNumber);
        if (x->declined) return;
      }
    }
    setw(&c->d[3], alu_lsrw(c, W(c->d[3]), 4));   /* next nibble */
    lift_charge(x, 0x166D8);
    c->a[0] += 0x1C;
    lift_charge(x, 0x166DA);
    c->a[1] += 2;
    lift_charge(x, 0x166DE);
    uint32_t nd4 = W(W(c->d[4]) - 1);             /* dbf: no flags */
    setw(&c->d[4], nd4);
    int taken = (nd4 != 0xFFFF);
    lift_charge_dbcc(x, 0x166E0, taken, !taken);
    if (!taken) break;
  }
  lift_charge(x, 0x166E4);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Render_DrawOverlays (sub_1661A; called from sub_15EC0 right after
 * Render_DrawObjectList)
 * Reposition the overlays, then draw all seven through the normal object
 * pipeline. For each overlay that actually emitted sprites (the hardware
 * list pointer moved), add its board X-adjust from 2(a3) to the X of the
 * first two emitted pieces (a0 kept the old list position).
 */
void Render_DrawOverlays(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x1661A, 4, Overlay_TrackTargets);
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, 6));
  lift_charge(x, 0x1661E);
  c->a[3] = OVERLAY_TABLE;
  lift_charge(x, 0x16622);
  for (;;)
  {
    c->a[0] = SEW(c->a[6]);                       /* movea.w a6,a0 */
    lift_charge(x, 0x16626);
    lift_call(x, 0x16628, 4, Render_DrawObject);
    if (x->declined) return;
    alu_cmpl(c, SEW(c->a[6]), c->a[0]);           /* cmpa.w a6,a0 */
    lift_charge(x, 0x1662C);
    int none = c->zf;
    lift_charge_bcc(x, 0x1662E, none);
    if (!none)
    {
      setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 2)));
      lift_charge(x, 0x16632);
      lift_w16(x, c->a[0] + 6,                    /* add.w d1,6(a0) */
               alu_addw(c, W(c->d[1]), lift_r16(x, c->a[0] + 6)));
      lift_charge(x, 0x16636);
      lift_w16(x, c->a[0] + 0xE,                  /* add.w d1,$E(a0) */
               alu_addw(c, W(c->d[1]), lift_r16(x, c->a[0] + 0xE)));
      lift_charge(x, 0x1663A);
    }
    c->a[3] += 0x1C;
    lift_charge(x, 0x1663E);
    uint32_t nd0 = W(W(c->d[0]) - 1);             /* dbf */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x16642, taken, !taken);
    if (!taken) break;
  }
  lift_charge(x, 0x16646);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Overlay_SetTrackedNibble (sub_C656; called from sub_9FD0 and sub_A9D6,
 * likely the per-team tracked-player-index setters that feed
 * R_TRACK_NIBBLES above)
 *   in:  d4 = nibble selector (bit position = (2+d4)*2, mod 64), a3 =
 *        player object (uses $52(a3), the object's camera-zone field)
 * Clears the mask window rol'd from $FFF0 by that bit position in
 * R_TRACK_NIBBLES, then ORs in $52(a3) shifted into the same window.
 * d0/d1 are pushed/popped around the whole body, so despite being used as
 * scratch throughout, they end up unchanged on return (only flags from
 * the final or.w survive); d2-d7/a-regs untouched.
 */
void Overlay_SetTrackedNibble(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t entry_d0 = c->d[0], entry_d1 = c->d[1];

  /* movem.l d0-d1,-(sp): d1 pushed first (higher addr), d0 last (top of stack) */
  c->a[7] -= 4; lift_w32(x, c->a[7], entry_d1);
  c->a[7] -= 4; lift_w32(x, c->a[7], entry_d0);
  lift_charge_movem(x, 0xC656);

  uint32_t d0 = alu_moveql(c, 2);                 /* moveq #2,d0 */
  lift_charge(x, 0xC65A);
  d0 = alu_addw(c, W(c->d[4]), d0);               /* add.w d4,d0 */
  lift_charge(x, 0xC65C);
  d0 = alu_addw(c, d0, d0);                       /* add.w d0,d0 */
  lift_charge(x, 0xC65E);

  uint32_t d1 = alu_movew(c, 0xFFF0);             /* move.w #$FFF0,d1 */
  lift_charge(x, 0xC660);
  d1 = alu_rolw(c, d1, (int)(d0 & 63));            /* rol.w d0,d1 */
  lift_charge_shift_reg(x, 0xC664, (int)(d0 & 63));

  uint32_t track = alu_andw(c, d1, lift_r16(x, R_TRACK_NIBBLES));  /* and.w d1,(abs) */
  lift_w16(x, R_TRACK_NIBBLES, track);
  lift_charge(x, 0xC666);

  d1 = alu_movew(c, lift_r16(x, c->a[3] + 0x52));  /* move.w $52(a3),d1 */
  lift_charge(x, 0xC66A);
  d1 = alu_aslw(c, d1, (int)(d0 & 63));            /* asl.w d0,d1 */
  lift_charge_shift_reg(x, 0xC66E, (int)(d0 & 63));

  track = alu_movew(c, W(track) | W(d1));          /* or.w d1,(abs) */
  lift_w16(x, R_TRACK_NIBBLES, track);
  lift_charge(x, 0xC670);

  /* movem.l (sp)+,d0-d1: restores the entry values unchanged */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xC674);

  lift_charge(x, 0xC678);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_WriteTwoDigits (sub_18BDC; called from sub_12E12/sub_18A90 and
 * others — scoreboard/HUD text formatting)
 *   in:  d0 = byte value 0-255 (as two nibbles), a1 = output cursor
 *   out: writes 2 ASCII chars to (a1)+: the high nibble as a digit (or
 *        a space if it's zero, via the $F0+$30->$20 byte-truncation
 *        trick — suppresses a leading zero), then the low nibble always
 *        as a digit. d0 clobbered.
 */
void Text_WriteTwoDigits(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0];

  c->a[7] -= 2; lift_w16(x, c->a[7], alu_movew(c, W(saved_d0)));  /* move.w d0,-(sp) */
  lift_charge(x, 0x18BDC);

  setb(&c->d[0], alu_lsrb(c, W(c->d[0]) & 0xFF, 4));      /* lsr.b #4,d0 */
  lift_charge(x, 0x18BDE);
  int nz = !c->zf;                                          /* bne.w loc_18BE8 */
  lift_charge_bcc(x, 0x18BE0, nz);
  if (!nz)
  {
    setb(&c->d[0], alu_moveb(c, 0xF0));                     /* move.b #$F0,d0 */
    lift_charge(x, 0x18BE4);
  }

  /* loc_18BE8 */
  setb(&c->d[0], alu_addb(c, 0x30, W(c->d[0]) & 0xFF));     /* add.b #$30,d0 */
  lift_charge(x, 0x18BE8);
  lift_w8(x, c->a[1], alu_moveb(c, W(c->d[0])));            /* move.b d0,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x18BEC);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));       /* move.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0x18BEE);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));              /* and.w #$F,d0 */
  lift_charge(x, 0x18BF0);
  setb(&c->d[0], alu_addb(c, 0x30, W(c->d[0]) & 0xFF));      /* add.b #$30,d0 */
  lift_charge(x, 0x18BF4);
  lift_w8(x, c->a[1], alu_moveb(c, W(c->d[0])));             /* move.b d0,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x18BF8);

  lift_charge(x, 0x18BFA);                                   /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_TEXTBUF_LEN 0xFFFFBFA4u   /* word: text buffer length/cursor */

/*
 * Text_AlignBufferEven (sub_18BAE; called from sub_18AE8/sub_18B26)
 *   in: a1 = current write cursor into the buffer based at R_TEXTBUF_LEN
 *   If (a1 - R_TEXTBUF_LEN) is odd, writes a zero pad byte at (a1)+ and
 *   bumps the length by one. Stores the final length back to
 *   R_TEXTBUF_LEN. a1 clobbered.
 */
void Text_AlignBufferEven(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, W(c->a[1])));            /* move.w a1,d0 */
  lift_charge(x, 0x18BAE);
  setw(&c->d[0], alu_subw(c, R_TEXTBUF_LEN & 0xFFFF, W(c->d[0])));  /* sub.w #$BFA4,d0 */
  lift_charge(x, 0x18BB0);
  alu_btst(c, W(c->d[0]), 0);                            /* btst #0,d0 */
  lift_charge(x, 0x18BB4);
  int odd = !c->zf;                                        /* beq.w loc_18BC0 */
  lift_charge_bcc(x, 0x18BB8, !odd);
  if (odd)
  {
    lift_w8(x, c->a[1], 0);                                /* clr.b (a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0x18BBC);
    setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));            /* addq.w #1,d0 */
    lift_charge(x, 0x18BBE);
  }

  /* loc_18BC0 */
  c->a[1] = R_TEXTBUF_LEN;                                 /* movea.w #$BFA4,a1 */
  lift_charge(x, 0x18BC0);
  lift_w16(x, c->a[1], alu_movew(c, W(c->d[0])));          /* move.w d0,(a1) */
  lift_charge(x, 0x18BC4);

  lift_charge(x, 0x18BC6);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_AppendString (sub_11D9E; called from sub_11D96 and sub_799E)
 *   in:  a3 = pascal-style string (word length, then up to length-1
 *        chars — the routine treats the length as a max scan bound and
 *        relies on a null terminator within it), a1 = source stream
 *        (word max-count, then chars, 0-terminated)
 * Scans forward from a3+2 for the existing string's null terminator
 * (bounded by (a3)-3 bytes), then copies bytes from (a1)+ (bounded by
 * its own leading word - 3) until a null byte is copied or the count
 * expires. If the final write cursor is odd, appends one more null byte
 * to align it. Updates the length word at (a3) to the new total size.
 * d0/a0 saved/restored via the movem.
 */
void Text_AppendString(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];

  /* movem.l d0/a0,-(sp): a0 pushed first (high addr), d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0x11D9E);

  c->a[0] = a3 + 2;                                       /* lea 2(a3),a0 */
  lift_charge(x, 0x11DA2);
  setw(&c->d[0], alu_movew(c, lift_r16(x, a3)));           /* move.w (a3),d0 */
  lift_charge(x, 0x11DA6);
  setw(&c->d[0], alu_subw(c, 3, W(c->d[0])));              /* subq.w #3,d0 */
  lift_charge(x, 0x11DA8);
  int bmi1 = c->nf;                                          /* bmi.w loc_11DB6 */
  lift_charge_bcc(x, 0x11DAA, bmi1);

  if (!bmi1)
  {
    for (;;)
    {
      /* loc_11DAE */
      c->a[0] += 1;                                          /* addq.w #1,a0 */
      lift_charge(x, 0x11DAE);
      alu_moveb(c, lift_r8(x, c->a[0]));                      /* tst.b (a0) */
      lift_charge(x, 0x11DB0);
      int zero = c->zf;
      int taken = 0, expired = 0;
      if (!zero)
      {
        uint32_t nd0 = W(W(c->d[0]) - 1);
        setw(&c->d[0], nd0);
        expired = (nd0 == 0xFFFF);
        taken = !expired;
      }
      lift_charge_dbcc(x, 0x11DB2, taken, expired);           /* dbeq d0,loc_11DAE */
      if (zero || expired) break;
    }
  }

  /* loc_11DB6 */
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));        /* move.w (a1)+,d0 */
  c->a[1] += 2;
  lift_charge(x, 0x11DB6);
  setw(&c->d[0], alu_subw(c, 3, W(c->d[0])));                /* subq.w #3,d0 */
  lift_charge(x, 0x11DB8);
  int bmi2 = c->nf;                                            /* bmi.w loc_11DDC */
  lift_charge_bcc(x, 0x11DBA, bmi2);

  if (!bmi2)
  {
    for (;;)
    {
      /* loc_11DBE */
      lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,(a0)+ */
      c->a[1] += 1;
      c->a[0] += 1;
      lift_charge(x, 0x11DBE);
      int nz = !c->zf;                                          /* bne.w loc_11DC6 */
      lift_charge_bcc(x, 0x11DC0, nz);
      if (!nz)
      {
        c->a[0] -= 1;                                           /* subq.w #1,a0 */
        lift_charge(x, 0x11DC4);
      }

      /* loc_11DC6 */
      uint32_t nd0 = W(W(c->d[0]) - 1);                          /* dbf d0,loc_11DBE */
      setw(&c->d[0], nd0);
      int taken = (nd0 != 0xFFFF);
      lift_charge_dbcc(x, 0x11DC6, taken, !taken);
      if (!taken) break;
    }

    c->d[0] = alu_movel(c, c->a[0]);                            /* move.l a0,d0 */
    lift_charge(x, 0x11DCA);
    alu_btst(c, c->d[0], 0);                                    /* btst #0,d0 */
    lift_charge(x, 0x11DCC);
    int even = c->zf;                                             /* beq.w loc_11DD8 */
    lift_charge_bcc(x, 0x11DD0, even);
    if (!even)
    {
      lift_w8(x, c->a[0], 0);                                     /* clr.b (a0)+ */
      c->a[0] += 1;
      lift_charge(x, 0x11DD4);
      c->d[0] = alu_addl(c, 1, c->d[0]);                          /* addq.l #1,d0 */
      lift_charge(x, 0x11DD6);
    }

    /* loc_11DD8 */
    c->d[0] = alu_subl(c, a3, c->d[0]);                           /* sub.l a3,d0 */
    lift_charge(x, 0x11DD8);
    lift_w16(x, a3, alu_movew(c, W(c->d[0])));                    /* move.w d0,(a3) */
    lift_charge(x, 0x11DDA);
  }

  /* loc_11DDC: movem.l (sp)+,d0/a0 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x11DDC);

  lift_charge(x, 0x11DE0);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_WriteNullThenByteFwd (sub_F72DA; called from sub_F71A2)
 *   in:  a0 = output cursor
 *   Writes 2 bytes to (a0)+: a zero, then the byte at $FFFFBF20 (a
 *   fixed table, only the ODD byte of each word entry is meaningful —
 *   the following `tst.w (a1)+` just advances past the padding byte
 *   too, testing the full word but discarding the result). Runs once
 *   (dbf with d0=0). d0/d1/a0/a1 saved/restored.
 */
void Text_WriteNullThenByteFwd(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_a0 = c->a[0], saved_a1 = c->a[1];

  /* movem.l d0-d1/a0-a1,-(sp): a1 pushed first (high addr) ... d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF72DA);

  c->a[1] = 0xFFFFBF20u;                                   /* movea.l #$FFFFBF20,a1 */
  lift_charge(x, 0xF72DE);
  setw(&c->d[0], alu_movew(c, 0));                          /* move.w #0,d0 */
  lift_charge(x, 0xF72E4);

  for (;;)
  {
    /* loc_F72E8 */
    lift_w8(x, c->a[0], alu_moveb(c, 0));                    /* clr.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xF72E8);
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1])));       /* move.b (a1),d1 */
    lift_charge(x, 0xF72EA);
    lift_w8(x, c->a[0], alu_moveb(c, W(c->d[1])));           /* move.b d1,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xF72EC);
    alu_movew(c, lift_r16(x, c->a[1]));                       /* tst.w (a1)+ */
    c->a[1] += 2;
    lift_charge(x, 0xF72EE);
    uint32_t nd0 = W(W(c->d[0]) - 1);                         /* dbf d0,loc_F72E8 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xF72F0, taken, !taken);
    if (!taken) break;
  }

  /* movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF72F4);

  lift_charge(x, 0xF72F8);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_WriteNullThenByteBack (sub_F72FA; called from sub_F71A2)
 *   in:  a0 = output cursor
 *   Writes 2 bytes to (a0)+: a zero, then the byte at $FFFFBF2A. Then
 *   walks a1 BACKWARD (`tst.w -(a1)`, testing the word but discarding
 *   the result). Runs once (dbf with d0=0). d0/a0/a1 saved/restored.
 */
void Text_WriteNullThenByteBack(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0], saved_a1 = c->a[1];

  /* movem.l d0/a0-a1,-(sp): a1 pushed first (high addr), a0, d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xF72FA);

  c->a[1] = 0xFFFFBF2Au;                                   /* movea.l #$FFFFBF2A,a1 */
  lift_charge(x, 0xF72FE);
  setw(&c->d[0], alu_movew(c, 0));                          /* move.w #0,d0 */
  lift_charge(x, 0xF7304);

  for (;;)
  {
    /* loc_F7308 */
    lift_w8(x, c->a[0], alu_moveb(c, 0));                    /* clr.b (a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xF7308);
    lift_w8(x, c->a[0], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1),(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xF730A);
    c->a[1] -= 2;                                             /* tst.w -(a1) */
    alu_movew(c, lift_r16(x, c->a[1]));
    lift_charge(x, 0xF730C);
    uint32_t nd0 = W(W(c->d[0]) - 1);                         /* dbf d0,loc_F7308 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xF730E, taken, !taken);
    if (!taken) break;
  }

  /* movem.l (sp)+,d0/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF7312);

  lift_charge(x, 0xF7316);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Overlay_UpdateTrackedNudge(lift_ctx *);

/*
 * Overlay_ProcessTrackedEntries (sub_1257E; called from sub_124A6)
 *   Walks a table of 2-byte entries at $FFFFC3A4 until a zero
 *   terminator, running Overlay_UpdateTrackedNudge on each entry.
 */
void Overlay_ProcessTrackedEntries(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-d1/a0-a1,-(sp): push order a1,a0,d1,d0 */
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->a[0]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[1]);
  c->a[7] -= 4; lift_w32(x, c->a[7], c->d[0]);
  lift_charge_movem(x, 0x1257E);

  c->a[0] = 0xFFFFC3A4;                                            /* movea.w #$C3A4,a0 */
  lift_charge(x, 0x12582);

  for (;;)
  {
    /* loc_12586 */
    lift_call(x, 0x12586, 4, Overlay_UpdateTrackedNudge);           /* bsr.w sub_12596 */
    if (x->declined) return;
    c->a[0] += 2;                                                   /* addq.w #2,a0 */
    lift_charge(x, 0x1258A);
    alu_movew(c, lift_r16(x, c->a[0]));                             /* tst.w (a0) */
    lift_charge(x, 0x1258C);
    int bneTaken = !c->zf;
    lift_charge_bcc(x, 0x1258E, bneTaken);                          /* bne.s loc_12586 */
    if (!bneTaken) break;
  }

  /* movem.l (sp)+,d0-d1/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x12590);

  lift_charge(x, 0x12594);                                           /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Overlay_UpdateTrackedNudge (sub_12596; called from Overlay_ProcessTrackedEntries)
 *   in: a0 = tracked-entry pointer ($FFFFC3A4 table)
 *   Reads the entry's on-ice slot id from byte 1(a0) (masked to 0-127,
 *   scaled by 128 to index the object table at $FFFFB04A), and nudges
 *   R_UNK_BF96 by +/-$58 (sign from that object's $62 bit7) clamped
 *   against its current value, then nudges R_UNK_BF94 by +/-$46
 *   (sign from R_UNK_BF94's own current sign).
 */
void Overlay_UpdateTrackedNudge(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 1)));            /* move.b 1(a0),d0 */
  lift_charge(x, 0x12596);
  setw(&c->d[0], alu_andw(c, 0x7F, W(c->d[0])));                    /* and.w #$7F,d0 */
  lift_charge(x, 0x1259A);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));                       /* asl.w #7,d0 */
  lift_charge(x, 0x1259E);
  c->a[1] = 0xFFFFB04A;                                              /* movea.w #$B04A,a1 */
  lift_charge(x, 0x125A0);
  setw(&c->d[1], alu_movew(c, 0x58));                                /* move.w #$58,d1 */
  lift_charge(x, 0x125A4);
  alu_btst(c, lift_r8(x, c->a[1] + SEW(c->d[0]) + 0x62), 7);        /* btst #7,$62(a1,d0.w) */
  lift_charge(x, 0x125A8);
  int bneTaken = !c->zf;
  lift_charge_bcc(x, 0x125AE, bneTaken);                             /* bne.w loc_125C6 */

  if (!bneTaken)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));                         /* neg.w d1 */
    lift_charge(x, 0x125B2);
    alu_cmpw(c, lift_r16(x, 0xFFFFBF96u), W(c->d[1]));                /* cmp.w (abs),d1 */
    lift_charge(x, 0x125B4);
    int lt = (c->nf != c->vf);                                       /* blt */
    lift_charge_bcc(x, 0x125B8, lt);                                 /* blt.w locret_15464 */
    if (lt)
    {
      lift_charge(x, SHARED_RTS);                                    /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    lift_w16(x, 0xFFFFBF96u, alu_movew(c, 0xFFBF));                  /* move.w #$FFBF,(abs) */
    lift_charge(x, 0x125BC);
    lift_charge(x, 0x125C2);                                          /* bra.w loc_125D4 */
  }
  else
  {
    alu_cmpw(c, lift_r16(x, 0xFFFFBF96u), W(c->d[1]));                /* cmp.w (abs),d1 */
    lift_charge(x, 0x125C6);
    int gt = (!c->zf && c->nf == c->vf);                              /* bgt */
    lift_charge_bcc(x, 0x125CA, gt);                                  /* bgt.w locret_15464 */
    if (gt)
    {
      lift_charge(x, SHARED_RTS);                                     /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
    lift_w16(x, 0xFFFFBF96u, alu_movew(c, 0x41));                     /* move.w #$41,(abs) */
    lift_charge(x, 0x125CE);
  }

  /* loc_125D4 */
  setw(&c->d[0], alu_movew(c, 0x46));                                 /* move.w #$46,d0 */
  lift_charge(x, 0x125D4);
  alu_movew(c, lift_r16(x, 0xFFFFBF94u));                             /* tst.w (abs) */
  lift_charge(x, 0x125D8);
  int bpl = !c->nf;
  lift_charge_bcc(x, 0x125DC, bpl);                                   /* bpl.w loc_125E2 */
  if (!bpl)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                          /* neg.w d0 */
    lift_charge(x, 0x125E0);
  }

  /* loc_125E2 */
  lift_w16(x, 0xFFFFBF94u, alu_movew(c, W(c->d[0])));                 /* move.w d0,(abs) */
  lift_charge(x, 0x125E2);

  lift_charge(x, 0x125E6);                                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Buf_CopyBytes(lift_ctx *);                    /* save.c */
void Object_LookupRecordThenSyncScore(lift_ctx *); /* game.c */
void Sram_SyncTeamRecord(lift_ctx *);              /* game.c */

/*
 * sub_F998E (bsr'd from sub_F98C6, sub_FA75C and others; jsr'd from
 * sub_FCB9A)
 *   in:  d0.w = value 0-999ish, a1 = pascal-string buffer (word length,
 *        then chars)
 *   out: digits written at a1+2 (hundreds suppressed when 0, tens
 *        suppressed when 0 and the value < 10 — i.e. "5" not "05", but
 *        "105" keeps its 0), NUL pad to even length, total length (+2
 *        for the length word itself) stored at (a1); a1 = advanced
 *        write cursor (NOT restored); d0-d6/a0 movem.w-saved/restored
 *        (sign-extended on the pop).
 * Digit split via divu #$64 then divu #$A on the remainder.
 */
void sub_F998E(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t, emitTens;

  c->a[7] -= 16;                                   /* movem.w d0-d6/a0,-(sp) */
  for (i = 0; i < 7; i++) lift_w16(x, c->a[7] + 2 * i, W(c->d[i]));
  lift_w16(x, c->a[7] + 14, W(c->a[0]));
  lift_charge_movem(x, 0xF998E);
  c->a[7] -= 4;                                    /* move.l a1,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[1]));
  lift_charge(x, 0xF9992);
  setw(&c->d[6], alu_movew(c, W(c->d[0])));        /* move.w d0,d6 */
  lift_charge(x, 0xF9994);
  c->a[1] += 2;                                    /* addq.l #2,a1 */
  lift_charge(x, 0xF9996);
  setw(&c->d[1], alu_movew(c, 0));                 /* clr.w d1 */
  lift_charge(x, 0xF9998);
  c->d[0] = alu_extl(c, c->d[0]);                  /* ext.l d0 */
  lift_charge(x, 0xF999A);
  lift_charge_divu(x, 0xF999C, 0x64, c->d[0]);     /* divu.w #$64,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0x64, c->d[0]);
  alu_tstw(c, W(c->d[0]));                         /* tst.w d0 */
  lift_charge(x, 0xF99A0);
  t = c->zf;
  lift_charge_bcc(x, 0xF99A2, t);                  /* beq.w loc_F99AE */
  if (!t)
  {
    setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0]))); /* add.w #$30,d0 */
    lift_charge(x, 0xF99A6);
    lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));    /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xF99AA);
    setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));    /* addq.w #1,d1 */
    lift_charge(x, 0xF99AC);
  }
  /* loc_F99AE */
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xF99AE);
  c->d[0] = alu_extl(c, c->d[0]);                  /* ext.l d0 */
  lift_charge(x, 0xF99B0);
  lift_charge_divu(x, 0xF99B2, 0xA, c->d[0]);      /* divu.w #$A,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0xA, c->d[0]);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));   /* add.w #$30,d0 */
  lift_charge(x, 0xF99B6);
  alu_cmpb(c, 0x30, c->d[0]);                      /* cmp.b #$30,d0 */
  lift_charge(x, 0xF99BA);
  t = !c->zf;
  lift_charge_bcc(x, 0xF99BE, t);                  /* bne.w loc_F99CA */
  emitTens = t;
  if (!t)
  {
    alu_cmpw(c, 0xA, W(c->d[6]));                  /* cmp.w #$A,d6 */
    lift_charge(x, 0xF99C2);
    t = (c->nf != c->vf);                          /* blt.w loc_F99CE */
    lift_charge_bcc(x, 0xF99C6, t);
    emitTens = !t;
  }
  if (emitTens)
  {
    /* loc_F99CA */
    lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));    /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xF99CA);
    setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));    /* addq.w #1,d1 */
    lift_charge(x, 0xF99CC);
  }
  /* loc_F99CE */
  c->d[0] = alu_swap(c, c->d[0]);                  /* swap d0 */
  lift_charge(x, 0xF99CE);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));   /* add.w #$30,d0 */
  lift_charge(x, 0xF99D0);
  lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));      /* move.b d0,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0xF99D4);
  setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));      /* addq.w #1,d1 */
  lift_charge(x, 0xF99D6);
  alu_btst(c, c->d[1], 0);                         /* btst #0,d1 */
  lift_charge(x, 0xF99D8);
  t = c->zf;
  lift_charge_bcc(x, 0xF99DC, t);                  /* beq.w loc_F99E6 */
  if (!t)
  {
    lift_w8(x, c->a[1], alu_moveb(c, 0));          /* move.b #0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xF99E0);
    setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));    /* addq.w #1,d1 */
    lift_charge(x, 0xF99E4);
  }
  /* loc_F99E6: movea.l (sp)+,a0 */
  c->a[0] = lift_r32(x, c->a[7]);
  c->a[7] += 4;
  lift_charge(x, 0xF99E6);
  setw(&c->d[1], alu_addw(c, 2, W(c->d[1])));      /* addq.w #2,d1 */
  lift_charge(x, 0xF99E8);
  alu_movew(c, W(c->d[1]));                        /* move.w d1,(a0) */
  lift_w16(x, c->a[0], W(c->d[1]));
  lift_charge(x, 0xF99EA);
  /* movem.w (sp)+,d0-d6/a0: sign-extends into the full register */
  for (i = 0; i < 7; i++) c->d[i] = SEW(lift_r16(x, c->a[7] + 2 * i));
  c->a[0] = SEW(lift_r16(x, c->a[7] + 14));
  c->a[7] += 16;
  lift_charge_movem(x, 0xF99EC);
  lift_charge(x, 0xF99F0);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_F98C6 (called from ROM:F8AB0 — the intermission "Record" stat
 * line builder)
 *   in: a1 = output pascal-string buffer, d0/d1 = record args for
 *       Object_LookupRecordThenSyncScore
 * Copies "Record " into the buffer (Buf_CopyBytes), resolves the record
 * value (sub_F99F2); zero → the buffer is reset to an empty 2-byte
 * string. Otherwise formats the number into the scratch buffer at
 * $FFFFBF20 (sub_F998E, value parked at $FFFFBF14), appends it
 * (Text_AppendString), then appends the unit picked by d1 and
 * plurality: " saves"/" save" (d1 != 0) or " goals"/" goal" (d1 == 0),
 * from the ROM strings at $F995A-$F9972. d0-d7/a1-a6 movem-restored.
 */
void sub_F98C6(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 56;                                   /* movem.l d0-d7/a1-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 6; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[1 + i]);
  lift_charge_movem(x, 0xF98C6);
  c->a[7] -= 4;                                    /* move.l a1,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[1]));
  lift_charge(x, 0xF98CA);
  c->a[3] = c->a[1];                               /* movea.l a1,a3 */
  lift_charge(x, 0xF98CC);
  c->a[1] = 0xF9950;                               /* movea.l #aRecord,a1 */
  lift_charge(x, 0xF98CE);
  c->a[7] -= 8;                                    /* movem.l d0-d1,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_charge_movem(x, 0xF98D4);
  lift_call(x, 0xF98D8, 4, Buf_CopyBytes);         /* bsr.w sub_F997A */
  if (x->declined) return;
  c->d[0] = lift_r32(x, c->a[7]);                  /* movem.l (sp)+,d0-d1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0xF98DC);
  lift_call(x, 0xF98E0, 4, Object_LookupRecordThenSyncScore);  /* bsr.w sub_F99F2 */
  if (x->declined) return;
  alu_tstw(c, W(c->d[0]));                         /* tst.w d0 */
  lift_charge(x, 0xF98E4);
  t = !c->zf;
  lift_charge_bcc(x, 0xF98E6, t);                  /* bne.w loc_F98F4 */
  if (!t)
  {
    c->a[1] = lift_r32(x, c->a[7]);                /* movea.l (sp)+,a1 */
    c->a[7] += 4;
    lift_charge(x, 0xF98EA);
    alu_movew(c, 2);                               /* move.w #2,(a1) */
    lift_w16(x, c->a[1], 2);
    lift_charge(x, 0xF98EC);
    lift_charge(x, 0xF98F0);                       /* bra.w loc_F994A */
  }
  else
  {
    /* loc_F98F4 */
    c->a[1] = 0xFFFFBF20;                          /* movea.l #$FFFFBF20,a1 */
    lift_charge(x, 0xF98F4);
    lift_w16(x, 0xFFFFBF14, alu_movew(c, W(c->d[0])));  /* move.w d0,(BF14).w */
    lift_charge(x, 0xF98FA);
    lift_call(x, 0xF98FE, 4, sub_F998E);           /* bsr.w sub_F998E */
    if (x->declined) return;
    c->a[3] = lift_r32(x, c->a[7]);                /* movea.l (sp),a3 — no pop */
    lift_charge(x, 0xF9902);
    c->a[1] = 0xFFFFBF20;
    lift_charge(x, 0xF9904);
    lift_call(x, 0xF990A, 6, Text_AppendString);   /* jsr sub_11D9E */
    if (x->declined) return;
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFBF14)));  /* move.w (BF14).w,d0 */
    lift_charge(x, 0xF9910);
    c->a[1] = 0xF996A;                             /* " saves" */
    lift_charge(x, 0xF9914);
    alu_cmpw(c, 1, W(c->d[0]));                    /* cmp.w #1,d0 */
    lift_charge(x, 0xF991A);
    t = !c->zf;
    lift_charge_bcc(x, 0xF991E, t);                /* bne.w loc_F9928 */
    if (!t)
    {
      c->a[1] = 0xF9972;                           /* " save" */
      lift_charge(x, 0xF9922);
    }
    /* loc_F9928 */
    alu_tstw(c, W(c->d[1]));                       /* tst.w d1 */
    lift_charge(x, 0xF9928);
    t = !c->zf;
    lift_charge_bcc(x, 0xF992A, t);                /* bne.w loc_F9942 */
    if (!t)
    {
      c->a[1] = 0xF995A;                           /* " goals" */
      lift_charge(x, 0xF992E);
      alu_cmpw(c, 1, W(c->d[0]));                  /* cmp.w #1,d0 */
      lift_charge(x, 0xF9934);
      t = !c->zf;
      lift_charge_bcc(x, 0xF9938, t);              /* bne.w loc_F9942 */
      if (!t)
      {
        c->a[1] = 0xF9962;                         /* " goal" */
        lift_charge(x, 0xF993C);
      }
    }
    /* loc_F9942: movea.l (sp)+,a3 */
    c->a[3] = lift_r32(x, c->a[7]);
    c->a[7] += 4;
    lift_charge(x, 0xF9942);
    lift_call(x, 0xF9944, 6, Text_AppendString);   /* jsr sub_11D9E */
    if (x->declined) return;
  }
  /* loc_F994A: movem.l (sp)+,d0-d7/a1-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 6; i++) c->a[1 + i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 56;
  lift_charge_movem(x, 0xF994A);
  lift_charge(x, 0xF994E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FCB9A (called from ROM:loc_178C0)
 * Formats a team-record stat number into the scratch text buffer at
 * $FFFFBF20: syncs the $FFFFCFFE record block for the team index at
 * $FFFFC330 (Sram_SyncTeamRecord, stack-arg pushed and caller-popped),
 * takes byte 8 of the block (or $50 when zero), and runs the decimal
 * formatter sub_F998E on it. Everything (d0-a0/a2-a6, a1 via an
 * explicit push/pop) restored; flags come back from sub_F998E's final
 * length store.
 */
void sub_FCB9A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  /* movem.l d0-a0/a2-a6,-(sp): d0-d7, a0, a2-a6 */
  c->a[7] -= 56;
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  lift_w32(x, c->a[7] + 32, c->a[0]);
  for (i = 0; i < 5; i++) lift_w32(x, c->a[7] + 36 + 4 * i, c->a[2 + i]);
  lift_charge_movem(x, 0xFCB9A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC330)));  /* move.w (C330).w,d1 */
  lift_charge(x, 0xFCB9E);
  c->d[1] = alu_extl(c, c->d[1]);                  /* ext.l d1 */
  lift_charge(x, 0xFCBA2);
  c->a[0] = 0xFFFFCFFE;                            /* movea.l #$FFFFCFFE,a0 */
  lift_charge(x, 0xFCBA4);
  c->a[7] -= 4;                                    /* move.l a0,-(sp) */
  lift_w32(x, c->a[7], alu_movel(c, c->a[0]));
  lift_charge(x, 0xFCBAA);
  lift_call(x, 0xFCBAC, 6, Sram_SyncTeamRecord);   /* jsr sub_F9BE2 */
  if (x->declined) return;
  c->a[0] = lift_r32(x, c->a[7]);                  /* movea.l (sp)+,a0 */
  c->a[7] += 4;
  lift_charge(x, 0xFCBB2);
  c->a[1] = 0xFFFFBF20;                            /* movea.l #$FFFFBF20,a1 */
  lift_charge(x, 0xFCBB4);
  setw(&c->d[0], alu_movew(c, 0));                 /* clr.w d0 */
  lift_charge(x, 0xFCBBA);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + 8)));  /* move.b 8(a0),d0 */
  lift_charge(x, 0xFCBBC);
  t = !c->zf;
  lift_charge_bcc(x, 0xFCBC0, t);                  /* bne.w loc_FCBC8 */
  if (!t)
  {
    setb(&c->d[0], alu_moveb(c, 0x50));            /* move.b #$50,d0 */
    lift_charge(x, 0xFCBC4);
  }
  /* loc_FCBC8: move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], alu_movel(c, c->a[1]));
  lift_charge(x, 0xFCBC8);
  lift_call(x, 0xFCBCA, 6, sub_F998E);             /* jsr sub_F998E */
  if (x->declined) return;
  c->a[1] = lift_r32(x, c->a[7]);                  /* movea.l (sp)+,a1 */
  c->a[7] += 4;
  lift_charge(x, 0xFCBD0);
  /* movem.l (sp)+,d0-a0/a2-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  c->a[0] = lift_r32(x, c->a[7] + 32);
  for (i = 0; i < 5; i++) c->a[2 + i] = lift_r32(x, c->a[7] + 36 + 4 * i);
  c->a[7] += 56;
  lift_charge_movem(x, 0xFCBD2);
  lift_charge(x, 0xFCBD6);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_AppendInlineString (sub_11D96; called from sub_799E+B178,
 * sub_18A90+1E and others)
 *   in:  a3 = destination record buffer (as for Text_AppendString)
 *   out: returns PAST the caller's inline string data
 * The inline-argument calling convention: the caller's `bsr` return
 * address points at a length-prefixed string literal embedded in the
 * code stream rather than at an instruction. This routine pops that
 * address into a1, hands it to Text_AppendString (which appends the
 * string to the (a3) record and leaves a1 just past the data), then
 * `jmp (a1)` resumes the caller at the first real instruction after
 * the literal.
 *
 * Note the unusual exit: the return address is consumed by the very
 * first instruction, so there is no rts and nothing to pop at the end
 * — the final pc is simply a1.  (triage.py flags the `jmp (a1)` as a
 * jmp-out; it is a return, not a control-flow escape.)
 */
void Text_AppendInlineString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = lift_r32(x, c->a[7]);                  /* move.l (sp)+,a1 */
  c->a[7] += 4;
  lift_charge(x, 0x11D96);

  lift_call(x, 0x11D98, 4, Text_AppendString);     /* bsr.w sub_11D9E */
  if (x->declined) return;

  lift_charge(x, 0x11D9C);                         /* jmp (a1) */
  c->pc = c->a[1] & 0xFFFFFF;
}

/*
 * Text_FormatScorerName (sub_FB992; called from ROM:$FB22E, $FB3AA and
 * Text_BuildPlayedByRecord; all of d0-a6 saved/restored via movem)
 *   in:  a1 = destination cursor
 *   out: 12 bytes written at (a1); $FFFFD4E8 = the visible length
 * Copies the 12-byte name field of record $FFFFD4EA out of the table at
 * $FFFFD45A, substituting '-' for any NUL byte. The length counter at
 * $FFFFD4E8 is incremented for every byte written but decremented again
 * for each substituted NUL, so it ends up counting only the real
 * characters while the destination stays a fixed 12 bytes wide.
 * a1 itself is restored by the epilogue movem, so the caller's cursor
 * is unchanged.
 */
void Text_FormatScorerName(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a6,-(sp): a6 pushed first, d0 lands lowest */
  uint32_t saved[15];
  for (int i = 0; i < 7; i++) saved[i] = c->a[6 - i];      /* a6..a0 */
  for (int i = 0; i < 8; i++) saved[7 + i] = c->d[7 - i];  /* d7..d0 */
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFB992);

  c->a[0] = 0xFFFFD45Au;                                  /* movea.l #$FFFFD45A,a0 */
  lift_charge(x, 0xFB996);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFD4EAu))); /* move.w ($D4EA).w,d2 */
  lift_charge(x, 0xFB99C);
  lift_charge_mulu(x, 0xFB9A0, 0xC);                      /* mulu.w #$C,d2 */
  c->d[2] = alu_mulu(c, 0xC, c->d[2]);
  lift_w16(x, 0xFFFFD4E8u, alu_movew(c, 0));              /* move.w #0,($D4E8).w */
  lift_charge(x, 0xFB9A4);
  setw(&c->d[3], alu_movew(c, 0xB));                      /* move.w #$B,d3 */
  lift_charge(x, 0xFB9AA);

  for (;;)
  {
    /* loc_FB9AE */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2]))));
    lift_charge(x, 0xFB9AE);                              /* move.b (a0,d2.w),d0 */
    int is_nul = c->zf;                                   /* beq.w loc_FB9BA */
    lift_charge_bcc(x, 0xFB9B2, is_nul);
    if (!is_nul)
    {
      lift_charge_bcc(x, 0xFB9B6, 1);                     /* bra.w loc_FB9C2 */
    }
    else
    {
      /* loc_FB9BA */
      setb(&c->d[0], alu_moveb(c, 0x2D));                 /* move.b #$2D,d0 */
      lift_charge(x, 0xFB9BA);
      lift_w16(x, 0xFFFFD4E8u,                            /* subq.w #1,($D4E8).w */
               alu_subw(c, 1, lift_r16(x, 0xFFFFD4E8u)));
      lift_charge(x, 0xFB9BE);
    }

    /* loc_FB9C2 */
    lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));           /* move.b d0,(a1)+ */
    c->a[1] += 1;
    lift_charge(x, 0xFB9C2);
    lift_w16(x, 0xFFFFD4E8u,                              /* addq.w #1,($D4E8).w */
             alu_addw(c, 1, lift_r16(x, 0xFFFFD4E8u)));
    lift_charge(x, 0xFB9C4);
    setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));           /* addq.w #1,d2 */
    lift_charge(x, 0xFB9C8);

    uint32_t nd3 = W(W(c->d[3]) - 1);                     /* dbf d3,loc_FB9AE */
    setw(&c->d[3], nd3);
    int taken = (nd3 != 0xFFFF);
    lift_charge_dbcc(x, 0xFB9CA, taken, !taken);
    if (!taken) break;
  }

  for (int i = 0; i < 7; i++) c->a[6 - i] = saved[i];      /* movem.l (sp)+,d0-a6 */
  for (int i = 0; i < 8; i++) c->d[7 - i] = saved[7 + i];
  c->a[7] += 15 * 4;
  lift_charge_movem(x, 0xFB9CE);

  lift_charge(x, 0xFB9D2);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_BuildPlayedByRecord — the shared body of sub_FEEC8 (entry at
 * $FEEC8, reading $FFFFD042) and sub_FEF5A (entry at $FEF5A, reading
 * $FFFFD044). Both push the identical movem frame, load their own word
 * into d0, and converge on loc_FEED0; `enter` is the address of the
 * caller's own first instruction so the prologue is charged correctly.
 *
 *   out: a1 = the record to display
 * With d0 == 0 there is nothing to name and a1 is pointed at the empty
 * record word_FEF50. Otherwise d0 selects the name record: it is stashed
 * at $FFFFD4EA, Text_FormatScorerName renders the 12-byte name into the
 * $FFFFD4DC scratch, the visible length at $FFFFD4E8 is NUL-terminated
 * and rounded up to an even word count, and that becomes the length
 * prefix at $FFFFD4DA. The display record at $FFFFBF20 is then built as
 * "(played by " (Buf_CopyBytes from word_FEF42) + the name
 * (Text_AppendString) + ")" (Text_AppendInlineString with the literal at
 * $FEF32), and a1 is left pointing at it.
 */
static void Text_BuildPlayedByBody(lift_ctx *x, unsigned int enter,
                                   uint32_t src_addr, int has_bra)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a0/a2-a6,-(sp): a6 pushed first, d0 lands lowest */
  uint32_t saved[14];
  saved[0] = c->a[6]; saved[1] = c->a[5]; saved[2] = c->a[4];
  saved[3] = c->a[3]; saved[4] = c->a[2]; saved[5] = c->a[0];
  for (int i = 0; i < 8; i++) saved[6 + i] = c->d[7 - i];
  for (int i = 0; i < 14; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, enter);

  setw(&c->d[0], alu_movew(c, lift_r16(x, src_addr)));   /* move.w (src).w,d0 */
  lift_charge(x, enter + 4);
  /* the $FEF5A entry reaches the body via an explicit branch; the
   * $FEEC8 entry simply falls through into loc_FEED0 */
  if (has_bra) lift_charge_bcc(x, enter + 8, 1);         /* bra.w loc_FEED0 */

  /* loc_FEED0 */
  alu_tstw(c, W(c->d[0]));                               /* tst.w d0 */
  lift_charge(x, 0xFEED0);
  int empty = c->zf;                                     /* beq.w loc_FEF52 */
  lift_charge_bcc(x, 0xFEED2, empty);

  if (!empty)
  {
    c->a[1] = 0xFFFFD4DCu;                               /* movea.l #$FFFFD4DC,a1 */
    lift_charge(x, 0xFEED6);
    lift_w16(x, 0xFFFFD4EAu, alu_movew(c, W(c->d[0])));  /* move.w d0,($D4EA).w */
    lift_charge(x, 0xFEEDC);

    lift_call(x, 0xFEEE0, 4, Text_FormatScorerName);     /* bsr.w sub_FB992 */
    if (x->declined) return;

    c->a[1] += SEW(lift_r16(x, 0xFFFFD4E8u));            /* adda.w ($D4E8).w,a1 */
    lift_charge(x, 0xFEEE4);
    lift_w8(x, c->a[1], alu_moveb(c, 0));                /* move.b #0,(a1) */
    lift_charge(x, 0xFEEE8);
    lift_w16(x, 0xFFFFD4E8u,                             /* addq.w #1,($D4E8).w */
             alu_addw(c, 1, lift_r16(x, 0xFFFFD4E8u)));
    lift_charge(x, 0xFEEEC);
    lift_w16(x, 0xFFFFD4E8u,                             /* and.w #$FFFE,($D4E8).w */
             alu_andw(c, 0xFFFE, lift_r16(x, 0xFFFFD4E8u)));
    lift_charge(x, 0xFEEF0);
    lift_w16(x, 0xFFFFD4E8u,                             /* addq.w #2,($D4E8).w */
             alu_addw(c, 2, lift_r16(x, 0xFFFFD4E8u)));
    lift_charge(x, 0xFEEF6);
    lift_w16(x, 0xFFFFD4DAu,                             /* move.w ($D4E8).w,($D4DA).w */
             alu_movew(c, lift_r16(x, 0xFFFFD4E8u)));
    lift_charge(x, 0xFEEFA);

    c->a[1] = 0xFFFFD4DAu;                               /* movea.l #$FFFFD4DA,a1 */
    lift_charge(x, 0xFEF00);
    c->a[7] -= 4;                                        /* move.l a1,-(sp) */
    lift_w32(x, c->a[7], alu_movel(c, c->a[1]));
    lift_charge(x, 0xFEF06);
    c->a[3] = 0xFFFFBF20u;                               /* movea.l #$FFFFBF20,a3 */
    lift_charge(x, 0xFEF08);
    c->a[1] = 0x000FEF42u;                               /* movea.l #word_FEF42,a1 */
    lift_charge(x, 0xFEF0E);

    lift_call(x, 0xFEF14, 4, Buf_CopyBytes);             /* bsr.w sub_F997A */
    if (x->declined) return;

    c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;        /* move.l (sp)+,a1 */
    lift_charge(x, 0xFEF18);
    c->a[3] = 0xFFFFBF20u;                               /* movea.l #$FFFFBF20,a3 */
    lift_charge(x, 0xFEF1A);

    lift_call(x, 0xFEF20, 6, Text_AppendString);         /* jsr sub_11D9E */
    if (x->declined) return;

    c->a[3] = 0xFFFFBF20u;                               /* movea.l #$FFFFBF20,a3 */
    lift_charge(x, 0xFEF26);

    /* jsr sub_11D96 — consumes the ")" literal at $FEF32, resumes $FEF36 */
    lift_call(x, 0xFEF2C, 6, Text_AppendInlineString);
    if (x->declined) return;

    c->a[1] = 0xFFFFBF20u;                               /* movea.l #$FFFFBF20,a1 */
    lift_charge(x, 0xFEF36);
  }
  else
  {
    /* loc_FEF52 */
    c->a[1] = 0x000FEF50u;                               /* movea.l #word_FEF50,a1 */
    lift_charge(x, 0xFEF52);
    lift_charge_bcc(x, 0xFEF58, 1);                      /* bra.s loc_FEF3C */
  }

  /* loc_FEF3C */
  c->a[6] = saved[0]; c->a[5] = saved[1]; c->a[4] = saved[2];
  c->a[3] = saved[3]; c->a[2] = saved[4]; c->a[0] = saved[5];
  for (int i = 0; i < 8; i++) c->d[7 - i] = saved[6 + i];
  c->a[7] += 14 * 4;
  lift_charge_movem(x, 0xFEF3C);

  lift_charge(x, 0xFEF40);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* sub_FEEC8 (called from ROM:loc_178CA) — "played by" for $FFFFD042. */
void Text_BuildPlayedByRecordD042(lift_ctx *x)
{
  Text_BuildPlayedByBody(x, 0xFEEC8, 0xFFFFD042u, 0);
}

/*
 * sub_FEF5A (called from ROM:loc_178D4) — the alternate entry: same
 * frame and same body, but names the record selected by $FFFFD044.
 * Its `bra.w loc_FEED0` is a shared-tail entry into sub_FEEC8's body,
 * not a far-branch into an unrelated routine.
 */
void Text_BuildPlayedByRecordD044(lift_ctx *x)
{
  Text_BuildPlayedByBody(x, 0xFEF5A, 0xFFFFD044u, 1);
}
