/*
 * render.c — sprite rendering pipeline routines (lifted).
 *
 * The rink is drawn from a world coordinate system; sprites are positioned
 * by transforming world coords through the camera and emitted as Genesis
 * hardware sprite entries (8 bytes each, max 64 per frame).
 */
#include "util68k.h"

#define R_RINK_FLIP   0xFFC2ECu   /* bit 7: rink is drawn flipped */
#define R_CAMERA_Y    0xFFBD18u
#define R_CAMERA_X    0xFFBD1Cu
#define OFFSCREEN_Y   0x4E20      /* sentinel: sprite culled */

void Vector_ToOctant(lift_ctx *);  /* game.c */

/*
 * The on-ice object table: 16 slots of 64 bytes at $FFB04A (the same table
 * TickTimerTable_B04A ticks). Fields used by the draw path:
 *   +$00 world X        +$14 world Y      +$18 height (negative = hidden)
 *   +$04 sprite attr word (VDP flip/palette bits)
 *   +$06 anim frame: <=0 = no sprite; low 11 bits frame index; top 5 bits
 *        are attr-override bits eor'd into +$04 for the duration of the draw
 *   +$08 last frame whose tiles were streamed to VRAM (stream cache key)
 *   +$0A per-piece cache of the VRAM tile slot assigned to each piece
 *   +$12 VRAM tile base for this object (added to slot and tile attr)
 * Draw order (back-to-front object indices) is the 16-byte list at $FFB88A.
 */
#define SPRITE_DEFS   0x5DE7Au    /* ROM: sprite piece-table directory */
#define TILE_GFX      0x5DE84u    /* ROM: tile graphics, 32 bytes/tile */
#define SIZE_TILES    0x1920Cu    /* ROM: VDP size byte -> tile count */
#define OBJ_TABLE     0xFFFFB04Au
#define DRAW_ORDER    0xFFFFB88Au
#define R_TILE_ATTR_BIAS 0xFFFFB01Au  /* tile-attr bias added by the clipped-piece writer below (also touched by unlifted sub_16AA8/sub_16CD2) */

/*
 * Render_WorldToScreen (sub_16920, called from sub_16782)
 *   in:  d0 = world X, d1 = world Y, d2 = height above ice
 *   out: d0 = screen X + $100, d1 = screen Y (or $4E20 if culled), d2 = h/2
 *
 * Two view modes: normal (camera-relative) or flipped rink (axes swapped,
 * X negated, Y offset by $C5). Coordinates outside +/-$90 of the view
 * centre are culled by returning the sentinel in d1. Height tilts Y by
 * 1.5x (isometric-ish foreshortening: y += h + h/2).
 */
void Render_WorldToScreen(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2];
  int flipped = (lift_r8(x, R_RINK_FLIP) >> 7) & 1;

  c->zf = !flipped;                               /* btst */
  lift_charge(x, 0x16920);
  lift_charge_bcc(x, 0x16926, !flipped);
  if (flipped)
  {
    uint32_t t = d0; d0 = d1; d1 = t;             /* exg d0,d1 (full 32-bit) */
    setw(&d0, alu_negw(c, W(d0)));
    setw(&d1, alu_subw(c, 0xC5, W(d1)));
    lift_charge(x, 0x1692A);
    lift_charge(x, 0x1692C);
    lift_charge(x, 0x1692E);
    lift_charge(x, 0x16932);                      /* bra.w */
  }
  else
  {
    setw(&d0, alu_subw(c, lift_r16(x, R_CAMERA_X), W(d0)));
    setw(&d1, alu_subw(c, lift_r16(x, R_CAMERA_Y), W(d1)));
    lift_charge(x, 0x16936);
    lift_charge(x, 0x1693A);
  }

  int culled = 0;
  alu_cmpw(c, 0x90, W(d0));
  lift_charge(x, 0x1693E);
  int t1 = (!c->zf && c->nf == c->vf);            /* bgt: X too far right */
  lift_charge_bcc(x, 0x16942, t1);
  if (t1) culled = 1;
  if (!culled)
  {
    alu_cmpw(c, 0xFF70, W(d0));
    lift_charge(x, 0x16946);
    int t2 = (c->nf != c->vf);                    /* blt: X too far left */
    lift_charge_bcc(x, 0x1694A, t2);
    if (t2) culled = 1;
  }
  if (!culled)
  {
    setw(&d0, alu_addw(c, 0x100, W(d0)));
    setw(&d1, alu_addw(c, W(d2), W(d1)));
    setw(&d2, alu_asrw(c, W(d2), 1));
    setw(&d1, alu_addw(c, W(d2), W(d1)));
    lift_charge(x, 0x1694E);
    lift_charge(x, 0x16952);
    lift_charge(x, 0x16954);
    lift_charge(x, 0x16956);
    alu_cmpw(c, 0x90, W(d1));
    lift_charge(x, 0x16958);
    int t3 = (!c->zf && c->nf == c->vf);
    lift_charge_bcc(x, 0x1695C, t3);
    if (t3) culled = 1;
    if (!culled)
    {
      alu_cmpw(c, 0xFF70, W(d1));
      lift_charge(x, 0x16960);
      int t4 = (c->nf != c->vf);
      lift_charge_bcc(x, 0x16964, t4);
      if (t4) culled = 1;
    }
    if (!culled)
    {
      setw(&d1, alu_negw(c, W(d1)));
      setw(&d1, alu_addw(c, 0xF0, W(d1)));
      lift_charge(x, 0x16968);
      lift_charge(x, 0x1696A);
      lift_charge(x, 0x1696E);                    /* rts */
    }
  }
  if (culled)
  {
    setw(&d1, alu_movew(c, OFFSCREEN_Y));
    lift_charge(x, 0x16970);
    lift_charge(x, 0x16974);                      /* rts */
  }

  c->d[0] = d0; c->d[1] = d1; c->d[2] = d2;
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sprite_EmitPieces (sub_16178)
 *   in:  a0 = sprite definition (+4: long offset to piece table,
 *             then per-frame [start,end] word offsets indexed by d2*2)
 *        d2 = frame index, d0/d1 = screen X/Y, d3 = tile-attr offset,
 *        a6 = hardware sprite list write pointer, d6 = sprites used so far
 *   out: a6/d6 advanced; d0-d5/a0 preserved. Stops at the 64-sprite cap.
 *
 * Each 8-byte piece is [x.w, y.w, tile.w, attr-hi/size.b, size.b] in the
 * definition and becomes a VDP sprite entry: Y+d1, size, link=d6,
 * (attr & $F800)+tile+d3, X+d0.
 */
void Sprite_EmitPieces(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t d6 = c->d[6], a6 = c->a[6];

  alu_cmpw(c, 0x40, W(d6));
  lift_charge(x, 0x16178);
  int full = (c->nf == c->vf);                    /* bge */
  lift_charge_bcc(x, 0x1617C, full);
  if (full)
  {
    lift_charge(x, 0x15464);                      /* shared rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* movem.l d0-d5/a0,-(sp): a0 pushed first (descending), d0 lands lowest */
  uint32_t saved[7] = {c->d[0], c->d[1], c->d[2], c->d[3], c->d[4], c->d[5], c->a[0]};
  for (int r = 6; r >= 0; r--)
  {
    c->a[7] -= 4;
    lift_w32(x, c->a[7], saved[r]);
  }
  lift_charge_movem(x, 0x16180);

  uint32_t a0 = c->a[0] + lift_r32(x, c->a[0] + 4);         /* adda: no flags */
  lift_charge(x, 0x16184);
  uint32_t d2 = c->d[2];
  setw(&d2, alu_addw(c, W(d2), W(d2)));                     /* index * 2 */
  lift_charge(x, 0x16188);
  int32_t idx = SW(d2);
  uint32_t start = lift_r16(x, a0 + idx);
  uint32_t d4 = alu_movew(c, lift_r16(x, a0 + 2 + idx));
  lift_charge(x, 0x1618A);
  d4 = alu_subw(c, start, d4);
  lift_charge(x, 0x1618E);
  d4 = alu_lsrw(c, d4, 3);                                  /* byte span / 8 */
  lift_charge(x, 0x16192);
  uint32_t npieces = d4;
  d4 = alu_subw(c, 1, d4);                                  /* dbf counter */
  lift_charge(x, 0x16194);
  a0 += (uint32_t)SW(start);                                /* adda.w: no flags */
  lift_charge(x, 0x16196);

  if (npieces == 0) { x->declined = 1; return; }            /* dbf would wrap */

  uint32_t i, emitted = 0;
  int hit_cap = 0;
  for (i = 0; i < npieces && !hit_cap; i++)
  {
    uint32_t y = alu_movew(c, lift_r16(x, a0 + 2));
    lift_w16(x, a6, y);
    lift_charge(x, 0x1619A);
    lift_w16(x, a6, alu_addw(c, W(c->d[1]), lift_r16(x, a6)));
    a6 += 2;
    lift_charge(x, 0x1619E);
    lift_w8(x, a6, alu_moveb(c, lift_r8(x, a0 + 7)));       /* size */
    a6 += 1;
    lift_charge(x, 0x161A0);
    lift_w8(x, a6, alu_moveb(c, d6));                       /* link */
    a6 += 1;
    lift_charge(x, 0x161A4);
    setw(&d2, alu_movew(c, lift_r16(x, a0 + 6)));
    lift_charge(x, 0x161A6);
    setw(&d2, alu_andw(c, 0xF800, W(d2)));
    lift_charge(x, 0x161AA);
    setw(&d2, alu_addw(c, lift_r16(x, a0 + 4), W(d2)));
    lift_charge(x, 0x161AE);
    setw(&d2, alu_addw(c, W(c->d[3]), W(d2)));
    lift_charge(x, 0x161B2);
    lift_w16(x, a6, alu_movew(c, W(d2)));                   /* tile attr */
    a6 += 2;
    lift_charge(x, 0x161B4);
    uint32_t xx = alu_movew(c, lift_r16(x, a0));
    lift_w16(x, a6, xx);
    lift_charge(x, 0x161B6);
    lift_w16(x, a6, alu_addw(c, W(c->d[0]), lift_r16(x, a6)));
    a6 += 2;
    lift_charge(x, 0x161B8);

    setw(&d6, alu_addw(c, 1, W(d6)));
    lift_charge(x, 0x161BA);
    emitted++;
    alu_cmpw(c, 0x40, W(d6));
    lift_charge(x, 0x161BC);
    hit_cap = c->zf;
    lift_charge_bcc(x, 0x161C0, hit_cap);
    if (!hit_cap)
    {
      a0 += 8;                                              /* addq to An: no flags */
      lift_charge(x, 0x161C4);
      lift_charge_dbcc(x, 0x161C6, i != npieces - 1, i == npieces - 1);
    }
  }

  /* movem.l (sp)+ restores d0-d5/a0 to entry values */
  c->a[7] += 28;
  lift_charge_movem(x, 0x161CA);
  lift_charge(x, 0x161CE);                                  /* rts */
  c->d[6] = d6;
  c->a[6] = a6;
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define OFF_A8922    0xA8922u    /* ROM: sprite-definition table (see Sprite_EmitPieces) */
#define TBL_1615E    0x1615Eu    /* ROM: 13-entry frame-index-by-height-band word table */

/*
 * Sprite_EmitHeightBandPieces (sub_160F4; called from sub_15EC0)
 *   in:  $FFFFB8AC.w = a height/position value (bmi -> early exit via the
 *        shared far rts at $15464); a6/d6 = hw sprite list cursor (see
 *        Sprite_EmitPieces)
 *   out: emits up to 3 sprite pieces via Sprite_EmitPieces (a6/d6 advanced
 *        each time); d0-d4 clobbered
 *
 * Always emits frame 1 of OFF_A8922 at (X=$12A,Y=$FFFFB026). Then derives a
 * second frame index from d0/6 mod 3 (+2) and emits that. If team stat
 * $FFFFC6DA <= $FFFFCA3E, tail-emits frame 5 and returns (via
 * Sprite_EmitPieces' own rts). Otherwise derives a third frame index from
 * (d0-$DA)/4*2 clamped to [0,$18] via TBL_1615E, and tail-emits that frame.
 */
void Sprite_EmitHeightBandPieces(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB8AC)));    /* move.w ($FFFFB8AC).w,d0 */
  lift_charge(x, 0x160F4);
  int neg = c->nf;                                          /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0x160F8, neg);
  if (neg)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = OFF_A8922;                                      /* move.l #off_A8922,a0 */
  lift_charge(x, 0x160FC);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));                /* lsr.w #2,d0 */
  lift_charge(x, 0x16102);
  setw(&c->d[1], alu_movew(c, 0x12A));                       /* move.w #$12A,d1 */
  lift_charge(x, 0x16104);
  c->d[2] = alu_moveql(c, 1);                                /* moveq #1,d2 */
  lift_charge(x, 0x16108);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFB026)));     /* move.w ($FFFFB026).w,d3 */
  lift_charge(x, 0x1610A);

  lift_call(x, 0x1610E, 4, Sprite_EmitPieces);               /* bsr.w sub_16178 */
  if (x->declined) return;

  setw(&c->d[2], alu_movew(c, W(c->d[0])));                  /* move.w d0,d2 */
  lift_charge(x, 0x16112);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 1));                /* lsr.w #1,d2 */
  lift_charge(x, 0x16114);
  c->d[2] = alu_extl(c, W(c->d[2]));                         /* ext.l d2 */
  lift_charge(x, 0x16116);

  lift_charge_divu(x, 0x16118, 3, c->d[2]);                  /* divu.w #3,d2 */
  if (x->declined) return;
  c->d[2] = alu_divu(c, 3, c->d[2]);
  c->d[2] = alu_swap(c, c->d[2]);                            /* swap d2 */
  lift_charge(x, 0x1611C);
  setw(&c->d[2], alu_addw(c, 2, W(c->d[2])));                /* addq.w #2,d2 */
  lift_charge(x, 0x1611E);

  lift_call(x, 0x16120, 4, Sprite_EmitPieces);               /* bsr.w sub_16178 */
  if (x->declined) return;

  c->d[2] = alu_moveql(c, 5);                                /* moveq #5,d2 */
  lift_charge(x, 0x16124);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFC6DA)));     /* move.w ($FFFFC6DA).w,d4 */
  lift_charge(x, 0x16126);
  alu_cmpw(c, lift_r16(x, 0xFFFFCA3E), W(c->d[4]));          /* cmp.w ($FFFFCA3E).w,d4 */
  lift_charge(x, 0x1612A);
  int ls = (c->cf || c->zf);                                 /* bls.w sub_16178 */
  lift_charge_bcc(x, 0x1612E, ls);
  if (ls)
  {
    Sprite_EmitPieces(x);                                    /* tail branch: callee's rts returns to our caller */
    return;
  }

  setw(&c->d[2], alu_movew(c, W(c->d[0])));                  /* move.w d0,d2 */
  lift_charge(x, 0x16132);
  setw(&c->d[2], alu_subw(c, 0xDA, W(c->d[2])));             /* sub.w #$DA,d2 */
  lift_charge(x, 0x16134);
  int nonneg = !c->nf;                                       /* bpl.w loc_1613E */
  lift_charge_bcc(x, 0x16138, nonneg);
  if (!nonneg)
  {
    setw(&c->d[2], 0);                                       /* clr.w d2 */
    lift_charge(x, 0x1613C);
  }

  /* loc_1613E */
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 2));                /* lsr.w #2,d2 */
  lift_charge(x, 0x1613E);
  setw(&c->d[2], alu_addw(c, W(c->d[2]), W(c->d[2])));       /* add.w d2,d2 */
  lift_charge(x, 0x16140);
  alu_cmpw(c, 0x1A, W(c->d[2]));                             /* cmpi.w #$1A,d2 */
  lift_charge(x, 0x16142);
  int lt = (c->nf != c->vf);                                 /* blt.w loc_16150 */
  lift_charge_bcc(x, 0x16146, lt);
  if (!lt)
  {
    c->d[2] = 0x18;                                          /* move.l #$18,d2 */
    lift_charge(x, 0x1614A);
  }

  /* loc_16150 */
  c->a[1] = TBL_1615E;                                       /* move.l #word_1615E,a1 */
  lift_charge(x, 0x16150);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1] + SEW(c->d[2]))));  /* move.w (a1,d2.w),d2 */
  lift_charge(x, 0x16156);

  lift_charge(x, 0x1615A);                    /* bra.w sub_16178 — tail, rts to our caller */
  Sprite_EmitPieces(x);
}

/*
 * Sprite_EmitObject (sub_167AA; bsr from Render_DrawObject, tail-jump from
 * sub_164D6)
 *   in:  a3 = object, d0/d1 = screen X/Y, a5 = tile-DMA queue write ptr,
 *        a6 = hw sprite list write ptr, d6 = sprite count
 *   out: a5/a6/d6 advanced; d0-d5/a0-a2 and the attr word at 4(a3) preserved
 *
 * Emits every 8-byte piece [x.w y.w tile.w attr.b size.b] of the object's
 * current animation frame. When the frame changed since the last draw, each
 * piece's tile graphics are also streamed: a VRAM slot is assigned from a
 * running tile count (d3) and a queue entry {ROM src.l, words.w, slot*32.w}
 * is pushed via a5 — unless the piece's tile range is a subset of the
 * previous piece's, which then shares the previous slot. The slot low byte
 * is cached at $A(a3)+piece so later frames can just OR it into the attr.
 * The frame word's top 5 bits flip the attr word for the whole call (eor on
 * entry, original restored from the stack at exit). No 64-sprite cap check
 * here — callers guarantee room.
 */
void Sprite_EmitObject(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3], sp = c->a[7];
  uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3],
           d4 = c->d[4], d5 = c->d[5], d6 = c->d[6];
  uint32_t a0 = c->a[0], a1 = c->a[1], a2 = c->a[2], a5 = c->a[5],
           a6 = c->a[6];

  sp -= 2;                                        /* push attr word */
  lift_w16(x, sp, alu_movew(c, lift_r16(x, a3 + 4)));
  lift_charge(x, 0x167AA);
  {
    uint32_t regs[9] = {d0, d1, d2, d3, d4, d5, a0, a1, a2};
    for (int r = 8; r >= 0; r--) { sp -= 4; lift_w32(x, sp, regs[r]); }
  }
  lift_charge_movem(x, 0x167AE);

  setw(&d4, alu_movew(c, lift_r16(x, a3 + 6)));   /* anim frame word */
  lift_charge(x, 0x167B2);
  int skip = c->nf;                               /* bmi: hidden */
  lift_charge_bcc(x, 0x167B6, skip);
  if (!skip)
  {
    skip = c->zf;                                 /* beq: no sprite */
    lift_charge_bcc(x, 0x167BA, skip);
  }
  if (!skip)
  {
    setw(&d4, alu_andw(c, 0xF800, W(d4)));
    lift_charge(x, 0x167BE);
    /* eor.w d4,4(a3): apply the frame word's attr-override bits */
    lift_w16(x, a3 + 4, alu_movew(c, lift_r16(x, a3 + 4) ^ W(d4)));
    lift_charge(x, 0x167C2);
    setw(&d4, alu_movew(c, lift_r16(x, a3 + 6)));
    lift_charge(x, 0x167C6);
    setw(&d4, alu_andw(c, 0x7FF, W(d4)));         /* frame index */
    lift_charge(x, 0x167CA);
    a2 = SPRITE_DEFS;
    lift_charge(x, 0x167CE);
    a2 += lift_r32(x, a2 + 4);                    /* frame-offset table */
    lift_charge(x, 0x167D4);
    setw(&d4, alu_addw(c, W(d4), W(d4)));         /* index * 2 */
    lift_charge(x, 0x167D8);
    alu_cmpw(c, lift_r16(x, a2 + 2), W(d4));
    lift_charge(x, 0x167DA);
    skip = (c->nf == c->vf);                      /* bge: frame out of range */
    lift_charge_bcc(x, 0x167DE, skip);
  }
  if (!skip)
  {
    int32_t idx = SW(d4);
    setw(&d5, alu_movew(c, lift_r16(x, a2 + 2 + idx)));
    lift_charge(x, 0x167E2);
    setw(&d5, alu_subw(c, lift_r16(x, a2 + idx), W(d5)));
    lift_charge(x, 0x167E6);
    setw(&d5, alu_lsrw(c, W(d5), 3));             /* byte span / 8 = pieces */
    lift_charge(x, 0x167EA);
    uint32_t npieces = W(d5);
    setw(&d5, alu_subw(c, 1, W(d5)));             /* dbf counter */
    lift_charge(x, 0x167EC);
    a2 += SEW(lift_r16(x, a2 + idx));             /* adda.w: first piece */
    lift_charge(x, 0x167EE);
    setw(&d3, alu_movew(c, 0));                   /* running tile count */
    lift_charge(x, 0x167F2);
    setw(&d4, alu_movew(c, 0));
    lift_charge(x, 0x167F4);

    if (npieces == 0) { x->declined = 1; return; }  /* dbf would wrap */

    for (;;)
    {
      sp -= 2;                                    /* push d0 (screen X) */
      lift_w16(x, sp, alu_movew(c, W(d0)));
      lift_charge(x, 0x167F6);
      setw(&d0, alu_movew(c, lift_r16(x, a3 + 6)));
      lift_charge(x, 0x167F8);
      setw(&d0, alu_andw(c, 0x7FF, W(d0)));
      lift_charge(x, 0x167FC);
      alu_cmpw(c, lift_r16(x, a3 + 8), W(d0));
      lift_charge(x, 0x16800);
      int streamed = c->zf;                       /* beq: tiles already in VRAM */
      lift_charge_bcc(x, 0x16804, streamed);
      if (!streamed)
      {
        alu_movew(c, W(d5));                      /* tst.w d5 */
        lift_charge(x, 0x16808);
        int notlast = !c->zf;
        lift_charge_bcc(x, 0x1680A, notlast);
        if (!notlast)
        {
          lift_w16(x, a3 + 8, alu_movew(c, W(d0)));  /* cache the frame */
          lift_charge(x, 0x1680E);
        }
        /* movem.w d0-d4,-(sp) */
        sp -= 10;
        lift_w16(x, sp + 0, W(d0)); lift_w16(x, sp + 2, W(d1));
        lift_w16(x, sp + 4, W(d2)); lift_w16(x, sp + 6, W(d3));
        lift_w16(x, sp + 8, W(d4));
        lift_charge_movem(x, 0x16812);
        setw(&d2, alu_movew(c, lift_r16(x, a2 + 4)));   /* piece tile# */
        lift_charge(x, 0x16816);
        setw(&d4, alu_movew(c, 0));
        lift_charge(x, 0x1681A);
        d4 = (d4 & ~0xFFu) | alu_moveb(c, lift_r8(x, a2 + 7));
        lift_charge(x, 0x1681C);
        a0 = SIZE_TILES;
        lift_charge(x, 0x16820);
        d4 = (d4 & ~0xFFu) | alu_moveb(c, lift_r8(x, a0 + SW(d4)));
        lift_charge(x, 0x16826);
        /* subset test vs previous piece's range (pushed d2=tile, d4=count) */
        alu_cmpw(c, lift_r16(x, sp + 8), W(d4));
        lift_charge(x, 0x1682A);
        int t = (!c->zf && c->nf == c->vf);       /* bgt: needs more tiles */
        lift_charge_bcc(x, 0x1682E, t);
        int shared = 0;
        if (!t)
        {
          alu_cmpw(c, lift_r16(x, sp + 4), W(d2));
          lift_charge(x, 0x16832);
          t = (c->nf != c->vf);                   /* blt: starts before prev */
          lift_charge_bcc(x, 0x16836, t);
          if (!t)
          {
            setw(&d0, alu_movew(c, lift_r16(x, sp + 4)));
            lift_charge(x, 0x1683A);
            setw(&d0, alu_addw(c, lift_r16(x, sp + 8), W(d0)));
            lift_charge(x, 0x1683E);
            setw(&d0, alu_subw(c, W(d2), W(d0)));
            lift_charge(x, 0x16842);
            setw(&d0, alu_subw(c, W(d4), W(d0)));
            lift_charge(x, 0x16844);
            t = c->nf;                            /* bmi: extends past prev */
            lift_charge_bcc(x, 0x16846, t);
            if (!t) shared = 1;
          }
        }
        if (shared)
        {
          /* reuse the previous slot: pop d0/d1, keep live d2/d3/d4 */
          d0 = SEW(lift_r16(x, sp)); sp += 2;     /* movem.w sign-extends */
          d1 = SEW(lift_r16(x, sp)); sp += 2;
          lift_charge_movem(x, 0x1684A);
          sp += 6;
          lift_charge(x, 0x1684E);
          lift_charge(x, 0x16850);                /* bra.w */
        }
        else
        {
          setw(&d3, alu_addw(c, lift_r16(x, sp + 8), W(d3)));  /* advance slot */
          lift_charge(x, 0x16854);
          d0 = SEW(lift_r16(x, sp)); sp += 2;
          d1 = SEW(lift_r16(x, sp)); sp += 2;
          lift_charge_movem(x, 0x16858);
          sp += 6;
          lift_charge(x, 0x1685C);
          sp -= 10;                               /* re-push with new d3 */
          lift_w16(x, sp + 0, W(d0)); lift_w16(x, sp + 2, W(d1));
          lift_w16(x, sp + 4, W(d2)); lift_w16(x, sp + 6, W(d3));
          lift_w16(x, sp + 8, W(d4));
          lift_charge_movem(x, 0x1685E);
          setw(&d3, alu_addw(c, lift_r16(x, a3 + 0x12), W(d3)));
          lift_charge(x, 0x16862);
          d2 = alu_extl(c, d2);
          lift_charge(x, 0x16866);
          d2 = alu_asll(c, d2, 5);                /* tile# * 32 bytes */
          lift_charge(x, 0x16868);
          d2 = alu_addl(c, TILE_GFX, d2);
          lift_charge(x, 0x1686A);
          setw(&d4, alu_aslw(c, W(d4), 4));       /* tiles -> words */
          lift_charge(x, 0x16870);
          setw(&d3, alu_aslw(c, W(d3), 5));       /* slot -> VRAM addr */
          lift_charge(x, 0x16872);
          lift_w32(x, a5, alu_movel(c, d2)); a5 += 4;   /* queue: src */
          lift_charge(x, 0x16874);
          lift_w16(x, a5, alu_movew(c, W(d4))); a5 += 2;  /* len */
          lift_charge(x, 0x16876);
          lift_w16(x, a5, alu_movew(c, W(d3))); a5 += 2;  /* dest */
          lift_charge(x, 0x16878);
          d0 = SEW(lift_r16(x, sp)); sp += 2;     /* movem.w (sp)+,d0-d4 */
          d1 = SEW(lift_r16(x, sp)); sp += 2;
          d2 = SEW(lift_r16(x, sp)); sp += 2;
          d3 = SEW(lift_r16(x, sp)); sp += 2;
          d4 = SEW(lift_r16(x, sp)); sp += 2;
          lift_charge_movem(x, 0x1687A);
        }
        lift_w8(x, a3 + 0xA + SW(d5), alu_moveb(c, d3));  /* cache slot byte */
        lift_charge(x, 0x1687E);
      }
      /* emit the hardware sprite entry */
      setw(&d0, alu_movew(c, lift_r16(x, sp))); sp += 2;  /* pop screen X */
      lift_charge(x, 0x16882);
      sp -= 6;                                    /* movem.w d0-d2,-(sp) */
      lift_w16(x, sp + 0, W(d0)); lift_w16(x, sp + 2, W(d1));
      lift_w16(x, sp + 4, W(d2));
      lift_charge_movem(x, 0x16884);
      setw(&d2, alu_movew(c, lift_r16(x, a2 + 2)));  /* piece Y */
      lift_charge(x, 0x16888);
      alu_btst(c, lift_r8(x, a3 + 4), 4);         /* attr bit 12: V flip */
      lift_charge(x, 0x1688C);
      int noflip = c->zf;
      lift_charge_bcc(x, 0x16892, noflip);
      if (!noflip)
      {
        d2 = (d2 & ~0xFFu) | alu_moveb(c, lift_r8(x, a2 + 7));
        lift_charge(x, 0x16896);
        setw(&d2, alu_andw(c, 3, W(d2)));         /* height in tiles - 1 */
        lift_charge(x, 0x1689A);
        setw(&d2, alu_addw(c, 1, W(d2)));
        lift_charge(x, 0x1689E);
        setw(&d2, alu_aslw(c, W(d2), 3));         /* * 8 px */
        lift_charge(x, 0x168A0);
        setw(&d2, alu_negw(c, W(d2)));
        lift_charge(x, 0x168A2);
        setw(&d2, alu_subw(c, lift_r16(x, a2 + 2), W(d2)));  /* mirror Y */
        lift_charge(x, 0x168A4);
      }
      setw(&d1, alu_addw(c, W(d2), W(d1)));
      lift_charge(x, 0x168A8);
      lift_w16(x, a6, alu_movew(c, W(d1)));       /* sprite Y */
      lift_charge(x, 0x168AA);
      setw(&d2, alu_movew(c, lift_r16(x, a2)));   /* piece X */
      lift_charge(x, 0x168AC);
      alu_btst(c, lift_r8(x, a3 + 4), 3);         /* attr bit 11: H flip */
      lift_charge(x, 0x168AE);
      noflip = c->zf;
      lift_charge_bcc(x, 0x168B4, noflip);
      if (!noflip)
      {
        d2 = (d2 & ~0xFFu) | alu_moveb(c, lift_r8(x, a2 + 7));
        lift_charge(x, 0x168B8);
        setw(&d2, alu_andw(c, 0xC, W(d2)));       /* width in tiles - 1 */
        lift_charge(x, 0x168BC);
        setw(&d2, alu_addw(c, 4, W(d2)));
        lift_charge(x, 0x168C0);
        setw(&d2, alu_aslw(c, W(d2), 1));         /* * 8 px (field was *4) */
        lift_charge(x, 0x168C2);
        setw(&d2, alu_negw(c, W(d2)));
        lift_charge(x, 0x168C4);
        setw(&d2, alu_subw(c, lift_r16(x, a2), W(d2)));  /* mirror X */
        lift_charge(x, 0x168C6);
      }
      setw(&d0, alu_addw(c, W(d2), W(d0)));
      lift_charge(x, 0x168C8);
      lift_w16(x, a6 + 6, alu_movew(c, W(d0)));   /* sprite X */
      lift_charge(x, 0x168CA);
      lift_w8(x, a6 + 2, alu_moveb(c, lift_r8(x, a2 + 7)));  /* size */
      lift_charge(x, 0x168CE);
      lift_w8(x, a6 + 3, alu_moveb(c, d6));       /* link */
      lift_charge(x, 0x168D4);
      setw(&d2, alu_movew(c, lift_r16(x, a2 + 6)));  /* piece attr */
      lift_charge(x, 0x168D8);
      setw(&d0, alu_movew(c, lift_r16(x, a3 + 4)));
      lift_charge(x, 0x168DC);
      setw(&d2, alu_movew(c, W(d0) ^ W(d2)));     /* eor: combine flips */
      lift_charge(x, 0x168E0);
      setw(&d2, alu_andw(c, 0xF800, W(d2)));
      lift_charge(x, 0x168E2);
      alu_btst(c, lift_r8(x, a3 + 5), 0);         /* attr bit 8 */
      lift_charge(x, 0x168E6);
      int plain = c->zf;
      lift_charge_bcc(x, 0x168EC, plain);
      if (!plain)
      {
        alu_btst(c, d2, 14);                      /* palette high bit set? */
        lift_charge(x, 0x168F0);
        int z = c->zf;
        lift_charge_bcc(x, 0x168F4, z);
        if (!z)
        {
          d2 = alu_bset(c, d2, 13);               /* bump palette */
          lift_charge(x, 0x168F8);
        }
      }
      d2 = (d2 & ~0xFFu) |                        /* or.b: cached VRAM slot */
           alu_moveb(c, lift_r8(x, a3 + 0xA + SW(d5)) | (d2 & 0xFF));
      lift_charge(x, 0x168FC);
      setw(&d2, alu_addw(c, lift_r16(x, a3 + 0x12), W(d2)));  /* + tile base */
      lift_charge(x, 0x16900);
      lift_w16(x, a6 + 4, alu_movew(c, W(d2)));   /* tile attr */
      lift_charge(x, 0x16904);
      d0 = SEW(lift_r16(x, sp)); sp += 2;         /* movem.w (sp)+,d0-d2 */
      d1 = SEW(lift_r16(x, sp)); sp += 2;
      d2 = SEW(lift_r16(x, sp)); sp += 2;
      lift_charge_movem(x, 0x16908);
      setw(&d6, alu_addw(c, 1, W(d6)));
      lift_charge(x, 0x1690C);
      a6 += 8;
      lift_charge(x, 0x1690E);
      a2 += 8;
      lift_charge(x, 0x16910);
      uint32_t nd5 = W(W(d5) - 1);                /* dbf: no flags */
      setw(&d5, nd5);
      int taken = (nd5 != 0xFFFF);
      lift_charge_dbcc(x, 0x16912, taken, !taken);
      if (!taken) break;
    }
  }

  /* loc_16916: restore d0-d5/a0-a2 and the original attr word */
  d0 = lift_r32(x, sp);      d1 = lift_r32(x, sp + 4);
  d2 = lift_r32(x, sp + 8);  d3 = lift_r32(x, sp + 12);
  d4 = lift_r32(x, sp + 16); d5 = lift_r32(x, sp + 20);
  a0 = lift_r32(x, sp + 24); a1 = lift_r32(x, sp + 28);
  a2 = lift_r32(x, sp + 32);
  sp += 36;
  lift_charge_movem(x, 0x16916);
  lift_w16(x, a3 + 4, alu_movew(c, lift_r16(x, sp)));
  sp += 2;
  lift_charge(x, 0x1691A);
  lift_charge(x, 0x1691E);                        /* rts */

  c->d[0] = d0; c->d[1] = d1; c->d[2] = d2; c->d[3] = d3;
  c->d[4] = d4; c->d[5] = d5; c->d[6] = d6;
  c->a[0] = a0; c->a[1] = a1; c->a[2] = a2; c->a[5] = a5; c->a[6] = a6;
  c->pc = lift_r32(x, sp) & 0xFFFFFF;
  c->a[7] = sp + 4;
}

/*
 * Sprite_EmitClippedPieces (sub_16246; callee of Sprite_EmitClippedRun,
 * also called directly twice from sub_161D0)
 *   in:  d0 = piece-table index byte (0 = no-op), d4 = clip-window centre
 *        X-ish value, d5 = clip-window centre Y-ish value, d6 = sprites
 *        used so far, a1 = piece-table base (like Sprite_EmitPieces' a0,
 *        but already offset by the caller), a6 = hw sprite list write ptr
 *   out: a6/d6 advanced by however many pieces were in-window (capped at
 *        64 total); d0 lastingly changed only by the rink-flip +$1F index
 *        bump below; d1-d5/a1 preserved; a0 left at its final scratch
 *        position (last piece walked, or the base if none were), not
 *        restored — a genuine (if meaningless) output register
 *
 * Piece format and lookup mirror Sprite_EmitPieces' 8-byte entries
 * ([x.w y.w tile.w attr.b size.b], span/8 = piece count via the table at
 * a1+d0*2). Before the piece loop this builds a clip window from d4/d5
 * (fixed-constant offsets, same $90/$F0-ish shape as Render_WorldToScreen's
 * cull test); each piece whose own [x,x+span]/[y,y+span] box (read from the
 * piece data itself) falls outside the window is skipped, in-window pieces
 * get a 4-word hw sprite entry (Y, size, link, tile-attr, X) written to
 * (a6)+. Bails early to the shared far rts if d0 is zero or the sprite cap
 * is already reached.
 */
void Sprite_EmitClippedPieces(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp = c->a[7];

  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0x16246);
  int none = c->zf;                               /* beq */
  lift_charge_bcc(x, 0x16248, none);
  if (none)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
    c->pc = lift_r32(x, sp) & 0xFFFFFF;
    c->a[7] = sp + 4;
    return;
  }

  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);
  lift_charge(x, 0x1624C);
  int flip = !c->zf;
  lift_charge_bcc(x, 0x16252, !flip);
  if (flip)
  {
    setw(&c->d[0], alu_addw(c, 0x1F, W(c->d[0])));
    lift_charge(x, 0x16256);
  }

  alu_cmpw(c, 0x40, W(c->d[6]));
  lift_charge(x, 0x1625A);
  int full = (c->nf == c->vf);                    /* bge */
  lift_charge_bcc(x, 0x1625E, full);
  if (full)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
    c->pc = lift_r32(x, sp) & 0xFFFFFF;
    c->a[7] = sp + 4;
    return;
  }

  /* movem.l d0-d5,-(sp): pushed D5..D0 in that order, D0 lands lowest */
  {
    uint32_t saved[6] = {c->d[0], c->d[1], c->d[2], c->d[3], c->d[4], c->d[5]};
    for (int r = 5; r >= 0; r--) { sp -= 4; lift_w32(x, sp, saved[r]); }
  }
  lift_charge_movem(x, 0x16262);

  uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d3 = c->d[3],
           d4 = c->d[4], d5 = c->d[5], d6 = c->d[6];
  uint32_t a0, a6 = c->a[6];

  setw(&d0, alu_addw(c, W(d0), W(d0)));           /* index * 2 */
  lift_charge(x, 0x16266);
  a0 = c->a[1];                                   /* move.l a1,a0: no flags */
  lift_charge(x, 0x16268);
  {
    int32_t idx = SW(d0);
    setw(&d1, alu_movew(c, lift_r16(x, a0 + 2 + idx)));
    lift_charge(x, 0x1626A);
    setw(&d1, alu_subw(c, lift_r16(x, a0 + idx), W(d1)));
    lift_charge(x, 0x1626E);
    setw(&d1, alu_lsrw(c, W(d1), 3));             /* byte span / 8 = pieces */
    lift_charge(x, 0x16272);
    uint32_t npieces = W(d1);
    setw(&d1, alu_subw(c, 1, W(d1)));             /* dbf counter setup */
    lift_charge(x, 0x16274);

    if (npieces == 0) { x->declined = 1; return; }  /* dbf would wrap */

    sp -= 2;                                      /* push piece-count-1 */
    lift_w16(x, sp, alu_movew(c, W(d1)));
    lift_charge(x, 0x16276);
    a0 += SEW(lift_r16(x, a0 + idx));             /* adda.w: first piece */
    lift_charge(x, 0x16278);
  }

  setw(&d0, alu_movew(c, W(d4)));
  lift_charge(x, 0x1627C);
  setw(&d0, alu_addw(c, 0xC0, W(d0)));
  lift_charge(x, 0x1627E);
  setw(&d1, alu_movew(c, W(d0)));
  lift_charge(x, 0x16282);
  setw(&d0, alu_subw(c, 0x90, W(d0)));
  lift_charge(x, 0x16284);
  setw(&d1, alu_addw(c, 0x80, W(d1)));
  lift_charge(x, 0x16288);
  setw(&d2, alu_movew(c, 0x170));
  lift_charge(x, 0x1628C);
  setw(&d2, alu_subw(c, W(d5), W(d2)));
  lift_charge(x, 0x16290);
  setw(&d3, alu_movew(c, W(d2)));
  lift_charge(x, 0x16292);
  setw(&d2, alu_subw(c, 0x80, W(d2)));
  lift_charge(x, 0x16294);
  setw(&d3, alu_addw(c, 0x70, W(d3)));
  lift_charge(x, 0x16298);
  setw(&d4, alu_movew(c, lift_r16(x, sp)));       /* pop piece-count-1 */
  sp += 2;
  lift_charge(x, 0x1629C);

  for (;;)
  {
    alu_cmpw(c, lift_r16(x, a0 + 2), W(d2));
    lift_charge(x, 0x1629E);
    int out = (!c->zf && c->nf == c->vf);         /* bgt */
    lift_charge_bcc(x, 0x162A2, out);
    if (!out)
    {
      alu_cmpw(c, lift_r16(x, a0 + 2), W(d3));
      lift_charge(x, 0x162A6);
      out = (c->nf != c->vf);                     /* blt */
      lift_charge_bcc(x, 0x162AA, out);
    }
    if (!out)
    {
      alu_cmpw(c, lift_r16(x, a0), W(d0));
      lift_charge(x, 0x162AE);
      out = (!c->zf && c->nf == c->vf);           /* bgt */
      lift_charge_bcc(x, 0x162B0, out);
    }
    if (!out)
    {
      alu_cmpw(c, lift_r16(x, a0), W(d1));
      lift_charge(x, 0x162B4);
      out = (c->nf != c->vf);                     /* blt */
      lift_charge_bcc(x, 0x162B6, out);
    }

    int capped = 0;
    if (!out)
    {
      setw(&d5, alu_movew(c, lift_r16(x, a0 + 2)));
      lift_charge(x, 0x162BA);
      setw(&d5, alu_addw(c, 0x70, W(d5)));
      lift_charge(x, 0x162BE);
      setw(&d5, alu_subw(c, W(d2), W(d5)));
      lift_charge(x, 0x162C2);
      lift_w16(x, a6, alu_movew(c, W(d5))); a6 += 2;
      lift_charge(x, 0x162C4);
      lift_w8(x, a6, alu_moveb(c, lift_r8(x, a0 + 7))); a6 += 1;
      lift_charge(x, 0x162C6);
      lift_w8(x, a6, alu_moveb(c, d6)); a6 += 1;
      lift_charge(x, 0x162CA);
      setw(&d5, alu_movew(c, lift_r16(x, a0 + 6)));
      lift_charge(x, 0x162CC);
      setw(&d5, alu_andw(c, 0xF800, W(d5)));
      lift_charge(x, 0x162D0);
      setw(&d5, alu_addw(c, lift_r16(x, a0 + 4), W(d5)));
      lift_charge(x, 0x162D4);
      setw(&d5, alu_addw(c, lift_r16(x, R_TILE_ATTR_BIAS), W(d5)));
      lift_charge(x, 0x162D8);
      lift_w16(x, a6, alu_movew(c, W(d5))); a6 += 2;
      lift_charge(x, 0x162DC);
      setw(&d5, alu_movew(c, lift_r16(x, a0)));
      lift_charge(x, 0x162DE);
      setw(&d5, alu_addw(c, 0x70, W(d5)));
      lift_charge(x, 0x162E0);
      setw(&d5, alu_subw(c, W(d0), W(d5)));
      lift_charge(x, 0x162E4);
      lift_w16(x, a6, alu_movew(c, W(d5))); a6 += 2;
      lift_charge(x, 0x162E6);
      setw(&d6, alu_addw(c, 1, W(d6)));
      lift_charge(x, 0x162E8);
      alu_cmpw(c, 0x40, W(d6));
      lift_charge(x, 0x162EA);
      capped = c->zf;                             /* beq: cap hit exactly */
      lift_charge_bcc(x, 0x162EE, capped);
    }
    if (capped) break;                            /* -> loc_162F8, no dbf */

    a0 += 8;                                       /* addq.w #8,a0: no flags */
    lift_charge(x, 0x162F2);
    uint32_t nd4 = W(W(d4) - 1);
    setw(&d4, nd4);
    int taken = (nd4 != 0xFFFF);
    lift_charge_dbcc(x, 0x162F4, taken, !taken);
    if (!taken) break;
  }

  /* movem.l (sp)+,d0-d5: restores d0-d5 to the values pushed above (d0 is
     the ext.w'd/flip-adjusted value; d1-d5 are the caller's originals) */
  sp += 24;
  lift_charge_movem(x, 0x162F8);
  lift_charge(x, 0x162FC);                        /* rts */
  c->d[6] = d6;
  c->a[0] = a0;
  c->a[6] = a6;
  c->pc = lift_r32(x, sp) & 0xFFFFFF;
  c->a[7] = sp + 4;
}

/*
 * Sprite_EmitClippedRun (sub_16226; called from sub_161D0)
 *   in:  d2 = raw run length (clamped here to 0-3), d3 = piece-table index
 *        base, plus everything Sprite_EmitClippedPieces needs (d4/d5/d6/
 *        a1/a6)
 *   out: same as Sprite_EmitClippedPieces, called once per index d3+i for
 *        i = clamp(d2,0..3)-1 downto 0
 *
 * Clamps d2 to at most 3, then calls Sprite_EmitClippedPieces once for each
 * index d3+d2 .. d3+0. The dbf is entered via an unconditional branch
 * straight to the decrement/test (no separate "count-1" setup instruction),
 * so the loop runs exactly the clamped d2 value's worth of iterations.
 */
void Sprite_EmitClippedRun(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[2], alu_andw(c, 0xF, W(c->d[2])));
  lift_charge(x, 0x16226);
  alu_cmpw(c, 3, W(c->d[2]));
  lift_charge(x, 0x1622A);
  int inrange = (c->cf || c->zf);                 /* bls */
  lift_charge_bcc(x, 0x1622E, inrange);
  if (!inrange)
  {
    c->d[2] = alu_moveql(c, 3);
    lift_charge(x, 0x16232);
  }
  lift_charge(x, 0x16234);                        /* bra.w: straight to dbf */

  for (;;)
  {
    uint32_t nd2 = W(W(c->d[2]) - 1);
    setw(&c->d[2], nd2);
    int expired = (nd2 == 0xFFFF);
    lift_charge_dbcc(x, 0x16240, !expired, expired);
    if (expired) break;

    setw(&c->d[0], alu_movew(c, W(c->d[3])));
    lift_charge(x, 0x16238);
    setw(&c->d[0], alu_addw(c, W(c->d[2]), W(c->d[0])));
    lift_charge(x, 0x1623A);
    lift_call(x, 0x1623C, 4, Sprite_EmitClippedPieces);
    if (x->declined) return;
  }

  lift_charge(x, 0x16244);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_SUPPRESS_BF78  0xFFFFBF78u  /* bit 4 gates this draw entirely (purpose unclear) */
#define IND_SLOT_TABLE   0xFFFFBDA8u  /* 3 x 4-byte records just before OVERLAY_TABLE ($FFBDB4):
                                       * +0 frame index (<=0 = hidden, same convention as the
                                       * on-ice object table's frame field), +2 flags word (bit 3
                                       * tested below); purpose of the 3 slots not established */
#define IND_DEFS_HDR     0x0A78AEu    /* ROM: piece-definition header, same shape as BOARD_DEFS_HDR/SPRITE_DEFS */
#define IND_X_BIAS       0xFFFFB034u
#define IND_Y_BIAS       0xFFFFB032u
#define IND_TILE_BIAS    0xFFFFB01Cu

/*
 * Render_DrawIndicatorPieces (sub_16044; called from sub_15EC0, the render
 * dispatcher — first of a run of per-frame draw calls, most still
 * unlifted; see QUEUE.md skip notes for sub_161D0/sub_15FF0, its
 * immediate siblings there)
 *   in:  d6 = sprites used so far, a6 = hw sprite list write pointer
 *   out: a6/d6 advanced by however many pieces were emitted; d0-d5/a2/a3
 *        left at whatever their last loop iteration computed (no movem
 *        here, nothing is restored)
 *
 * Bails immediately (shared far rts) if bit 0 of $FFFFC2EC is clear, or
 * if bit 4 of $FFFFBF78 is clear. Otherwise walks the 3 fixed
 * IND_SLOT_TABLE records: a slot whose frame index is <= 0 is skipped
 * entirely (its record is also NOT advanced — the next outer pass
 * re-reads the same slot). A live slot resolves a piece-table base the
 * same way Sprite_EmitPieces/Render_DrawBoardPieces do (IND_DEFS_HDR +
 * long at +4), then emits every piece in that slot's frame directly as
 * an 8-byte hw sprite entry (mirrors Sprite_EmitClippedPieces' entry
 * layout), biased by $FFFFB034/$FFFFB032/$FFFFB01C and eor'd against
 * the slot's own tile-bank word (bits $F800 of its +2 field). Unlike
 * the other emit routines, there is no 64-sprite cap check here.
 */
void Render_DrawIndicatorPieces(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, R_RINK_FLIP), 0);            /* btst #0,($FFFFC2EC).w — same byte as R_RINK_FLIP, bit 0 */
  lift_charge(x, 0x16044);
  int bail1 = !c->zf;                                 /* bne */
  lift_charge_bcc(x, 0x1604A, bail1);
  if (bail1)
  {
    lift_charge(x, 0x15464);                          /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, R_SUPPRESS_BF78), 4);
  lift_charge(x, 0x1604E);
  int bail2 = c->zf;                                  /* beq */
  lift_charge_bcc(x, 0x16054, bail2);
  if (bail2)
  {
    lift_charge(x, 0x15464);                          /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[3] = IND_SLOT_TABLE;                           /* movea.w #$BDA8,a3: sign-extends, no flags */
  lift_charge(x, 0x16058);
  c->d[0] = alu_moveql(c, 2);                         /* moveq #2,d0 */
  lift_charge(x, 0x1605C);

  for (;;)
  {
    setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[3])));  /* move.w (a3),d4 */
    lift_charge(x, 0x1605E);
    int neg = c->nf;                                  /* bmi */
    lift_charge_bcc(x, 0x16060, neg);
    int zero = 0;
    if (!neg)
    {
      zero = c->zf;                                   /* beq (same move's flags) */
      lift_charge_bcc(x, 0x16064, zero);
    }

    if (!neg && !zero)
    {
      c->a[2] = IND_DEFS_HDR;                         /* movea.l #$A78AE,a2: no flags */
      lift_charge(x, 0x16068);
      c->a[2] += lift_r32(x, c->a[2] + 4);             /* adda.l 4(a2),a2: no flags */
      lift_charge(x, 0x1606E);
      setw(&c->d[4], alu_addw(c, W(c->d[4]), W(c->d[4])));  /* add.w d4,d4 */
      lift_charge(x, 0x16072);

      int32_t idx = SW(W(c->d[4]));
      uint32_t start = lift_r16(x, c->a[2] + idx);
      setw(&c->d[5], alu_movew(c, lift_r16(x, c->a[2] + 2 + idx)));  /* end */
      lift_charge(x, 0x16074);
      setw(&c->d[5], alu_subw(c, start, W(c->d[5])));  /* span = end - start */
      lift_charge(x, 0x16078);
      setw(&c->d[5], alu_lsrw(c, W(c->d[5]), 3));       /* /8 = piece count */
      lift_charge(x, 0x1607C);
      uint32_t npieces = W(c->d[5]);
      setw(&c->d[5], alu_subw(c, 1, W(c->d[5])));       /* dbf counter setup */
      lift_charge(x, 0x1607E);
      c->a[2] += SW(start);                             /* adda.w (a2,d4.w),a2: no flags */
      lift_charge(x, 0x16080);

      if (npieces == 0) { x->declined = 1; return; }    /* dbf would wrap */

      for (;;)
      {
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 2)));  /* move.w 2(a2),d2 */
        lift_charge(x, 0x16084);
        setw(&c->d[2], alu_addw(c, 0x80, W(c->d[2])));
        lift_charge(x, 0x16088);
        setw(&c->d[2], alu_addw(c, lift_r16(x, IND_X_BIAS), W(c->d[2])));
        lift_charge(x, 0x1608C);
        lift_w16(x, c->a[6], alu_movew(c, W(c->d[2])));  /* move.w d2,(a6) */
        lift_charge(x, 0x16090);
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2])));  /* move.w (a2),d2 */
        lift_charge(x, 0x16092);

        alu_btst(c, lift_r8(x, c->a[3] + 2), 3);          /* btst #3,2(a3) */
        lift_charge(x, 0x16094);
        int skip = c->zf;                                 /* beq */
        lift_charge_bcc(x, 0x1609A, skip);
        if (!skip)
        {
          setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[2] + 7)));  /* move.b 7(a2),d2 */
          lift_charge(x, 0x1609E);
          setw(&c->d[2], alu_andw(c, 0xC, W(c->d[2])));
          lift_charge(x, 0x160A2);
          setw(&c->d[2], alu_addw(c, 4, W(c->d[2])));      /* addq.w #4,d2 */
          lift_charge(x, 0x160A6);
          setw(&c->d[2], alu_aslw(c, W(c->d[2]), 1));       /* asl.w #1,d2 */
          lift_charge(x, 0x160A8);
          setw(&c->d[2], alu_negw(c, W(c->d[2])));          /* neg.w d2 */
          lift_charge(x, 0x160AA);
          setw(&c->d[2], alu_subw(c, lift_r16(x, c->a[2]), W(c->d[2])));  /* sub.w (a2),d2 */
          lift_charge(x, 0x160AC);
        }

        setw(&c->d[2], alu_addw(c, 0x80, W(c->d[2])));
        lift_charge(x, 0x160AE);
        setw(&c->d[2], alu_addw(c, lift_r16(x, IND_Y_BIAS), W(c->d[2])));
        lift_charge(x, 0x160B2);
        lift_w16(x, c->a[6] + 6, alu_movew(c, W(c->d[2])));  /* move.w d2,6(a6) */
        lift_charge(x, 0x160B6);
        lift_w8(x, c->a[6] + 2, alu_moveb(c, lift_r8(x, c->a[2] + 7)));  /* move.b 7(a2),2(a6) */
        lift_charge(x, 0x160BA);
        lift_w8(x, c->a[6] + 3, alu_moveb(c, W(c->d[6])));   /* move.b d6,3(a6) */
        lift_charge(x, 0x160C0);
        setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 6)));  /* move.w 6(a2),d2 */
        lift_charge(x, 0x160C4);
        setw(&c->d[2], alu_andw(c, 0xF800, W(c->d[2])));
        lift_charge(x, 0x160C8);
        setw(&c->d[2], alu_movew(c, W(c->d[2]) | lift_r16(x, c->a[2] + 4)));  /* or.w 4(a2),d2 */
        lift_charge(x, 0x160CC);
        setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[3] + 2)));  /* move.w 2(a3),d1 */
        lift_charge(x, 0x160D0);
        setw(&c->d[1], alu_andw(c, 0xF800, W(c->d[1])));
        lift_charge(x, 0x160D4);
        setw(&c->d[2], alu_movew(c, W(c->d[2]) ^ W(c->d[1])));  /* eor.w d1,d2 */
        lift_charge(x, 0x160D8);
        setw(&c->d[2], alu_addw(c, lift_r16(x, IND_TILE_BIAS), W(c->d[2])));
        lift_charge(x, 0x160DA);
        lift_w16(x, c->a[6] + 4, alu_movew(c, W(c->d[2])));  /* move.w d2,4(a6) */
        lift_charge(x, 0x160DE);
        setw(&c->d[6], alu_addw(c, 1, W(c->d[6])));           /* addq.w #1,d6 */
        lift_charge(x, 0x160E2);
        c->a[6] += 8;                                          /* addq.w #8,a6: no flags */
        lift_charge(x, 0x160E4);
        c->a[2] += 8;                                          /* addq.w #8,a2: no flags */
        lift_charge(x, 0x160E6);

        uint32_t nd5 = W(W(c->d[5]) - 1);
        setw(&c->d[5], nd5);
        int taken5 = (nd5 != 0xFFFF);
        lift_charge_dbcc(x, 0x160E8, taken5, !taken5);
        if (!taken5) break;
      }

      c->a[3] += 4;                                          /* addq.w #4,a3: no flags */
      lift_charge(x, 0x160EC);
    }

    uint32_t nd0 = W(W(c->d[0]) - 1);
    setw(&c->d[0], nd0);
    int taken0 = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x160EE, taken0, !taken0);
    if (!taken0) break;
  }

  lift_charge(x, 0x160F2);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Render_DrawObject (sub_16782; called from Render_DrawObjectList and
 * sub_1661A)
 *   in:  a3 = object block
 * Project the object's world position through the camera; if the object is
 * visible (height not negative, projection not culled), emit its sprite
 * pieces. d0-d2 preserved.
 */
void Render_DrawObject(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  c->a[7] -= 12;                                  /* movem.l d0-d2,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->d[2]);
  lift_charge_movem(x, 0x16782);

  setw(&c->d[0], alu_movew(c, lift_r16(x, a3)));        /* world X */
  lift_charge(x, 0x16786);
  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x14))); /* world Y */
  lift_charge(x, 0x16788);
  setw(&c->d[2], alu_movew(c, lift_r16(x, a3 + 0x18))); /* height */
  lift_charge(x, 0x1678C);
  int hidden = c->nf;
  lift_charge_bcc(x, 0x16790, hidden);
  if (!hidden)
  {
    lift_call(x, 0x16794, 4, Render_WorldToScreen);
    alu_cmpw(c, OFFSCREEN_Y, W(c->d[1]));
    lift_charge(x, 0x16798);
    int culled = c->zf;
    lift_charge_bcc(x, 0x1679C, culled);
    if (!culled)
    {
      lift_call(x, 0x167A0, 4, Sprite_EmitObject);
      if (x->declined) return;
    }
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x167A4);
  lift_charge(x, 0x167A8);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Render_DrawObjectList (sub_165FC; called from sub_15EC0)
 * Draw all 16 on-ice objects back-to-front: each byte of the depth-sorted
 * order list at $FFB88A is an object index into the 64-byte-slot table at
 * $FFB04A.
 */
void Render_DrawObjectList(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[4] = DRAW_ORDER;                           /* movea.w sign-extends */
  lift_charge(x, 0x165FC);
  setw(&c->d[0], alu_movew(c, 0xF));
  lift_charge(x, 0x16600);
  for (;;)
  {
    setw(&c->d[1], alu_movew(c, 0));
    lift_charge(x, 0x16604);
    c->d[1] = (c->d[1] & ~0xFFu) | alu_moveb(c, lift_r8(x, c->a[4]));
    c->a[4] += 1;
    lift_charge(x, 0x16606);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 6));   /* index * 64 */
    lift_charge(x, 0x16608);
    c->a[3] = OBJ_TABLE;
    lift_charge(x, 0x1660A);
    c->a[3] += SEW(c->d[1]);                      /* adda.w */
    lift_charge(x, 0x1660E);
    lift_call(x, 0x16610, 4, Render_DrawObject);
    if (x->declined) return;
    uint32_t nd0 = W(W(c->d[0]) - 1);             /* dbf */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x16614, taken, !taken);
    if (!taken) break;
  }
  lift_charge(x, 0x16618);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_CAM_LOCK     0xFFFFC2ECu  /* bit 6: camera target already fixed this frame */
#define R_CAM_TARGET_Y 0xFFFFBF90u  /* cached camera target Y */
#define R_CAM_TARGET_X 0xFFFFBF8Eu  /* cached camera target X */
#define R_CAM_TRACK_IX 0xFFFFB7AAu  /* signed table index; <0 = use fixed entry at $B74A */
#define R_CAM_YADJ     0xFFFFBF92u  /* per-frame Y offset applied to the tracked object, clamped [-$32,-$FFCE] */

/*
 * Camera_UpdateScroll (sub_AFCA; called from sub_794E)
 * Recomputes the camera's target position (unless R_CAM_LOCK bit 6 is
 * set, in which case the last-cached target is reused) from a tracked
 * object's world position — either a fixed table entry at $B74A, or one
 * of the 16 player slots at $B04A selected by R_CAM_TRACK_IX — then
 * eases the live camera scroll ($FFBD18 Y / $FFBD1C X) toward that
 * target: within a dead zone the axis doesn't move at all; outside it,
 * the step is the clamped delta / 16 (minimum step 1).
 */
void Camera_UpdateScroll(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[2], alu_movew(c, lift_r16(x, R_CAM_TARGET_Y)));   /* move.w (abs),d2 */
  lift_charge(x, 0xAFCA);
  setw(&c->d[3], alu_movew(c, lift_r16(x, R_CAM_TARGET_X)));   /* move.w (abs),d3 */
  lift_charge(x, 0xAFCE);

  alu_btst(c, lift_r8(x, R_CAM_LOCK), 6);                      /* btst #6,(abs) */
  lift_charge(x, 0xAFD2);
  int locked = !c->zf;
  lift_charge_bcc(x, 0xAFD8, locked);

  if (!locked)
  {
    c->a[3] = 0xFFFFB74A;                                        /* movea.w #$B74A,a3 */
    lift_charge(x, 0xAFDC);
    setw(&c->d[0], alu_movew(c, lift_r16(x, R_CAM_TRACK_IX)));  /* move.w (abs),d0 */
    lift_charge(x, 0xAFE0);
    int fixed = c->nf;                                           /* bmi.w loc_B02A */
    lift_charge_bcc(x, 0xAFE4, fixed);

    if (!fixed)
    {
      setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));                /* asl.w #7,d0 */
      lift_charge(x, 0xAFE8);
      c->a[3] = 0xFFFFB04A;                                       /* movea.w #$B04A,a3 */
      lift_charge(x, 0xAFEA);
      c->a[3] = c->a[3] + SEW(c->d[0]);                           /* adda.w d0,a3 */
      lift_charge(x, 0xAFEE);

      setw(&c->d[0], alu_movew(c, W(c->d[7])));                  /* move.w d7,d0 */
      lift_charge(x, 0xAFF0);
      setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));       /* add.w d0,d0 */
      lift_charge(x, 0xAFF2);

      alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);                /* btst #7,$62(a3) */
      lift_charge(x, 0xAFF4);
      int noadj = c->zf;                                          /* beq.w loc_B016 */
      lift_charge_bcc(x, 0xAFFA, noadj);

      if (!noadj)
      {
        uint32_t adj = alu_addw(c, W(c->d[0]), lift_r16(x, R_CAM_YADJ));  /* add.w d0,(abs) */
        lift_w16(x, R_CAM_YADJ, adj);
        lift_charge(x, 0xAFFE);

        alu_cmpw(c, 0x32, lift_r16(x, R_CAM_YADJ));               /* cmp.w #$32,(abs) */
        lift_charge(x, 0xB002);
        int lt = (c->nf != c->vf);                                 /* blt.w loc_B02A */
        lift_charge_bcc(x, 0xB008, lt);
        if (!lt)
        {
          lift_w16(x, R_CAM_YADJ, alu_movew(c, 0x32));             /* move.w #$32,(abs) */
          lift_charge(x, 0xB00C);
          lift_charge(x, 0xB012);                                  /* bra.w loc_B02A */
        }
      }
      else
      {
        uint32_t r = alu_subw(c, W(c->d[0]), lift_r16(x, R_CAM_YADJ));  /* sub.w d0,(abs) */
        lift_w16(x, R_CAM_YADJ, r);
        lift_charge(x, 0xB016);

        alu_cmpw(c, 0xFFCE, lift_r16(x, R_CAM_YADJ));              /* cmp.w #$FFCE,(abs) */
        lift_charge(x, 0xB01A);
        int gt = (!c->zf) && (c->nf == c->vf);                     /* bgt.w loc_B02A */
        lift_charge_bcc(x, 0xB020, gt);
        if (!gt)
        {
          lift_w16(x, R_CAM_YADJ, alu_movew(c, 0xFFCE));           /* move.w #$FFCE,(abs) */
          lift_charge(x, 0xB024);
        }
      }
    }

    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[3] + 0x2A)));    /* move.w $2A(a3),d2 */
    lift_charge(x, 0xB02A);
    setw(&c->d[2], alu_asrw(c, W(c->d[2]), 7));                    /* asr.w #7,d2 */
    lift_charge(x, 0xB02E);
    setw(&c->d[2], alu_addw(c, lift_r16(x, c->a[3] + 0x14), W(c->d[2])));  /* add.w $14(a3),d2 */
    lift_charge(x, 0xB030);
    setw(&c->d[2], alu_addw(c, lift_r16(x, R_CAM_YADJ), W(c->d[2])));  /* add.w (abs),d2 */
    lift_charge(x, 0xB034);
    setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[3])));            /* move.w (a3),d3 */
    lift_charge(x, 0xB038);
    lift_w16(x, R_CAM_TARGET_Y, alu_movew(c, W(c->d[2])));         /* move.w d2,(abs) */
    lift_charge(x, 0xB03A);
    lift_w16(x, R_CAM_TARGET_X, alu_movew(c, W(c->d[3])));         /* move.w d3,(abs) */
    lift_charge(x, 0xB03E);
  }

  /* loc_B042: converge camera Y toward d2 */
  setw(&c->d[0], alu_movew(c, W(c->d[2])));                        /* move.w d2,d0 */
  lift_charge(x, 0xB042);
  setw(&c->d[0], alu_subw(c, lift_r16(x, R_CAMERA_Y), W(c->d[0])));  /* sub.w (abs),d0 */
  lift_charge(x, 0xB044);
  alu_cmpw(c, 0xFFF6, W(c->d[0]));                                  /* cmp.w #$FFF6,d0 */
  lift_charge(x, 0xB048);
  int geY = (c->nf == c->vf);                                       /* bge.w loc_B066 */
  lift_charge_bcc(x, 0xB04C, geY);

  int skipStepY = 0;
  if (!geY)
  {
    setw(&c->d[1], alu_movew(c, W(c->d[2])));                       /* move.w d2,d1 */
    lift_charge(x, 0xB050);
    setw(&c->d[1], alu_subw(c, 0xFFF6, W(c->d[1])));                /* sub.w #$FFF6,d1 */
    lift_charge(x, 0xB052);
    alu_cmpw(c, 0xFF38, W(c->d[1]));                                 /* cmp.w #$FF38,d1 */
    lift_charge(x, 0xB056);
    int gt = (!c->zf) && (c->nf == c->vf);                           /* bgt.w loc_B080 */
    lift_charge_bcc(x, 0xB05A, gt);
    if (!gt)
    {
      setw(&c->d[1], alu_movew(c, 0xFF38));                          /* move.w #$FF38,d1 */
      lift_charge(x, 0xB05E);
      lift_charge(x, 0xB062);                                        /* bra.w loc_B080 */
    }
  }
  else
  {
    alu_cmpw(c, 0xA, W(c->d[0]));                                     /* cmp.w #$A,d0 */
    lift_charge(x, 0xB066);
    int le = c->zf || (c->nf != c->vf);                               /* ble.w loc_B094 */
    lift_charge_bcc(x, 0xB06A, le);
    if (le)
    {
      skipStepY = 1;
    }
    else
    {
      setw(&c->d[1], alu_movew(c, W(c->d[2])));                       /* move.w d2,d1 */
      lift_charge(x, 0xB06E);
      setw(&c->d[1], alu_subw(c, 0xA, W(c->d[1])));                   /* sub.w #$A,d1 */
      lift_charge(x, 0xB070);
      alu_cmpw(c, 0x100, W(c->d[1]));                                  /* cmp.w #$100,d1 */
      lift_charge(x, 0xB074);
      int lt2 = (c->nf != c->vf);                                      /* blt.w loc_B080 */
      lift_charge_bcc(x, 0xB078, lt2);
      if (!lt2)
      {
        setw(&c->d[1], alu_movew(c, 0x100));                           /* move.w #$100,d1 */
        lift_charge(x, 0xB07C);
      }
    }
  }

  if (!skipStepY)
  {
    /* loc_B080 */
    setw(&c->d[1], alu_subw(c, lift_r16(x, R_CAMERA_Y), W(c->d[1])));  /* sub.w (abs),d1 */
    lift_charge(x, 0xB080);
    int zeroY = c->zf;                                                 /* beq.w loc_B094 */
    lift_charge_bcc(x, 0xB084, zeroY);
    if (!zeroY)
    {
      setw(&c->d[1], alu_asrw(c, W(c->d[1]), 4));                       /* asr.w #4,d1 */
      lift_charge(x, 0xB088);
      int nz = !c->zf;                                                  /* bne.w loc_B090 */
      lift_charge_bcc(x, 0xB08A, nz);
      if (!nz)
      {
        setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));                     /* addq.w #1,d1 */
        lift_charge(x, 0xB08E);
      }
      lift_w16(x, R_CAMERA_Y, alu_addw(c, W(c->d[1]), lift_r16(x, R_CAMERA_Y)));  /* add.w d1,(abs) */
      lift_charge(x, 0xB090);
    }
  }

  /* loc_B094: converge camera X toward d3 */
  setw(&c->d[0], alu_movew(c, W(c->d[3])));                          /* move.w d3,d0 */
  lift_charge(x, 0xB094);
  setw(&c->d[0], alu_subw(c, lift_r16(x, R_CAMERA_X), W(c->d[0])));  /* sub.w (abs),d0 */
  lift_charge(x, 0xB096);
  alu_cmpw(c, 0xFFD8, W(c->d[0]));                                    /* cmp.w #$FFD8,d0 */
  lift_charge(x, 0xB09A);
  int geX = (c->nf == c->vf);                                         /* bge.w loc_B0B8 */
  lift_charge_bcc(x, 0xB09E, geX);

  int returnNow = 0;
  if (!geX)
  {
    setw(&c->d[1], alu_movew(c, W(c->d[3])));                          /* move.w d3,d1 */
    lift_charge(x, 0xB0A2);
    setw(&c->d[1], alu_subw(c, 0xFFD8, W(c->d[1])));                   /* sub.w #$FFD8,d1 */
    lift_charge(x, 0xB0A4);
    alu_cmpw(c, 0xFFC4, W(c->d[1]));                                    /* cmp.w #$FFC4,d1 */
    lift_charge(x, 0xB0A8);
    int geX2 = (c->nf == c->vf);                                        /* bge.w loc_B0D2 */
    lift_charge_bcc(x, 0xB0AC, geX2);
    if (!geX2)
    {
      setw(&c->d[1], alu_movew(c, 0xFFC4));                             /* move.w #$FFC4,d1 */
      lift_charge(x, 0xB0B0);
      lift_charge(x, 0xB0B4);                                           /* bra.w loc_B0D2 */
    }
  }
  else
  {
    alu_cmpw(c, 0x28, W(c->d[0]));                                       /* cmp.w #$28,d0 */
    lift_charge(x, 0xB0B8);
    int leX = c->zf || (c->nf != c->vf);                                 /* ble.w locret_B0E6 */
    lift_charge_bcc(x, 0xB0BC, leX);
    if (leX)
    {
      returnNow = 1;
    }
    else
    {
      setw(&c->d[1], alu_movew(c, W(c->d[3])));                          /* move.w d3,d1 */
      lift_charge(x, 0xB0C0);
      setw(&c->d[1], alu_subw(c, 0x28, W(c->d[1])));                     /* sub.w #$28,d1 */
      lift_charge(x, 0xB0C2);
      alu_cmpw(c, 0x3C, W(c->d[1]));                                      /* cmp.w #$3C,d1 */
      lift_charge(x, 0xB0C6);
      int leX2 = c->zf || (c->nf != c->vf);                               /* ble.w loc_B0D2 */
      lift_charge_bcc(x, 0xB0CA, leX2);
      if (!leX2)
      {
        setw(&c->d[1], alu_movew(c, 0x3C));                               /* move.w #$3C,d1 */
        lift_charge(x, 0xB0CE);
      }
    }
  }

  if (!returnNow)
  {
    /* loc_B0D2 */
    setw(&c->d[1], alu_subw(c, lift_r16(x, R_CAMERA_X), W(c->d[1])));    /* sub.w (abs),d1 */
    lift_charge(x, 0xB0D2);
    int zeroX = c->zf;                                                    /* beq.w locret_B0E6 */
    lift_charge_bcc(x, 0xB0D6, zeroX);
    if (!zeroX)
    {
      setw(&c->d[1], alu_asrw(c, W(c->d[1]), 4));                          /* asr.w #4,d1 */
      lift_charge(x, 0xB0DA);
      int nzX = !c->zf;                                                    /* bne.w loc_B0E2 */
      lift_charge_bcc(x, 0xB0DC, nzX);
      if (!nzX)
      {
        setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));                        /* addq.w #1,d1 */
        lift_charge(x, 0xB0E0);
      }
      lift_w16(x, R_CAMERA_X, alu_addw(c, W(c->d[1]), lift_r16(x, R_CAMERA_X)));  /* add.w d1,(abs) */
      lift_charge(x, 0xB0E2);
    }
  }

  lift_charge(x, 0xB0E6);                                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_CAM_ZONE_FLAG 0xFFFFC2F8u  /* bit 5: tracked object deep + centered (camera zoom/cut trigger) */

/*
 * Render_UpdateZoneFlag (sub_F8BA0; called from sub_794E)
 * Clears R_CAM_ZONE_FLAG bit 5, then — unless R_CAM_TRACK_IX is
 * negative (no tracked object) — re-sets it if the tracked player
 * (selected the same way Camera_UpdateScroll picks its target, from
 * the $B04A player table) is far enough up/down the ice (|world Y-ish
 * field at +$14, sign-flipped per the +$62 bit 7 side flag| >= $58)
 * while still within +/-$47 of centre on world X (+0).
 */
void Render_UpdateZoneFlag(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, R_CAM_ZONE_FLAG, alu_bclr(c, lift_r8(x, R_CAM_ZONE_FLAG), 5));  /* bclr #5,(abs) */
  lift_charge(x, 0xF8BA0);

  alu_movew(c, lift_r16(x, R_CAM_TRACK_IX));       /* tst.w (abs) */
  lift_charge(x, 0xF8BA6);
  int none = c->nf;                                 /* bmi.w locret_F8BF2 */
  lift_charge_bcc(x, 0xF8BAA, none);
  if (none)
  {
    lift_charge(x, 0xF8BF2);                         /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0];
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);      /* movem.l d0/a0,-(sp): a0 pushed first */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);      /* d0 lands lowest */
  lift_charge_movem(x, 0xF8BAE);

  c->a[0] = 0xFFFFB04A;                               /* movea.l #$FFFFB04A,a0 */
  lift_charge(x, 0xF8BB2);
  setw(&c->d[0], alu_movew(c, lift_r16(x, R_CAM_TRACK_IX)));  /* move.w (abs),d0 */
  lift_charge(x, 0xF8BB8);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));          /* asl.w #7,d0 */
  lift_charge(x, 0xF8BBC);
  c->a[0] += SEW(c->d[0]);                              /* adda.w d0,a0 */
  lift_charge(x, 0xF8BBE);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x14)));  /* move.w $14(a0),d0 */
  lift_charge(x, 0xF8BC0);
  alu_btst(c, lift_r8(x, c->a[0] + 0x62), 7);           /* btst #7,$62(a0) */
  lift_charge(x, 0xF8BC4);
  int side = !c->zf;                                     /* bne.w loc_F8BD0 */
  lift_charge_bcc(x, 0xF8BCA, side);
  if (!side)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));             /* neg.w d0 */
    lift_charge(x, 0xF8BCE);
  }

  /* loc_F8BD0 */
  alu_cmpw(c, 0x58, W(c->d[0]));                          /* cmp.w #$58,d0 */
  lift_charge(x, 0xF8BD0);
  int lt = (c->nf != c->vf);                               /* blt.w loc_F8BEE */
  lift_charge_bcc(x, 0xF8BD4, lt);
  int setFlag = 0;
  if (!lt)
  {
    alu_cmpw(c, 0x47, lift_r16(x, c->a[0]));               /* cmp.w #$47,(a0) */
    lift_charge(x, 0xF8BD8);
    int gt = (!c->zf) && (c->nf == c->vf);                  /* bgt.w loc_F8BEE */
    lift_charge_bcc(x, 0xF8BDC, gt);
    if (!gt)
    {
      alu_cmpw(c, 0xFFB9, lift_r16(x, c->a[0]));            /* cmp.w #$FFB9,(a0) */
      lift_charge(x, 0xF8BE0);
      int lt2 = (c->nf != c->vf);                            /* blt.w loc_F8BEE */
      lift_charge_bcc(x, 0xF8BE4, lt2);
      if (!lt2) { setFlag = 1; }
    }
  }
  if (setFlag)
  {
    lift_w8(x, R_CAM_ZONE_FLAG, alu_bset(c, lift_r8(x, R_CAM_ZONE_FLAG), 5));  /* bset #5,(abs) */
    lift_charge(x, 0xF8BE8);
  }

  /* loc_F8BEE: movem.l (sp)+,d0/a0 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xF8BEE);

  lift_charge(x, 0xF8BF2);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_CAM_FLAGS    0xFFFFBF80u  /* bit0/1/2: camera anchor state flags */
#define R_CAM_SPEED    0xFFFFB75Eu  /* comparison threshold for the anchor's approach */
#define CAM_ANCHOR_X   0xFFFFB74Au  /* fixed camera anchor object's world X (see Camera_UpdateScroll) */

/*
 * Camera_UpdateAnchorFlags (sub_10246; called from sub_FF0C)
 * Bails via the shared far rts if R_CAM_FLAGS bit 2 is clear, bit 0 is
 * set, or R_CAM_TRACK_IX is negative (no tracked player). Otherwise
 * compares $108 (or -$108, picked by R_CAM_FLAGS bit 1) against
 * R_CAM_SPEED; if that comparison doesn't favor the disable path, bails
 * too. Finally, if the camera anchor's world X is within [-$2C,$2C] of
 * centre, clears R_CAM_FLAGS bit 2; otherwise sets bit 0. Exact role TBD
 * (looks like an anchor-approach/re-lock gate feeding Camera_UpdateScroll's
 * R_CAM_LOCK path); behaviour preserved bit-for-bit.
 */
void Camera_UpdateAnchorFlags(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, R_CAM_FLAGS), 2);          /* btst #2,(abs) */
  lift_charge(x, 0x10246);
  int clear = c->zf;                                 /* beq.w locret_15464 */
  lift_charge_bcc(x, 0x1024C, clear);
  if (clear)
  {
    lift_charge(x, 0x15464);                        /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_btst(c, lift_r8(x, R_CAM_FLAGS), 0);          /* btst #0,(abs) */
  lift_charge(x, 0x10250);
  int set = !c->zf;                                   /* bne.w locret_15464 */
  lift_charge_bcc(x, 0x10256, set);
  if (set)
  {
    lift_charge(x, 0x15464);                        /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  alu_movew(c, lift_r16(x, R_CAM_TRACK_IX));        /* tst.w (abs) */
  lift_charge(x, 0x1025A);
  int none = !c->nf;                                  /* bpl.w locret_15464 */
  lift_charge_bcc(x, 0x1025E, none);
  if (none)
  {
    lift_charge(x, 0x15464);                        /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[0], alu_movew(c, 0x108));               /* move.w #$108,d0 */
  lift_charge(x, 0x10262);
  alu_btst(c, lift_r8(x, R_CAM_FLAGS), 1);          /* btst #1,(abs) */
  lift_charge(x, 0x10266);
  int alt = !c->zf;                                    /* bne.w loc_1027C */
  lift_charge_bcc(x, 0x1026C, alt);

  if (!alt)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));           /* neg.w d0 */
    lift_charge(x, 0x10270);
    alu_cmpw(c, lift_r16(x, R_CAM_SPEED), W(c->d[0])); /* cmp.w (abs),d0 */
    lift_charge(x, 0x10272);
    int gt = (!c->zf) && (c->nf == c->vf);              /* bgt.w loc_10284 */
    lift_charge_bcc(x, 0x10276, gt);
    if (!gt)
    {
      lift_charge(x, 0x1027A);                          /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  else
  {
    /* loc_1027C */
    alu_cmpw(c, lift_r16(x, R_CAM_SPEED), W(c->d[0])); /* cmp.w (abs),d0 */
    lift_charge(x, 0x1027C);
    int gt = (!c->zf) && (c->nf == c->vf);              /* bgt.w locret_15464 */
    lift_charge_bcc(x, 0x10280, gt);
    if (gt)
    {
      lift_charge(x, 0x15464);                        /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_10284 */
  alu_cmpw(c, 0x2C, lift_r16(x, CAM_ANCHOR_X));       /* cmp.w #$2C,(abs) */
  lift_charge(x, 0x10284);
  int hiOut = (!c->zf) && (c->nf == c->vf);             /* bgt.w loc_102A0 */
  lift_charge_bcc(x, 0x1028A, hiOut);
  if (!hiOut)
  {
    alu_cmpw(c, 0xFFD4, lift_r16(x, CAM_ANCHOR_X));     /* cmp.w #$FFD4,(abs) */
    lift_charge(x, 0x1028E);
    int loOut = (c->nf != c->vf);                        /* blt.w loc_102A0 */
    lift_charge_bcc(x, 0x10294, loOut);
    if (!loOut)
    {
      lift_w8(x, R_CAM_FLAGS, alu_bclr(c, lift_r8(x, R_CAM_FLAGS), 2));  /* bclr #2,(abs) */
      lift_charge(x, 0x10298);
      lift_charge(x, 0x1029E);                            /* rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  /* loc_102A0 */
  lift_w8(x, R_CAM_FLAGS, alu_bset(c, lift_r8(x, R_CAM_FLAGS), 0));  /* bset #0,(abs) */
  lift_charge(x, 0x102A0);
  lift_charge(x, 0x102A6);                                /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_SHAKE_X 0xFFFFB772u  /* signed byte: camera shake/offset X source */
#define R_SHAKE_Y 0xFFFFB774u  /* signed byte: camera shake/offset Y source */

/*
 * Camera_ComputeShakeOffset (sub_10588; called from sub_E6CE and
 * sub_105A2, both via bsr)
 *   out: d0 = (R_SHAKE_X >> 1, sign-extended) + CAM_ANCHOR_X
 *        d1 = (R_SHAKE_Y >> 1, sign-extended) + R_CAM_SPEED
 * Halves two signed byte offsets and adds them onto the camera anchor's
 * world X and the speed-threshold word (reused here as a Y-ish base;
 * exact role TBD). Behaviour preserved bit-for-bit.
 */
void Camera_ComputeShakeOffset(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, R_SHAKE_X)));  /* move.b (abs),d0 */
  lift_charge(x, 0x10588);
  setb(&c->d[0], alu_asrb(c, W(c->d[0]) & 0xFF, 1));    /* asr.b #1,d0 */
  lift_charge(x, 0x1058C);
  setw(&c->d[0], alu_extw(c, W(c->d[0])));               /* ext.w d0 */
  lift_charge(x, 0x1058E);
  setw(&c->d[0], alu_addw(c, lift_r16(x, CAM_ANCHOR_X), W(c->d[0])));  /* add.w (abs),d0 */
  lift_charge(x, 0x10590);

  setb(&c->d[1], alu_moveb(c, lift_r8(x, R_SHAKE_Y)));  /* move.b (abs),d1 */
  lift_charge(x, 0x10594);
  setb(&c->d[1], alu_asrb(c, W(c->d[1]) & 0xFF, 1));    /* asr.b #1,d1 */
  lift_charge(x, 0x10598);
  setw(&c->d[1], alu_extw(c, W(c->d[1])));               /* ext.w d1 */
  lift_charge(x, 0x1059A);
  setw(&c->d[1], alu_addw(c, lift_r16(x, R_CAM_SPEED), W(c->d[1])));  /* add.w (abs),d1 */
  lift_charge(x, 0x1059C);

  lift_charge(x, 0x105A0);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ClampOffsetIfNotTracked (sub_DB3E; called twice from sub_D51C)
 *   in:  a3 = on-ice object, d1 = candidate offset, d3 = subtrahend
 *   out: d0 = 0 if this object isn't the tracked one (R_CAM_TRACK_IX !=
 *        $52(a3)); d1 = clamp(d1, [-$103,$103]... signed order below) - d3;
 *        d2 clobbered (always overwritten with R_CAM_TRACK_IX)
 */
void Object_ClampOffsetIfNotTracked(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[2], alu_movew(c, lift_r16(x, R_CAM_TRACK_IX)));  /* move.w (abs),d2 */
  lift_charge(x, 0xDB3E);
  alu_cmpw(c, lift_r16(x, c->a[3] + 0x52), W(c->d[2]));         /* cmp.w $52(a3),d2 */
  lift_charge(x, 0xDB42);
  int tracked = c->zf;                                           /* bne.w loc_DB4C */
  lift_charge_bcc(x, 0xDB46, !tracked);
  if (tracked)
  {
    setw(&c->d[0], alu_movew(c, 0));                             /* clr.w d0 */
    lift_charge(x, 0xDB4A);
  }

  /* loc_DB4C */
  alu_cmpw(c, 0x103, W(c->d[1]));                                 /* cmp.w #$103,d1 */
  lift_charge(x, 0xDB4C);
  int lt = (c->nf != c->vf);                                      /* blt.w loc_DB58 */
  lift_charge_bcc(x, 0xDB50, lt);
  if (!lt)
  {
    setw(&c->d[1], alu_movew(c, 0x103));                          /* move.w #$103,d1 */
    lift_charge(x, 0xDB54);
  }

  /* loc_DB58 */
  alu_cmpw(c, 0xFEFD, W(c->d[1]));                                /* cmp.w #$FEFD,d1 */
  lift_charge(x, 0xDB58);
  int gt = (!c->zf) && (c->nf == c->vf);                          /* bgt.w loc_DB64 */
  lift_charge_bcc(x, 0xDB5C, gt);
  if (!gt)
  {
    setw(&c->d[1], alu_movew(c, 0xFEFD));                         /* move.w #$FEFD,d1 */
    lift_charge(x, 0xDB60);
  }

  /* loc_DB64 */
  setw(&c->d[1], alu_subw(c, W(c->d[3]), W(c->d[1])));            /* sub.w d3,d1 */
  lift_charge(x, 0xDB64);

  lift_charge(x, 0xDB66);                                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_ComputeApproachOctant (sub_E594; DATA XREF from $CD9E/$D1DE and
 * others — a script/AI dispatch table)
 *   in:  a3 = on-ice object
 *   out: d0 = octant (Vector_ToOctant) on most paths, or 2/6 on the two
 *        final distance-gated paths below; d1/d2/a0 clobbered
 *
 * Bails via the shared far rts if R_CAM_TRACK_IX is negative, or if
 * either of two shake-adjusted position deltas ($28/$2A byte fields
 * minus R_SHAKE_X/Y, extended and combined with world position minus
 * the camera anchor/speed fields) falls outside [-$28,$28]. Then calls
 * Vector_ToOctant(dx,dy) on world position minus anchor/speed. If
 * R_SNAP_HALT bit 5 is clear, returns that octant directly. Otherwise
 * re-checks a camera-relative Y band ([-10,10] around $58, sign-flipped
 * per $62 bit7) and, if in range, further narrows by world X vs the
 * camera anchor to pick d0=2 or 6 (falling through to the octant value
 * on any of the gate failures). Exact role TBD; behaviour preserved
 * bit-for-bit.
 */
void Object_ComputeApproachOctant(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  alu_movew(c, lift_r16(x, R_CAM_TRACK_IX));          /* tst.w (abs) */
  lift_charge(x, 0xE594);
  int none = c->nf;                                     /* bmi.w locret_15464 */
  lift_charge_bcc(x, 0xE598, none);
  if (none)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setb(&c->d[2], alu_moveb(c, lift_r8(x, a3 + 0x28)));  /* move.b $28(a3),d2 */
  lift_charge(x, 0xE59C);
  setb(&c->d[2], alu_subb(c, lift_r8(x, R_SHAKE_X), W(c->d[2]) & 0xFF));  /* sub.b (abs),d2 */
  lift_charge(x, 0xE5A0);
  setw(&c->d[2], alu_extw(c, W(c->d[2])));               /* ext.w d2 */
  lift_charge(x, 0xE5A4);
  setw(&c->d[2], alu_addw(c, lift_r16(x, a3), W(c->d[2])));  /* add.w (a3),d2 */
  lift_charge(x, 0xE5A6);
  setw(&c->d[2], alu_subw(c, lift_r16(x, CAM_ANCHOR_X), W(c->d[2])));  /* sub.w (abs),d2 */
  lift_charge(x, 0xE5A8);
  alu_cmpw(c, 0x28, W(c->d[2]));                          /* cmp.w #$28,d2 */
  lift_charge(x, 0xE5AC);
  int gt = (!c->zf) && (c->nf == c->vf);                  /* bgt.w locret_15464 */
  lift_charge_bcc(x, 0xE5B0, gt);
  if (gt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  alu_cmpw(c, 0xFFD8, W(c->d[2]));                        /* cmp.w #$FFD8,d2 */
  lift_charge(x, 0xE5B4);
  int lt = (c->nf != c->vf);                              /* blt.w locret_15464 */
  lift_charge_bcc(x, 0xE5B8, lt);
  if (lt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setb(&c->d[1], alu_moveb(c, lift_r8(x, a3 + 0x2A)));    /* move.b $2A(a3),d1 */
  lift_charge(x, 0xE5BC);
  setb(&c->d[1], alu_subb(c, lift_r8(x, R_SHAKE_Y), W(c->d[1]) & 0xFF));  /* sub.b (abs),d1 */
  lift_charge(x, 0xE5C0);
  setw(&c->d[1], alu_extw(c, W(c->d[1])));                 /* ext.w d1 */
  lift_charge(x, 0xE5C4);
  setw(&c->d[1], alu_addw(c, lift_r16(x, a3 + 0x14), W(c->d[1])));  /* add.w $14(a3),d1 */
  lift_charge(x, 0xE5C6);
  setw(&c->d[1], alu_subw(c, lift_r16(x, R_CAM_SPEED), W(c->d[1])));  /* sub.w (abs),d1 */
  lift_charge(x, 0xE5CA);
  alu_cmpw(c, 0x28, W(c->d[1]));                            /* cmp.w #$28,d1 */
  lift_charge(x, 0xE5CE);
  gt = (!c->zf) && (c->nf == c->vf);                        /* bgt.w locret_15464 */
  lift_charge_bcc(x, 0xE5D2, gt);
  if (gt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  alu_cmpw(c, 0xFFD8, W(c->d[1]));                          /* cmp.w #$FFD8,d1 */
  lift_charge(x, 0xE5D6);
  lt = (c->nf != c->vf);                                    /* blt.w locret_15464 */
  lift_charge_bcc(x, 0xE5DA, lt);
  if (lt)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[0], alu_movew(c, lift_r16(x, a3)));            /* move.w (a3),d0 */
  lift_charge(x, 0xE5DE);
  setw(&c->d[0], alu_subw(c, lift_r16(x, CAM_ANCHOR_X), W(c->d[0])));  /* sub.w (abs),d0 */
  lift_charge(x, 0xE5E0);
  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x14)));     /* move.w $14(a3),d1 */
  lift_charge(x, 0xE5E4);
  setw(&c->d[1], alu_subw(c, lift_r16(x, R_CAM_SPEED), W(c->d[1])));  /* sub.w (abs),d1 */
  lift_charge(x, 0xE5E8);
  lift_call(x, 0xE5EC, 4, Vector_ToOctant);                 /* bsr.w sub_10676 */
  if (x->declined) return;

  alu_btst(c, lift_r8(x, 0xFFFFC2EAu), 5);                  /* btst #5,(abs) */
  lift_charge(x, 0xE5F0);
  int haveOctant = c->zf;                                    /* beq.w locret_E62C */
  lift_charge_bcc(x, 0xE5F6, haveOctant);
  if (haveOctant)
  {
    lift_charge(x, 0xE62C);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[1], alu_movew(c, lift_r16(x, a3 + 0x14)));     /* move.w $14(a3),d1 */
  lift_charge(x, 0xE5FA);
  alu_btst(c, lift_r8(x, a3 + 0x62), 7);                    /* btst #7,$62(a3) */
  lift_charge(x, 0xE5FE);
  int side = !c->zf;                                          /* bne.w loc_E60A */
  lift_charge_bcc(x, 0xE604, side);
  if (!side)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));                  /* neg.w d1 */
    lift_charge(x, 0xE608);
  }

  /* loc_E60A */
  setw(&c->d[1], alu_subw(c, 0x58, W(c->d[1])));              /* sub.w #$58,d1 */
  lift_charge(x, 0xE60A);
  alu_cmpw(c, 0xA, W(c->d[1]));                                /* cmp.w #$A,d1 */
  lift_charge(x, 0xE60E);
  gt = (!c->zf) && (c->nf == c->vf);                           /* bgt.w locret_E62C */
  lift_charge_bcc(x, 0xE612, gt);
  if (gt)
  {
    lift_charge(x, 0xE62C);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  alu_cmpw(c, 0xFFCE, W(c->d[1]));                             /* cmp.w #$FFCE,d1 */
  lift_charge(x, 0xE616);
  lt = (c->nf != c->vf);                                       /* blt.w locret_E62C */
  lift_charge_bcc(x, 0xE61A, lt);
  if (lt)
  {
    lift_charge(x, 0xE62C);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->d[0] = alu_moveql(c, 2);                                  /* moveq #2,d0 */
  lift_charge(x, 0xE61E);
  setw(&c->d[1], alu_movew(c, lift_r16(x, a3)));               /* move.w (a3),d1 */
  lift_charge(x, 0xE620);
  alu_cmpw(c, lift_r16(x, CAM_ANCHOR_X), W(c->d[1]));          /* cmp.w (abs),d1 */
  lift_charge(x, 0xE622);
  gt = (!c->zf) && (c->nf == c->vf);                            /* bgt.w locret_E62C */
  lift_charge_bcc(x, 0xE626, gt);
  if (!gt)
  {
    c->d[0] = alu_moveql(c, 6);                                 /* moveq #6,d0 */
    lift_charge(x, 0xE62A);
  }

  lift_charge(x, 0xE62C);                                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_UNK_B012 0xFFFFB012u  /* word added into the piece-table entry below */

/*
 * Sprite_BuildPieceHeader (sub_16468; called twice from sub_162FE)
 *   in:  a1 = piece-table base, d0 = packed index (word in the high
 *        16 bits, doubled to a byte offset), a0 = output cursor
 *   out: (a0-2) = table[$64 + d0*2].w + R_UNK_B012 | $8000, a0 -= 2;
 *        d0 = that same word, sign-extended to a full long (index
 *        swapped back into the high word first, i.e. round-trips)
 */
void Sprite_BuildPieceHeader(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t v = (c->d[0] << 16) | (c->d[0] >> 16);   /* swap d0 */
    alu_tstl(c, v);
    c->d[0] = v;
  }
  lift_charge(x, 0x16468);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));           /* asl.w #1,d0 */
  lift_charge(x, 0x1646A);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0x64 + SEW(c->d[0]))));  /* move.w $64(a1,d0.w),d0 */
  lift_charge(x, 0x1646C);
  setw(&c->d[0], alu_addw(c, lift_r16(x, R_UNK_B012), W(c->d[0])));  /* add.w (abs),d0 */
  lift_charge(x, 0x16470);
  setw(&c->d[0], alu_movew(c, W(c->d[0]) | 0x8000));    /* or.w #$8000,d0 */
  lift_charge(x, 0x16474);
  c->a[0] -= 2;                                          /* move.w d0,-(a0) */
  lift_w16(x, c->a[0], alu_movew(c, W(c->d[0])));
  lift_charge(x, 0x16478);
  {
    uint32_t v = (c->d[0] << 16) | (c->d[0] >> 16);     /* swap d0 */
    alu_tstl(c, v);
    c->d[0] = v;
  }
  lift_charge(x, 0x1647A);
  c->d[0] = alu_extl(c, W(c->d[0]));                     /* ext.l d0 */
  lift_charge(x, 0x1647C);

  lift_charge(x, 0x1647E);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_FD4B8   0x0FD4B8u  /* ROM: 8-entry byte table, D598 -> adjusted index */
#define R_UNK_D598  0xFFFFD598u
#define R_UNK_D5AA  0xFFFFD5AAu

/*
 * Piece_LookupFrameCount (sub_FD492; called from sub_FD46C)
 *   in:  a0 = struct pointer (uses a long at +$1E as a base, plus a word
 *        at that base's +6 as an offset — likely a piece-table header)
 *   out: d0 = piece_table[adjusted_index] - 1, where adjusted_index =
 *        (sign-extended) byte_FD4B8[R_UNK_D598] (also cached to
 *        R_UNK_D5AA); a0 clobbered
 */
void Piece_LookupFrameCount(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = lift_r32(x, c->a[0] + 0x1E);                  /* move.l $1E(a0),a0 */
  lift_charge(x, 0xFD492);
  c->a[0] += SEW(lift_r16(x, c->a[0] + 6));                /* adda.w 6(a0),a0 */
  lift_charge(x, 0xFD496);
  setw(&c->d[0], alu_movew(c, lift_r16(x, R_UNK_D598)));  /* move.w (abs),d0 */
  lift_charge(x, 0xFD49A);
  c->a[1] = TBL_FD4B8;                                     /* movea.l #TBL_FD4B8,a1 */
  lift_charge(x, 0xFD49E);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1] + SW(c->d[0]))));  /* move.b (a1,d0.w),d0 */
  lift_charge(x, 0xFD4A4);
  setw(&c->d[0], alu_extw(c, W(c->d[0])));                 /* ext.w d0 */
  lift_charge(x, 0xFD4A8);
  lift_w16(x, R_UNK_D5AA, alu_movew(c, W(c->d[0])));       /* move.w d0,(abs) */
  lift_charge(x, 0xFD4AA);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SW(c->d[0]))));  /* move.b (a0,d0.w),d0 */
  lift_charge(x, 0xFD4AE);
  setw(&c->d[0], alu_extw(c, W(c->d[0])));                 /* ext.w d0 */
  lift_charge(x, 0xFD4B2);
  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));               /* subq.w #1,d0 */
  lift_charge(x, 0xFD4B4);

  lift_charge(x, 0xFD4B6);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Piece_AdvanceChain (sub_18BC8; called from sub_18A90/sub_18AE8 and
 * others)
 *   in:  a2 = struct pointer (uses a long at +$1E as the chain base),
 *        d0 = hop count - 1 (standard dbf idiom)
 *   out: a0 = the base advanced by (d0+1) hops of {add the word at the
 *        current position, then step forward 8 bytes for all but the
 *        first hop}; d0 = -1
 */
void Piece_AdvanceChain(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = lift_r32(x, c->a[2] + 0x1E);            /* move.l $1E(a2),a0 */
  lift_charge(x, 0x18BC8);
  c->a[0] += SEW(lift_r16(x, c->a[0]));               /* adda.w (a0),a0 */
  lift_charge(x, 0x18BCC);
  lift_charge(x, 0x18BCE);                            /* bra.w loc_18BD6 */

  for (;;)
  {
    uint32_t nd0 = W(W(c->d[0]) - 1);                 /* dbf d0,loc_18BD2 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x18BD6, taken, !taken);
    if (!taken) break;

    /* loc_18BD2 */
    c->a[0] += SEW(lift_r16(x, c->a[0]));             /* adda.w (a0),a0 */
    lift_charge(x, 0x18BD2);
    c->a[0] += 8;                                      /* addq.w #8,a0 */
    lift_charge(x, 0x18BD4);
  }

  lift_charge(x, 0x18BDA);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Ptr_ChainAdd (sub_13510; called from ROM:8BBE and ROM:12CD2)
 *   in:  a1 = pointer, d0 = hop count - 1 (standard dbf idiom)
 *   out: a1 advanced by (d0+1) hops of a1 += (word at a1); d0 = -1
 */
void Ptr_ChainAdd(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_charge(x, 0x13510);                            /* bra.w loc_13516 */

  for (;;)
  {
    uint32_t nd0 = W(W(c->d[0]) - 1);                 /* dbf d0,loc_13514 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x13516, taken, !taken);
    if (!taken) break;

    /* loc_13514 */
    c->a[1] += SEW(lift_r16(x, c->a[1]));             /* adda.w (a1),a1 */
    lift_charge(x, 0x13514);
  }

  lift_charge(x, 0x1351A);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TEAM_HOME_R 0xFFFFC6CEu   /* in-game team state block (see game.c) */
#define TEAM_SIZE_R 0x364u        /* away block follows the home block */

/*
 * Team_CachePieceFrameCounts (sub_FD46C; called from ROM:FCE3E and
 * sub_FCF86)
 * Runs Piece_LookupFrameCount for the home team block, caching the
 * result to $FFFFD59C, then for the away team block, caching to
 * $FFFFD59E. d0/a0/a1 saved/restored around the whole body.
 */
void Team_CachePieceFrameCounts(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0], saved_a1 = c->a[1];

  /* movem.l d0/a0-a1,-(sp): a1 pushed first (high addr), a0, d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xFD46C);

  c->a[0] = TEAM_HOME_R;                                    /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFD470);
  lift_call(x, 0xFD476, 4, Piece_LookupFrameCount);       /* bsr.w sub_FD492 */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD59Cu, alu_movew(c, W(c->d[0])));     /* move.w d0,(abs) */
  lift_charge(x, 0xFD47A);

  c->a[0] = TEAM_HOME_R + TEAM_SIZE_R;                         /* movea.l #$FFFFCA32,a0 */
  lift_charge(x, 0xFD47E);
  lift_call(x, 0xFD484, 4, Piece_LookupFrameCount);        /* bsr.w sub_FD492 */
  if (x->declined) return;
  lift_w16(x, 0xFFFFD59Eu, alu_movew(c, W(c->d[0])));      /* move.w d0,(abs) */
  lift_charge(x, 0xFD488);

  /* movem.l (sp)+,d0/a0-a1 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFD48C);

  lift_charge(x, 0xFD490);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Anim_ResetCycleCounters (sub_FD4C0; called from ROM:FCF06/FCF2C)
 * Clears the two nested cycle counters at $FFFFD5A6/$FFFFD5A4 (see
 * sub_FD4CA, which increments and wraps them: inner mod 7, outer mod 6).
 */
void Anim_ResetCycleCounters(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w16(x, 0xFFFFD5A6u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFD4C0);
  lift_w16(x, 0xFFFFD5A4u, alu_movew(c, 0));            /* clr.w (abs) */
  lift_charge(x, 0xFD4C4);

  lift_charge(x, 0xFD4C8);                               /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Color_CopyBytesModeOrder (sub_17E04; called from sub_17D80)
 *   in:  a0 = source byte stream (2 bytes consumed), a1 = dest struct
 *   If R_UNK_CEEA is 2, 3, or 5: clears bit0 of $E(a1) and copies
 *   source bytes to 1(a1) then 3(a1). Otherwise: sets bit0 of $E(a1)
 *   and copies to 3(a1) then 1(a1) (reversed order). a0 advances by 2
 *   either way.
 */
void Color_CopyBytesModeOrder(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, 2, lift_r16(x, 0xFFFFCEEAu));            /* cmp.w #2,(abs) */
  lift_charge(x, 0x17E04);
  int m2 = c->zf;                                        /* beq.w loc_17E32 */
  lift_charge_bcc(x, 0x17E0A, m2);

  if (!m2)
  {
    alu_cmpw(c, 3, lift_r16(x, 0xFFFFCEEAu));            /* cmp.w #3,(abs) */
    lift_charge(x, 0x17E0E);
    int m3 = c->zf;                                        /* beq.w loc_17E32 */
    lift_charge_bcc(x, 0x17E14, m3);
    if (!m3)
    {
      alu_cmpw(c, 5, lift_r16(x, 0xFFFFCEEAu));            /* cmp.w #5,(abs) */
      lift_charge(x, 0x17E18);
      int m5 = c->zf;                                        /* beq.w loc_17E32 */
      lift_charge_bcc(x, 0x17E1E, m5);
      if (!m5)
      {
        lift_w8(x, c->a[1] + 0xE, alu_bset(c, lift_r8(x, c->a[1] + 0xE), 0));  /* bset #0,$E(a1) */
        lift_charge(x, 0x17E22);
        lift_w8(x, c->a[1] + 3, alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,3(a1) */
        c->a[0] += 1;
        lift_charge(x, 0x17E28);
        lift_w8(x, c->a[1] + 1, alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,1(a1) */
        c->a[0] += 1;
        lift_charge(x, 0x17E2C);

        lift_charge(x, 0x17E30);                              /* rts */
        c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
        c->a[7] += 4;
        return;
      }
    }
  }

  /* loc_17E32 */
  lift_w8(x, c->a[1] + 0xE, alu_bclr(c, lift_r8(x, c->a[1] + 0xE), 0));  /* bclr #0,$E(a1) */
  lift_charge(x, 0x17E32);
  lift_w8(x, c->a[1] + 1, alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,1(a1) */
  c->a[0] += 1;
  lift_charge(x, 0x17E38);
  lift_w8(x, c->a[1] + 3, alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,3(a1) */
  c->a[0] += 1;
  lift_charge(x, 0x17E3C);

  lift_charge(x, 0x17E40);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* callees from other decomp files used by the sub_17D80 family below */
void Rng_NextScaled(lift_ctx *);          /* math.c */
void Calc_HalvingAccumulator(lift_ctx *); /* game.c */

/*
 * sub_18032 (called from sub_18002 and via sub_17D80's batch)
 *   in:  d3 = bitmask of already-used indices (bits 0-25)
 *   out: d0.w = a random index in [0,$1A) whose d3 bit was clear;
 *        that bit is now set in d3
 *   Rejection-samples Rng_NextScaled($1A) until it draws an index whose
 *   d3 bit is still clear (bne loops while the bset finds the bit already
 *   set). First user of lift_charge_bitop_reg — the dynamic `bset d0,d3`
 *   costs +14 cycles whenever the drawn bit is >= 16.
 */
void sub_18032(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int spins = 0;

  for (;;)
  {
    /* callers never fill all 26 bits (at most 18 in play), but a full
     * mask would spin forever — decline rather than hang */
    if (++spins > 4096) { x->declined = 1; return; }

    c->d[0] = alu_moveql(c, 0x1A);                     /* moveq #$1A,d0 */
    lift_charge(x, 0x18032);
    lift_call(x, 0x18034, 4, Rng_NextScaled);          /* bsr.w sub_11086 */

    {
      uint32_t bit = c->d[0] & 31;                     /* bset d0,d3 */
      lift_charge_bitop_reg(x, 0x18038, c->d[0]);
      c->d[3] = alu_bset(c, c->d[3], (int)bit);
    }
    int taken = !c->zf;                                /* bne.s sub_18032:
                                                        * bit was already set
                                                        * -> redraw */
    lift_charge_bcc(x, 0x1803A, taken);
    if (!taken) break;
  }

  lift_charge(x, 0x1803C);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_18002 (called from sub_17E42:loc_17FDC)
 *   Marks $FFFFCEE6 (st), then deals 16 DISTINCT random indices 0-$19
 *   into the 8 $10-byte structs at $FFFFCE66 (word at +0 and +2 of each):
 *   the exclusion mask d3 starts with the two words at $FFFFC330/$FFFFC332
 *   pre-set (those indices can't be drawn), and every draw marks its bit.
 *   out: d3 = final mask, d1 = word read from $C332, d2 = $FFFF (expired
 *   dbf), d0 = last drawn index, a1 = $FFFFCEE6+$80.
 */
void sub_18002(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFFFCEE6u, 0xFF);                       /* st (CEE6).w */
  lift_charge(x, 0x18002);
  c->d[3] = alu_movel(c, 0);                           /* clr.l d3 */
  lift_charge(x, 0x18006);

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC330u))); /* move.w (abs),d1 */
  lift_charge(x, 0x18008);
  lift_charge_bitop_reg(x, 0x1800C, c->d[1]);          /* bset d1,d3 */
  c->d[3] = alu_bset(c, c->d[3], (int)(c->d[1] & 31));

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFC332u))); /* move.w (abs),d1 */
  lift_charge(x, 0x1800E);
  lift_charge_bitop_reg(x, 0x18012, c->d[1]);          /* bset d1,d3 */
  c->d[3] = alu_bset(c, c->d[3], (int)(c->d[1] & 31));

  c->a[1] = 0xFFFFCE66u;                               /* movea.w #$CE66,a1 */
  lift_charge(x, 0x18014);
  c->d[2] = alu_moveql(c, 7);                          /* moveq #7,d2 */
  lift_charge(x, 0x18018);

  for (;;)                                             /* loc_1801A */
  {
    lift_call(x, 0x1801A, 4, sub_18032);               /* bsr.w sub_18032 */
    alu_movew(c, W(c->d[0]));                          /* move.w d0,(a1) */
    lift_w16(x, c->a[1], W(c->d[0]));
    lift_charge(x, 0x1801E);

    lift_call(x, 0x18020, 4, sub_18032);               /* bsr.w sub_18032 */
    alu_movew(c, W(c->d[0]));                          /* move.w d0,2(a1) */
    lift_w16(x, c->a[1] + 2, W(c->d[0]));
    lift_charge(x, 0x18024);

    c->a[1] += 0x10;                                   /* adda.w #$10,a1 */
    lift_charge(x, 0x18028);

    uint32_t nd2 = W(c->d[2] - 1);                     /* dbf d2,loc_1801A */
    setw(&c->d[2], nd2);
    int taken = (nd2 != 0xFFFF);
    lift_charge_dbcc(x, 0x1802C, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x18030);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_17E42 (called from ROM:17BEC and sub_17D80)
 *   Rebuilds the four words at $FFFFC328/$C32A/$C32C/$C32E (cleared
 *   first). Three paths:
 *   - $FFFFD064 == 0: just re-deal via sub_18002.
 *   - $FFFFD048 == 0 or $FFFFC2FA bit0 set: look the pair up in the
 *     ROM word-pair table at $17FE6 indexed by $FFFFD04A*4, mirroring it
 *     into $C32C/$C32E for indices $14/$18, then re-deal via sub_18002.
 *   - otherwise: search the 8 $CE66 structs downward from index d1
 *     (Calc_HalvingAccumulator's output) for one whose +0 or +2 word
 *     equals the byte picked from $CEF4[$CEEE]; on a hit, publish the
 *     pair to $FFFFD04C/$D04E (order per which word matched), remember
 *     the match ($CEE6 = struct index, $C330/$C332 = the pair), and set
 *     $C328/$C32A from one of the ROM tables at $17F52/$17F72 indexed by
 *     (match-kind + $FFFFCEF0)*4, with $C32C/$C32E variants for specific
 *     indices. No re-deal on this path (falls straight to the epilogue).
 *   d0-d3/a0-a1 saved/restored (movem); d4 clobbered (mulu) on the
 *   search path only. Exact role TBD (uniform/color-clash pick, judging
 *   by the sub_17D80 family); behaviour preserved bit-for-bit.
 */
void sub_17E42(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp0 = c->a[7];

  /* movem.l d0-d3/a0-a1,-(sp): 6 longs, ascending d0,d1,d2,d3,a0,a1 */
  lift_w32(x, sp0 - 24, c->d[0]);
  lift_w32(x, sp0 - 20, c->d[1]);
  lift_w32(x, sp0 - 16, c->d[2]);
  lift_w32(x, sp0 - 12, c->d[3]);
  lift_w32(x, sp0 - 8,  c->a[0]);
  lift_w32(x, sp0 - 4,  c->a[1]);
  c->a[7] = sp0 - 24;
  lift_charge_movem(x, 0x17E42);

  alu_movew(c, 0); lift_w16(x, 0xFFFFC328u, 0);        /* clr.w (C328).w */
  lift_charge(x, 0x17E46);
  alu_movew(c, 0); lift_w16(x, 0xFFFFC32Au, 0);        /* clr.w (C32A).w */
  lift_charge(x, 0x17E4A);
  alu_movew(c, 0); lift_w16(x, 0xFFFFC32Cu, 0);        /* clr.w (C32C).w */
  lift_charge(x, 0x17E4E);
  alu_movew(c, 0); lift_w16(x, 0xFFFFC32Eu, 0);        /* clr.w (C32E).w */
  lift_charge(x, 0x17E52);

  alu_tstw(c, lift_r16(x, 0xFFFFD064u));               /* tst.w (D064).w */
  lift_charge(x, 0x17E56);
  int z = c->zf;                                       /* beq.w loc_17FDC */
  lift_charge_bcc(x, 0x17E5A, z);
  if (z) goto redeal;

  alu_tstw(c, lift_r16(x, 0xFFFFD048u));               /* tst.w (D048).w */
  lift_charge(x, 0x17E5E);
  z = c->zf;                                           /* beq.w loc_17F9A */
  lift_charge_bcc(x, 0x17E62, z);
  if (z) goto rom_table;

  alu_btst(c, lift_r8(x, 0xFFFFC2FAu), 0);             /* btst #0,(C2FA).w */
  lift_charge(x, 0x17E66);
  int t = !c->zf;                                      /* bne.w loc_17F9A */
  lift_charge_bcc(x, 0x17E6C, t);
  if (t) goto rom_table;

  lift_call(x, 0x17E70, 4, Calc_HalvingAccumulator);   /* bsr.w sub_1828A */

  c->a[0] = 0xFFFFCEF4u;                               /* movea.w #$CEF4,a0 */
  lift_charge(x, 0x17E74);
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFFFCEEEu))); /* move.w (abs),d2 */
  lift_charge(x, 0x17E78);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[2])))); /* move.b (a0,d2.w),d2 */
  lift_charge(x, 0x17E7C);
  c->a[1] = 0xFFFFCE66u;                               /* movea.w #$CE66,a1 */
  lift_charge(x, 0x17E80);
  c->d[4] = alu_moveql(c, 0x10);                       /* moveq #$10,d4 */
  lift_charge(x, 0x17E84);
  lift_charge_mulu(x, 0x17E86, W(c->d[1]));            /* mulu.w d1,d4 */
  c->d[4] = alu_mulu(c, W(c->d[1]), W(c->d[4]));
  c->a[1] += SEW(c->d[4]);                             /* adda.w d4,a1 */
  lift_charge(x, 0x17E88);
  lift_w8(x, 0xFFFFCEE6u, 0xFF);                       /* st (CEE6).w */
  lift_charge(x, 0x17E8A);
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0x17E8E);

  {
    int spins = 0;
    for (;;)                                           /* loc_17E90 */
    {
      /* d1 is (16>>k)-1 <= 15 in practice; a dbf entered with a negative
       * d1 would walk a1 far below the struct table — decline that */
      if (++spins > 1000) { x->declined = 1; return; }

      alu_cmpw(c, lift_r16(x, c->a[1]), W(c->d[2]));   /* cmp.w (a1),d2 */
      lift_charge(x, 0x17E90);
      z = c->zf;                                       /* beq.w loc_17EC6 */
      lift_charge_bcc(x, 0x17E92, z);
      if (z) goto found_first;

      alu_cmpw(c, lift_r16(x, c->a[1] + 2), W(c->d[2])); /* cmp.w 2(a1),d2 */
      lift_charge(x, 0x17E96);
      z = c->zf;                                       /* beq.w loc_17EAA */
      lift_charge_bcc(x, 0x17E9A, z);
      if (z) goto found_second;

      c->a[1] -= 0x10;                                 /* suba.w #$10,a1 */
      lift_charge(x, 0x17E9E);

      uint32_t nd1 = W(c->d[1] - 1);                   /* dbf d1,loc_17E90 */
      setw(&c->d[1], nd1);
      int tk = (nd1 != 0xFFFF);
      lift_charge_dbcc(x, 0x17EA2, tk, !tk);
      if (!tk) break;
    }
  }
  lift_charge(x, 0x17EA6);                             /* bra.w loc_17FE0 */
  goto epilogue;

found_second:                                          /* loc_17EAA */
  c->d[0] = alu_moveql(c, 4);                          /* moveq #4,d0 */
  lift_charge(x, 0x17EAA);
  alu_tstw(c, lift_r16(x, 0xFFFFD046u));               /* tst.w (D046).w */
  lift_charge(x, 0x17EAC);
  z = c->zf;                                           /* beq.w loc_17EB8 */
  lift_charge_bcc(x, 0x17EB0, z);
  if (!z)
  {
    setw(&c->d[0], alu_movew(c, 5));                   /* move.w #5,d0 */
    lift_charge(x, 0x17EB4);
  }
  {                                                    /* loc_17EB8 */
    uint32_t v = lift_r16(x, c->a[1]);                 /* move.w (a1),(D04E).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFD04Eu, v);
    lift_charge(x, 0x17EB8);
    v = lift_r16(x, c->a[1] + 2);                      /* move.w 2(a1),(D04C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFD04Cu, v);
    lift_charge(x, 0x17EBC);
  }
  lift_charge(x, 0x17EC2);                             /* bra.w loc_17ED2 */
  goto merged;

found_first:                                           /* loc_17EC6 */
  setw(&c->d[0], alu_movew(c, 0));                     /* clr.w d0 */
  lift_charge(x, 0x17EC6);
  {
    uint32_t v = lift_r16(x, c->a[1] + 2);             /* move.w 2(a1),(D04E).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFD04Eu, v);
    lift_charge(x, 0x17EC8);
    v = lift_r16(x, c->a[1]);                          /* move.w (a1),(D04C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFD04Cu, v);
    lift_charge(x, 0x17ECE);
  }

merged:                                                /* loc_17ED2 */
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFCEF0u), W(c->d[0]))); /* add.w (abs),d0 */
  lift_charge(x, 0x17ED2);
  alu_movew(c, W(c->d[1]));                            /* move.w d1,(CEE6).w */
  lift_w16(x, 0xFFFFCEE6u, W(c->d[1]));
  lift_charge(x, 0x17ED6);
  {
    uint32_t v = lift_r16(x, c->a[1]);                 /* move.w (a1),(C330).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC330u, v);
    lift_charge(x, 0x17EDA);
    v = lift_r16(x, c->a[1] + 2);                      /* move.w 2(a1),(C332).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC332u, v);
    lift_charge(x, 0x17EDE);
  }
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));          /* asl.w #2,d0 */
  lift_charge(x, 0x17EE4);
  c->a[0] = 0x17F52;                                   /* lea word_17F52(pc),a0 */
  lift_charge(x, 0x17EE6);
  alu_tstw(c, lift_r16(x, 0xFFFFD046u));               /* tst.w (D046).w */
  lift_charge(x, 0x17EEA);
  z = c->zf;                                           /* beq.w loc_17EF6 */
  lift_charge_bcc(x, 0x17EEE, z);
  if (!z)
  {
    c->a[0] = 0x17F72;                                 /* lea word_17F72(pc),a0 */
    lift_charge(x, 0x17EF2);
  }
  {                                                    /* loc_17EF6 */
    uint32_t v = lift_r16(x, c->a[0] + SEW(c->d[0]));  /* move.w (a0,d0.w),(C328).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC328u, v);
    lift_charge(x, 0x17EF6);
    v = lift_r16(x, c->a[0] + SEW(c->d[0]) + 2);       /* move.w 2(a0,d0.w),(C32A).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Au, v);
    lift_charge(x, 0x17EFC);
  }
  alu_tstw(c, lift_r16(x, 0xFFFFD046u));               /* tst.w (D046).w */
  lift_charge(x, 0x17F02);
  z = c->zf;                                           /* beq.w loc_17FE0 */
  lift_charge_bcc(x, 0x17F06, z);
  if (z) goto epilogue;

  alu_cmpw(c, 4, W(c->d[0]));                          /* cmp.w #4,d0 */
  lift_charge(x, 0x17F0A);
  z = c->zf;                                           /* beq.w loc_17F1E */
  lift_charge_bcc(x, 0x17F0E, z);
  if (z) goto mirror_pair;
  alu_cmpw(c, 0x18, W(c->d[0]));                       /* cmp.w #$18,d0 */
  lift_charge(x, 0x17F12);
  z = c->zf;                                           /* beq.w loc_17F1E */
  lift_charge_bcc(x, 0x17F16, z);
  if (z) goto mirror_pair;
  lift_charge(x, 0x17F1A);                             /* bra.w loc_17F2E */
  goto second_band;

mirror_pair:                                           /* loc_17F1E */
  {
    uint32_t v = lift_r16(x, 0xFFFFC328u);             /* move.w (C328).w,(C32C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Cu, v);
    lift_charge(x, 0x17F1E);
    v = lift_r16(x, 0xFFFFC32Au);                      /* move.w (C32A).w,(C32E).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Eu, v);
    lift_charge(x, 0x17F24);
  }
  lift_charge(x, 0x17F2A);                             /* bra.w loc_17FE0 */
  goto epilogue;

second_band:                                           /* loc_17F2E */
  alu_cmpw(c, 8, W(c->d[0]));                          /* cmp.w #8,d0 */
  lift_charge(x, 0x17F2E);
  z = c->zf;                                           /* beq.w loc_17F42 */
  lift_charge_bcc(x, 0x17F32, z);
  if (z) goto mirror_first_only;
  alu_cmpw(c, 0x1C, W(c->d[0]));                       /* cmp.w #$1C,d0 */
  lift_charge(x, 0x17F36);
  z = c->zf;                                           /* beq.w loc_17F42 */
  lift_charge_bcc(x, 0x17F3A, z);
  if (z) goto mirror_first_only;
  lift_charge(x, 0x17F3E);                             /* bra.w loc_17FE0 */
  goto epilogue;

mirror_first_only:                                     /* loc_17F42 */
  {
    uint32_t v = lift_r16(x, 0xFFFFC328u);             /* move.w (C328).w,(C32C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Cu, v);
    lift_charge(x, 0x17F42);
  }
  alu_movew(c, 0);                                     /* move.w #0,(C32E).w */
  lift_w16(x, 0xFFFFC32Eu, 0);
  lift_charge(x, 0x17F48);
  lift_charge(x, 0x17F4E);                             /* bra.w loc_17FE0 */
  goto epilogue;

rom_table:                                             /* loc_17F9A */
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD04Au))); /* move.w (D04A).w,d0 */
  lift_charge(x, 0x17F9A);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));          /* asl.w #2,d0 */
  lift_charge(x, 0x17F9E);
  c->a[0] = 0x17FE6;                                   /* lea unk_17FE6(pc),a0 */
  lift_charge(x, 0x17FA0);
  {
    uint32_t v = lift_r16(x, c->a[0] + SEW(c->d[0]));  /* move.w (a0,d0.w),(C328).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC328u, v);
    lift_charge(x, 0x17FA4);
    v = lift_r16(x, c->a[0] + SEW(c->d[0]) + 2);       /* move.w 2(a0,d0.w),(C32A).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Au, v);
    lift_charge(x, 0x17FAA);
  }
  alu_cmpw(c, 0x14, W(c->d[0]));                       /* cmp.w #$14,d0 */
  lift_charge(x, 0x17FB0);
  t = !c->zf;                                          /* bne.w loc_17FC8 */
  lift_charge_bcc(x, 0x17FB4, t);
  if (!t)
  {
    uint32_t v = lift_r16(x, 0xFFFFC328u);             /* move.w (C328).w,(C32C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Cu, v);
    lift_charge(x, 0x17FB8);
    alu_movew(c, 0);                                   /* move.w #0,(C32E).w */
    lift_w16(x, 0xFFFFC32Eu, 0);
    lift_charge(x, 0x17FBE);
    lift_charge(x, 0x17FC4);                           /* bra.w loc_17FDC */
    goto redeal;
  }
  alu_cmpw(c, 0x18, W(c->d[0]));                       /* loc_17FC8: cmp.w #$18,d0 */
  lift_charge(x, 0x17FC8);
  t = !c->zf;                                          /* bne.w loc_17FDC */
  lift_charge_bcc(x, 0x17FCC, t);
  if (!t)
  {
    uint32_t v = lift_r16(x, 0xFFFFC328u);             /* move.w (C328).w,(C32C).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Cu, v);
    lift_charge(x, 0x17FD0);
    v = lift_r16(x, 0xFFFFC32Au);                      /* move.w (C32A).w,(C32E).w */
    alu_movew(c, v); lift_w16(x, 0xFFFFC32Eu, v);
    lift_charge(x, 0x17FD6);
  }

redeal:                                                /* loc_17FDC */
  lift_call(x, 0x17FDC, 4, sub_18002);                 /* bsr.w sub_18002 */

epilogue:                                              /* loc_17FE0 */
  /* movem.l (sp)+,d0-d3/a0-a1 — reads back the entry values staged above */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x17FE0);

  lift_charge(x, 0x17FE4);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_17D80 (called from sub_17CA0/sub_17D16)
 *   Seeds the $FFFFCEF4 block: copies 4 longs (8 word-pairs) from the ROM
 *   table at $5576 + $FFFFCEE8*$10, then expands the bit stream at
 *   $FFFFCEF2 into 15 bytes at $FFFFCF04 — bit i of $CEF2 picks byte 0 or
 *   1 of pair i (the pairs just copied, read as bytes at $CEF4+2i).
 *   Then, unless Calc_HalvingAccumulator's d1 is negative, resets the
 *   first d1+1 structs at $FFFFCE66 (clearing +8/+$A/+$C and flag bit1 of
 *   +$E, and re-filling the +1/+3 bytes from $CEF4+2*d2 via
 *   Color_CopyBytesModeOrder's advancing cursor) and finishes with
 *   sub_17E42. d0-d4/a0-a3 saved/restored (movem). Exact role TBD;
 *   behaviour preserved bit-for-bit.
 */
void sub_17D80(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp0 = c->a[7];

  /* movem.l d0-d4/a0-a3,-(sp): 9 longs, ascending d0..d4,a0..a3 */
  lift_w32(x, sp0 - 36, c->d[0]);
  lift_w32(x, sp0 - 32, c->d[1]);
  lift_w32(x, sp0 - 28, c->d[2]);
  lift_w32(x, sp0 - 24, c->d[3]);
  lift_w32(x, sp0 - 20, c->d[4]);
  lift_w32(x, sp0 - 16, c->a[0]);
  lift_w32(x, sp0 - 12, c->a[1]);
  lift_w32(x, sp0 - 8,  c->a[2]);
  lift_w32(x, sp0 - 4,  c->a[3]);
  c->a[7] = sp0 - 36;
  lift_charge_movem(x, 0x17D80);

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFCEE8u))); /* move.w (abs),d1 */
  lift_charge(x, 0x17D84);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 4));          /* asl.w #4,d1 */
  lift_charge(x, 0x17D88);
  c->a[0] = 0xFFFFCEF4u;                               /* movea.w #$CEF4,a0 */
  lift_charge(x, 0x17D8A);
  c->a[1] = 0x5576;                                    /* movea.l #$5576,a1 */
  lift_charge(x, 0x17D8E);
  c->a[1] += SEW(c->d[1]);                             /* adda.w d1,a1 */
  lift_charge(x, 0x17D94);

  {
    uint32_t v = lift_r32(x, c->a[1]);                 /* move.l (a1),(a0) */
    alu_movel(c, v); lift_w32(x, c->a[0], v);
    lift_charge(x, 0x17D96);
    v = lift_r32(x, c->a[1] + 4);                      /* move.l 4(a1),4(a0) */
    alu_movel(c, v); lift_w32(x, c->a[0] + 4, v);
    lift_charge(x, 0x17D98);
    v = lift_r32(x, c->a[1] + 8);                      /* move.l 8(a1),8(a0) */
    alu_movel(c, v); lift_w32(x, c->a[0] + 8, v);
    lift_charge(x, 0x17D9E);
    v = lift_r32(x, c->a[1] + 0xC);                    /* move.l $C(a1),$C(a0) */
    alu_movel(c, v); lift_w32(x, c->a[0] + 0xC, v);
    lift_charge(x, 0x17DA4);
  }

  c->a[1] = c->a[0] + 0x10;                            /* lea $10(a0),a1 */
  lift_charge(x, 0x17DAA);
  c->d[2] = alu_moveql(c, 0xE);                        /* moveq #$E,d2 */
  lift_charge(x, 0x17DAE);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFCEF2u))); /* move.w (abs),d0 */
  lift_charge(x, 0x17DB0);

  for (;;)                                             /* loc_17DB4 */
  {
    setw(&c->d[1], alu_movew(c, W(c->d[0])));          /* move.w d0,d1 */
    lift_charge(x, 0x17DB4);
    setw(&c->d[1], alu_andw(c, 1, W(c->d[1])));        /* andi.w #1,d1 */
    lift_charge(x, 0x17DB6);
    {
      uint32_t b = lift_r8(x, c->a[0] + SEW(c->d[1])); /* move.b (a0,d1.w),(a1)+ */
      alu_moveb(c, b);
      lift_w8(x, c->a[1], b);
      c->a[1] += 1;
    }
    lift_charge(x, 0x17DBA);
    c->a[0] += 2;                                      /* addq.w #2,a0 */
    lift_charge(x, 0x17DBE);
    setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));        /* lsr.w #1,d0 */
    lift_charge(x, 0x17DC0);

    uint32_t nd2 = W(c->d[2] - 1);                     /* dbf d2,loc_17DB4 */
    setw(&c->d[2], nd2);
    int taken = (nd2 != 0xFFFF);
    lift_charge_dbcc(x, 0x17DC2, taken, !taken);
    if (!taken) break;
  }

  lift_call(x, 0x17DC6, 4, Calc_HalvingAccumulator);   /* bsr.w sub_1828A */
  alu_tstw(c, W(c->d[1]));                             /* tst.w d1 */
  lift_charge(x, 0x17DCA);
  int mi = c->nf;                                      /* bmi.w loc_17DFE */
  lift_charge_bcc(x, 0x17DCC, mi);
  if (!mi)
  {
    c->a[0] = 0xFFFFCEF4u;                             /* movea.w #$CEF4,a0 */
    lift_charge(x, 0x17DD0);
    c->a[0] += SEW(c->d[2]);                           /* adda.w d2,a0 */
    lift_charge(x, 0x17DD4);
    c->a[0] += SEW(c->d[2]);                           /* adda.w d2,a0 */
    lift_charge(x, 0x17DD6);
    c->a[1] = 0xFFFFCE66u;                             /* movea.w #$CE66,a1 */
    lift_charge(x, 0x17DD8);

    for (;;)                                           /* loc_17DDC */
    {
      alu_movew(c, 0); lift_w16(x, c->a[1] + 0xA, 0);  /* clr.w $A(a1) */
      lift_charge(x, 0x17DDC);
      alu_movew(c, 0); lift_w16(x, c->a[1] + 0xC, 0);  /* clr.w $C(a1) */
      lift_charge(x, 0x17DE0);
      alu_movew(c, 0); lift_w16(x, c->a[1] + 8, 0);    /* clr.w 8(a1) */
      lift_charge(x, 0x17DE4);
      lift_w8(x, c->a[1] + 0xE,                        /* bclr #1,$E(a1) */
              alu_bclr(c, lift_r8(x, c->a[1] + 0xE), 1));
      lift_charge(x, 0x17DE8);

      lift_call(x, 0x17DEE, 4, Color_CopyBytesModeOrder); /* bsr.w sub_17E04 */

      c->a[1] += 0x10;                                 /* adda.w #$10,a1 */
      lift_charge(x, 0x17DF2);

      uint32_t nd1 = W(c->d[1] - 1);                   /* dbf d1,loc_17DDC */
      setw(&c->d[1], nd1);
      int taken = (nd1 != 0xFFFF);
      lift_charge_dbcc(x, 0x17DF6, taken, !taken);
      if (!taken) break;
    }

    lift_call(x, 0x17DFA, 4, sub_17E42);               /* bsr.w sub_17E42 */
  }

  /* loc_17DFE: movem.l (sp)+,d0-d4/a0-a3 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[4] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[3] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0x17DFE);

  lift_charge(x, 0x17E02);                             /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Piece_CopyRecordToCache (sub_1721C; called from sub_1720C)
 *   in:  a0 = struct pointer (long at +$1E is a table base, +2(that
 *        table) another offset — same header shape as
 *        Piece_LookupFrameCount/Piece_AdvanceChain), d1 = byte offset
 *   Copies 8 longwords (32 bytes) from the computed source to a fixed
 *   cache at $FFFFBD68+d1. a2/a1/d0 clobbered.
 */
void Piece_CopyRecordToCache(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = lift_r32(x, c->a[0] + 0x1E);              /* move.l $1E(a0),a2 */
  lift_charge(x, 0x1721C);
  c->a[2] += SEW(lift_r16(x, c->a[2] + 2));            /* adda.w 2(a2),a2 */
  lift_charge(x, 0x17220);
  c->a[2] += SEW(c->d[1]);                              /* adda.w d1,a2 */
  lift_charge(x, 0x17224);
  c->a[1] = 0xFFFFBD68u;                                /* movea.w #$BD68,a1 */
  lift_charge(x, 0x17226);
  c->a[1] += SEW(c->d[1]);                              /* adda.w d1,a1 */
  lift_charge(x, 0x1722A);
  c->d[0] = alu_moveql(c, 7);                           /* moveq #7,d0 */
  lift_charge(x, 0x1722C);

  for (;;)
  {
    lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[2])));  /* move.l (a2)+,(a1)+ */
    c->a[2] += 4;
    c->a[1] += 4;
    lift_charge(x, 0x1722E);
    uint32_t nd0 = W(W(c->d[0]) - 1);                      /* dbf d0,loc_1722E */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x17230, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x17234);                                 /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Object_RingBufferWriteByte(lift_ctx *);  /* game.c */

/*
 * Object_QueueFrame18 (sub_E412; called from sub_DEEE/sub_E1F4 as a
 * sibling tail call — it discards its own return address on entry and
 * tail-jumps into Object_RingBufferWriteByte, whose rts returns to
 * THIS routine's caller's caller)
 *   in: a3 = on-ice object
 * Computes a camera-relative distance ($108 - |R_CAM_SPEED|, sign
 * chosen by $62(a3) bit7, /8), clamped to a max of $14, stores it to
 * $42(a3), then queues frame index $12 via Object_RingBufferWriteByte.
 */
void Object_QueueFrame18(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t a3 = c->a[3];

  c->a[7] += 4;                                          /* addq.w #4,sp: discard our own return addr */
  lift_charge(x, 0xE412);

  setw(&c->d[0], alu_movew(c, lift_r16(x, R_CAM_SPEED))); /* move.w (abs),d0 */
  lift_charge(x, 0xE414);
  alu_btst(c, lift_r8(x, a3 + 0x62), 7);                  /* btst #7,$62(a3) */
  lift_charge(x, 0xE418);
  int side = !c->zf;                                        /* bne.w loc_E424 */
  lift_charge_bcc(x, 0xE41E, side);
  if (!side)
  {
    setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
    lift_charge(x, 0xE422);
  }

  /* loc_E424 */
  setw(&c->d[1], alu_movew(c, 0x108));                      /* move.w #$108,d1 */
  lift_charge(x, 0xE424);
  setw(&c->d[1], alu_subw(c, W(c->d[0]), W(c->d[1])));      /* sub.w d0,d1 */
  lift_charge(x, 0xE428);
  setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 3));                /* lsr.w #3,d1 */
  lift_charge(x, 0xE42A);
  alu_cmpw(c, 0x14, W(c->d[1]));                              /* cmp.w #$14,d1 */
  lift_charge(x, 0xE42C);
  int lt = (c->nf != c->vf);                                   /* blt.w loc_E436 */
  lift_charge_bcc(x, 0xE430, lt);
  if (!lt)
  {
    c->d[1] = alu_moveql(c, 0x14);                            /* moveq #$14,d1 */
    lift_charge(x, 0xE434);
  }

  /* loc_E436 */
  lift_w16(x, a3 + 0x42, alu_movew(c, W(c->d[1])));            /* move.w d1,$42(a3) */
  lift_charge(x, 0xE436);
  c->d[0] = alu_movel(c, 0x12);                                /* move.l #$12,d0 */
  lift_charge(x, 0xE43A);
  lift_charge(x, 0xE440);                                       /* bra.w sub_10662 */

  Object_RingBufferWriteByte(x);                                /* tail jump */
}

/*
 * Piece_ResolveTripleChain (sub_FD5F4; called from ROM:1790E/17922)
 *   in:  a2 = struct pointer (same $1E-based header as
 *        Piece_LookupFrameCount/Piece_AdvanceChain)
 *   out: a1 = a2's chain base, advanced by its own +4 field once, then
 *        by the word at each new position three more times (four hops
 *        total). d0/a0/a2 saved/restored; a1 is the real output.
 */
void Piece_ResolveTripleChain(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_a0 = c->a[0], saved_a2 = c->a[2];

  /* movem.l d0/a0/a2,-(sp): a2 pushed first (high addr), a0, d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a2);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xFD5F4);

  c->a[1] = c->a[2];                                       /* movea.l a2,a1 */
  lift_charge(x, 0xFD5F8);
  c->a[1] = lift_r32(x, c->a[1] + 0x1E);                    /* movea.l $1E(a1),a1 */
  lift_charge(x, 0xFD5FA);
  c->a[1] += SEW(lift_r16(x, c->a[1] + 4));                  /* adda.w 4(a1),a1 */
  lift_charge(x, 0xFD5FE);
  c->a[1] += SEW(lift_r16(x, c->a[1]));                       /* adda.w (a1),a1 */
  lift_charge(x, 0xFD602);
  c->a[1] += SEW(lift_r16(x, c->a[1]));                       /* adda.w (a1),a1 */
  lift_charge(x, 0xFD604);
  c->a[1] += SEW(lift_r16(x, c->a[1]));                       /* adda.w (a1),a1 */
  lift_charge(x, 0xFD606);

  /* movem.l (sp)+,d0/a0/a2 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[2] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFD608);

  lift_charge(x, 0xFD60C);                                    /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Object_FindOccupiedZoneBackward (sub_B86A; called from sub_B0E8)
 *   in: d0 = starting object-slot index
 *   Scans backward from slot d0 through up to 6 slots (stride $80) for
 *   one whose $34 field is nonzero. If found, returns its camera zone
 *   ($52) in d0; if none found within the scan, returns d0 = -1. d1/a0
 *   saved/restored.
 */
void Object_FindOccupiedZoneBackward(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d1 = c->d[1], saved_a0 = c->a[0];

  /* movem.l d1/a0,-(sp): a0 pushed first (high addr), d1 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  lift_charge_movem(x, 0xB86A);

  c->a[0] = 0xFFFFB04Au;                                  /* movea.l #$FFFFB04A,a0 */
  lift_charge(x, 0xB86E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));               /* asl.w #7,d0 */
  lift_charge(x, 0xB874);
  c->a[0] += SEW(c->d[0]);                                    /* adda.w d0,a0 */
  lift_charge(x, 0xB876);
  setw(&c->d[1], alu_movew(c, 5));                             /* move.w #5,d1 */
  lift_charge(x, 0xB878);

  int found = 0;
  for (;;)
  {
    /* loc_B87C */
    alu_movew(c, lift_r16(x, c->a[0] + 0x34));                /* tst.w $34(a0) */
    lift_charge(x, 0xB87C);
    int zero = c->zf;                                           /* beq.w loc_B894 */
    lift_charge_bcc(x, 0xB880, zero);
    if (zero) { found = 1; break; }

    c->a[0] -= 0x80;                                            /* suba.w #$80,a0 */
    lift_charge(x, 0xB884);
    uint32_t nd1 = W(W(c->d[1]) - 1);                            /* dbf d1,loc_B87C */
    setw(&c->d[1], nd1);
    int taken = (nd1 != 0xFFFF);
    lift_charge_dbcc(x, 0xB888, taken, !taken);
    if (!taken) break;
  }

  if (!found)
  {
    setw(&c->d[0], alu_movew(c, 0xFFFF));                       /* move.w #$FFFF,d0 */
    lift_charge(x, 0xB88C);
    lift_charge(x, 0xB890);                                      /* bra.w loc_B898 */
  }
  else
  {
    /* loc_B894 */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 0x52)));   /* move.w $52(a0),d0 */
    lift_charge(x, 0xB894);
  }

  /* loc_B898: movem.l (sp)+,d1/a0 */
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xB898);

  lift_charge(x, 0xB89C);                                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define TBL_F86F2 0x000F86F2u  /* ROM: table of long addresses, indexed by d3*4 */

/*
 * Lookup_JumpTableEntry (sub_FD1F0; called from sub_FD084)
 *   in/out: d3 = table index (multiplied by 4 in place)
 *   out: a0 = the long value at TBL_F86F2[d3] (an address the caller
 *        presumably uses itself — this routine only fetches it)
 */
void Lookup_JumpTableEntry(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));            /* asl.w #2,d3 */
  lift_charge(x, 0xFD1F0);
  c->a[0] = TBL_F86F2;                                     /* movea.l #off_F86F2,a0 */
  lift_charge(x, 0xFD1F2);
  c->a[0] = lift_r32(x, c->a[0] + SW(c->d[3]));            /* movea.l (a0,d3.w),a0 */
  lift_charge(x, 0xFD1F8);

  lift_charge(x, 0xFD1FC);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Piece_CopyRecordToCacheBothTeams (sub_1720C; called from ROM:9D74/13774)
 * Runs Piece_CopyRecordToCache for the home team with d1=0, then
 * tail-falls into it again (not via bsr — its rts returns to THIS
 * routine's caller) for the away team with d1=$20.
 */
void Piece_CopyRecordToCacheBothTeams(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[1], alu_movew(c, 0));                        /* clr.w d1 */
  lift_charge(x, 0x1720C);
  c->a[0] = 0xFFFFC6CEu;                                    /* movea.w #$C6CE,a0 */
  lift_charge(x, 0x1720E);
  lift_call(x, 0x17212, 4, Piece_CopyRecordToCache);        /* bsr.w sub_1721C */
  if (x->declined) return;
  c->d[1] = alu_moveql(c, 0x20);                             /* moveq #$20,d1 */
  lift_charge(x, 0x17216);
  c->a[0] += 0x364;                                           /* adda.w #$364,a0 */
  lift_charge(x, 0x17218);

  Piece_CopyRecordToCache(x);                                 /* fall-through tail */
}

#define TBL_5605A 0x0005605Au   /* ROM: long self-relative offset to a 16-long block */

/*
 * Copy_5605ABlockToBD28 (sub_16C96; called from ROM:9D6E/16AC6)
 * Computes a0 = TBL_5605A + *(long at TBL_5605A) (a self-relative
 * offset), then copies 16 longwords (64 bytes) from there to
 * $FFFFBD28. d0/a0/a1 clobbered (not saved/restored).
 */
void Copy_5605ABlockToBD28(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = TBL_5605A;                                    /* movea.l #off_5605A,a0 */
  lift_charge(x, 0x16C96);
  c->a[0] += lift_r32(x, c->a[0]);                          /* adda.l (a0),a0 */
  lift_charge(x, 0x16C9C);
  c->d[0] = alu_moveql(c, 0xF);                              /* moveq #$F,d0 */
  lift_charge(x, 0x16C9E);
  c->a[1] = 0xFFFFBD28u;                                     /* movea.w #$BD28,a1 */
  lift_charge(x, 0x16CA0);

  for (;;)
  {
    lift_w32(x, c->a[1], alu_movel(c, lift_r32(x, c->a[0])));  /* move.l (a0)+,(a1)+ */
    c->a[0] += 4;
    c->a[1] += 4;
    lift_charge(x, 0x16CA4);
    uint32_t nd0 = W(W(c->d[0]) - 1);                          /* dbf d0,loc_16CA4 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0x16CA6, taken, !taken);
    if (!taken) break;
  }

  lift_charge(x, 0x16CAA);                                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_ROWQ_DIRTY 0xFFFFC2EEu  /* bit 1: the $FFC3F6 row block needs re-queueing */
#define R_ROWQ_BASE  0xFFFFC3F6u  /* 8 records, $E bytes apart, queued as 7-word tile uploads */
#define R_ROWQ_VRAM  0xFFFFB004u  /* +0: VRAM base slot; +2: per-row slot-stride shift */

/*
 * Render_QueueRowTilesC3F6 (sub_15FF0; called from the per-frame render
 * dispatcher sub_15EC0 — re-lifted 2026-07-31 after the HBlank
 * interrupted-check fix; the lift itself was always correct)
 *   in:  a5 = tile-DMA queue write pointer
 *   out: a5 advanced by 8 entries (64 bytes); a0 = $FFFFC3F6 + $70,
 *        a1 = $FFFFB004, d0/d1/d2 left at their final loop values
 *        (no movem, nothing restored)
 * Consumes the dirty flag (bclr bit 1 of $FFC2EE; bails via the shared
 * far rts when it was already clear), then queues 8 tile-DMA entries
 * {src = $FFC3F6 + row*$E, 7 words, VRAM slot}: the slot starts at
 * ([B004] + stride-shifted base, +2/+11 rink-flip adjustments, all
 * doubled) and advances by (2 << [B006]) per row.
 */
void Render_QueueRowTilesC3F6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t b = lift_r8(x, R_ROWQ_DIRTY);       /* bclr #1,($FFFFC2EE).w */
    lift_w8(x, R_ROWQ_DIRTY, alu_bclr(c, b, 1));
    lift_charge(x, 0x15FF0);
  }
  if (c->zf)                                     /* beq.w -> shared far rts */
  {
    lift_charge_bcc(x, 0x15FF6, 1);
    lift_charge(x, 0x15464);                      /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_charge_bcc(x, 0x15FF6, 0);
  c->a[0] = R_ROWQ_BASE;                         /* movea.w #$C3F6,a0 (sign-ext) */
  lift_charge(x, 0x15FFA);
  c->a[1] = R_ROWQ_VRAM;                         /* movea.w #$B004,a1 (sign-ext) */
  lift_charge(x, 0x15FFE);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1] + 2)));  /* move.w 2(a1),d2 */
  lift_charge(x, 0x16002);
  c->d[0] = alu_moveql(c, 2);                    /* moveq #2,d0 */
  lift_charge(x, 0x16006);
  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);       /* btst #7,($FFFFC2EC).w */
  lift_charge(x, 0x16008);
  lift_charge_bcc(x, 0x1600E, c->zf);            /* beq.w loc_16014 */
  if (!c->zf)
  {
    c->d[0] = alu_moveql(c, 0xF);                /* moveq #$F,d0 */
    lift_charge(x, 0x16012);
  }
  {
    int cnt = (int)(W(c->d[2]) & 63);            /* asl.w d2,d0 */
    if (cnt) setw(&c->d[0], alu_aslw(c, W(c->d[0]), cnt));
    else { alu_movew(c, W(c->d[0])); c->cf = 0; } /* count 0: NZ, V=C=0, X kept */
    lift_charge_shift_reg(x, 0x16014, cnt);
    c->d[1] = alu_moveql(c, 2);                  /* moveq #2,d1 */
    lift_charge(x, 0x16016);
    if (cnt) setw(&c->d[1], alu_aslw(c, W(c->d[1]), cnt));  /* asl.w d2,d1 */
    else { alu_movew(c, W(c->d[1])); c->cf = 0; }
    lift_charge_shift_reg(x, 0x16018, cnt);
  }
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));    /* addq.w #2,d0 */
  lift_charge(x, 0x1601A);
  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);       /* btst #7,($FFFFC2EC).w */
  lift_charge(x, 0x1601C);
  lift_charge_bcc(x, 0x16022, c->zf);            /* beq.w loc_1602A */
  if (!c->zf)
  {
    setw(&c->d[0], alu_addw(c, 0xB, W(c->d[0])));  /* add.w #$B,d0 */
    lift_charge(x, 0x16026);
  }
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));    /* asl.w #1,d0 */
  lift_charge(x, 0x1602A);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[1]), W(c->d[0])));  /* add.w (a1),d0 */
  lift_charge(x, 0x1602C);
  c->d[2] = alu_moveql(c, 7);                    /* moveq #7,d2 */
  lift_charge(x, 0x1602E);
  for (;;)
  {
    lift_w32(x, c->a[5], alu_movel(c, c->a[0])); /* move.l a0,(a5)+ */
    c->a[5] += 4;
    lift_charge(x, 0x16030);
    lift_w16(x, c->a[5], alu_movew(c, 7));       /* move.w #7,(a5)+ */
    c->a[5] += 2;
    lift_charge(x, 0x16032);
    lift_w16(x, c->a[5], alu_movew(c, W(c->d[0])));  /* move.w d0,(a5)+ */
    c->a[5] += 2;
    lift_charge(x, 0x16036);
    c->a[0] += 0xE;                              /* adda.w #$E,a0: no flags */
    lift_charge(x, 0x16038);
    setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));  /* add.w d1,d0 */
    lift_charge(x, 0x1603C);
    setw(&c->d[2], W(c->d[2]) - 1);              /* dbf d2: no CCR */
    if (W(c->d[2]) != 0xFFFF) { lift_charge_dbcc(x, 0x1603E, 1, 0); continue; }
    lift_charge_dbcc(x, 0x1603E, 0, 1);
    break;
  }
  lift_charge(x, 0x16042);                       /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

#define R_RUNQ_GATE   0xFFFFC2F4u  /* bit 4 suppresses this draw entirely */
#define R_RUNQ_NIBBLES 0xFFFFC3EAu /* two packed run lengths: low nibble frame
                                    * base $1A, high nibble frame base $1D */
#define RUNQ_DEFS_HDR 0x0A4B54u    /* ROM: piece-definition header (+4: long
                                    * self-relative offset), same shape as
                                    * IND_DEFS_HDR */

/*
 * Render_DrawClippedRunGroups (sub_161D0; called from the per-frame
 * render dispatcher sub_15EC0 — re-lifted 2026-07-31 after the HBlank
 * interrupted-check fix; the lift itself was always correct)
 *   in:  d6 = sprites used so far, a6 = hw sprite list write pointer
 *   out: everything Sprite_EmitClippedRun/Pieces leave behind
 * Bails (bare rts $162FC) when $FFC2F4 bit 4 is set. Otherwise resolves
 * the RUNQ_DEFS_HDR piece table, loads the clip-window centre from the
 * camera ($FFBD1C/$FFBD18 — or fixed -$40/$100 when the rink is drawn
 * flipped), then emits: the low-nibble run of $FFC3EA at frame base $1A,
 * the high-nibble run at base $1D (both via Sprite_EmitClippedRun), and
 * two single pieces indexed by the bytes at $FFB89A/$FFB89B (via
 * Sprite_EmitClippedPieces — the second as a bra tail whose rts returns
 * to this routine's caller).
 */
void Render_DrawClippedRunGroups(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, R_RUNQ_GATE), 4);       /* btst #4,($FFFFC2F4).w */
  lift_charge(x, 0x161D0);
  if (!c->zf)                                    /* bne.w -> bare rts $162FC */
  {
    lift_charge_bcc(x, 0x161D6, 1);
    lift_charge(x, 0x162FC);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }
  lift_charge_bcc(x, 0x161D6, 0);
  c->a[1] = RUNQ_DEFS_HDR;                       /* movea.l #off_A4B54,a1 */
  lift_charge(x, 0x161DA);
  c->a[1] += lift_r32(x, c->a[1] + 4);           /* adda.l 4(a1),a1: no flags */
  lift_charge(x, 0x161E0);
  setw(&c->d[4], alu_movew(c, lift_r16(x, R_CAMERA_X)));  /* move.w (BD1C).w,d4 */
  lift_charge(x, 0x161E4);
  setw(&c->d[5], alu_movew(c, lift_r16(x, R_CAMERA_Y)));  /* move.w (BD18).w,d5 */
  lift_charge(x, 0x161E8);
  alu_btst(c, lift_r8(x, R_RINK_FLIP), 7);       /* btst #7,($FFFFC2EC).w */
  lift_charge(x, 0x161EC);
  lift_charge_bcc(x, 0x161F2, c->zf);            /* beq.w loc_161FE */
  if (!c->zf)
  {
    c->d[4] = alu_moveql(c, -0x40);              /* moveq #-$40,d4 */
    lift_charge(x, 0x161F6);
    c->d[5] = alu_movel(c, 0x100);               /* move.l #$100,d5 */
    lift_charge(x, 0x161F8);
  }
  setw(&c->d[0], alu_movew(c, 0));               /* clr.w d0 */
  lift_charge(x, 0x161FE);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, R_RUNQ_NIBBLES)));  /* move.b (C3EA).w,d2 */
  lift_charge(x, 0x16200);
  c->d[3] = alu_moveql(c, 0x1A);                 /* moveq #$1A,d3 */
  lift_charge(x, 0x16204);
  lift_call(x, 0x16206, 4, Sprite_EmitClippedRun);        /* bsr.w */
  setb(&c->d[2], alu_moveb(c, lift_r8(x, R_RUNQ_NIBBLES)));
  lift_charge(x, 0x1620A);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 4));    /* lsr.w #4,d2 */
  lift_charge(x, 0x1620E);
  c->d[3] = alu_moveql(c, 0x1D);                 /* moveq #$1D,d3 */
  lift_charge(x, 0x16210);
  lift_call(x, 0x16212, 4, Sprite_EmitClippedRun);        /* bsr.w */
  setb(&c->d[0], alu_moveb(c, lift_r8(x, 0xFFFFB89Au)));  /* move.b (B89A).w,d0 */
  lift_charge(x, 0x16216);
  lift_call(x, 0x1621A, 4, Sprite_EmitClippedPieces);     /* bsr.w */
  setb(&c->d[0], alu_moveb(c, lift_r8(x, 0xFFFFB89Bu)));  /* move.b (B89B).w,d0 */
  lift_charge(x, 0x1621E);
  lift_charge(x, 0x16222);      /* bra.w sub_16246 — tail, rts to our caller */
  Sprite_EmitClippedPieces(x);
}

#define R_CAM_ZONE_A 0x05605Au   /* ROM: per-camera-zone piece table (normal rink) */
#define R_CAM_ZONE_B 0x0B5180u   /* ROM: per-camera-zone piece table (rink flipped, C2F4 bit4) */

/*
 * Render_QueueCameraZonePieces (sub_15F34; called from the per-frame
 * render dispatcher sub_15EC0)
 *   in:  a5 = render-descriptor write pointer (see Render_QueueRowTilesC3F6
 *        for the sibling a5-cursor convention)
 *   out: a5 advanced by 8 bytes per zone crossed; d0/d1/d3/d4/a0/a1
 *        clobbered; camera-scroll shadow words at $FFFFBD1E/BD20 and the
 *        wrapped row counter at $FFFFBD1A updated
 *
 * Bails via the shared far rts at $15464 when the rink-flip flag
 * ($FFFFC2EC bit7) is set. Computes a scroll-derived start row (d1, from
 * camera Y $FFFFBD18 shifted >>3) and an end row (d4, camera X-derived via
 * $FFFFBD1C), storing scroll shadow copies to $FFFFBD1E/BD20 (the latter
 * negated) along the way; bails again if start==end. Selects one of two
 * ROM per-zone tables (R_CAM_ZONE_A, or R_CAM_ZONE_B when $FFFFC2F4 bit4
 * is set) and offsets it by the table's own header long at +4 (ROM data,
 * not RAM-derived — statically safe). The direction check (blt) reuses
 * the flags from the earlier start==end comparison, since the intervening
 * table-select btst only touches Z — matches real 68k behaviour. Then
 * walks up to 32 rows (dbeq d3, capped) from start toward end, writing an
 * 8-byte descriptor per row: +0 a long piece pointer (table base + 4 +
 * 2*mulu(rowIndex, tableWord)), +4 a copy of the table's first word, +6 a
 * VRAM-row-relative Y computed from a wrapped row index * $80 + $FFFFB008.
 */
void Render_QueueCameraZonePieces(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 7);                  /* btst #7,($C2EC).w */
  lift_charge(x, 0x15F34);
  lift_charge_bcc(x, 0x15F3A, !c->zf);                       /* bne.w locret_15464 */
  if (!c->zf)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->d[0] = alu_moveql(c, (int32_t)0xFFFFFFC0);              /* moveq #$FFFFFFC0,d0 */
  lift_charge(x, 0x15F3E);
  setw(&c->d[0], alu_subw(c, lift_r16(x, 0xFFFFBD1Cu), W(c->d[0])));  /* sub.w ($BD1C).w,d0 */
  lift_charge(x, 0x15F40);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFBD18u)));    /* move.w ($BD18).w,d1 */
  lift_charge(x, 0x15F44);
  lift_w16(x, 0xFFFFBD1Eu, alu_movew(c, W(c->d[0])));        /* move.w d0,($BD1E).w */
  lift_charge(x, 0x15F48);
  lift_w16(x, 0xFFFFBD20u, alu_movew(c, W(c->d[1])));        /* move.w d1,($BD20).w */
  lift_charge(x, 0x15F4C);
  lift_w16(x, 0xFFFFBD20u, alu_negw(c, lift_r16(x, 0xFFFFBD20u)));  /* neg.w ($BD20).w */
  lift_charge(x, 0x15F50);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFBD1Au)));    /* move.w ($BD1A).w,d4 */
  lift_charge(x, 0x15F54);
  setw(&c->d[1], alu_asrw(c, W(c->d[1]), 3));                /* asr.w #3,d1 */
  lift_charge(x, 0x15F58);
  lift_w16(x, 0xFFFFBD1Au, alu_movew(c, W(c->d[1])));        /* move.w d1,($BD1A).w */
  lift_charge(x, 0x15F5A);
  c->d[3] = alu_moveql(c, 0x1F);                              /* moveq #$1F,d3 */
  lift_charge(x, 0x15F5E);
  alu_cmpw(c, W(c->d[4]), W(c->d[1]));                        /* cmp.w d4,d1 */
  lift_charge(x, 0x15F60);
  lift_charge_bcc(x, 0x15F62, c->zf);                         /* beq.w locret_15464 */
  if (c->zf)
  {
    lift_charge(x, 0x15464);
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  c->a[0] = R_CAM_ZONE_A;                                     /* move.l #off_5605A,a0 */
  lift_charge(x, 0x15F66);
  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);                    /* btst #4,($C2F4).w */
  lift_charge(x, 0x15F6C);
  lift_charge_bcc(x, 0x15F72, c->zf);                         /* beq.w loc_15F7C */
  if (!c->zf)
  {
    c->a[0] = R_CAM_ZONE_B;                                   /* move.l #off_B5180,a0 */
    lift_charge(x, 0x15F76);
  }

  /* loc_15F7C */
  c->a[0] += lift_r32(x, c->a[0] + 4);                        /* adda.l 4(a0),a0: no flags */
  lift_charge(x, 0x15F7C);
  int lt = (c->nf != c->vf);        /* blt.w loc_15FBA — stale flags from the cmp d4,d1 above;
                                      * the table-select btst only touched Z, matching real hw */
  lift_charge_bcc(x, 0x15F80, lt);

  for (;;)
  {
    uint32_t base;
    if (!lt)
    {
      /* loc_15F84 */
      setw(&c->d[0], alu_movew(c, W(c->d[1])));               /* move.w d1,d0 */
      lift_charge(x, 0x15F84);
      setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
      lift_charge(x, 0x15F86);
      setw(&c->d[0], alu_addw(c, 0x1E, W(c->d[0])));          /* add.w #$1E,d0 */
      lift_charge(x, 0x15F88);
      base = 0x1E;
    }
    else
    {
      /* loc_15FBA */
      setw(&c->d[0], alu_movew(c, W(c->d[1])));               /* move.w d1,d0 */
      lift_charge(x, 0x15FBA);
      setw(&c->d[0], alu_negw(c, W(c->d[0])));                /* neg.w d0 */
      lift_charge(x, 0x15FBC);
      setw(&c->d[0], alu_addw(c, 0x1D, W(c->d[0])));          /* add.w #$1D,d0 */
      lift_charge(x, 0x15FBE);
      base = 0x3D;
    }
    setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));            /* and.w #$1F,d0 */
    lift_charge(x, !lt ? 0x15F8C : 0x15FC2);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 7));                /* asl.w #7,d0 */
    lift_charge(x, !lt ? 0x15F90 : 0x15FC6);
    setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFFFB008u), W(c->d[0])));  /* add.w ($B008).w,d0 */
    lift_charge(x, !lt ? 0x15F92 : 0x15FC8);
    lift_w16(x, c->a[5] + 6, alu_movew(c, W(c->d[0])));        /* move.w d0,6(a5) */
    lift_charge(x, !lt ? 0x15F96 : 0x15FCC);

    setw(&c->d[0], alu_movew(c, base));                        /* move.w #$1E/$3D,d0 */
    lift_charge(x, !lt ? 0x15F9A : 0x15FD0);
    setw(&c->d[0], alu_subw(c, W(c->d[1]), W(c->d[0])));       /* sub.w d1,d0 */
    lift_charge(x, !lt ? 0x15F9E : 0x15FD4);
    lift_w16(x, c->a[5] + 4, alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0),4(a5) */
    lift_charge(x, !lt ? 0x15FA0 : 0x15FD6);
    c->d[0] = alu_mulu(c, lift_r16(x, c->a[0]), W(c->d[0]));   /* mulu.w (a0),d0 */
    lift_charge_mulu(x, !lt ? 0x15FA4 : 0x15FDA, lift_r16(x, c->a[0]));
    setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));       /* add.w d0,d0 */
    lift_charge(x, !lt ? 0x15FA6 : 0x15FDC);
    c->a[1] = c->a[0] + 4 + SW(c->d[0]);                       /* lea 4(a0,d0.w),a1: no flags */
    lift_charge(x, !lt ? 0x15FA8 : 0x15FDE);
    lift_w32(x, c->a[5], alu_movel(c, c->a[1]));               /* move.l a1,(a5) */
    lift_charge(x, !lt ? 0x15FAC : 0x15FE2);
    c->a[5] += 8;                                              /* addq.w #8,a5: no flags */
    lift_charge(x, !lt ? 0x15FAE : 0x15FE4);

    if (!lt)
    {
      setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));              /* subq.w #1,d1 */
      lift_charge(x, 0x15FB0);
    }
    else
    {
      setw(&c->d[1], alu_addw(c, 1, W(c->d[1])));              /* addq.w #1,d1 */
      lift_charge(x, 0x15FE6);
    }
    alu_cmpw(c, W(c->d[4]), W(c->d[1]));                       /* cmp.w d4,d1 */
    lift_charge(x, !lt ? 0x15FB2 : 0x15FE8);

    if (c->zf)                                                 /* dbeq d3,loc_15F84/loc_15FBA:
                                                                 * condition true -> exit, counter
                                                                 * untouched */
    {
      lift_charge_dbcc(x, !lt ? 0x15FB4 : 0x15FEA, 0, 0);
      break;
    }
    {
      uint32_t nd3 = W(W(c->d[3]) - 1);
      setw(&c->d[3], nd3);
      int expired = (nd3 == 0xFFFF);
      lift_charge_dbcc(x, !lt ? 0x15FB4 : 0x15FEA, !expired, expired);
      if (expired) break;
    }
  }

  lift_charge(x, !lt ? 0x15FB8 : 0x15FEE);                     /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void sub_1826C(lift_ctx *);  /* math.c */

/*
 * sub_1803E (called from sub_17CA0+8, sub_180FC+5C and others; the
 * $FFFFCE66-subsystem seeder alongside sub_17D80/sub_18002)
 *   in: a3 = base of a 5-word multi-word value (saved to the stack and
 *       restored at the end — sub_1826C divides it in place)
 * Extracts digits of the big value by repeated multi-word division
 * (sub_1826C, remainder returned in d0): for each of the 8 $10-byte
 * structs at $FFFFCED6 descending, two mod-5 digits into +6 and +4
 * (a digit of 4 sets the struct's $E bit2, whose bit was cleared
 * first); then mod $4000 → $FFFFCEF2, mod 8 → $FFFFCEF0 (values 1/2
 * remapped to 4 unless $FFFFD046 is set), mod $10 → $FFFFCEEE, mod 4
 * → $FFFFCEEC, mod 8 → $FFFFCEEA, mod $20 → $FFFFCEE8. Finally the
 * 5 words at a3 are restored from the stack copy. d0/d2 = -1, a0 =
 * a3+10, a1 = $FFFFCE56 on exit (no register save).
 */
void sub_1803E(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->d[0] = alu_moveql(c, 4);                      /* moveq #4,d0 */
  lift_charge(x, 0x1803E);
  c->a[0] = c->a[3] + 0xA;                         /* lea $A(a3),a0 */
  lift_charge(x, 0x18040);
  for (;;)
  {
    /* loc_18044: move.w -(a0),-(sp) */
    c->a[0] -= 2;
    {
      uint32_t v = alu_movew(c, lift_r16(x, c->a[0]));
      c->a[7] -= 2;
      lift_w16(x, c->a[7], v);
    }
    lift_charge(x, 0x18044);
    int expired = (W(c->d[0]) == 0);               /* dbf d0,loc_18044 */
    setw(&c->d[0], W(c->d[0] - 1));
    lift_charge_dbcc(x, 0x18046, !expired, expired);
    if (expired) break;
  }
  c->d[2] = alu_moveql(c, 7);                      /* moveq #7,d2 */
  lift_charge(x, 0x1804A);
  c->a[1] = 0xFFFFCED6;                            /* movea.w #$CED6,a1 */
  lift_charge(x, 0x1804C);
  for (;;)
  {
    /* loc_18050: bclr #2,$E(a1) */
    {
      uint32_t v = alu_bclr(c, lift_r8(x, c->a[1] + 0xE), 2);
      lift_w8(x, c->a[1] + 0xE, v);
    }
    lift_charge(x, 0x18050);
    c->d[0] = alu_moveql(c, 5);                    /* moveq #5,d0 */
    lift_charge(x, 0x18056);
    lift_call(x, 0x18058, 4, sub_1826C);           /* bsr.w sub_1826C */
    if (x->declined) return;
    lift_w16(x, c->a[1] + 6, alu_movew(c, W(c->d[0])));  /* move.w d0,6(a1) */
    lift_charge(x, 0x1805C);
    alu_cmpw(c, 4, W(c->d[0]));                    /* cmp.w #4,d0 */
    lift_charge(x, 0x18060);
    t = !c->zf;
    lift_charge_bcc(x, 0x18064, t);                /* bne.w loc_1806E */
    if (!t)
    {
      uint32_t v = alu_bset(c, lift_r8(x, c->a[1] + 0xE), 2);  /* bset #2,$E(a1) */
      lift_w8(x, c->a[1] + 0xE, v);
      lift_charge(x, 0x18068);
    }
    /* loc_1806E */
    c->d[0] = alu_moveql(c, 5);                    /* moveq #5,d0 */
    lift_charge(x, 0x1806E);
    lift_call(x, 0x18070, 4, sub_1826C);           /* bsr.w sub_1826C */
    if (x->declined) return;
    lift_w16(x, c->a[1] + 4, alu_movew(c, W(c->d[0])));  /* move.w d0,4(a1) */
    lift_charge(x, 0x18074);
    alu_cmpw(c, 4, W(c->d[0]));                    /* cmp.w #4,d0 */
    lift_charge(x, 0x18078);
    t = !c->zf;
    lift_charge_bcc(x, 0x1807C, t);                /* bne.w loc_18086 */
    if (!t)
    {
      uint32_t v = alu_bset(c, lift_r8(x, c->a[1] + 0xE), 2);  /* bset #2,$E(a1) */
      lift_w8(x, c->a[1] + 0xE, v);
      lift_charge(x, 0x18080);
    }
    /* loc_18086 */
    c->a[1] -= 0x10;                               /* suba.w #$10,a1 */
    lift_charge(x, 0x18086);
    int expired = (W(c->d[2]) == 0);               /* dbf d2,loc_18050 */
    setw(&c->d[2], W(c->d[2] - 1));
    lift_charge_dbcc(x, 0x1808A, !expired, expired);
    if (expired) break;
  }
  setw(&c->d[0], alu_movew(c, 0x4000));            /* move.w #$4000,d0 */
  lift_charge(x, 0x1808E);
  lift_call(x, 0x18092, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEF2, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEF2).w */
  lift_charge(x, 0x18096);
  setw(&c->d[0], alu_movew(c, 8));                 /* move.w #8,d0 */
  lift_charge(x, 0x1809A);
  lift_call(x, 0x1809E, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEF0, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEF0).w */
  lift_charge(x, 0x180A2);
  alu_tstw(c, lift_r16(x, 0xFFFFD046));            /* tst.w (D046).w */
  lift_charge(x, 0x180A6);
  t = !c->zf;
  lift_charge_bcc(x, 0x180AA, t);                  /* bne.w loc_180C8 */
  if (!t)
  {
    alu_cmpw(c, 1, lift_r16(x, 0xFFFFCEF0));       /* cmp.w #1,(CEF0).w */
    lift_charge(x, 0x180AE);
    t = c->zf;
    lift_charge_bcc(x, 0x180B4, t);                /* beq.w loc_180C2 */
    int remap = t;
    if (!remap)
    {
      alu_cmpw(c, 2, lift_r16(x, 0xFFFFCEF0));     /* cmp.w #2,(CEF0).w */
      lift_charge(x, 0x180B8);
      remap = c->zf;
      lift_charge_bcc(x, 0x180BE, !remap);         /* bne.w loc_180C8 */
    }
    if (remap)
    {
      lift_w16(x, 0xFFFFCEF0, alu_movew(c, 4));    /* loc_180C2: move.w #4,(CEF0).w */
      lift_charge(x, 0x180C2);
    }
  }
  /* loc_180C8 */
  c->d[0] = alu_moveql(c, 0x10);                   /* moveq #$10,d0 */
  lift_charge(x, 0x180C8);
  lift_call(x, 0x180CA, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEEE, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEEE).w */
  lift_charge(x, 0x180CE);
  c->d[0] = alu_moveql(c, 4);                      /* moveq #4,d0 */
  lift_charge(x, 0x180D2);
  lift_call(x, 0x180D4, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEEC, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEEC).w */
  lift_charge(x, 0x180D8);
  c->d[0] = alu_moveql(c, 8);                      /* moveq #8,d0 */
  lift_charge(x, 0x180DC);
  lift_call(x, 0x180DE, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEEA, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEEA).w */
  lift_charge(x, 0x180E2);
  c->d[0] = alu_moveql(c, 0x20);                   /* moveq #$20,d0 */
  lift_charge(x, 0x180E6);
  lift_call(x, 0x180E8, 4, sub_1826C);
  if (x->declined) return;
  lift_w16(x, 0xFFFFCEE8, alu_movew(c, W(c->d[0])));  /* move.w d0,(CEE8).w */
  lift_charge(x, 0x180EC);
  c->d[0] = alu_moveql(c, 4);                      /* moveq #4,d0 */
  lift_charge(x, 0x180F0);
  c->a[0] = c->a[3];                               /* lea (a3),a0 */
  lift_charge(x, 0x180F2);
  for (;;)
  {
    /* loc_180F4: move.w (sp)+,(a0)+ */
    {
      uint32_t v = alu_movew(c, lift_r16(x, c->a[7]));
      c->a[7] += 2;
      lift_w16(x, c->a[0], v);
      c->a[0] += 2;
    }
    lift_charge(x, 0x180F4);
    int expired = (W(c->d[0]) == 0);               /* dbf d0,loc_180F4 */
    setw(&c->d[0], W(c->d[0] - 1));
    lift_charge_dbcc(x, 0x180F6, !expired, expired);
    if (expired) break;
  }
  lift_charge(x, 0x180FA);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FD78A (called from sub_15EC0's render dispatch)
 *   in: d6 = hw sprites used so far, a6 = hw sprite list write ptr
 *   Emit the pieces of one streamed frame ($FFFFD6AE selects it) of the
 *   $FFFFD6BA piece table as raw hw sprite entries, clipped to a window
 *   around the camera ($FFFFBD1C/$BD18). Bails via the rts at $FD898
 *   when $FFFFD6B4 is negative, the table pointer is null, any of the
 *   mode gates $FFFFC2F4.4/$C2EE.3/$C2EC.7 are set, the frame index
 *   ext.w's to 0, or the 64-sprite cap is already met. Otherwise walks
 *   the frame's 8-byte pieces ([x.w y.w tile.w attr.b size.b], count =
 *   span/8 from the header pair): each in-window piece emits {Y+$70-y0,
 *   size, link=d6, (attr&$F800)+tile+($FFFFD6AC), X+$70-x0} and bumps
 *   d6, stopping at 64 sprites. d0-d5 are movem-saved around the walk
 *   (restored at exit); a0 ends at the last piece walked, a1 at the
 *   frame table base — both genuine (if meaningless) outputs.
 *   The dbf-loop trailer at $FD7D0-$FD7EE inside this routine's IDA
 *   boundary (and its bsr to loc_FD7F0) is DEAD code: no label, no
 *   xref, zero profile weight — the entry path bra's straight to
 *   loc_FD7F0 and never reaches it.
 */
void sub_FD78A(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, i, iter;
  uint32_t idx;

  alu_tstw(c, lift_r16(x, 0xFFFFD6B4u));           /* tst.w ($FFFFD6B4).w */
  lift_charge(x, 0xFD78A);
  t = c->nf;                                       /* bmi.w locret_FD898 */
  lift_charge_bcc(x, 0xFD78E, t);
  if (t) goto bail;
  alu_tstl(c, lift_r32(x, 0xFFFFD6BAu));           /* tst.l ($FFFFD6BA).w */
  lift_charge(x, 0xFD792);
  t = c->zf;                                       /* beq.w locret_FD898 */
  lift_charge_bcc(x, 0xFD796, t);
  if (t) goto bail;
  alu_btst(c, lift_r8(x, 0xFFFFC2F4u), 4);         /* btst #4,($FFFFC2F4).w */
  lift_charge(x, 0xFD79A);
  t = !c->zf;                                      /* bne.w locret_FD898 */
  lift_charge_bcc(x, 0xFD7A0, t);
  if (t) goto bail;
  alu_btst(c, lift_r8(x, 0xFFFFC2EEu), 3);         /* btst #3,($FFFFC2EE).w */
  lift_charge(x, 0xFD7A4);
  t = !c->zf;                                      /* bne.w locret_FD898 */
  lift_charge_bcc(x, 0xFD7AA, t);
  if (t) goto bail;
  alu_btst(c, lift_r8(x, 0xFFFFC2ECu), 7);         /* btst #7,($FFFFC2EC).w */
  lift_charge(x, 0xFD7AE);
  t = !c->zf;                                      /* bne.w locret_FD898 */
  lift_charge_bcc(x, 0xFD7B4, t);
  if (t) goto bail;

  c->a[1] = lift_r32(x, 0xFFFFD6BAu);              /* movea.l ($FFFFD6BA).w,a1 */
  lift_charge(x, 0xFD7B8);
  c->a[1] += lift_r32(x, (c->a[1] + 4) & 0xFFFFFF);/* adda.l 4(a1),a1 */
  lift_charge(x, 0xFD7BC);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFFFBD1Cu))); /* move.w ($BD1C).w,d4 */
  lift_charge(x, 0xFD7C0);
  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFFFBD18u))); /* move.w ($BD18).w,d5 */
  lift_charge(x, 0xFD7C4);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD6AEu))); /* move.w ($D6AE).w,d0 */
  lift_charge(x, 0xFD7C8);
  lift_charge(x, 0xFD7CC);                         /* bra.w loc_FD7F0 */

  /* loc_FD7F0 */
  setw(&c->d[0], alu_extw(c, c->d[0]));            /* ext.w d0 */
  lift_charge(x, 0xFD7F0);
  t = c->zf;                                       /* beq.w locret_FD898 */
  lift_charge_bcc(x, 0xFD7F2, t);
  if (t) goto bail;
  alu_cmpw(c, 0x40, W(c->d[6]));                   /* cmp.w #$40,d6 */
  lift_charge(x, 0xFD7F6);
  t = (c->nf == c->vf);                            /* bge.w locret_FD898 */
  lift_charge_bcc(x, 0xFD7FA, t);
  if (t) goto bail;

  /* movem.l d0-d5,-(sp) */
  c->a[7] -= 24;
  for (i = 0; i < 6; i++) lift_w32(x, c->a[7] + 4u * i, c->d[i]);
  lift_charge_movem(x, 0xFD7FE);

  setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));    /* add.w d0,d0 */
  lift_charge(x, 0xFD802);
  c->a[0] = c->a[1];                               /* movea.l a1,a0 */
  lift_charge(x, 0xFD804);
  idx = (c->a[0] + SW(c->d[0])) & 0xFFFFFF;
  setw(&c->d[1], alu_movew(c, lift_r16(x, idx + 2)));     /* move.w 2(a0,d0.w),d1 */
  lift_charge(x, 0xFD806);
  setw(&c->d[1], alu_subw(c, lift_r16(x, idx), W(c->d[1]))); /* sub.w (a0,d0.w),d1 */
  lift_charge(x, 0xFD80A);
  setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 3));      /* lsr.w #3,d1 */
  lift_charge(x, 0xFD80E);
  setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));      /* subq.w #1,d1 */
  lift_charge(x, 0xFD810);
  c->a[7] -= 2;                                    /* move.w d1,-(sp) */
  lift_w16(x, c->a[7], alu_movew(c, W(c->d[1])));
  lift_charge(x, 0xFD812);
  c->a[0] += SW(lift_r16(x, idx));                 /* adda.w (a0,d0.w),a0 */
  lift_charge(x, 0xFD814);
  setw(&c->d[0], alu_movew(c, W(c->d[4])));        /* move.w d4,d0 */
  lift_charge(x, 0xFD818);
  setw(&c->d[0], alu_addw(c, 0xC0, W(c->d[0])));   /* addi.w #$C0,d0 */
  lift_charge(x, 0xFD81A);
  setw(&c->d[1], alu_movew(c, W(c->d[0])));        /* move.w d0,d1 */
  lift_charge(x, 0xFD81E);
  setw(&c->d[0], alu_subw(c, 0x90, W(c->d[0])));   /* subi.w #$90,d0 */
  lift_charge(x, 0xFD820);
  setw(&c->d[1], alu_addw(c, 0x80, W(c->d[1])));   /* addi.w #$80,d1 */
  lift_charge(x, 0xFD824);
  setw(&c->d[2], alu_movew(c, 0x170));             /* move.w #$170,d2 */
  lift_charge(x, 0xFD828);
  setw(&c->d[2], alu_subw(c, W(c->d[5]), W(c->d[2]))); /* sub.w d5,d2 */
  lift_charge(x, 0xFD82C);
  setw(&c->d[3], alu_movew(c, W(c->d[2])));        /* move.w d2,d3 */
  lift_charge(x, 0xFD82E);
  setw(&c->d[2], alu_subw(c, 0x80, W(c->d[2])));   /* subi.w #$80,d2 */
  lift_charge(x, 0xFD830);
  setw(&c->d[3], alu_addw(c, 0x70, W(c->d[3])));   /* addi.w #$70,d3 */
  lift_charge(x, 0xFD834);
  setw(&c->d[4], alu_movew(c, lift_r16(x, c->a[7]))); /* move.w (sp)+,d4 */
  c->a[7] += 2;
  lift_charge(x, 0xFD838);

  iter = 0;
  for (;;)
  {
    /* loc_FD83A — one piece */
    if (++iter > 8192) { x->declined = 1; return; }  /* dbf-wrap guard */
    alu_cmpw(c, lift_r16(x, (c->a[0] + 2) & 0xFFFFFF), W(c->d[2])); /* cmp.w 2(a0),d2 */
    lift_charge(x, 0xFD83A);
    t = (!c->zf && c->nf == c->vf);                /* bgt.w loc_FD88E */
    lift_charge_bcc(x, 0xFD83E, t);
    if (t) goto next;
    alu_cmpw(c, lift_r16(x, (c->a[0] + 2) & 0xFFFFFF), W(c->d[3])); /* cmp.w 2(a0),d3 */
    lift_charge(x, 0xFD842);
    t = (c->nf != c->vf);                          /* blt.w loc_FD88E */
    lift_charge_bcc(x, 0xFD846, t);
    if (t) goto next;
    alu_cmpw(c, lift_r16(x, c->a[0] & 0xFFFFFF), W(c->d[0])); /* cmp.w (a0),d0 */
    lift_charge(x, 0xFD84A);
    t = (!c->zf && c->nf == c->vf);                /* bgt.w loc_FD88E */
    lift_charge_bcc(x, 0xFD84C, t);
    if (t) goto next;
    alu_cmpw(c, lift_r16(x, c->a[0] & 0xFFFFFF), W(c->d[1])); /* cmp.w (a0),d1 */
    lift_charge(x, 0xFD850);
    t = (c->nf != c->vf);                          /* blt.w loc_FD88E */
    lift_charge_bcc(x, 0xFD852, t);
    if (t) goto next;

    setw(&c->d[5], alu_movew(c, lift_r16(x, (c->a[0] + 2) & 0xFFFFFF))); /* move.w 2(a0),d5 */
    lift_charge(x, 0xFD856);
    setw(&c->d[5], alu_addw(c, 0x70, W(c->d[5]))); /* addi.w #$70,d5 */
    lift_charge(x, 0xFD85A);
    setw(&c->d[5], alu_subw(c, W(c->d[2]), W(c->d[5]))); /* sub.w d2,d5 */
    lift_charge(x, 0xFD85E);
    lift_w16(x, c->a[6], alu_movew(c, W(c->d[5]))); /* move.w d5,(a6)+ */
    c->a[6] += 2;
    lift_charge(x, 0xFD860);
    lift_w8(x, c->a[6], alu_moveb(c, lift_r8(x, (c->a[0] + 7) & 0xFFFFFF))); /* move.b 7(a0),(a6)+ */
    c->a[6] += 1;
    lift_charge(x, 0xFD862);
    lift_w8(x, c->a[6], alu_moveb(c, c->d[6] & 0xFF)); /* move.b d6,(a6)+ */
    c->a[6] += 1;
    lift_charge(x, 0xFD866);
    setw(&c->d[5], alu_movew(c, lift_r16(x, (c->a[0] + 6) & 0xFFFFFF))); /* move.w 6(a0),d5 */
    lift_charge(x, 0xFD868);
    setw(&c->d[5], alu_andw(c, 0xF800, W(c->d[5]))); /* andi.w #$F800,d5 */
    lift_charge(x, 0xFD86C);
    setw(&c->d[5], alu_addw(c, lift_r16(x, (c->a[0] + 4) & 0xFFFFFF), W(c->d[5]))); /* add.w 4(a0),d5 */
    lift_charge(x, 0xFD870);
    setw(&c->d[5], alu_addw(c, lift_r16(x, 0xFFFFD6ACu), W(c->d[5]))); /* add.w ($D6AC).w,d5 */
    lift_charge(x, 0xFD874);
    lift_w16(x, c->a[6], alu_movew(c, W(c->d[5]))); /* move.w d5,(a6)+ */
    c->a[6] += 2;
    lift_charge(x, 0xFD878);
    setw(&c->d[5], alu_movew(c, lift_r16(x, c->a[0] & 0xFFFFFF))); /* move.w (a0),d5 */
    lift_charge(x, 0xFD87A);
    setw(&c->d[5], alu_addw(c, 0x70, W(c->d[5]))); /* addi.w #$70,d5 */
    lift_charge(x, 0xFD87C);
    setw(&c->d[5], alu_subw(c, W(c->d[0]), W(c->d[5]))); /* sub.w d0,d5 */
    lift_charge(x, 0xFD880);
    lift_w16(x, c->a[6], alu_movew(c, W(c->d[5]))); /* move.w d5,(a6)+ */
    c->a[6] += 2;
    lift_charge(x, 0xFD882);
    setw(&c->d[6], alu_addw(c, 1, W(c->d[6])));    /* addq.w #1,d6 */
    lift_charge(x, 0xFD884);
    alu_cmpw(c, 0x40, W(c->d[6]));                 /* cmp.w #$40,d6 */
    lift_charge(x, 0xFD886);
    t = c->zf;                                     /* beq.w loc_FD894 */
    lift_charge_bcc(x, 0xFD88A, t);
    if (t) break;

next:
    /* loc_FD88E */
    c->a[0] += 8;                                  /* addq.w #8,a0 */
    lift_charge(x, 0xFD88E);
    {
      uint32_t nd4 = (c->d[4] - 1) & 0xFFFF;       /* dbf d4,loc_FD83A */
      int taken = (nd4 != 0xFFFF);
      setw(&c->d[4], nd4);
      lift_charge_dbcc(x, 0xFD890, taken, !taken);
      if (!taken) break;
    }
  }

  /* loc_FD894: movem.l (sp)+,d0-d5 */
  for (i = 0; i < 6; i++) c->d[i] = lift_r32(x, c->a[7] + 4u * i);
  c->a[7] += 24;
  lift_charge_movem(x, 0xFD894);

bail:
  lift_charge(x, 0xFD898);                         /* locret_FD898: rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FED2A (called from ROM:17536 — the bracket/board draw path)
 *   in:  none (a6/a0/d0-d3/d6 all set up here)
 * Emits the scrolling board's sprite run and then closes the list.
 * a6 walks the sprite table at $FFFFC018 and a0 points at the piece
 * chain in ROM ($F3098); the link word ($FFFFB016) is OR'd with $8000
 * into d3 and the board's screen position comes from $FFFFB8AE /
 * $FFFFB8B0 (the pair sub_17572 maintains). Sprite_EmitPieces does the
 * work and leaves a6 past the last entry it wrote — if it wrote nothing
 * (a6 still $FFFFC018) two empty longs are laid down so the table is
 * never degenerate. The last entry's link byte is then cleared
 * (-5(a6)) to terminate the chain, and the entry count
 * ((a6 - $FFFFC018) >> 1) is stored to $FFFFC2E8.
 */
void sub_FED2A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[6] = 0xFFFFC018u;                           /* move.w #$C018,a6 */
  lift_charge(x, 0xFED2A);
  c->d[6] = alu_moveql(c, 1);                      /* moveq #1,d6 */
  lift_charge(x, 0xFED2E);
  c->a[0] = 0x000F3098u;                           /* move.l #unk_F3098,a0 */
  lift_charge(x, 0xFED30);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFFFB016u)));  /* move.w (B016).w,d3 */
  lift_charge(x, 0xFED36);
  setw(&c->d[3], alu_orw(c, 0x8000, W(c->d[3])));  /* or.w #$8000,d3 */
  lift_charge(x, 0xFED3A);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFB8AEu)));  /* move.w (B8AE).w,d0 */
  lift_charge(x, 0xFED3E);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFFFB8B0u)));  /* move.w (B8B0).w,d1 */
  lift_charge(x, 0xFED42);
  setw(&c->d[2], alu_movew(c, 1));                 /* move.w #1,d2 */
  lift_charge(x, 0xFED46);

  lift_call(x, 0xFED4A, 6, Sprite_EmitPieces);     /* jsr sub_16178 */
  if (x->declined) return;

  alu_cmpl(c, SEW(0xC018), c->a[6]);               /* cmp.w #$C018,a6 */
  lift_charge(x, 0xFED50);
  {
    int wrote = !c->zf;
    lift_charge_bcc(x, 0xFED54, wrote);            /* bne.w loc_FED5C */
    if (!wrote)
    {
      lift_w32(x, c->a[6], 0);                     /* clr.l (a6)+ */
      c->a[6] += 4;
      c->nf = 0; c->zf = 1; c->vf = 0; c->cf = 0;
      lift_charge(x, 0xFED58);
      lift_w32(x, c->a[6], 0);                     /* clr.l (a6)+ */
      c->a[6] += 4;
      c->nf = 0; c->zf = 1; c->vf = 0; c->cf = 0;
      lift_charge(x, 0xFED5A);
    }
  }

  /* loc_FED5C */
  lift_w8(x, c->a[6] - 5, 0);                      /* clr.b -5(a6) */
  c->nf = 0; c->zf = 1; c->vf = 0; c->cf = 0;
  lift_charge(x, 0xFED5C);
  c->d[0] = alu_movel(c, c->a[6]);                 /* move.l a6,d0 */
  lift_charge(x, 0xFED60);
  c->d[0] = alu_subl(c, 0xFFFFC018u, c->d[0]);     /* sub.l #$FFFFC018,d0 */
  lift_charge(x, 0xFED62);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));      /* lsr.w #1,d0 */
  lift_charge(x, 0xFED68);
  lift_w16(x, 0xFFFFC2E8u, alu_movew(c, W(c->d[0])));  /* move.w d0,($FFFFC2E8).w */
  lift_charge(x, 0xFED6A);

  lift_charge(x, 0xFED6E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}
