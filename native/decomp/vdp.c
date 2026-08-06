/*
 * vdp.c — VDP text/tile primitives (lifted). FIRST users of the staged
 * hw-write machinery (native/decomp/HW-STAGING.md): these routines write
 * the VDP control and data ports, staged via lift_whw_* and replayed
 * through GPGX's real port handlers on live commit.
 *
 * The chain's register convention (from the listing): VDP_SetAddress
 * leaves a0 = $C00000 on purpose — callers stream `move.w dN,(a0)` data
 * writes through it. The text screen cursor lives at $FFFFB028 (column)
 * / $FFFFB02A (row, wraps mod 32); $FFFFB004 + ($FFFFB02E).w indexes a
 * per-plane {name-table base, row shift} pair. $FFFFBF78 bit 2 is the
 * ROM's own "VDP address register in use" guard against its VBlank ISR —
 * atomic lift commit satisfies it structurally.
 */
#include "util68k.h"

#define T_CURSOR_COL  0xFFB028u
#define T_CURSOR_ROW  0xFFB02Au
#define T_PLANE_SEL   0xFFB02Eu   /* index into the $FFFFB004 plane table */
#define T_VDP_GUARD   0xFFBF78u   /* bit 2: ISR must leave the VDP address alone */
#define T_CHARSET_BIAS 0xFFB030u  /* index into the $FFFFB012/$FFFFBF52 bias tables */

void Script_DecodeRotatedField(lift_ctx *);   /* anim.c — $11AF4 table entry 2 */

/*
 * VDP_SetAddress ($11680; the listing's own label)
 *   in:  d0.w = VRAM word address / 2 (a name-table entry index)
 *   out: d0 = VDP command word pair, a0 = $C00000 (the data port, for the
 *        caller's subsequent writes)
 * Builds the "VRAM write" command from the entry index and writes it to
 * the control port ($C00004) as one move.l — staged as two 16-bit ctrl
 * words at one timestamp, exactly the two bus accesses the 68k performs.
 */
void VDP_SetAddress(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = 0xC00000;                             /* move.l #$C00000,a0 */
  lift_charge(x, 0x11680);
  c->d[0] = alu_asll(c, c->d[0], 2);              /* asl.l #2,d0 */
  lift_charge(x, 0x11686);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));     /* lsr.w #2,d0 */
  lift_charge(x, 0x11688);
  setw(&c->d[0], alu_orw(c, 0x4000, W(c->d[0]))); /* or.w #$4000,d0 */
  lift_charge(x, 0x1168A);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x1168E);
  setw(&c->d[0], alu_andw(c, 3, W(c->d[0])));     /* and.w #3,d0 */
  lift_charge(x, 0x11690);

  alu_movel(c, c->d[0]);                          /* move.l d0,4(a0) — flags */
  lift_whw_ctrl32(x, 0x11694, c->d[0]);           /* ctrl port: address setup */
  if (x->declined) return;

  lift_charge(x, 0x11698);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_SetCursorVramAddr (sub_11952)
 * Compute the name-table entry index for the current text cursor
 * (base + (row << rowshift + col) from the selected plane's table entry)
 * and set the VDP address through VDP_SetAddress. d0-d2 preserved;
 * a0 = $C00000 on return (deliberately — see header).
 */
void Text_SetCursorVramAddr(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 12;                                  /* movem.l d0-d2,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->d[2]);
  lift_charge_movem(x, 0x11952);

  setw(&c->d[0], alu_movew(c, lift_r16(x, T_CURSOR_COL)));  /* move.w ($B028).w,d0 */
  lift_charge(x, 0x11956);
  setw(&c->d[1], alu_movew(c, lift_r16(x, T_CURSOR_ROW)));  /* move.w ($B02A).w,d1 */
  lift_charge(x, 0x1195A);
  c->a[0] = 0xFFFFB004;                           /* move.l #$FFFFB004,a0 */
  lift_charge(x, 0x1195E);
  c->a[0] += SEW(lift_r16(x, T_PLANE_SEL));       /* add.w ($B02E).w,a0 — adda: sign-extends, no CCR */
  lift_charge(x, 0x11964);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[0] + 2)));   /* move.w 2(a0),d2 */
  lift_charge(x, 0x11968);

  {
    int cnt = c->d[2] & 63;                       /* asl.w d2,d1 */
    if (cnt)
      setw(&c->d[1], alu_aslw(c, W(c->d[1]), cnt));
    else
    {
      uint32_t xs = c->xf;                        /* count 0: X unchanged */
      setw(&c->d[1], alu_aslw(c, W(c->d[1]), 0));
      c->xf = xs;
    }
    lift_charge_shift_reg(x, 0x1196C, cnt);
  }
  setw(&c->d[0], alu_addw(c, W(c->d[1]), W(c->d[0])));      /* add.w d1,d0 */
  lift_charge(x, 0x1196E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
  lift_charge(x, 0x11970);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[0]), W(c->d[0]))); /* add.w (a0),d0 */
  lift_charge(x, 0x11972);

  lift_call(x, 0x11974, 4, VDP_SetAddress);       /* bsr.w VDP_SetAddress */
  if (x->declined) return;

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x11978);
  lift_charge(x, 0x1197C);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_FillRows (sub_1197E)
 *   in: d0.w = words per row, d1.w = row count, d2.w = tile entry
 * Fill d1 rows of d0 name-table words with tile d2, re-deriving the VRAM
 * address from the cursor each row and advancing the cursor row mod 32.
 * All registers preserved. The data-port writes go through the staged hw
 * log; a zero d0/d1 would dbf/bne-wrap into a 64K-iteration fill — decline
 * (CLAUDE.md degenerate-count rule) and let the interpreter have it.
 */
void Text_FillRows(lift_ctx *x)
{
  rcpu_t *c = x->c;

  if (W(c->d[0]) == 0 || W(c->d[1]) == 0) { x->declined = 1; return; }

  c->a[7] -= 16;                                  /* movem.l d0-d2/a0,-(sp) */
  lift_w32(x, c->a[7], c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_charge_movem(x, 0x1197E);

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x11982);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w — byte op */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x11986);
  }
  c->a[7] -= 4;                                   /* movem.w d0-d1,-(sp) */
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0x1198C);

  do
  {
    /* loc_11990 */
    lift_call(x, 0x11990, 2, Text_SetCursorVramAddr); /* bsr.s sub_11952 */
    if (x->declined) return;
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7]))); /* move.w (sp),d0 */
    lift_charge(x, 0x11992);
    setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));   /* subq.w #1,d0 */
    lift_charge(x, 0x11994);

    do
    {
      /* loc_11996: move.w d2,(a0) — the data port, via the a0 convention */
      if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
      alu_movew(c, W(c->d[2]));
      lift_whw_data16(x, 0x11996, W(c->d[2]));
      if (x->declined) return;
      setw(&c->d[0], W(c->d[0] - 1));             /* dbf d0,loc_11996 */
      {
        int taken = (W(c->d[0]) != 0xFFFF);
        lift_charge_dbcc(x, 0x11998, taken, !taken);
        if (!taken) break;
      }
    } while (1);

    {
      uint32_t v = lift_r16(x, T_CURSOR_ROW);     /* addq.w #1,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
      lift_charge(x, 0x1199C);
      v = lift_r16(x, T_CURSOR_ROW);              /* and.w #$1F,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_andw(c, 0x1F, v));
      lift_charge(x, 0x119A0);
    }
    {
      uint32_t v = lift_r16(x, c->a[7] + 2);      /* subq.w #1,2(sp) — row count */
      lift_w16(x, c->a[7] + 2, alu_subw(c, 1, v));
      lift_charge(x, 0x119A6);
    }
    lift_charge_bcc(x, 0x119AA, !c->zf);          /* bne.s loc_11990 */
  } while (!c->zf);

  c->a[7] += 4;                                   /* addq.w #4,sp — no CCR */
  lift_charge(x, 0x119AC);
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x119AE);
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a0 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x119B2);
  lift_charge(x, 0x119B6);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_EmitFrameTile (sub_11A2C)
 * One name-table word of a box frame: fetch the tile pattern at
 * (a1,d4.w) from the frame-piece table, bias it by the caller's tile
 * base d2, and stream it out the data port left open by sub_11952.
 * d4 is the caller's cursor into the piece table (advanced by 2 or 6).
 */
void Text_EmitFrameTile(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1] + SEW(c->d[4])))); /* move.w (a1,d4.w),d3 */
  lift_charge(x, 0x11A2C);
  setw(&c->d[3], alu_addw(c, W(c->d[2]), W(c->d[3])));  /* add.w d2,d3 */
  lift_charge(x, 0x11A30);

  if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
  alu_movew(c, W(c->d[3]));                             /* move.w d3,(a0) */
  lift_whw_data16(x, 0x11A32, W(c->d[3]));
  if (x->declined) return;

  lift_charge(x, 0x11A34);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_EmitFrameRow (sub_11A06)
 *   in: 4(sp).w = row width in tiles, d4 = frame-piece table cursor,
 *       d2 = tile base, a1 = piece table
 * Emits one row of the box: left edge, (width-2) middle tiles, right
 * edge — then advances the text cursor row. Leaves d4 pointing at the
 * next row's piece triplet (+6 total).
 */
void Text_EmitFrameRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x11A06, 4, Text_SetCursorVramAddr);     /* bsr.w sub_11952 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, T_CURSOR_ROW);             /* addq.w #1,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
    lift_charge(x, 0x11A0A);
  }
  lift_call(x, 0x11A0E, 4, Text_EmitFrameTile);         /* bsr.w sub_11A2C */
  if (x->declined) return;
  setw(&c->d[4], alu_addw(c, 2, W(c->d[4])));           /* addq.w #2,d4 */
  lift_charge(x, 0x11A12);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7] + 4))); /* move.w 4(sp),d0 */
  lift_charge(x, 0x11A14);
  setw(&c->d[0], alu_subw(c, 3, W(c->d[0])));           /* subq.w #3,d0 */
  lift_charge(x, 0x11A18);
  if (W(c->d[0]) == 0xFFFF) { x->declined = 1; return; } /* dbf would wrap 64K */

  do
  {
    /* loc_11A1A */
    lift_call(x, 0x11A1A, 4, Text_EmitFrameTile);       /* bsr.w sub_11A2C */
    if (x->declined) return;
    setw(&c->d[0], W(c->d[0] - 1));                     /* dbf d0,loc_11A1A */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11A1E, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  setw(&c->d[4], alu_addw(c, 2, W(c->d[4])));           /* addq.w #2,d4 */
  lift_charge(x, 0x11A22);
  lift_call(x, 0x11A24, 4, Text_EmitFrameTile);         /* bsr.w sub_11A2C */
  if (x->declined) return;
  setw(&c->d[4], alu_addw(c, 2, W(c->d[4])));           /* addq.w #2,d4 */
  lift_charge(x, 0x11A28);

  lift_charge(x, 0x11A2A);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawFrame (sub_119B8)
 *   in: d0.w = width in tiles, d1.w = height in tiles (both consumed off
 *       the stack by the row emitter), cursor at $FFFFB028/$B02A
 * Draws a box: top row, (height-2) middle rows, bottom row. The piece
 * table is the relocatable blob at ROM $55B7E (+its own +4 offset word,
 * then skip the 4-byte header); d4 walks it 6 bytes per row band, and
 * the middle band is re-used by rewinding d4 by 6 each iteration.
 * $FFFFBF78 bit 2 is saved/set/restored around the whole draw (the
 * VBlank ISR's "don't touch the VDP address" guard).
 */
void Text_DrawFrame(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;

  c->a[7] -= 28;                                  /* movem.l d0-d4/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->d[3]);
  lift_w32(x, c->a[7] + 16, c->d[4]);
  lift_w32(x, c->a[7] + 20, c->a[0]);
  lift_w32(x, c->a[7] + 24, c->a[1]);
  lift_charge_movem(x, 0x119B8);

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x119BC);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x119C0);
  }
  c->a[7] -= 4;                                   /* movem.w d0-d1,-(sp) */
  lift_w16(x, c->a[7],     W(c->d[0]));
  lift_w16(x, c->a[7] + 2, W(c->d[1]));
  lift_charge_movem(x, 0x119C6);
  sp = c->a[7];                                   /* the width/height frame */

  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFB02C)));  /* move.w ($B02C).w,d2 */
  lift_charge(x, 0x119CA);
  setw(&c->d[2], alu_addw(c, lift_r16(x, 0xFFB01E), W(c->d[2]))); /* add.w ($B01E).w,d2 */
  lift_charge(x, 0x119CE);
  c->a[1] = 0x00055B7E;                           /* move.l #unk_55B7E,a1 */
  lift_charge(x, 0x119D2);
  c->a[1] += lift_r32(x, c->a[1] + 4);            /* add.l 4(a1),a1 — adda, no CCR */
  lift_charge(x, 0x119D8);
  c->a[1] += 4;                                   /* addq.w #4,a1 — no CCR */
  lift_charge(x, 0x119DC);
  setw(&c->d[4], alu_movew(c, 0));                /* clr.w d4 */
  lift_charge(x, 0x119DE);

  lift_call(x, 0x119E0, 4, Text_EmitFrameRow);    /* bsr.w sub_11A06 — top */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, sp + 2);             /* subq.w #3,2(sp) */
    lift_w16(x, sp + 2, alu_subw(c, 3, v));
    lift_charge(x, 0x119E4);
  }

  do
  {
    /* loc_119E8 */
    lift_call(x, 0x119E8, 4, Text_EmitFrameRow); /* bsr.w sub_11A06 — middle */
    if (x->declined) return;
    setw(&c->d[4], alu_subw(c, 6, W(c->d[4])));   /* subq.w #6,d4 — reuse band */
    lift_charge(x, 0x119EC);
    {
      uint32_t v = lift_r16(x, sp + 2);           /* subq.w #1,2(sp) */
      lift_w16(x, sp + 2, alu_subw(c, 1, v));
      lift_charge(x, 0x119EE);
    }
    lift_charge_bcc(x, 0x119F2, !c->nf);          /* bpl.s loc_119E8 */
  } while (!c->nf);

  setw(&c->d[4], alu_addw(c, 6, W(c->d[4])));     /* addq.w #6,d4 */
  lift_charge(x, 0x119F4);
  lift_call(x, 0x119F6, 4, Text_EmitFrameRow);    /* bsr.w sub_11A06 — bottom */
  if (x->declined) return;

  c->a[7] += 4;                                   /* addq.w #4,sp — no CCR */
  lift_charge(x, 0x119FA);
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x119FC);
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d4/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->d[4] = lift_r32(x, c->a[7] + 16);
  c->a[0] = lift_r32(x, c->a[7] + 20);
  c->a[1] = lift_r32(x, c->a[7] + 24);
  c->a[7] += 28;
  lift_charge_movem(x, 0x11A00);
  lift_charge(x, 0x11A04);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawString (sub_11BA4)
 *   in: a1 = packed text chunk — a leading word length, then bytes:
 *       >0 = a character, 0 = padding/skip, <0 = an escape that
 *       re-positions the cursor (palette/plane in the low bits, then two
 *       bytes of column/row), $40 '@' = blank tile, $5E '^' = advance
 *       one column without drawing.
 *   out: a1 past the chunk. Registers otherwise preserved.
 * Character tiles come from Art_BoardText ($AAC52) or off_BE26A
 * ($BE26A) depending on the rink-flip bit ($FFFFC2F8 bit 3), biased by
 * the matching per-plane base ($FFFFB012 / $FFFFBF52) and the tile base
 * d2, and streamed out the data port opened by sub_11952.
 */
void Text_DrawString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x11BA4);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x11BA8);
  }
  c->a[7] -= 24;                                  /* movem.l d0-d3/a0/a2,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->d[3]);
  lift_w32(x, c->a[7] + 16, c->a[0]);
  lift_w32(x, c->a[7] + 20, c->a[2]);
  lift_charge_movem(x, 0x11BAE);

  lift_call(x, 0x11BB2, 4, Text_SetCursorVramAddr);     /* bsr.w sub_11952 */
  if (x->declined) return;
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFB02C)));  /* move.w ($B02C).w,d2 */
  lift_charge(x, 0x11BB6);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1)+,d3 */
  c->a[1] += 2;
  lift_charge(x, 0x11BBA);
  setw(&c->d[3], alu_subw(c, 2, W(c->d[3])));           /* subq.w #2,d3 */
  lift_charge(x, 0x11BBC);
  lift_charge_bcc(x, 0x11BBE, 1);                       /* bra.w loc_11C5A */

  for (;;)
  {
    /* loc_11C5A */
    if (W(c->d[3]) == 0xFFFF) { x->declined = 1; return; } /* dbf would wrap 64K */
    setw(&c->d[3], W(c->d[3] - 1));                     /* dbf d3,loc_11BC2 */
    {
      int taken = (W(c->d[3]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11C5A, taken, !taken);
      if (!taken) break;
    }

    /* loc_11BC2 */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,d0 */
    c->a[1] += 1;
    lift_charge(x, 0x11BC2);
    lift_charge_bcc(x, 0x11BC4, c->zf);                 /* beq.w loc_11C5A */
    if (c->zf) continue;

    setw(&c->d[0], alu_extw(c, c->d[0]));               /* ext.w d0 */
    lift_charge(x, 0x11BC8);
    lift_charge_bcc(x, 0x11BCA, !c->nf);                /* bpl.w loc_11C04 */
    if (!c->nf)
    {
      /* loc_11C04 */
      alu_cmpb(c, 0x40, c->d[0]);                       /* cmp.b #$40,d0 */
      lift_charge(x, 0x11C04);
      lift_charge_bcc(x, 0x11C08, !c->zf);              /* bne.w loc_11C14 */
      if (c->zf)
      {
        setw(&c->d[0], alu_movew(c, 0x7FF));            /* move.w #$7FF,d0 */
        lift_charge(x, 0x11C0C);
        lift_charge_bcc(x, 0x11C10, 1);                 /* bra.w loc_11C52 */
      }
      else
      {
        /* loc_11C14 */
        alu_cmpb(c, 0x5E, c->d[0]);                     /* cmp.b #$5E,d0 */
        lift_charge(x, 0x11C14);
        lift_charge_bcc(x, 0x11C18, c->zf);             /* beq.w loc_11C68 */
        if (c->zf)
        {
          /* loc_11C68 */
          uint32_t v = lift_r16(x, T_CURSOR_COL);       /* addq.w #1,($B028).w */
          lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
          lift_charge(x, 0x11C68);
          lift_call(x, 0x11C6C, 4, Text_SetCursorVramAddr); /* bsr.w sub_11952 */
          if (x->declined) return;
          lift_charge_bcc(x, 0x11C70, 1);               /* bra.s loc_11C5A */
          continue;
        }

        setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
        lift_charge(x, 0x11C1C);
        c->a[2] = 0x000AAC52;                           /* move.l #Art_BoardText,a2 */
        lift_charge(x, 0x11C1E);
        alu_btst(c, lift_r8(x, 0xFFC2F8), 3);           /* btst #3,($C2F8).w */
        lift_charge(x, 0x11C24);
        lift_charge_bcc(x, 0x11C2A, c->zf);             /* beq.w loc_11C34 */
        if (!c->zf)
        {
          c->a[2] = 0x000BE26A;                         /* move.l #off_BE26A,a2 */
          lift_charge(x, 0x11C2E);
        }
        /* loc_11C34 */
        c->a[2] += lift_r32(x, c->a[2] + 4);            /* add.l 4(a2),a2 — adda */
        lift_charge(x, 0x11C34);
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 4 + SEW(c->d[0])))); /* move.w 4(a2,d0.w),d0 */
        lift_charge(x, 0x11C38);
        alu_btst(c, lift_r8(x, 0xFFC2F8), 3);           /* btst #3,($C2F8).w */
        lift_charge(x, 0x11C3C);
        lift_charge_bcc(x, 0x11C42, c->zf);             /* beq.w loc_11C4E */
        if (!c->zf)
        {
          setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFBF52), W(c->d[0]))); /* add.w ($BF52).w,d0 */
          lift_charge(x, 0x11C46);
          lift_charge_bcc(x, 0x11C4A, 1);               /* bra.w loc_11C52 */
        }
        else
        {
          /* loc_11C4E */
          setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFB012), W(c->d[0]))); /* add.w ($B012).w,d0 */
          lift_charge(x, 0x11C4E);
        }
      }

      /* loc_11C52 */
      setw(&c->d[0], alu_addw(c, W(c->d[2]), W(c->d[0])));  /* add.w d2,d0 */
      lift_charge(x, 0x11C52);
      if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
      alu_movew(c, W(c->d[0]));                         /* move.w d0,(a0) */
      lift_whw_data16(x, 0x11C54, W(c->d[0]));
      if (x->declined) return;
      {
        uint32_t v = lift_r16(x, T_CURSOR_COL);         /* addq.w #1,($B028).w */
        lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
        lift_charge(x, 0x11C56);
      }
      continue;                                         /* falls into loc_11C5A */
    }

    /* negative byte: cursor/plane escape */
    setw(&c->d[0], alu_negw(c, W(c->d[0])));            /* neg.w d0 */
    lift_charge(x, 0x11BCE);
    setw(&c->d[2], alu_movew(c, W(c->d[0])));           /* move.w d0,d2 */
    lift_charge(x, 0x11BD0);
    setw(&c->d[2], alu_aslw(c, W(c->d[2]), 8));         /* asl.w #8,d2 */
    lift_charge(x, 0x11BD2);
    setw(&c->d[2], alu_aslw(c, W(c->d[2]), 1));         /* asl.w #1,d2 */
    lift_charge(x, 0x11BD4);
    setw(&c->d[2], alu_andw(c, 0xF800, W(c->d[2])));    /* and.w #$F800,d2 */
    lift_charge(x, 0x11BD6);
    {
      uint32_t v = W(c->d[2]);                          /* move.w d2,($B02C).w */
      alu_movew(c, v);
      lift_w16(x, 0xFFB02C, v);
      lift_charge(x, 0x11BDA);
    }
    setw(&c->d[0], alu_andw(c, 3, W(c->d[0])));         /* and.w #3,d0 */
    lift_charge(x, 0x11BDE);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));         /* asl.w #2,d0 */
    lift_charge(x, 0x11BE2);
    setw(&c->d[0], alu_subw(c, 4, W(c->d[0])));         /* subq.w #4,d0 */
    lift_charge(x, 0x11BE4);
    {
      uint32_t v = W(c->d[0]);                          /* move.w d0,($B02E).w */
      alu_movew(c, v);
      lift_w16(x, T_PLANE_SEL, v);
      lift_charge(x, 0x11BE6);
    }
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,d0 */
    c->a[1] += 1;
    lift_charge(x, 0x11BEA);
    setw(&c->d[0], alu_extw(c, c->d[0]));               /* ext.w d0 */
    lift_charge(x, 0x11BEC);
    {
      uint32_t v = W(c->d[0]);                          /* move.w d0,($B028).w */
      alu_movew(c, v);
      lift_w16(x, T_CURSOR_COL, v);
      lift_charge(x, 0x11BEE);
    }
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,d1 */
    c->a[1] += 1;
    lift_charge(x, 0x11BF2);
    setw(&c->d[1], alu_extw(c, c->d[1]));               /* ext.w d1 */
    lift_charge(x, 0x11BF4);
    {
      uint32_t v = W(c->d[1]);                          /* move.w d1,($B02A).w */
      alu_movew(c, v);
      lift_w16(x, T_CURSOR_ROW, v);
      lift_charge(x, 0x11BF6);
    }
    lift_call(x, 0x11BFA, 4, Text_SetCursorVramAddr);   /* bsr.w sub_11952 */
    if (x->declined) return;
    setw(&c->d[3], alu_subw(c, 2, W(c->d[3])));         /* subq.w #2,d3 */
    lift_charge(x, 0x11BFE);
    lift_charge_bcc(x, 0x11C00, 1);                     /* bra.w loc_11C5A */
  }

  /* loc_11C5E */
  c->d[0] = lift_r32(x, c->a[7]);                       /* movem.l (sp)+,d0-d3/a0/a2 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->a[0] = lift_r32(x, c->a[7] + 16);
  c->a[2] = lift_r32(x, c->a[7] + 20);
  c->a[7] += 24;
  lift_charge_movem(x, 0x11C5E);
  {
    uint32_t v = lift_r16(x, c->a[7]);                  /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x11C62);
  }
  lift_charge(x, 0x11C66);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawInlineString (sub_11B92)
 * Inline-argument wrapper: the caller's bsr return address points at the
 * packed text chunk. Load it into a1, draw, and store the advanced a1
 * back as the return address so control resumes past the inline data.
 */
void Text_DrawInlineString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0x11B92);
  c->a[1] = lift_r32(x, c->a[7] + 4);             /* move.l 4(sp),a1 — movea */
  lift_charge(x, 0x11B94);

  lift_call(x, 0x11B98, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;

  alu_movel(c, c->a[1]);                          /* move.l a1,4(sp) */
  lift_w32(x, c->a[7] + 4, c->a[1]);
  lift_charge(x, 0x11B9C);
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0x11BA0);
  lift_charge(x, 0x11BA2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_PlotTileAt (sub_11EDA)
 *   in: d3 = tile word, d6 = tile bias, d5 = column, d4 = row-ish offset
 * Address the name-table cell for (d5 shifted by the plane's row shift,
 * + d4) on the currently selected plane and drop one tile word there.
 * d1/a0 are saved and restored; d0/d3 are left modified (the callers'
 * convention).
 */
void Text_PlotTileAt(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_addw(c, W(c->d[6]), W(c->d[3])));  /* add.w d6,d3 */
  lift_charge(x, 0x11EDA);

  c->a[7] -= 8;                                   /* movem.l d1/a0,-(sp) */
  lift_w32(x, c->a[7],     c->d[1]);
  lift_w32(x, c->a[7] + 4, c->a[0]);
  lift_charge_movem(x, 0x11EDC);

  setw(&c->d[0], alu_movew(c, W(c->d[5])));       /* move.w d5,d0 */
  lift_charge(x, 0x11EE0);
  c->a[0] = 0xFFFFB004;                           /* move.l #$FFFFB004,a0 */
  lift_charge(x, 0x11EE2);
  c->a[0] += SEW(lift_r16(x, T_PLANE_SEL));       /* add.w ($B02E).w,a0 — adda */
  lift_charge(x, 0x11EE8);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 2)));  /* move.w 2(a0),d1 */
  lift_charge(x, 0x11EEC);
  {
    int cnt = c->d[1] & 63;                       /* asl.w d1,d0 */
    if (cnt)
      setw(&c->d[0], alu_aslw(c, W(c->d[0]), cnt));
    else
    {
      uint32_t xs = c->xf;                        /* count 0: X unchanged */
      setw(&c->d[0], alu_aslw(c, W(c->d[0]), 0));
      c->xf = xs;
    }
    lift_charge_shift_reg(x, 0x11EF0, cnt);
  }
  setw(&c->d[0], alu_addw(c, W(c->d[4]), W(c->d[0])));     /* add.w d4,d0 */
  lift_charge(x, 0x11EF2);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
  lift_charge(x, 0x11EF4);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[0]), W(c->d[0]))); /* add.w (a0),d0 */
  lift_charge(x, 0x11EF6);

  lift_call(x, 0x11EF8, 4, VDP_SetAddress);       /* bsr.w VDP_SetAddress */
  if (x->declined) return;

  if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
  alu_movew(c, W(c->d[3]));                       /* move.w d3,(a0) */
  lift_whw_data16(x, 0x11EFC, W(c->d[3]));
  if (x->declined) return;

  c->d[1] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d1/a0 */
  c->a[0] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0x11EFE);
  lift_charge(x, 0x11F02);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * VDP_FillVramWords (sub_11544)
 *   in: d0.w = byte count (halved to a word count), d1.l = VRAM word
 *       address / 2, d2.w = the fill word
 * Sets the VRAM write address from d1 (the same command encoding as
 * VDP_SetAddress, but built in place against its own a1 = $C00000) and
 * streams d2 out the data port d0/2 times, as two separate word writes
 * per iteration bounced through the $FFFFD06A scratch long.
 */
void VDP_FillVramWords(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 8;                                   /* movem.l d3/a1,-(sp) */
  lift_w32(x, c->a[7],     c->d[3]);
  lift_w32(x, c->a[7] + 4, c->a[1]);
  lift_charge_movem(x, 0x11544);

  c->a[1] = 0x00C00000;                           /* move.l #$C00000,a1 — movea */
  lift_charge(x, 0x11548);
  c->d[1] = alu_andl(c, 0xFFFF, c->d[1]);         /* and.l #$FFFF,d1 */
  lift_charge(x, 0x1154E);
  c->d[1] = alu_asll(c, c->d[1], 2);              /* asl.l #2,d1 */
  lift_charge(x, 0x11554);
  setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 2));     /* lsr.w #2,d1 */
  lift_charge(x, 0x11556);
  setw(&c->d[1], alu_orw(c, 0x4000, W(c->d[1]))); /* or.w #$4000,d1 */
  lift_charge(x, 0x11558);
  c->d[1] = alu_swap(c, c->d[1]);                 /* swap d1 */
  lift_charge(x, 0x1155C);

  alu_movel(c, c->d[1]);                          /* move.l d1,4(a1) — flags */
  lift_whw_ctrl32(x, 0x1155E, c->d[1]);           /* ctrl port: address setup */
  if (x->declined) return;

  setw(&c->d[3], alu_movew(c, W(c->d[2])));       /* move.w d2,d3 */
  lift_charge(x, 0x11562);
  c->d[3] = alu_swap(c, c->d[3]);                 /* swap d3 */
  lift_charge(x, 0x11564);
  setw(&c->d[3], alu_movew(c, W(c->d[2])));       /* move.w d2,d3 */
  lift_charge(x, 0x11566);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));     /* lsr.w #1,d0 */
  lift_charge(x, 0x11568);
  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));     /* subq.w #1,d0 */
  lift_charge(x, 0x1156A);
  if (W(c->d[0]) == 0xFFFF) { x->declined = 1; return; }  /* dbf would wrap 64K */

  do
  {
    /* loc_1156C */
    alu_movel(c, c->d[3]);                        /* move.l d3,($D06A).w */
    lift_w32(x, 0xFFD06A, c->d[3]);
    lift_charge(x, 0x1156C);

    if ((c->a[1] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
    {
      uint32_t v = lift_r16(x, 0xFFD06A);         /* move.w ($D06A).w,(a1) */
      alu_movew(c, v);
      lift_whw_data16(x, 0x11570, v);
      if (x->declined) return;
      v = lift_r16(x, 0xFFD06C);                  /* move.w ($D06C).w,(a1) */
      alu_movew(c, v);
      lift_whw_data16(x, 0x11574, v);
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_1156C */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11578, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  c->d[3] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d3/a1 */
  c->a[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0x1157C);
  lift_charge(x, 0x11580);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_EscSetPlane (sub_11B28) — one of the escape handlers reached from
 * the $11AF4 dispatch table: take the next stream byte as a plane index
 * (low 2 bits), store the $FFFFB004 table offset, and tail into
 * sub_11952 to re-address the VDP for the new plane.
 */
void Text_EscSetPlane(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B28);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B2A);
  setw(&c->d[0], alu_andw(c, 3, W(c->d[0])));     /* and.w #3,d0 */
  lift_charge(x, 0x11B2C);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0x11B30);
  setw(&c->d[0], alu_subw(c, 4, W(c->d[0])));     /* subq.w #4,d0 */
  lift_charge(x, 0x11B32);
  {
    uint32_t v = W(c->d[0]);                      /* move.w d0,($B02E).w */
    alu_movew(c, v);
    lift_w16(x, T_PLANE_SEL, v);
    lift_charge(x, 0x11B34);
  }
  lift_charge_bcc(x, 0x11B38, 1);                 /* bra.w sub_11952 — tail */
  Text_SetCursorVramAddr(x);
}

/*
 * Text_EscSetColumn (sub_11B4C) — escape handler: next stream byte is
 * the new cursor column; tail into sub_11952 to re-address.
 */
void Text_EscSetColumn(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x11B4C);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B4E);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B50);
  {
    uint32_t v = W(c->d[0]);                      /* move.w d0,($B028).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x11B52);
  }
  lift_charge_bcc(x, 0x11B56, 1);                 /* bra.w sub_11952 — tail */
  Text_SetCursorVramAddr(x);
}

/*
 * Text_EscSetRow (loc_11B5A) — escape handler: next stream byte is the
 * new cursor row; tail into sub_11952 to re-address.
 */
void Text_EscSetRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x11B5A);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B5C);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B5E);
  {
    uint32_t v = W(c->d[0]);                      /* move.w d0,($B02A).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_ROW, v);
    lift_charge(x, 0x11B60);
  }
  lift_charge_bcc(x, 0x11B64, 1);                 /* bra.w sub_11952 — tail */
  Text_SetCursorVramAddr(x);
}

/*
 * Text_EscAddColumn (loc_11B68) — escape handler: next stream byte is a
 * SIGNED column delta; tail into sub_11952 to re-address.
 */
void Text_EscAddColumn(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B68);
  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0x11B6A);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B6C);
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* add.w d0,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_addw(c, W(c->d[0]), v));
    lift_charge(x, 0x11B6E);
  }
  lift_charge_bcc(x, 0x11B72, 1);                 /* bra.w sub_11952 — tail */
  Text_SetCursorVramAddr(x);
}

/*
 * Text_EscAddRow (loc_11B76) — escape handler: next stream byte is a
 * SIGNED row delta; tail into sub_11952 to re-address.
 */
void Text_EscAddRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B76);
  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0x11B78);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B7A);
  {
    uint32_t v = lift_r16(x, T_CURSOR_ROW);       /* add.w d0,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, W(c->d[0]), v));
    lift_charge(x, 0x11B7C);
  }
  lift_charge_bcc(x, 0x11B80, 1);                 /* bra.w sub_11952 — tail */
  Text_SetCursorVramAddr(x);
}

/*
 * Text_EscSetCharsetRow (loc_11B84) — escape handler: next stream byte
 * (doubled into a word-table offset) picks the row of the per-plane
 * bias table ($FFFFB012/$FFFFBF52) that the character path adds to each
 * glyph's tile word. No re-address needed — plain rts.
 */
void Text_EscSetCharsetRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x11B84);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
  c->a[1] += 1;
  lift_charge(x, 0x11B86);
  setw(&c->d[3], alu_subw(c, 1, W(c->d[3])));     /* subq.w #1,d3 */
  lift_charge(x, 0x11B88);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
  lift_charge(x, 0x11B8A);
  {
    uint32_t v = W(c->d[0]);                      /* move.w d0,($B030).w */
    alu_movew(c, v);
    lift_w16(x, T_CHARSET_BIAS, v);
    lift_charge(x, 0x11B8C);
  }
  lift_charge(x, 0x11B90);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_EscSetAll (loc_11B18) — escape handler, table entry 8: palette
 * (sub_11B3C), plane (sub_11B28), column (sub_11B4C), then tail into
 * the row handler — four stream bytes consumed in one escape.
 */
void Text_EscSetAll(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x11B18, 4, Script_DecodeRotatedField);  /* bsr.w sub_11B3C */
  if (x->declined) return;
  lift_call(x, 0x11B1C, 4, Text_EscSetPlane);           /* bsr.w sub_11B28 */
  if (x->declined) return;
  lift_call(x, 0x11B20, 4, Text_EscSetColumn);          /* bsr.w sub_11B4C */
  if (x->declined) return;
  lift_charge_bcc(x, 0x11B24, 1);                 /* bra.w loc_11B5A — tail */
  Text_EscSetRow(x);
}

/*
 * Text_DrawTableString (sub_11A48)
 *   in: a1 = packed text chunk (leading word length, then bytes), a0 =
 *       the data port left open by the caller's chain.
 * The table-dispatching sibling of Text_DrawString: bytes 1..$7F draw a
 * glyph ('@' = blank tile, '^' = advance without drawing); byte 0 and
 * negative bytes index the STATIC ROM dispatch table at dword_11AF4
 * (9 entries — entry 0 is the shared bare rts at $15464, the rest are
 * the Text_Esc* handlers above) via `jsr (a2)`. The jsr is lifted as a
 * switch over the enumerated table: same charge (the jsr's table cost
 * at $11A8E is target-independent), lift_call per lifted target, and a
 * staged push/pop pair for the bare-rts entry. An index outside the
 * 9-entry table (corrupt stream) declines rather than guessing.
 * Unlike Text_DrawString, the glyph bias is INDEXED: ($B030).w picks a
 * row of the $B012/$BF52 table selected into a3 at entry.
 */
void Text_DrawTableString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x11A48);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x11A4C);
  }
  c->a[7] -= 28;                                  /* movem.l d0-d3/a0/a2-a3,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->d[3]);
  lift_w32(x, c->a[7] + 16, c->a[0]);
  lift_w32(x, c->a[7] + 20, c->a[2]);
  lift_w32(x, c->a[7] + 24, c->a[3]);
  lift_charge_movem(x, 0x11A52);

  c->a[3] = 0xFFFFB012;                           /* movea.w #$B012,a3 — sign-extends */
  lift_charge(x, 0x11A56);
  alu_btst(c, lift_r8(x, 0xFFC2F8), 3);           /* btst #3,($C2F8).w */
  lift_charge(x, 0x11A5A);
  lift_charge_bcc(x, 0x11A60, c->zf);             /* beq.w loc_11A68 */
  if (!c->zf)
  {
    c->a[3] = 0xFFFFBF52;                         /* movea.w #$BF52,a3 — sign-extends */
    lift_charge(x, 0x11A64);
  }

  /* loc_11A68 */
  lift_call(x, 0x11A68, 4, Text_SetCursorVramAddr);     /* bsr.w sub_11952 */
  if (x->declined) return;
  setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFB02C)));  /* move.w ($B02C).w,d2 */
  lift_charge(x, 0x11A6C);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1)+,d3 */
  c->a[1] += 2;
  lift_charge(x, 0x11A70);
  setw(&c->d[3], alu_subw(c, 2, W(c->d[3])));           /* subq.w #2,d3 */
  lift_charge(x, 0x11A72);
  lift_charge_bcc(x, 0x11A74, 1);                       /* bra.w loc_11ADC */

  for (;;)
  {
    /* loc_11ADC */
    if (W(c->d[3]) == 0xFFFF) { x->declined = 1; return; } /* dbf would wrap 64K */
    setw(&c->d[3], W(c->d[3] - 1));                     /* dbf d3,loc_11A78 */
    {
      int taken = (W(c->d[3]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11ADC, taken, !taken);
      if (!taken) break;
    }

    /* loc_11A78 */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,d0 */
    c->a[1] += 1;
    lift_charge(x, 0x11A78);
    setw(&c->d[0], alu_extw(c, c->d[0]));               /* ext.w d0 */
    lift_charge(x, 0x11A7A);
    {
      int gt = !c->zf && (c->nf == c->vf);              /* bgt.w loc_11A94 */
      lift_charge_bcc(x, 0x11A7C, gt);
      if (!gt)
      {
        /* escape: index the dword_11AF4 dispatch table */
        setw(&c->d[0], alu_negw(c, W(c->d[0])));        /* neg.w d0 */
        lift_charge(x, 0x11A80);
        setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
        lift_charge(x, 0x11A82);
        c->a[2] = 0x00011AF4;                           /* move.l #dword_11AF4,a2 */
        lift_charge(x, 0x11A84);
        c->a[2] = lift_r32(x, c->a[2] + SEW(c->d[0]));  /* move.l (a2,d0.w),a2 — movea */
        lift_charge(x, 0x11A8A);

        /* jsr (a2) — the table is static ROM, so enumerate it; the
         * jsr's charge at $11A8E is target-independent. */
        switch (c->a[2] & 0xFFFFFF)
        {
        case 0x015464:                                  /* entry 0: shared bare rts */
          c->a[7] -= 4;
          lift_w32(x, c->a[7], 0x11A90);                /* the jsr's return push */
          lift_charge(x, 0x11A8E);                      /* jsr (a2) */
          lift_charge(x, 0x15464);                      /* rts straight back */
          c->a[7] += 4;
          break;
        case 0x011B28: lift_call(x, 0x11A8E, 2, Text_EscSetPlane);          break;
        case 0x011B3C: lift_call(x, 0x11A8E, 2, Script_DecodeRotatedField); break;
        case 0x011B4C: lift_call(x, 0x11A8E, 2, Text_EscSetColumn);         break;
        case 0x011B5A: lift_call(x, 0x11A8E, 2, Text_EscSetRow);            break;
        case 0x011B68: lift_call(x, 0x11A8E, 2, Text_EscAddColumn);         break;
        case 0x011B76: lift_call(x, 0x11A8E, 2, Text_EscAddRow);            break;
        case 0x011B84: lift_call(x, 0x11A8E, 2, Text_EscSetCharsetRow);     break;
        case 0x011B18: lift_call(x, 0x11A8E, 2, Text_EscSetAll);            break;
        default: x->declined = 1; return;               /* off-table index */
        }
        if (x->declined) return;
        lift_charge_bcc(x, 0x11A90, 1);                 /* bra.w loc_11ADC */
        continue;
      }
    }

    /* loc_11A94 */
    alu_cmpb(c, 0x40, c->d[0]);                         /* cmp.b #$40,d0 — '@'? */
    lift_charge(x, 0x11A94);
    lift_charge_bcc(x, 0x11A98, !c->zf);                /* bne.w loc_11AA4 */
    if (c->zf)
    {
      setw(&c->d[0], alu_movew(c, 0x7FF));              /* move.w #$7FF,d0 */
      lift_charge(x, 0x11A9C);
      lift_charge_bcc(x, 0x11AA0, 1);                   /* bra.w loc_11AD4 */
    }
    else
    {
      /* loc_11AA4 */
      alu_cmpb(c, 0x5E, c->d[0]);                       /* cmp.b #$5E,d0 — '^'? */
      lift_charge(x, 0x11AA4);
      lift_charge_bcc(x, 0x11AA8, c->zf);               /* beq.w loc_11AEA */
      if (c->zf)
      {
        /* loc_11AEA: advance one column without drawing */
        uint32_t v = lift_r16(x, T_CURSOR_COL);         /* addq.w #1,($B028).w */
        lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
        lift_charge(x, 0x11AEA);
        lift_call(x, 0x11AEE, 4, Text_SetCursorVramAddr);   /* bsr.w sub_11952 */
        if (x->declined) return;
        lift_charge_bcc(x, 0x11AF2, 1);                 /* bra.s loc_11ADC */
        continue;
      }

      setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));       /* asl.w #1,d0 */
      lift_charge(x, 0x11AAC);
      c->a[2] = 0x000AAC52;                             /* move.l #Art_BoardText,a2 */
      lift_charge(x, 0x11AAE);
      alu_btst(c, lift_r8(x, 0xFFC2F8), 3);             /* btst #3,($C2F8).w */
      lift_charge(x, 0x11AB4);
      lift_charge_bcc(x, 0x11ABA, c->zf);               /* beq.w loc_11AC4 */
      if (!c->zf)
      {
        c->a[2] = 0x000BE26A;                           /* move.l #off_BE26A,a2 */
        lift_charge(x, 0x11ABE);
      }
      /* loc_11AC4 */
      c->a[2] += lift_r32(x, c->a[2] + 4);              /* adda.l 4(a2),a2 */
      lift_charge(x, 0x11AC4);
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 4 + SEW(c->d[0])))); /* move.w 4(a2,d0.w),d0 */
      lift_charge(x, 0x11AC8);
      setw(&c->d[1], alu_movew(c, lift_r16(x, T_CHARSET_BIAS)));  /* move.w ($B030).w,d1 */
      lift_charge(x, 0x11ACC);
      setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[3] + SEW(c->d[1])), W(c->d[0]))); /* add.w (a3,d1.w),d0 */
      lift_charge(x, 0x11AD0);
    }

    /* loc_11AD4 */
    setw(&c->d[0], alu_addw(c, W(c->d[2]), W(c->d[0]))); /* add.w d2,d0 */
    lift_charge(x, 0x11AD4);
    if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
    alu_movew(c, W(c->d[0]));                           /* move.w d0,(a0) */
    lift_whw_data16(x, 0x11AD6, W(c->d[0]));
    if (x->declined) return;
    {
      uint32_t v = lift_r16(x, T_CURSOR_COL);           /* addq.w #1,($B028).w */
      lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
      lift_charge(x, 0x11AD8);
    }
  }

  /* loc_11AE0: epilogue */
  c->d[0] = lift_r32(x, c->a[7]);                       /* movem.l (sp)+,d0-d3/a0/a2-a3 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->a[0] = lift_r32(x, c->a[7] + 16);
  c->a[2] = lift_r32(x, c->a[7] + 20);
  c->a[3] = lift_r32(x, c->a[7] + 24);
  c->a[7] += 28;
  lift_charge_movem(x, 0x11AE0);
  {
    uint32_t v = lift_r16(x, c->a[7]);                  /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x11AE4);
  }
  lift_charge(x, 0x11AE8);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawScoreboardCell (sub_8078)
 *   in: d0.w = byte offset into the $FFFFC334 tilemap buffer
 * Blits an 11-wide, 2-row block of name-table words straight out of that
 * buffer, then leaves the cursor 11 columns right of where it started.
 */
void Text_DrawScoreboardCell(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 20;                                  /* movem.l d0-d2/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_w32(x, c->a[7] + 16, c->a[1]);
  lift_charge_movem(x, 0x8078);

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x807C);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x8080);
  }
  c->a[1] = 0xFFFFC334;                           /* move.w #$C334,a1 — sign-extends */
  lift_charge(x, 0x8086);
  c->a[1] += SEW(c->d[0]);                        /* add.w d0,a1 — adda */
  lift_charge(x, 0x808A);
  c->d[2] = alu_moveql(c, 1);                     /* moveq #1,d2 */
  lift_charge(x, 0x808C);

  do
  {
    /* loc_808E */
    lift_call(x, 0x808E, 6, Text_SetCursorVramAddr);  /* jsr sub_11952 */
    if (x->declined) return;
    setw(&c->d[1], alu_movew(c, 0xA));            /* move.w #$A,d1 */
    lift_charge(x, 0x8094);

    do
    {
      /* loc_8098: move.w (a1)+,(a0) */
      uint32_t v = lift_r16(x, c->a[1]);
      c->a[1] += 2;
      if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
      alu_movew(c, v);
      lift_whw_data16(x, 0x8098, v);
      if (x->declined) return;
      setw(&c->d[1], W(c->d[1] - 1));             /* dbf d1,loc_8098 */
      {
        int taken = (W(c->d[1]) != 0xFFFF);
        lift_charge_dbcc(x, 0x809A, taken, !taken);
        if (!taken) break;
      }
    } while (1);

    {
      uint32_t v = lift_r16(x, T_CURSOR_ROW);     /* addq.w #1,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
      lift_charge(x, 0x809E);
    }
    setw(&c->d[2], W(c->d[2] - 1));               /* dbf d2,loc_808E */
    {
      int taken = (W(c->d[2]) != 0xFFFF);
      lift_charge_dbcc(x, 0x80A2, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* add.w #$B,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_addw(c, 0xB, v));
    lift_charge(x, 0x80A6);
    v = lift_r16(x, T_CURSOR_ROW);                /* subq.w #2,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_subw(c, 2, v));
    lift_charge(x, 0x80AC);
  }
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x80B0);
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[1] = lift_r32(x, c->a[7] + 16);
  c->a[7] += 20;
  lift_charge_movem(x, 0x80B4);
  lift_charge(x, 0x80B8);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawBigChar (sub_11E8E)
 *   in: d0.w = ASCII code, d4/d5 = cell row/column, d6 = tile bias
 * Draws one character of the tall (two-tile) board font: $1916A maps the
 * ASCII code to a glyph index (negative = single-row glyph), and the
 * $A9A10 blob supplies the tile words — one plot per row via sub_11EDA,
 * with the second row's word found (a0) entries further along.
 */
void Text_DrawBigChar(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_subw(c, 0x20, W(c->d[0])));  /* sub.w #$20,d0 */
  lift_charge(x, 0x11E8E);
  c->a[0] = 0x0001916A;                           /* move.l #unk_1916A,a0 */
  lift_charge(x, 0x11E92);
  c->d[2] = alu_moveql(c, 1);                     /* moveq #1,d2 */
  lift_charge(x, 0x11E98);
  setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[0]))));  /* move.b (a0,d0.w),d1 */
  lift_charge(x, 0x11E9A);
  setw(&c->d[1], alu_extw(c, c->d[1]));           /* ext.w d1 */
  lift_charge(x, 0x11E9E);
  lift_charge_bcc(x, 0x11EA0, !c->nf);            /* bpl.w loc_11EA8 */
  if (c->nf)
  {
    setw(&c->d[1], alu_negw(c, W(c->d[1])));      /* neg.w d1 */
    lift_charge(x, 0x11EA4);
    setw(&c->d[2], alu_movew(c, 0));              /* clr.w d2 */
    lift_charge(x, 0x11EA6);
  }

  /* loc_11EA8 */
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 1));     /* asl.w #1,d1 */
  lift_charge(x, 0x11EA8);
  c->a[0] = 0x000A9A10;                           /* move.l #off_A9A10,a0 */
  lift_charge(x, 0x11EAA);
  c->a[0] += lift_r32(x, c->a[0] + 4);            /* add.l 4(a0),a0 — adda */
  lift_charge(x, 0x11EB0);

  do
  {
    /* loc_11EB4 */
    setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[0] + 4 + SEW(c->d[1])))); /* move.w 4(a0,d1.w),d3 */
    lift_charge(x, 0x11EB4);
    lift_call(x, 0x11EB8, 4, Text_PlotTileAt);    /* bsr.w sub_11EDA */
    if (x->declined) return;
    setw(&c->d[7], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0),d7 */
    lift_charge(x, 0x11EBC);
    setw(&c->d[7], alu_aslw(c, W(c->d[7]), 1));   /* asl.w #1,d7 */
    lift_charge(x, 0x11EBE);
    setw(&c->d[1], alu_addw(c, W(c->d[7]), W(c->d[1])));  /* add.w d7,d1 */
    lift_charge(x, 0x11EC0);
    setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[0] + 4 + SEW(c->d[1])))); /* move.w 4(a0,d1.w),d3 */
    lift_charge(x, 0x11EC2);
    setw(&c->d[1], alu_subw(c, W(c->d[7]), W(c->d[1])));  /* sub.w d7,d1 */
    lift_charge(x, 0x11EC6);
    setw(&c->d[5], alu_addw(c, 1, W(c->d[5])));   /* addq.w #1,d5 */
    lift_charge(x, 0x11EC8);
    lift_call(x, 0x11ECA, 4, Text_PlotTileAt);    /* bsr.w sub_11EDA */
    if (x->declined) return;
    setw(&c->d[5], alu_subw(c, 1, W(c->d[5])));   /* subq.w #1,d5 */
    lift_charge(x, 0x11ECE);
    setw(&c->d[4], alu_addw(c, 1, W(c->d[4])));   /* addq.w #1,d4 */
    lift_charge(x, 0x11ED0);
    setw(&c->d[1], alu_addw(c, 2, W(c->d[1])));   /* addq.w #2,d1 */
    lift_charge(x, 0x11ED2);
    setw(&c->d[2], W(c->d[2] - 1));               /* dbf d2,loc_11EB4 */
    {
      int taken = (W(c->d[2]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11ED4, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  lift_charge(x, 0x11ED8);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawBigString (sub_11DF4)
 *   in: a1 = packed chunk (leading word length, then bytes)
 * The tall-font counterpart of Text_DrawString: same stream encoding
 * (0 ends the run, negative bytes are cursor/plane escapes carrying
 * palette + column + row), but each character is drawn two tiles high
 * through Text_DrawBigChar, and lower-case is folded to upper first.
 * d4/d5 track the cursor and are written back to $FFFFB028/$B02A at the
 * end; d6 carries the tile base ($FFFFB02C + $FFFFB010).
 */
void Text_DrawBigString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x11DF4);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x11DF8);
  }
  c->a[7] -= 40;                                  /* movem.l d0-d7/a0/a2,-(sp) */
  {
    int i;
    for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
    lift_w32(x, c->a[7] + 32, c->a[0]);
    lift_w32(x, c->a[7] + 36, c->a[2]);
  }
  lift_charge_movem(x, 0x11DFE);

  setw(&c->d[4], alu_movew(c, lift_r16(x, T_CURSOR_COL)));   /* move.w ($B028).w,d4 */
  lift_charge(x, 0x11E02);
  setw(&c->d[5], alu_movew(c, lift_r16(x, T_CURSOR_ROW)));   /* move.w ($B02A).w,d5 */
  lift_charge(x, 0x11E06);
  setw(&c->d[6], alu_movew(c, lift_r16(x, 0xFFB02C)));       /* move.w ($B02C).w,d6 */
  lift_charge(x, 0x11E0A);
  setw(&c->d[6], alu_addw(c, lift_r16(x, 0xFFB010), W(c->d[6])));  /* add.w ($B010).w,d6 */
  lift_charge(x, 0x11E0E);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1])));        /* move.w (a1)+,d3 */
  c->a[1] += 2;
  lift_charge(x, 0x11E12);
  setw(&c->d[3], alu_subw(c, 2, W(c->d[3])));     /* subq.w #2,d3 */
  lift_charge(x, 0x11E14);
  lift_charge_bcc(x, 0x11E16, 1);                 /* bra.w loc_11E78 */

  for (;;)
  {
    /* loc_11E78 */
    if (W(c->d[3]) == 0xFFFF) { x->declined = 1; return; }   /* dbf would wrap 64K */
    setw(&c->d[3], W(c->d[3] - 1));               /* dbf d3,loc_11E1A */
    {
      int taken = (W(c->d[3]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11E78, taken, !taken);
      if (!taken) break;
    }

    /* loc_11E1A */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d0 */
    c->a[1] += 1;
    lift_charge(x, 0x11E1A);
    lift_charge_bcc(x, 0x11E1C, c->zf);           /* beq.w loc_11E78 */
    if (c->zf) continue;

    setw(&c->d[0], alu_extw(c, c->d[0]));         /* ext.w d0 */
    lift_charge(x, 0x11E20);
    lift_charge_bcc(x, 0x11E22, !c->nf);          /* bpl.w loc_11E5C */
    if (!c->nf)
    {
      /* loc_11E5C: fold lower case to upper */
      alu_cmpb(c, 0x61, c->d[0]);                 /* cmp.b #$61,d0 */
      lift_charge(x, 0x11E5C);
      lift_charge_bcc(x, 0x11E60, c->nf != c->vf);      /* blt.w loc_11E70 */
      if (!(c->nf != c->vf))
      {
        alu_cmpb(c, 0x7A, c->d[0]);               /* cmp.b #$7A,d0 */
        lift_charge(x, 0x11E64);
        lift_charge_bcc(x, 0x11E68, !c->zf && c->nf == c->vf);  /* bgt.w loc_11E70 */
        if (!(!c->zf && c->nf == c->vf))
        {
          setb(&c->d[0], alu_addb(c, 0xE0, c->d[0]));   /* add.b #-$20,d0 */
          lift_charge(x, 0x11E6C);
        }
      }
      /* loc_11E70 */
      alu_movew(c, W(c->d[3]));                   /* move.w d3,-(sp) */
      c->a[7] -= 2;
      lift_w16(x, c->a[7], W(c->d[3]));
      lift_charge(x, 0x11E70);
      lift_call(x, 0x11E72, 4, Text_DrawBigChar); /* bsr.w sub_11E8E */
      if (x->declined) return;
      setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[7])));   /* move.w (sp)+,d3 */
      c->a[7] += 2;
      lift_charge(x, 0x11E76);
      continue;                                   /* falls into loc_11E78 */
    }

    /* negative byte: cursor/plane escape */
    setw(&c->d[0], alu_negw(c, W(c->d[0])));      /* neg.w d0 */
    lift_charge(x, 0x11E26);
    setw(&c->d[6], alu_movew(c, W(c->d[0])));     /* move.w d0,d6 */
    lift_charge(x, 0x11E28);
    setw(&c->d[6], alu_aslw(c, W(c->d[6]), 8));   /* asl.w #8,d6 */
    lift_charge(x, 0x11E2A);
    setw(&c->d[6], alu_aslw(c, W(c->d[6]), 1));   /* asl.w #1,d6 */
    lift_charge(x, 0x11E2C);
    setw(&c->d[6], alu_andw(c, 0xF800, W(c->d[6])));      /* and.w #$F800,d6 */
    lift_charge(x, 0x11E2E);
    {
      uint32_t v = W(c->d[6]);                    /* move.w d6,($B02C).w */
      alu_movew(c, v);
      lift_w16(x, 0xFFB02C, v);
      lift_charge(x, 0x11E32);
    }
    setw(&c->d[6], alu_addw(c, lift_r16(x, 0xFFB010), W(c->d[6])));  /* add.w ($B010).w,d6 */
    lift_charge(x, 0x11E36);
    setw(&c->d[0], alu_andw(c, 3, W(c->d[0])));   /* and.w #3,d0 */
    lift_charge(x, 0x11E3A);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));   /* asl.w #2,d0 */
    lift_charge(x, 0x11E3E);
    setw(&c->d[0], alu_subw(c, 4, W(c->d[0])));   /* subq.w #4,d0 */
    lift_charge(x, 0x11E40);
    {
      uint32_t v = W(c->d[0]);                    /* move.w d0,($B02E).w */
      alu_movew(c, v);
      lift_w16(x, T_PLANE_SEL, v);
      lift_charge(x, 0x11E42);
    }
    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d4 */
    c->a[1] += 1;
    lift_charge(x, 0x11E46);
    setw(&c->d[4], alu_extw(c, c->d[4]));         /* ext.w d4 */
    lift_charge(x, 0x11E48);
    {
      uint32_t v = W(c->d[4]);                    /* move.w d4,($B028).w */
      alu_movew(c, v);
      lift_w16(x, T_CURSOR_COL, v);
      lift_charge(x, 0x11E4A);
    }
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[1])));   /* move.b (a1)+,d5 */
    c->a[1] += 1;
    lift_charge(x, 0x11E4E);
    setw(&c->d[5], alu_extw(c, c->d[5]));         /* ext.w d5 */
    lift_charge(x, 0x11E50);
    {
      uint32_t v = W(c->d[5]);                    /* move.w d5,($B02A).w */
      alu_movew(c, v);
      lift_w16(x, T_CURSOR_ROW, v);
      lift_charge(x, 0x11E52);
    }
    setw(&c->d[3], alu_subw(c, 2, W(c->d[3])));   /* subq.w #2,d3 */
    lift_charge(x, 0x11E56);
    lift_charge_bcc(x, 0x11E58, 1);               /* bra.w loc_11E78 */
  }

  {
    uint32_t v = W(c->d[4]);                      /* move.w d4,($B028).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x11E7C);
    v = W(c->d[5]);                               /* move.w d5,($B02A).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_ROW, v);
    lift_charge(x, 0x11E80);
  }
  {
    int i;                                        /* movem.l (sp)+,d0-d7/a0/a2 */
    for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
    c->a[0] = lift_r32(x, c->a[7] + 32);
    c->a[2] = lift_r32(x, c->a[7] + 36);
    c->a[7] += 40;
  }
  lift_charge_movem(x, 0x11E84);
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x11E88);
  }
  lift_charge(x, 0x11E8C);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawBigInlineString (sub_11DE2)
 * Inline-argument wrapper over Text_DrawBigString — identical shape to
 * Text_DrawInlineString: the bsr return address IS the chunk, and the
 * advanced a1 is written back so control resumes past the data.
 */
void Text_DrawBigInlineString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0x11DE2);
  c->a[1] = lift_r32(x, c->a[7] + 4);             /* move.l 4(sp),a1 — movea */
  lift_charge(x, 0x11DE4);

  lift_call(x, 0x11DE8, 4, Text_DrawBigString);   /* bsr.w sub_11DF4 */
  if (x->declined) return;

  alu_movel(c, c->a[1]);                          /* move.l a1,4(sp) */
  lift_w32(x, c->a[7] + 4, c->a[1]);
  lift_charge(x, 0x11DEC);
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0x11DF0);
  lift_charge(x, 0x11DF2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* lifted elsewhere in the decomp; composed into the roster line below */
void Text_WriteTwoDigits(lift_ctx *);   /* sub_18BDC, overlay.c */
void Text_FormatClock(lift_ctx *);      /* sub_11CA2, overlay.c */

/*
 * Roster_DrawOnePlayerLine (sub_12E12)
 *   in: d0.w = roster slot index, a2 = team block
 * Draws one player line of a roster/stat screen: the player's number is
 * pulled from the $1E(a2) name blob (walked slot by slot, 8 bytes plus
 * each entry's own length word), written as two digits at $FFFFBFA6,
 * the $FFFFBFA4 field width is set to 4, and the assembled chunk is
 * drawn twice around a Text_FormatClock of the slot's $66 attribute word
 * (masked to 11 bits). Bails through the shared far rts once the cursor
 * row passes 10.
 */
void Roster_DrawOnePlayerLine(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_cmpw(c, 0xA, lift_r16(x, T_CURSOR_ROW));    /* cmp.w #$A,($B02A).w */
  lift_charge(x, 0x12E12);
  lift_charge_bcc(x, 0x12E18, !c->cf && !c->zf);  /* bhi.w locret_15464 */
  if (!c->cf && !c->zf)
  {
    lift_charge(x, 0x15464);                      /* shared far rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2] + 0x66 + SEW(c->d[0]))));  /* move.w $66(a2,d0.w),d2 */
  lift_charge(x, 0x12E1C);
  setw(&c->d[2], alu_andw(c, 0x7FF, W(c->d[2]))); /* and.w #$7FF,d2 */
  lift_charge(x, 0x12E20);
  c->a[1] = lift_r32(x, c->a[2] + 0x1E);          /* move.l $1E(a2),a1 — movea */
  lift_charge(x, 0x12E24);
  c->a[1] += SEW(lift_r16(x, c->a[1]));           /* add.w (a1),a1 — adda */
  lift_charge(x, 0x12E28);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));     /* lsr.w #1,d0 */
  lift_charge(x, 0x12E2A);
  if (W(c->d[0]) == 0xFFFF) { x->declined = 1; return; }  /* dbf would wrap 64K */

  do
  {
    /* loc_12E2C */
    c->a[1] += SEW(lift_r16(x, c->a[1]));         /* add.w (a1),a1 */
    lift_charge(x, 0x12E2C);
    c->a[1] += 8;                                 /* addq.w #8,a1 — no CCR */
    lift_charge(x, 0x12E2E);
    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_12E2C */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x12E30, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x12E34);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[1] - 8)));   /* move.b -8(a1),d0 */
  lift_charge(x, 0x12E36);
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* move.w ($B028).w,-(sp) */
    alu_movew(c, v);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0x12E3A);
  }
  c->a[1] = 0xFFFFBFA6;                           /* move.w #$BFA6,a1 — sign-extends */
  lift_charge(x, 0x12E3E);
  lift_call(x, 0x12E42, 4, Text_WriteTwoDigits);  /* bsr.w sub_18BDC */
  if (x->declined) return;
  c->a[1] = 0xFFFFBFA4;                           /* move.w #$BFA4,a1 */
  lift_charge(x, 0x12E46);
  alu_movew(c, 4);                                /* move.w #4,(a1) */
  lift_w16(x, c->a[1], 4);
  lift_charge(x, 0x12E4A);

  lift_call(x, 0x12E4E, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, W(c->d[2])));       /* move.w d2,d0 */
  lift_charge(x, 0x12E52);
  lift_call(x, 0x12E54, 4, Text_FormatClock);     /* bsr.w sub_11CA2 */
  if (x->declined) return;
  lift_call(x, 0x12E58, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;

  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x12E5C);
    v = lift_r16(x, T_CURSOR_ROW);                /* addq.w #1,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
    lift_charge(x, 0x12E60);
  }
  lift_charge(x, 0x12E64);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_DrawLinesBothStates (sub_12DDE)
 *   in: a2 = team block
 * Walks the $9A(a2) slot-order list twice — first drawing every slot
 * whose $66 attribute bit 6 is CLEAR, then every slot where it is SET —
 * so the two groups come out in list order but grouped. Each list ends
 * at the first negative byte; the second pass returns through the shared
 * far rts at $15464.
 */
void Roster_DrawLinesBothStates(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[0] = c->a[2] + 0x9A;                       /* lea $9A(a2),a0 — no CCR */
  lift_charge(x, 0x12DDE);

  for (;;)
  {
    /* loc_12DE2 */
    setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
    lift_charge(x, 0x12DE2);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0x12DE4);
    lift_charge_bcc(x, 0x12DE6, c->nf);           /* bmi.w loc_12DF8 */
    if (c->nf) break;

    alu_btst(c, lift_r8(x, c->a[2] + 0x66 + SEW(c->d[0])), 6);  /* btst #6,$66(a2,d0.w) */
    lift_charge(x, 0x12DEA);
    lift_charge_bcc(x, 0x12DF0, !c->zf);          /* bne.s loc_12DE2 */
    if (!c->zf) continue;

    lift_call(x, 0x12DF2, 4, Roster_DrawOnePlayerLine);  /* bsr.w sub_12E12 */
    if (x->declined) return;
    lift_charge_bcc(x, 0x12DF6, 1);               /* bra.s loc_12DE2 */
  }

  /* loc_12DF8 */
  c->a[0] = c->a[2] + 0x9A;                       /* lea $9A(a2),a0 */
  lift_charge(x, 0x12DF8);

  for (;;)
  {
    /* loc_12DFC */
    setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
    lift_charge(x, 0x12DFC);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0x12DFE);
    lift_charge_bcc(x, 0x12E00, c->nf);           /* bmi.w locret_15464 */
    if (c->nf) break;

    alu_btst(c, lift_r8(x, c->a[2] + 0x66 + SEW(c->d[0])), 6);  /* btst #6,$66(a2,d0.w) */
    lift_charge(x, 0x12E04);
    lift_charge_bcc(x, 0x12E0A, c->zf);           /* beq.s loc_12DFC */
    if (c->zf) continue;

    lift_call(x, 0x12E0C, 4, Roster_DrawOnePlayerLine);  /* bsr.w sub_12E12 */
    if (x->declined) return;
    lift_charge_bcc(x, 0x12E10, 1);               /* bra.s loc_12DFC */
  }

  lift_charge(x, 0x15464);                        /* shared far rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Text_FormatFixedWidthDecimal(lift_ctx *);   /* sub_11D3A, overlay.c */

/*
 * Text_DrawIndexedChunk (sub_16384)
 *   in: d0.w = index into the $16396 pointer table
 * Points a1 at the table entry (NOT at the pointer's target — the ROM
 * feeds Text_DrawString the dc.l itself, whose high word doubles as the
 * chunk length) and draws it.
 */
void Text_DrawIndexedChunk(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = 0x00016396;                           /* move.l #off_16396,a1 */
  lift_charge(x, 0x16384);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0x1638A);
  c->a[1] += SEW(c->d[0]);                        /* add.w d0,a1 — adda */
  lift_charge(x, 0x1638C);
  lift_call(x, 0x1638E, 6, Text_DrawString);      /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_charge(x, 0x16394);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawCenteredList (sub_FF2C8)
 *   in: a1 = first chunk of a run of packed chunks
 * Clears a 7-row band of the text plane (32 wide, tile $7FF) starting at
 * the $FFFFBD20-derived row, then draws each chunk of the run centred on
 * column $11 (col = $11 - (len/2)), one row apart mod 32, stopping when
 * the next chunk's second word is negative.
 */
void Text_DrawCenteredList(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFBD20)));   /* move.w ($BD20).w,d0 */
  lift_charge(x, 0xFF2C8);
  setw(&c->d[0], alu_asrw(c, W(c->d[0]), 3));     /* asr.w #3,d0 */
  lift_charge(x, 0xFF2CC);
  setw(&c->d[0], alu_addw(c, 0x1C, W(c->d[0])));  /* add.w #$1C,d0 */
  lift_charge(x, 0xFF2CE);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));  /* and.w #$1F,d0 */
  lift_charge(x, 0xFF2D2);
  {
    uint32_t v = W(c->d[0]);                      /* move.w d0,($B02A).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_ROW, v);
    lift_charge(x, 0xFF2D6);
    alu_movew(c, v);                              /* move.w d0,-(sp) */
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0xFF2DA);
  }
  alu_movew(c, 0);                                /* clr.w ($B028).w */
  lift_w16(x, T_CURSOR_COL, 0);
  lift_charge(x, 0xFF2DC);
  c->d[0] = alu_moveql(c, 0x20);                  /* moveq #$20,d0 */
  lift_charge(x, 0xFF2E0);
  c->d[1] = alu_moveql(c, 6);                     /* moveq #6,d1 */
  lift_charge(x, 0xFF2E2);
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0xFF2E4);
  lift_call(x, 0xFF2E8, 6, Text_FillRows);        /* jsr sub_1197E */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B02A).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_ROW, v);
    lift_charge(x, 0xFF2EE);
  }

  do
  {
    /* loc_FF2F2 */
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d0 */
    lift_charge(x, 0xFF2F2);
    setw(&c->d[0], alu_asrw(c, W(c->d[0]), 1));   /* asr.w #1,d0 */
    lift_charge(x, 0xFF2F4);
    setw(&c->d[0], alu_negw(c, W(c->d[0])));      /* neg.w d0 */
    lift_charge(x, 0xFF2F6);
    setw(&c->d[0], alu_addw(c, 0x11, W(c->d[0])));    /* add.w #$11,d0 */
    lift_charge(x, 0xFF2F8);
    {
      uint32_t v = W(c->d[0]);                    /* move.w d0,($B028).w */
      alu_movew(c, v);
      lift_w16(x, T_CURSOR_COL, v);
      lift_charge(x, 0xFF2FC);
    }
    lift_call(x, 0xFF300, 6, Text_DrawString);    /* jsr sub_11BA4 */
    if (x->declined) return;
    {
      uint32_t v = lift_r16(x, T_CURSOR_ROW);     /* addq.w #1,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
      lift_charge(x, 0xFF306);
      v = lift_r16(x, T_CURSOR_ROW);              /* and.w #$1F,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_andw(c, 0x1F, v));
      lift_charge(x, 0xFF30A);
    }
    alu_tstw(c, lift_r16(x, c->a[1] + 2));        /* tst.w 2(a1) */
    lift_charge(x, 0xFF310);
    lift_charge_bcc(x, 0xFF314, !c->nf);          /* bpl.s loc_FF2F2 */
  } while (!c->nf);

  lift_charge(x, 0xFF316);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawLabelAndNumber (sub_13454)
 *   in: 4(sp).w = the column both halves start from, d0.w = index into
 *       the $30E pointer table, d1.w = the number to print
 * Draws a label chunk from the indexed table at the given column, then
 * the number right-aligned in 2 digits at column+$15, tail-calling
 * Text_DrawString for the formatted digits.
 */
void Text_DrawLabelAndNumber(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t v = lift_r16(x, c->a[7] + 4);        /* move.w 4(sp),($B028).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x13454);
    v = lift_r16(x, T_CURSOR_ROW);                /* addq.w #1,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
    lift_charge(x, 0x1345A);
  }
  c->a[1] = 0x0000030E;                           /* move.w #$30E,a1 */
  lift_charge(x, 0x1345E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0x13462);
  c->a[1] = lift_r32(x, c->a[1] + SEW(c->d[0]));  /* move.l (a1,d0.w),a1 — movea */
  lift_charge(x, 0x13464);
  c->a[1] += SEW(lift_r16(x, c->a[1] + 4));       /* add.w 4(a1),a1 — adda */
  lift_charge(x, 0x13468);

  lift_call(x, 0x1346C, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;

  {
    uint32_t v = lift_r16(x, c->a[7] + 4);        /* move.w 4(sp),($B028).w */
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x13470);
    v = lift_r16(x, T_CURSOR_COL);                /* add.w #$15,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_addw(c, 0x15, v));
    lift_charge(x, 0x13476);
  }
  setw(&c->d[0], alu_movew(c, W(c->d[1])));       /* move.w d1,d0 */
  lift_charge(x, 0x1347C);
  c->d[1] = alu_moveql(c, 2);                     /* moveq #2,d1 */
  lift_charge(x, 0x1347E);
  lift_call(x, 0x13480, 4, Text_FormatFixedWidthDecimal);  /* bsr.w sub_11D3A */
  if (x->declined) return;
  lift_charge_bcc(x, 0x13484, 1);                 /* bra.w sub_11BA4 — tail */
  Text_DrawString(x);
}

/*
 * Text_DrawTeamLabelAndNumber (sub_12D0E)
 *   in: a2 = team block
 * Same shape as Text_DrawLabelAndNumber but the label chunk comes from
 * the team's own $1E(a2) blob (offset by its header word and then by its
 * first entry's length), and the number is $C(a2) printed at column $1C.
 */
void Text_DrawTeamLabelAndNumber(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = lift_r32(x, c->a[2] + 0x1E);          /* move.l $1E(a2),a1 — movea */
  lift_charge(x, 0x12D0E);
  c->a[1] += SEW(lift_r16(x, c->a[1] + 4));       /* add.w 4(a1),a1 — adda */
  lift_charge(x, 0x12D12);
  c->a[1] += SEW(lift_r16(x, c->a[1]));           /* add.w (a1),a1 — adda */
  lift_charge(x, 0x12D16);

  lift_call(x, 0x12D18, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;

  alu_movew(c, 0x1C);                             /* move.w #$1C,($B028).w */
  lift_w16(x, T_CURSOR_COL, 0x1C);
  lift_charge(x, 0x12D1C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0xC)));   /* move.w $C(a2),d0 */
  lift_charge(x, 0x12D22);
  c->d[1] = alu_moveql(c, 2);                     /* moveq #2,d1 */
  lift_charge(x, 0x12D26);
  lift_call(x, 0x12D28, 4, Text_FormatFixedWidthDecimal);  /* bsr.w sub_11D3A */
  if (x->declined) return;
  lift_charge_bcc(x, 0x12D2C, 1);                 /* bra.w sub_11BA4 — tail */
  Text_DrawString(x);
}

void sub_11D06(lift_ctx *);   /* overlay.c */

/*
 * Roster_DrawTeamHeader (sub_12D30)
 *   in: a2 = team block, d0.w = scoreboard-cell offset
 * Draws the team's logo cell five columns left of the cursor, drops two
 * rows, nudges one column left when the team's $C(a2) count is >= 10,
 * then builds the team-name chunk with sub_11D06 and centres it
 * (col -= (len-2)/2 - 1) before tail-calling the tall-font writer.
 */
void Roster_DrawTeamHeader(lift_ctx *x)
{
  rcpu_t *c = x->c;

  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* move.w ($B028).w,-(sp) */
    alu_movew(c, v);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0x12D30);
    v = lift_r16(x, T_CURSOR_COL);                /* subq.w #5,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_subw(c, 5, v));
    lift_charge(x, 0x12D34);
  }
  lift_call(x, 0x12D38, 6, Text_DrawScoreboardCell);   /* jsr sub_8078 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_CURSOR_COL, v);
    lift_charge(x, 0x12D3E);
    v = lift_r16(x, T_CURSOR_ROW);                /* addq.w #2,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 2, v));
    lift_charge(x, 0x12D42);
  }
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0xC)));   /* move.w $C(a2),d0 */
  lift_charge(x, 0x12D46);
  alu_cmpw(c, 0xA, W(c->d[0]));                   /* cmp.w #$A,d0 */
  lift_charge(x, 0x12D4A);
  lift_charge_bcc(x, 0x12D4E, c->nf == c->vf);    /* bge.w loc_12D56 */
  if (c->nf == c->vf)
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* subq.w #1,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_subw(c, 1, v));
    lift_charge(x, 0x12D56);
  }
  else
    lift_charge_bcc(x, 0x12D52, 1);               /* bra.w loc_12D5A */

  /* loc_12D5A */
  lift_call(x, 0x12D5A, 4, sub_11D06);            /* bsr.w sub_11D06 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d0 */
  lift_charge(x, 0x12D5E);
  setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));     /* subq.w #2,d0 */
  lift_charge(x, 0x12D60);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));     /* lsr.w #1,d0 */
  lift_charge(x, 0x12D62);
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* sub.w d0,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_subw(c, W(c->d[0]), v));
    lift_charge(x, 0x12D64);
    v = lift_r16(x, T_CURSOR_COL);                /* addq.w #1,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
    lift_charge(x, 0x12D68);
  }
  lift_charge_bcc(x, 0x12D6C, 1);                 /* bra.w sub_11DF4 — tail */
  Text_DrawBigString(x);
}

/* --- the scoreboard message board (sub_1268A family) ------------------ */

void Board_LoadMessageTilemap(lift_ctx *x);
void Board_DrawMessageText(lift_ctx *x);
void Board_ClearMessageLine(lift_ctx *x);

/*
 * Board_AdvanceMessageQueue (sub_1268A)
 * Steps the board's message cursor ($FFFFC3F2) through the current
 * script in the $18E0C table (selected by $FFFFC3F4), pulling the next
 * entry: a negative entry is the last one (the cursor is latched to $FF
 * with `st` so the queue stops advancing) and is negated back. The low
 * byte times the message ($FFFFC3F0 = entry * 8, doubled when both bit 0
 * and bit 3 of $FFFFC2FA are set); the high byte is the message id
 * handed to Board_LoadMessageTilemap. An already-exhausted queue
 * ($C3F2 negative) skips straight to the loader with id $40.
 */
void Board_AdvanceMessageQueue(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 20;                                  /* movem.l d0-d2/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_w32(x, c->a[7] + 16, c->a[1]);
  lift_charge_movem(x, 0x1268A);

  c->d[0] = alu_moveql(c, 0x40);                  /* moveq #$40,d0 */
  lift_charge(x, 0x1268E);
  alu_tstw(c, lift_r16(x, 0xFFC3F2));             /* tst.w ($C3F2).w */
  lift_charge(x, 0x12690);
  lift_charge_bcc(x, 0x12694, c->nf);             /* bmi.w loc_126EE */
  if (!c->nf)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFC3F2)));   /* move.w ($C3F2).w,d0 */
    lift_charge(x, 0x12698);
    {
      uint32_t v = lift_r16(x, 0xFFC3F2);         /* addq.w #2,($C3F2).w */
      lift_w16(x, 0xFFC3F2, alu_addw(c, 2, v));
      lift_charge(x, 0x1269C);
    }
    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFC3F4)));   /* move.w ($C3F4).w,d1 */
    lift_charge(x, 0x126A0);
    c->a[0] = 0x00018E0C;                         /* move.l #word_18E0C,a0 */
    lift_charge(x, 0x126A4);
    c->a[0] += SEW(lift_r16(x, c->a[0] + SEW(c->d[1])));   /* add.w (a0,d1.w),a0 — adda */
    lift_charge(x, 0x126AA);
    c->a[0] += 2;                                 /* addq.w #2,a0 */
    lift_charge(x, 0x126AE);
    c->a[0] += SEW(lift_r16(x, c->a[0]));         /* add.w (a0),a0 — adda */
    lift_charge(x, 0x126B0);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[0]))));  /* move.w (a0,d0.w),d0 */
    lift_charge(x, 0x126B2);
    lift_charge_bcc(x, 0x126B6, !c->nf);          /* bpl.w loc_126C0 */
    if (c->nf)
    {
      setw(&c->d[0], alu_negw(c, W(c->d[0])));    /* neg.w d0 */
      lift_charge(x, 0x126BA);
      lift_w8(x, 0xFFC3F2, 0xFF);                 /* st ($C3F2).w — no CCR */
      lift_charge(x, 0x126BC);
    }

    /* loc_126C0 */
    setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
    lift_charge(x, 0x126C0);
    setb(&c->d[1], alu_moveb(c, c->d[0]));        /* move.b d0,d1 */
    lift_charge(x, 0x126C2);
    setw(&c->d[1], alu_aslw(c, W(c->d[1]), 3));   /* asl.w #3,d1 */
    lift_charge(x, 0x126C4);
    {
      uint32_t v = W(c->d[1]);                    /* move.w d1,($C3F0).w */
      alu_movew(c, v);
      lift_w16(x, 0xFFC3F0, v);
      lift_charge(x, 0x126C6);
    }
    alu_btst(c, lift_r8(x, 0xFFC2FA), 0);         /* btst #0,($C2FA).w */
    lift_charge(x, 0x126CA);
    lift_charge_bcc(x, 0x126D0, c->zf);           /* beq.w loc_126EC */
    if (!c->zf)
    {
      alu_btst(c, lift_r8(x, 0xFFC2FA), 3);       /* btst #3,($C2FA).w */
      lift_charge(x, 0x126D4);
      lift_charge_bcc(x, 0x126DA, c->zf);         /* beq.w loc_126EC */
      if (!c->zf)
      {
        alu_movew(c, W(c->d[0]));                 /* move.w d0,-(sp) */
        c->a[7] -= 2;
        lift_w16(x, c->a[7], W(c->d[0]));
        lift_charge(x, 0x126DE);
        setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFC3F0)));   /* move.w ($C3F0).w,d0 */
        lift_charge(x, 0x126E0);
        setw(&c->d[0], alu_addw(c, W(c->d[0]), W(c->d[0])));   /* add.w d0,d0 */
        lift_charge(x, 0x126E4);
        {
          uint32_t v = W(c->d[0]);                /* move.w d0,($C3F0).w */
          alu_movew(c, v);
          lift_w16(x, 0xFFC3F0, v);
          lift_charge(x, 0x126E6);
        }
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));    /* move.w (sp)+,d0 */
        c->a[7] += 2;
        lift_charge(x, 0x126EA);
      }
    }
    /* loc_126EC */
    setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 8));   /* lsr.w #8,d0 */
    lift_charge(x, 0x126EC);
  }

  /* loc_126EE */
  lift_call(x, 0x126EE, 4, Board_LoadMessageTilemap);   /* bsr.w sub_126F8 */
  if (x->declined) return;

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[1] = lift_r32(x, c->a[7] + 16);
  c->a[7] += 20;
  lift_charge_movem(x, 0x126F2);
  lift_charge(x, 0x126F6);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_LoadMessageTilemap (sub_126F8)
 *   in: d0.w = message id, or $40 for "no message"
 * Copies the message's 56-word tilemap from the $5C408 blob (or $5CF64
 * when the rink is flipped, bit 7 of $FFFFC2EC) into the $FFFFC3F6 stage
 * buffer, biasing every word by $FFFFB026 (plus $8000 when NOT flipped),
 * and raises the "board dirty" bit 1 of $FFFFC2EE. Id $40 instead blanks
 * the buffer to tile $7FF — but only when unflipped; a flipped board
 * skips straight to the text pass with d0 = -1.
 */
void Board_LoadMessageTilemap(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 20;                                  /* movem.l d0-d2/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_w32(x, c->a[7] + 16, c->a[1]);
  lift_charge_movem(x, 0x126F8);

  alu_cmpw(c, 0x40, W(c->d[0]));                  /* cmp.w #$40,d0 */
  lift_charge(x, 0x126FC);
  lift_charge_bcc(x, 0x12700, c->zf);             /* beq.w loc_12750 */
  if (!c->zf)
  {
    c->d[0] = alu_mulu(c, 0x70, c->d[0]);         /* mulu.w #$70,d0 */
    lift_charge_mulu(x, 0x12704, 0x70);
    c->a[0] = 0x0005C408;                         /* move.l #unk_5C408,a0 */
    lift_charge(x, 0x12708);
    alu_btst(c, lift_r8(x, 0xFFC2EC), 7);         /* btst #7,($C2EC).w */
    lift_charge(x, 0x1270E);
    lift_charge_bcc(x, 0x12714, c->zf);           /* beq.w loc_1271E */
    if (!c->zf)
    {
      c->a[0] = 0x0005CF64;                       /* move.l #unk_5CF64,a0 */
      lift_charge(x, 0x12718);
    }
    /* loc_1271E */
    c->a[0] += lift_r32(x, c->a[0] + 4);          /* add.l 4(a0),a0 — adda */
    lift_charge(x, 0x1271E);
    c->a[0] += 4;                                 /* addq.w #4,a0 */
    lift_charge(x, 0x12722);
    c->a[0] += SEW(c->d[0]);                      /* add.w d0,a0 — adda */
    lift_charge(x, 0x12724);
    c->a[1] = 0xFFFFC3F6;                         /* move.w #$C3F6,a1 — sign-extends */
    lift_charge(x, 0x12726);
    setw(&c->d[2], alu_movew(c, lift_r16(x, 0xFFB026)));   /* move.w ($B026).w,d2 */
    lift_charge(x, 0x1272A);
    alu_btst(c, lift_r8(x, 0xFFC2EC), 7);         /* btst #7,($C2EC).w */
    lift_charge(x, 0x1272E);
    lift_charge_bcc(x, 0x12734, !c->zf);          /* bne.w loc_1273C */
    if (c->zf)
    {
      setw(&c->d[2], alu_orw(c, 0x8000, W(c->d[2])));   /* or.w #$8000,d2 */
      lift_charge(x, 0x12738);
    }
    /* loc_1273C */
    c->d[0] = alu_moveql(c, 0x37);                /* moveq #$37,d0 */
    lift_charge(x, 0x1273C);

    do
    {
      /* loc_1273E */
      uint32_t v = lift_r16(x, c->a[0]);          /* move.w (a0)+,(a1) */
      c->a[0] += 2;
      alu_movew(c, v);
      lift_w16(x, c->a[1], v);
      lift_charge(x, 0x1273E);
      v = lift_r16(x, c->a[1]);                   /* add.w d2,(a1)+ */
      lift_w16(x, c->a[1], alu_addw(c, W(c->d[2]), v));
      c->a[1] += 2;
      lift_charge(x, 0x12740);
      setw(&c->d[0], W(c->d[0] - 1));             /* dbf d0,loc_1273E */
      {
        int taken = (W(c->d[0]) != 0xFFFF);
        lift_charge_dbcc(x, 0x12742, taken, !taken);
        if (!taken) break;
      }
    } while (1);

    {
      uint32_t b = lift_r8(x, 0xFFC2EE);          /* bset #1,($C2EE).w */
      lift_w8(x, 0xFFC2EE, alu_bset(c, b, 1));
      lift_charge(x, 0x12746);
    }
    lift_charge_bcc(x, 0x1274C, 1);               /* bra.w loc_12774 */
  }
  else
  {
    /* loc_12750 */
    alu_btst(c, lift_r8(x, 0xFFC2EC), 7);         /* btst #7,($C2EC).w */
    lift_charge(x, 0x12750);
    lift_charge_bcc(x, 0x12756, !c->zf);          /* bne.w loc_1276E */
    if (c->zf)
    {
      c->a[1] = 0xFFFFC3F6;                       /* move.w #$C3F6,a1 */
      lift_charge(x, 0x1275A);
      c->d[0] = alu_moveql(c, 0x37);              /* moveq #$37,d0 */
      lift_charge(x, 0x1275E);

      do
      {
        /* loc_12760 */
        alu_movew(c, 0x7FF);                      /* move.w #$7FF,(a1)+ */
        lift_w16(x, c->a[1], 0x7FF);
        c->a[1] += 2;
        lift_charge(x, 0x12760);
        setw(&c->d[0], W(c->d[0] - 1));           /* dbf d0,loc_12760 */
        {
          int taken = (W(c->d[0]) != 0xFFFF);
          lift_charge_dbcc(x, 0x12764, taken, !taken);
          if (!taken) break;
        }
      } while (1);

      {
        uint32_t b = lift_r8(x, 0xFFC2EE);        /* bset #1,($C2EE).w */
        lift_w8(x, 0xFFC2EE, alu_bset(c, b, 1));
        lift_charge(x, 0x12768);
      }
    }
    /* loc_1276E */
    c->d[0] = alu_moveql(c, -1);                  /* moveq #-1,d0 */
    lift_charge(x, 0x1276E);
    lift_call(x, 0x12770, 4, Board_DrawMessageText);   /* bsr.w sub_1277A */
    if (x->declined) return;
  }

  /* loc_12774 */
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[1] = lift_r32(x, c->a[7] + 16);
  c->a[7] += 20;
  lift_charge_movem(x, 0x12774);
  lift_charge(x, 0x12778);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_DrawMessageText (sub_1277A)
 *   in: d0.w = message id (negative = the "no message" case)
 * The text half of a board update. A flipped rink ($FFFFC2EC bit 7) uses
 * the one-line path: blank the line, then, for a real message, centre
 * its string from the $18E0C table on the current column. Unflipped, a
 * negative id blanks a 13x4 block instead, and a real message is drawn
 * centred with a 3-row frame around it (the trailing space of an
 * odd-length string is trimmed from the frame width first).
 */
void Board_DrawMessageText(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 16;                                  /* movem.l d0-d2/a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0x1277A);

  alu_btst(c, lift_r8(x, 0xFFC2EC), 7);           /* btst #7,($C2EC).w */
  lift_charge(x, 0x1277E);
  lift_charge_bcc(x, 0x12784, !c->zf);            /* bne.w loc_127F6 */
  if (!c->zf)
  {
    /* loc_127F6 — flipped rink: one blanked line, then the message */
    lift_call(x, 0x127F6, 4, Board_ClearMessageLine);   /* bsr.w sub_12828 */
    if (x->declined) return;
    alu_tstw(c, W(c->d[0]));                      /* tst.w d0 */
    lift_charge(x, 0x127FA);
    lift_charge_bcc(x, 0x127FC, c->nf);           /* bmi.w loc_12822 */
    if (!c->nf)
    {
      lift_call(x, 0x12800, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
      if (x->declined) return;
      c->a[1] = 0x00018E0C;                       /* move.l #word_18E0C,a1 */
      lift_charge(x, 0x1280A);
      c->a[1] += SEW(lift_r16(x, c->a[1] + SEW(c->d[0])));   /* add.w (a1,d0.w),a1 */
      lift_charge(x, 0x12810);
      c->a[1] += 2;                               /* addq.w #2,a1 */
      lift_charge(x, 0x12814);
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));    /* move.w (a1),d0 */
      lift_charge(x, 0x12816);
      setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1)); /* lsr.w #1,d0 */
      lift_charge(x, 0x12818);
      {
        uint32_t v = lift_r16(x, T_CURSOR_COL);   /* sub.w d0,($B028).w */
        lift_w16(x, T_CURSOR_COL, alu_subw(c, W(c->d[0]), v));
        lift_charge(x, 0x1281A);
      }
      lift_call(x, 0x1281E, 4, Text_DrawString);  /* bsr.w sub_11BA4 */
      if (x->declined) return;
    }
  }
  else
  {
    alu_tstw(c, W(c->d[0]));                      /* tst.w d0 */
    lift_charge(x, 0x12788);
    lift_charge_bcc(x, 0x1278A, !c->nf);          /* bpl.w loc_127A8 */
    if (c->nf)
    {
      /* no message: blank a 13x4 block */
      lift_call(x, 0x1278E, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
      if (x->declined) return;
      c->d[0] = alu_moveql(c, 0xD);               /* moveq #$D,d0 */
      lift_charge(x, 0x12798);
      c->d[1] = alu_moveql(c, 3);                 /* moveq #3,d1 */
      lift_charge(x, 0x1279A);
      setw(&c->d[2], alu_movew(c, 0x7FF));        /* move.w #$7FF,d2 */
      lift_charge(x, 0x1279C);
      lift_call(x, 0x127A0, 4, Text_FillRows);    /* bsr.w sub_1197E */
      if (x->declined) return;
      lift_charge_bcc(x, 0x127A4, 1);             /* bra.w loc_12822 */
    }
    else
    {
      /* loc_127A8 — real message, framed and centred */
      lift_call(x, 0x127A8, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
      if (x->declined) return;
      c->a[1] = 0x00018E0C;                       /* move.l #word_18E0C,a1 */
      lift_charge(x, 0x127B2);
      c->a[1] += SEW(lift_r16(x, c->a[1] + SEW(c->d[0])));   /* add.w (a1,d0.w),a1 */
      lift_charge(x, 0x127B8);
      c->a[1] += 2;                               /* addq.w #2,a1 */
      lift_charge(x, 0x127BC);
      setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));    /* move.w (a1),d0 */
      lift_charge(x, 0x127BE);
      setw(&c->d[0], alu_subw(c, 2, W(c->d[0]))); /* subq.w #2,d0 */
      lift_charge(x, 0x127C0);
      lift_charge_bcc(x, 0x127C2, c->zf);         /* beq.w loc_12822 */
      if (!c->zf)
      {
        setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));   /* lsr.w #1,d0 */
        lift_charge(x, 0x127C6);
        {
          uint32_t v = lift_r16(x, T_CURSOR_COL); /* sub.w d0,($B028).w */
          lift_w16(x, T_CURSOR_COL, alu_subw(c, W(c->d[0]), v));
          lift_charge(x, 0x127C8);
        }
        lift_charge_bcc(x, 0x127CC, !c->nf);      /* bpl.w loc_127D4 */
        if (c->nf)
        {
          alu_movew(c, 0);                        /* clr.w ($B028).w */
          lift_w16(x, T_CURSOR_COL, 0);
          lift_charge(x, 0x127D0);
        }
        /* loc_127D4 */
        setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d0 */
        lift_charge(x, 0x127D4);
        alu_tstb(c, lift_r8(x, c->a[1] - 1 + SEW(c->d[0])));  /* tst.b -1(a1,d0.w) */
        lift_charge(x, 0x127D6);
        lift_charge_bcc(x, 0x127DA, !c->zf);      /* bne.w loc_127E0 */
        if (c->zf)
        {
          setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));   /* subq.w #1,d0 */
          lift_charge(x, 0x127DE);
        }
        /* loc_127E0 */
        c->d[1] = alu_moveql(c, 3);               /* moveq #3,d1 */
        lift_charge(x, 0x127E0);
        lift_call(x, 0x127E2, 4, Text_DrawFrame); /* bsr.w sub_119B8 */
        if (x->declined) return;
        {
          uint32_t v = lift_r16(x, T_CURSOR_ROW); /* subq.w #2,($B02A).w */
          lift_w16(x, T_CURSOR_ROW, alu_subw(c, 2, v));
          lift_charge(x, 0x127E6);
          v = lift_r16(x, T_CURSOR_COL);          /* addq.w #1,($B028).w */
          lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
          lift_charge(x, 0x127EA);
        }
        lift_call(x, 0x127EE, 4, Text_DrawString);    /* bsr.w sub_11BA4 */
        if (x->declined) return;
        lift_charge_bcc(x, 0x127F2, 1);           /* bra.w loc_12822 */
      }
    }
  }

  /* loc_12822 */
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x12822);
  lift_charge(x, 0x12826);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_ClearMessageLine (sub_12828)
 * Draws the inline 22-space chunk that blanks the board's message line.
 * The whole routine is one inline-argument call plus the rts that lives
 * past the data.
 */
void Board_ClearMessageLine(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x12828, 4, Text_DrawInlineString);   /* bsr.w sub_11B92 + $1C inline */
  if (x->declined) return;
  lift_charge(x, 0x12848);                        /* rts (past the inline data) */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_UpdateForNearestPlayer (sub_E62E)
 * The gameplay-side trigger for the message board: runs only while the
 * board is enabled ($FFFFC2EA bit 5) and not suppressed ($FFFFC2F2 bit
 * 2), both of which bail through the shared far rts.
 *
 * Scans six on-ice object slots from $FFFFB04A (or +$300 when the
 * object's camera zone $52(a3) is >= 6) looking for one whose height
 * $34(a0) is non-negative and whose X $14(a0) is on the near side of the
 * $5C threshold — the sign of the threshold and the direction of both
 * comparisons flip with the object's facing bit ($62(a3) bit 7), and the
 * $FFFFB75E camera edge short-circuits the whole scan.
 *
 * Found: id 6 and set the board-active bit 7 of $FFFFC2EE. Not found:
 * id $40 and clear it. Either way, the load only runs on the EDGE (the
 * bset/bclr's own Z) or when the message timer $FFFFC3F0 has gone
 * negative — otherwise the board is left alone.
 */
void Board_UpdateForNearestPlayer(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFC2EA), 5);           /* btst #5,($C2EA).w */
  lift_charge(x, 0x0E62E);
  lift_charge_bcc(x, 0x0E634, c->zf);             /* beq.w locret_15464 */
  if (c->zf) goto far_rts;
  alu_btst(c, lift_r8(x, 0xFFC2F2), 2);           /* btst #2,($C2F2).w */
  lift_charge(x, 0x0E638);
  lift_charge_bcc(x, 0x0E63E, !c->zf);            /* bne.w locret_15464 */
  if (!c->zf) goto far_rts;

  c->a[7] -= 12;                                  /* movem.l d0-d1/a0,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[0]);
  lift_charge_movem(x, 0x0E642);

  c->d[0] = alu_moveql(c, 5);                     /* moveq #5,d0 */
  lift_charge(x, 0x0E646);
  c->a[0] = 0xFFFFB04A;                           /* move.w #$B04A,a0 — sign-extends */
  lift_charge(x, 0x0E648);
  alu_cmpw(c, 6, lift_r16(x, c->a[3] + 0x52));    /* cmp.w #6,$52(a3) */
  lift_charge(x, 0x0E64C);
  lift_charge_bcc(x, 0x0E652, c->nf != c->vf);    /* blt.w loc_E65A */
  if (!(c->nf != c->vf))
  {
    c->a[0] += 0x300;                             /* add.w #$300,a0 — adda */
    lift_charge(x, 0x0E656);
  }

  /* loc_E65A */
  c->d[1] = alu_moveql(c, 0x5C);                  /* moveq #$5C,d1 */
  lift_charge(x, 0x0E65A);
  alu_btst(c, lift_r8(x, c->a[3] + 0x62), 7);     /* btst #7,$62(a3) */
  lift_charge(x, 0x0E65C);
  lift_charge_bcc(x, 0x0E662, !c->zf);            /* bne.w loc_E6B0 */
  if (!c->zf) goto loc_E6B0;

  setw(&c->d[1], alu_negw(c, W(c->d[1])));        /* neg.w d1 */
  lift_charge(x, 0x0E666);
  alu_cmpw(c, lift_r16(x, 0xFFB75E), W(c->d[1])); /* cmp.w ($B75E).w,d1 */
  lift_charge(x, 0x0E668);
  lift_charge_bcc(x, 0x0E66C, !c->zf && c->nf == c->vf);   /* bgt.w loc_E688 */
  if (!c->zf && c->nf == c->vf) goto loc_E688;

  do
  {
    /* loc_E670 */
    alu_tstw(c, lift_r16(x, c->a[0] + 0x34));     /* tst.w $34(a0) */
    lift_charge(x, 0x0E670);
    lift_charge_bcc(x, 0x0E674, c->nf);           /* bmi.w loc_E680 */
    if (!c->nf)
    {
      alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1]));   /* cmp.w $14(a0),d1 */
      lift_charge(x, 0x0E678);
      lift_charge_bcc(x, 0x0E67C, !c->zf && c->nf == c->vf);  /* bgt.w loc_E69A */
      if (!c->zf && c->nf == c->vf) goto loc_E69A;
    }
    /* loc_E680 */
    c->a[0] += 0x80;                              /* add.w #$80,a0 — adda */
    lift_charge(x, 0x0E680);
    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_E670 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x0E684, taken, !taken);
      if (!taken) break;
    }
  } while (1);

loc_E688:
  c->d[0] = alu_moveql(c, 0x40);                  /* moveq #$40,d0 */
  lift_charge(x, 0x0E688);
  {
    uint32_t b = lift_r8(x, 0xFFC2EE);            /* bclr #7,($C2EE).w */
    lift_w8(x, 0xFFC2EE, alu_bclr(c, b, 7));
    lift_charge(x, 0x0E68A);
  }
  lift_charge_bcc(x, 0x0E690, !c->zf);            /* bne.w loc_E6A4 */
  if (!c->zf) goto loc_E6A4;
  goto loc_E694;

loc_E69A:
  c->d[0] = alu_moveql(c, 6);                     /* moveq #6,d0 */
  lift_charge(x, 0x0E69A);
  {
    uint32_t b = lift_r8(x, 0xFFC2EE);            /* bset #7,($C2EE).w */
    lift_w8(x, 0xFFC2EE, alu_bset(c, b, 7));
    lift_charge(x, 0x0E69C);
  }
  lift_charge_bcc(x, 0x0E6A2, !c->zf);            /* bne.s loc_E694 */
  if (!c->zf) goto loc_E694;

loc_E6A4:
  alu_tstw(c, lift_r16(x, 0xFFC3F0));             /* tst.w ($C3F0).w */
  lift_charge(x, 0x0E6A4);
  lift_charge_bcc(x, 0x0E6A8, !c->nf);            /* bpl.s loc_E694 */
  if (!c->nf) goto loc_E694;
  lift_call(x, 0x0E6AA, 4, Board_LoadMessageTilemap);   /* bsr.w sub_126F8 */
  if (x->declined) return;
  lift_charge_bcc(x, 0x0E6AE, 1);                 /* bra.s loc_E694 */
  goto loc_E694;

loc_E6B0:
  alu_cmpw(c, lift_r16(x, 0xFFB75E), W(c->d[1])); /* cmp.w ($B75E).w,d1 */
  lift_charge(x, 0x0E6B0);
  lift_charge_bcc(x, 0x0E6B4, c->nf != c->vf);    /* blt.s loc_E688 */
  if (c->nf != c->vf) goto loc_E688;

  do
  {
    /* loc_E6B6 */
    alu_tstw(c, lift_r16(x, c->a[0] + 0x34));     /* tst.w $34(a0) */
    lift_charge(x, 0x0E6B6);
    lift_charge_bcc(x, 0x0E6BA, c->nf);           /* bmi.w loc_E6C4 */
    if (!c->nf)
    {
      alu_cmpw(c, lift_r16(x, c->a[0] + 0x14), W(c->d[1]));   /* cmp.w $14(a0),d1 */
      lift_charge(x, 0x0E6BE);
      lift_charge_bcc(x, 0x0E6C2, c->nf != c->vf);           /* blt.s loc_E69A */
      if (c->nf != c->vf) goto loc_E69A;
    }
    /* loc_E6C4 */
    c->a[0] += 0x80;                              /* add.w #$80,a0 — adda */
    lift_charge(x, 0x0E6C4);
    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_E6B6 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x0E6C8, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  lift_charge_bcc(x, 0x0E6CC, 1);                 /* bra.s loc_E688 */
  goto loc_E688;

loc_E694:
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d1/a0 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x0E694);
  lift_charge(x, 0x0E698);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  return;

far_rts:
  lift_charge(x, 0x15464);                        /* shared far rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_SetupStatusBanner (sub_134E4)
 * Positions the status banner: draws the inline "$BD row 3 col $17"
 * cursor chunk, and — unless the rink-flip bit 0 of $FFFFC2EC is set —
 * a second inline chunk at the mirrored $BF column. Returns the banner's
 * extent in d0/d1 ($1A wide, 5 tall) for the caller's fill or frame.
 */
void Text_SetupStatusBanner(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x134E4, 4, Text_DrawInlineString);   /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;
  alu_btst(c, lift_r8(x, 0xFFC2EC), 0);           /* btst #0,($C2EC).w */
  lift_charge(x, 0x134EE);
  lift_charge_bcc(x, 0x134F4, !c->zf);            /* bne.w loc_13502 */
  if (c->zf)
  {
    lift_call(x, 0x134F8, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
    if (x->declined) return;
  }
  /* loc_13502 */
  c->d[0] = alu_moveql(c, 0x1A);                  /* moveq #$1A,d0 */
  lift_charge(x, 0x13502);
  c->d[1] = alu_moveql(c, 5);                     /* moveq #5,d1 */
  lift_charge(x, 0x13504);
  lift_charge(x, 0x13506);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_ClearStatusBanner (sub_134D8)
 * Positions the banner with Text_SetupStatusBanner, then blanks its
 * $1A x 5 extent to tile $7FF by tail-calling Text_FillRows.
 */
void Text_ClearStatusBanner(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x134D8, 4, Text_SetupStatusBanner);   /* bsr.w sub_134E4 */
  if (x->declined) return;
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0x134DC);
  lift_charge_bcc(x, 0x134E0, 1);                 /* bra.w sub_1197E — tail */
  Text_FillRows(x);
}

/*
 * Stat_DrawParenthesisedCount — the shared tail of sub_FEAE4/sub_FEAFA
 * (loc_FEB0C). Both entries have already drawn the " (" chunk and biased
 * a2 to their own byte table; this reads the count at (a2,d0.w), picks
 * the field width from its magnitude (1 digit up to 9, 2 up to $63,
 * else 3), formats it, draws it, then draws the closing ")" chunk and
 * parks the cursor at column $E of the next row.
 */
static void Stat_DrawParenthesisedCount(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]))));  /* move.b (a2,d0.w),d0 */
  lift_charge(x, 0xFEB0C);
  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0xFEB10);
  setw(&c->d[1], alu_movew(c, 1));                /* move.w #1,d1 */
  lift_charge(x, 0xFEB12);
  alu_cmpw(c, 9, W(c->d[0]));                     /* cmp.w #9,d0 */
  lift_charge(x, 0xFEB16);
  lift_charge_bcc(x, 0xFEB1A, c->zf || c->nf != c->vf);   /* ble.w loc_FEB2E */
  if (!(c->zf || c->nf != c->vf))
  {
    setw(&c->d[1], alu_movew(c, 2));              /* move.w #2,d1 */
    lift_charge(x, 0xFEB1E);
    alu_cmpw(c, 0x63, W(c->d[0]));                /* cmp.w #$63,d0 */
    lift_charge(x, 0xFEB22);
    lift_charge_bcc(x, 0xFEB26, c->zf || c->nf != c->vf); /* ble.w loc_FEB2E */
    if (!(c->zf || c->nf != c->vf))
    {
      setw(&c->d[1], alu_movew(c, 3));            /* move.w #3,d1 */
      lift_charge(x, 0xFEB2A);
    }
  }

  /* loc_FEB2E */
  lift_call(x, 0xFEB2E, 6, Text_FormatFixedWidthDecimal);   /* jsr sub_11D3A */
  if (x->declined) return;
  lift_call(x, 0xFEB34, 6, Text_DrawString);      /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_call(x, 0xFEB3A, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 4 inline */
  if (x->declined) return;

  {
    uint32_t v = lift_r16(x, T_CURSOR_ROW);       /* addq.w #1,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
    lift_charge(x, 0xFEB44);
    alu_movew(c, 0xE);                            /* move.w #$E,($B028).w */
    lift_w16(x, T_CURSOR_COL, 0xE);
    lift_charge(x, 0xFEB48);
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0/a2 */
  c->a[2] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0xFEB4E);
  lift_charge(x, 0xFEB52);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawCountAtCE (sub_FEAE4) — the $CE-offset entry of the pair.
 */
void Stat_DrawCountAtCE(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 8;                                   /* movem.l d0/a2,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->a[2]);
  lift_charge_movem(x, 0xFEAE4);
  lift_call(x, 0xFEAE8, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 4 inline */
  if (x->declined) return;
  c->a[2] += 0xCE;                                /* add.w #$CE,a2 — adda */
  lift_charge(x, 0xFEAF2);
  lift_charge_bcc(x, 0xFEAF6, 1);                 /* bra.w loc_FEB0C */
  Stat_DrawParenthesisedCount(x);
}

/*
 * Stat_DrawCountAtB4 (sub_FEAFA) — the $B4-offset entry; falls straight
 * into the shared tail rather than branching to it.
 */
void Stat_DrawCountAtB4(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 8;                                   /* movem.l d0/a2,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->a[2]);
  lift_charge_movem(x, 0xFEAFA);
  lift_call(x, 0xFEAFE, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 4 inline */
  if (x->declined) return;
  c->a[2] += 0xB4;                                /* add.w #$B4,a2 — adda */
  lift_charge(x, 0xFEB08);
  Stat_DrawParenthesisedCount(x);                 /* falls into loc_FEB0C */
}

void sub_BA04(lift_ctx *);   /* overlay.c */

/*
 * Overlay_DrawPeriodLabel (sub_B9C2)
 * Picks the period-label row offset: $16 rows down when EXACTLY one of
 * "this is the home team block" (a2 == $FFFFC6CE) and "sides swapped"
 * ($FFFFC2EA bit 1) holds — the two `eor.w #$16,d0` cancel when both or
 * neither do. Draws the inline label chunk, applies the offset to the
 * cursor row, then asks sub_BA04 for the label's own extent, widening by
 * one row when it comes back negative. Returns d0 = 9, d1 = the height.
 */
void Overlay_DrawPeriodLabel(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x0B9C2);
  alu_cmpl(c, 0xFFFFC6CE, c->a[2]);               /* cmp.w #$C6CE,a2 — cmpa, 32-bit */
  lift_charge(x, 0x0B9C4);
  lift_charge_bcc(x, 0x0B9C8, !c->zf);            /* bne.w loc_B9D0 */
  if (c->zf)
  {
    setw(&c->d[0], alu_eorw(c, 0x16, W(c->d[0])));   /* eor.w #$16,d0 */
    lift_charge(x, 0x0B9CC);
  }
  /* loc_B9D0 */
  alu_btst(c, lift_r8(x, 0xFFC2EA), 1);           /* btst #1,($C2EA).w */
  lift_charge(x, 0x0B9D0);
  lift_charge_bcc(x, 0x0B9D6, c->zf);             /* beq.w loc_B9DE */
  if (!c->zf)
  {
    setw(&c->d[0], alu_eorw(c, 0x16, W(c->d[0])));   /* eor.w #$16,d0 */
    lift_charge(x, 0x0B9DA);
  }
  /* loc_B9DE */
  lift_call(x, 0x0B9DE, 4, Text_DrawInlineString);   /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, T_CURSOR_ROW);       /* add.w d0,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, W(c->d[0]), v));
    lift_charge(x, 0x0B9E8);
  }
  c->d[0] = alu_moveql(c, 2);                     /* moveq #2,d0 */
  lift_charge(x, 0x0B9EC);
  lift_call(x, 0x0B9EE, 4, sub_BA04);             /* bsr.w sub_BA04 */
  if (x->declined) return;
  c->d[1] = alu_moveql(c, 6);                     /* moveq #6,d1 */
  lift_charge(x, 0x0B9F2);
  alu_tstw(c, W(c->d[0]));                        /* tst.w d0 */
  lift_charge(x, 0x0B9F4);
  lift_charge_bcc(x, 0x0B9F6, !c->nf);            /* bpl.w loc_BA00 */
  if (c->nf)
  {
    setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));   /* subq.w #1,d1 */
    lift_charge(x, 0x0B9FA);
    {
      uint32_t v = lift_r16(x, T_CURSOR_ROW);     /* addq.w #1,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
      lift_charge(x, 0x0B9FC);
    }
  }
  /* loc_BA00 */
  c->d[0] = alu_moveql(c, 9);                     /* moveq #9,d0 */
  lift_charge(x, 0x0BA00);
  lift_charge(x, 0x0BA02);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_ClearOptionPanes (sub_F84FC)
 * Repaints the two option panes of a setup screen, but only when the
 * "panes dirty" bit 0 of $FFFFD42E is set — the bclr both tests and
 * consumes it, and a clear bit returns immediately. Bits 6 and 7 (the
 * per-pane highlight flags) are cleared with it.
 *
 * Bit 1 of the same byte selects the layout: set draws a narrow 8x8 pane
 * at column 3 plus a $19x8 pane at column $B, clear draws one $19x8 pane
 * at column 3 after blanking column $1C. Both variants blank to tile
 * $7FF and bracket their fills with inline cursor chunks.
 */
void Menu_ClearOptionPanes(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;

  c->a[7] -= 32;                                  /* movem.l d0-d7,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  lift_charge_movem(x, 0xF84FC);

  {
    uint32_t b = lift_r8(x, 0xFFD42E);            /* bclr #0,($D42E).w */
    lift_w8(x, 0xFFD42E, alu_bclr(c, b, 0));
    lift_charge(x, 0xF8500);
  }
  lift_charge_bcc(x, 0xF8506, c->zf);             /* beq.w loc_F85AA */
  if (!c->zf)
  {
    {
      uint32_t b = lift_r8(x, 0xFFD42E);          /* bclr #6,($D42E).w */
      lift_w8(x, 0xFFD42E, alu_bclr(c, b, 6));
      lift_charge(x, 0xF850A);
      b = lift_r8(x, 0xFFD42E);                   /* bclr #7,($D42E).w */
      lift_w8(x, 0xFFD42E, alu_bclr(c, b, 7));
      lift_charge(x, 0xF8510);
    }
    lift_call(x, 0xF8516, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;

    alu_movew(c, 5);                              /* move.w #5,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 5);
    lift_charge(x, 0xF8522);
    c->d[0] = alu_moveql(c, 8);                   /* moveq #8,d0 */
    lift_charge(x, 0xF8528);
    c->d[1] = alu_moveql(c, 8);                   /* moveq #8,d1 */
    lift_charge(x, 0xF852A);
    setw(&c->d[2], alu_movew(c, 0x7FF));          /* move.w #$7FF,d2 */
    lift_charge(x, 0xF852C);
    alu_btst(c, lift_r8(x, 0xFFD42E), 1);         /* btst #1,($D42E).w */
    lift_charge(x, 0xF8530);
    lift_charge_bcc(x, 0xF8536, c->zf);           /* beq.w loc_F8574 */
    if (!c->zf)
    {
      alu_movew(c, 3);                            /* move.w #3,($B028).w */
      lift_w16(x, T_CURSOR_COL, 3);
      lift_charge(x, 0xF853A);
      lift_call(x, 0xF8540, 6, Text_FillRows);    /* jsr sub_1197E */
      if (x->declined) return;
      lift_call(x, 0xF8546, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 6 inline */
      if (x->declined) return;
      alu_movew(c, 0xB);                          /* move.w #$B,($B028).w */
      lift_w16(x, T_CURSOR_COL, 0xB);
      lift_charge(x, 0xF8552);
      alu_movew(c, 5);                            /* move.w #5,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, 5);
      lift_charge(x, 0xF8558);
      setw(&c->d[0], alu_movew(c, 0x19));         /* move.w #$19,d0 */
      lift_charge(x, 0xF855E);
      setw(&c->d[1], alu_movew(c, 8));            /* move.w #8,d1 */
      lift_charge(x, 0xF8562);
      setw(&c->d[2], alu_movew(c, 0x7FF));        /* move.w #$7FF,d2 */
      lift_charge(x, 0xF8566);
      lift_call(x, 0xF856A, 6, Text_FillRows);    /* jsr sub_1197E */
      if (x->declined) return;
      lift_charge_bcc(x, 0xF8570, 1);             /* bra.w loc_F85AA */
    }
    else
    {
      /* loc_F8574 */
      alu_movew(c, 0x1C);                         /* move.w #$1C,($B028).w */
      lift_w16(x, T_CURSOR_COL, 0x1C);
      lift_charge(x, 0xF8574);
      lift_call(x, 0xF857A, 6, Text_FillRows);    /* jsr sub_1197E */
      if (x->declined) return;
      lift_call(x, 0xF8580, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 6 inline */
      if (x->declined) return;
      alu_movew(c, 3);                            /* move.w #3,($B028).w */
      lift_w16(x, T_CURSOR_COL, 3);
      lift_charge(x, 0xF858C);
      alu_movew(c, 5);                            /* move.w #5,($B02A).w */
      lift_w16(x, T_CURSOR_ROW, 5);
      lift_charge(x, 0xF8592);
      setw(&c->d[0], alu_movew(c, 0x19));         /* move.w #$19,d0 */
      lift_charge(x, 0xF8598);
      setw(&c->d[1], alu_movew(c, 8));            /* move.w #8,d1 */
      lift_charge(x, 0xF859C);
      setw(&c->d[2], alu_movew(c, 0x7FF));        /* move.w #$7FF,d2 */
      lift_charge(x, 0xF85A0);
      lift_call(x, 0xF85A4, 6, Text_FillRows);    /* jsr sub_1197E */
      if (x->declined) return;
    }
  }

  /* loc_F85AA */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  c->a[7] += 32;
  lift_charge_movem(x, 0xF85AA);
  lift_charge(x, 0xF85AE);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ------------------------------------------------------------------ */
/* Wave 43 — the sub_11A48 callers the computed-dispatch lift unblocked */
/* ------------------------------------------------------------------ */

void Ptr_ChainAdd(lift_ctx *);   /* sub_13510, game.c */

/*
 * Text_DrawInlineTableString (sub_11A36)
 * The table-string twin of Text_DrawInlineString: the caller's return
 * address IS the packed chunk. Load it into a1, draw it through
 * Text_DrawTableString, then store the advanced a1 back as the return
 * address so control resumes past the inline data.
 */
void Text_DrawInlineTableString(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0x11A36);
  c->a[1] = lift_r32(x, c->a[7] + 4);             /* move.l 4(sp),a1 — movea */
  lift_charge(x, 0x11A38);

  lift_call(x, 0x11A3C, 4, Text_DrawTableString); /* bsr.w sub_11A48 */
  if (x->declined) return;

  alu_movel(c, c->a[1]);                          /* move.l a1,4(sp) */
  lift_w32(x, c->a[7] + 4, c->a[1]);
  lift_charge(x, 0x11A40);
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0x11A44);
  lift_charge(x, 0x11A46);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_SkipChunksThenDraw (sub_13508)
 *   in: d0 = number of leading chunks to skip, a1 = chunk stream
 * Advance a1 past d0+1 length-prefixed chunks (Ptr_ChainAdd), then tail
 * into Text_DrawTableString to draw the one landed on.
 */
void Text_SkipChunksThenDraw(lift_ctx *x)
{
  lift_call(x, 0x13508, 4, Ptr_ChainAdd);         /* bsr.w sub_13510 */
  if (x->declined) return;
  lift_charge_bcc(x, 0x1350C, 1);                 /* bra.w sub_11A48 — tail */
  Text_DrawTableString(x);
}

/*
 * Menu_DrawGoalieModeLabel (sub_8008)
 *   in: a1 = menu-row descriptor whose byte at +2 tags this row 'x'
 * On the goalie-control row, draw "Manual Goalie" or "Auto Goalie" per
 * the per-side flag ($D05A home / $D05C away, picked by the rink-flip
 * bit 1 of $C2EC), then advance a1 past its own length-prefixed chunk.
 * Any other row just draws its own chunk unchanged.
 */
void Menu_DrawGoalieModeLabel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int ne;

  alu_cmpb(c, 0x78, lift_r8(x, c->a[1] + 2));     /* cmp.b #$78,2(a1) */
  lift_charge(x, 0x8008);
  ne = !c->zf;
  lift_charge_bcc(x, 0x800E, ne);                 /* bne.w loc_8048 */

  if (!ne)
  {
    int off;

    alu_movel(c, c->a[1]);                        /* move.l a1,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->a[1]);
    lift_charge(x, 0x8012);
    c->a[1] = 0x00008050;                         /* movea.l #aManualGoalie-2,a1 */
    lift_charge(x, 0x8014);
    alu_btst(c, lift_r8(x, 0xFFC2EC), 1);         /* btst #1,($C2EC).w */
    lift_charge(x, 0x801A);
    lift_charge_bcc(x, 0x8020, c->zf);            /* beq.w loc_802C */
    if (!c->zf)
    {
      alu_tstw(c, lift_r16(x, 0xFFD05C));         /* tst.w ($D05C).w */
      lift_charge(x, 0x8024);
      lift_charge_bcc(x, 0x8028, 1);              /* bra.w loc_8030 */
    }
    else
    {
      alu_tstw(c, lift_r16(x, 0xFFD05A));         /* tst.w ($D05A).w */
      lift_charge(x, 0x802C);
    }

    /* loc_8030 */
    lift_charge_bcc(x, 0x8030, c->zf);            /* beq.w loc_803A */
    if (!c->zf)
    {
      c->a[1] = 0x00008064;                       /* movea.l #aAutoGoalie-2,a1 */
      lift_charge(x, 0x8034);
    }

    /* loc_803A */
    lift_call(x, 0x803A, 6, Text_DrawTableString);   /* jsr sub_11A48 */
    if (x->declined) return;
    c->a[1] = lift_r32(x, c->a[7]);               /* move.l (sp)+,a1 — movea */
    c->a[7] += 4;
    lift_charge(x, 0x8040);
    off = (int)SEW(lift_r16(x, c->a[1]));         /* add.w (a1),a1 — adda, no flags */
    c->a[1] = (c->a[1] + (uint32_t)off) & 0xFFFFFFFFu;
    lift_charge(x, 0x8042);
    lift_charge_bcc(x, 0x8044, 1);                /* bra.w locret_804E */
  }
  else
  {
    /* loc_8048 */
    lift_call(x, 0x8048, 6, Text_DrawTableString);   /* jsr sub_11A48 */
    if (x->declined) return;
  }

  /* locret_804E */
  lift_charge(x, 0x804E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Hud_DrawFaceoffStrengthMeter (sub_FD4CA)
 * Animated three-cell meter: a 7-tick sub-counter ($D5A6) advances a
 * 0..5 level ($D5A4, clamped at 5), which indexes one of two ROM tables
 * of three-glyph strings — off_FD548 (']' bars, drawn when $D5A0 >
 * $D5A2) or off_FD560 ('[' bars, when less). On a tie both sides show
 * the blank chunk at word_FD5A8. $FFFFB030 is the table-string glyph
 * bias row: 2 while drawing, back to 0 after.
 */
void Hud_DrawFaceoffStrengthMeter(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, lt, gt, eq = 0;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFD4CA);

  lift_w16(x, 0xFFD5A6,                           /* addq.w #1,($D5A6).w */
           alu_addw(c, 1, W(lift_r16(x, 0xFFD5A6))));
  lift_charge(x, 0xFD4CE);
  alu_cmpw(c, 7, lift_r16(x, 0xFFD5A6));          /* cmp.w #7,($D5A6).w */
  lift_charge(x, 0xFD4D2);
  lt = (c->nf != c->vf);
  lift_charge_bcc(x, 0xFD4D8, lt);                /* blt.w loc_FD4F4 */
  if (!lt)
  {
    lift_w16(x, 0xFFD5A6, alu_movew(c, 0));       /* clr.w ($D5A6).w */
    lift_charge(x, 0xFD4DC);
    lift_w16(x, 0xFFD5A4,                         /* addq.w #1,($D5A4).w */
             alu_addw(c, 1, W(lift_r16(x, 0xFFD5A4))));
    lift_charge(x, 0xFD4E0);
    alu_cmpw(c, 6, lift_r16(x, 0xFFD5A4));        /* cmp.w #6,($D5A4).w */
    lift_charge(x, 0xFD4E4);
    lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xFD4EA, lt);              /* blt.w loc_FD4F4 */
    if (!lt)
    {
      lift_w16(x, 0xFFD5A4, alu_movew(c, 5));     /* move.w #5,($D5A4).w */
      lift_charge(x, 0xFD4EE);
    }
  }

  /* loc_FD4F4 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFD5A4)));  /* move.w ($D5A4).w,d1 */
  lift_charge(x, 0xFD4F4);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 2));     /* asl.w #2,d1 */
  lift_charge(x, 0xFD4F8);
  c->a[1] = 0x000FD548;                           /* movea.l #off_FD548,a1 */
  lift_charge(x, 0xFD4FA);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5A0)));  /* move.w ($D5A0).w,d0 */
  lift_charge(x, 0xFD500);
  alu_cmpw(c, lift_r16(x, 0xFFD5A2), W(c->d[0])); /* cmp.w ($D5A2).w,d0 */
  lift_charge(x, 0xFD504);
  gt = !c->zf && (c->nf == c->vf);
  lift_charge_bcc(x, 0xFD508, gt);                /* bgt.w loc_FD516 */
  if (!gt)
  {
    eq = c->zf;
    lift_charge_bcc(x, 0xFD50C, eq);              /* beq.w loc_FD51E */
    if (!eq)
    {
      c->a[1] = 0x000FD560;                       /* movea.l #off_FD560,a1 */
      lift_charge(x, 0xFD510);
    }
  }
  if (!eq)
  {
    /* loc_FD516 */
    c->a[1] = lift_r32(x, c->a[1] + SEW(c->d[1]));  /* move.l (a1,d1.w),a1 */
    lift_charge(x, 0xFD516);
    lift_charge_bcc(x, 0xFD51A, 1);               /* bra.w loc_FD524 */
  }
  else
  {
    /* loc_FD51E */
    c->a[1] = 0x000FD5A8;                         /* movea.l #word_FD5A8,a1 */
    lift_charge(x, 0xFD51E);
  }

  /* loc_FD524 */
  lift_w16(x, 0xFFB030, alu_movew(c, 2));         /* move.w #2,($B030).w */
  lift_charge(x, 0xFD524);
  lift_call(x, 0xFD52A, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  lift_call(x, 0xFD536, 6, Text_DrawTableString);    /* jsr sub_11A48 */
  if (x->declined) return;
  lift_w16(x, 0xFFB030, alu_movew(c, 0));         /* move.w #0,($B030).w */
  lift_charge(x, 0xFD53C);

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFD542);
  lift_charge(x, 0xFD546);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 2 ---- */

void Lookup_TeamOverallRating(lift_ctx *);   /* sub_FE172, game.c */

/*
 * Text_DrawTwoSpaces (sub_FCFDC)
 * Draw the two-space chunk at word_FCFF6 without disturbing the caller's
 * a1 or the text column ($B028) — both are saved and restored around the
 * call, so it pads the current row in place.
 */
void Text_DrawTwoSpaces(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t col;

  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0xFCFDC);
  col = lift_r16(x, T_CURSOR_COL);                /* move.w ($B028).w,-(sp) */
  alu_movew(c, col);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], col);
  lift_charge(x, 0xFCFDE);
  c->a[1] = 0x000FCFF6;                           /* movea.l #word_FCFF6,a1 */
  lift_charge(x, 0xFCFE2);
  lift_call(x, 0xFCFE8, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFCFEE);
  }
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFCFF2);
  lift_charge(x, 0xFCFF4);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawTeamOverall (sub_FCFB8)
 *   in: a0 = team block
 * Pad two spaces, look up the team's overall rating, format it two
 * digits wide and draw it. d0 is preserved across the draw (the rating
 * itself is the caller's return value).
 */
void Stat_DrawTeamOverall(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0xFCFB8, 4, Text_DrawTwoSpaces);   /* bsr.w sub_FCFDC */
  if (x->declined) return;
  alu_movel(c, c->a[2]);                          /* move.l a2,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[2]);
  lift_charge(x, 0xFCFBC);
  c->a[2] = c->a[0];                              /* movea.l a0,a2 */
  lift_charge(x, 0xFCFBE);
  lift_call(x, 0xFCFC0, 4, Lookup_TeamOverallRating);   /* bsr.w sub_FE172 */
  if (x->declined) return;
  c->a[2] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a2 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFCFC4);
  alu_movel(c, c->d[0]);                          /* move.l d0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge(x, 0xFCFC6);
  setw(&c->d[1], alu_movew(c, 2));                /* move.w #2,d1 */
  lift_charge(x, 0xFCFC8);
  lift_call(x, 0xFCFCC, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
  if (x->declined) return;
  lift_call(x, 0xFCFD2, 6, Text_DrawTableString);          /* jsr sub_11A48 */
  if (x->declined) return;
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));   /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0xFCFD8);
  lift_charge(x, 0xFCFDA);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_ClearMenuBody (sub_8774)
 * Draw the inline escape chunk at $877A (a single $BE control byte —
 * reset the cursor), then tail into Text_FillRows to blank $28 rows of
 * $A columns with the empty tile $7FF.
 */
void Text_ClearMenuBody(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x8774, 6, Text_DrawInlineString); /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  c->d[0] = alu_moveql(c, 0x28);                  /* moveq #$28,d0 */
  lift_charge(x, 0x8780);
  c->d[1] = alu_moveql(c, 0x0A);                  /* moveq #$A,d1 */
  lift_charge(x, 0x8782);
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0x8784);
  lift_charge(x, 0x8788);                         /* jmp sub_1197E — tail */
  Text_FillRows(x);
}

/*
 * Menu_DrawSetupLabels (sub_F8070)
 * Walk the label list at word_F80D4 down the setup screen: rows below
 * $F are skipped (a1 stepped past the chunk by its own length word),
 * rows $F..$19 are drawn at column 3 on plane 0, and the walk stops once
 * the row passes $19. $C2F8 bit 3 selects the alternate glyph bias table
 * for the duration.
 */
void Menu_DrawSetupLabels(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 8;                                   /* movem.l d0-d1,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_charge_movem(x, 0xF8070);
  lift_w8(x, 0xFFC2F8, alu_bset(c, lift_r8(x, 0xFFC2F8), 3));  /* bset #3 */
  lift_charge(x, 0xF8074);
  c->a[1] = 0x000F80D4;                           /* movea.l #word_F80D4,a1 */
  lift_charge(x, 0xF807A);
  setw(&c->d[0], alu_movew(c, 0x0F));             /* move.w #$F,d0 */
  lift_charge(x, 0xF8080);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFD422)));  /* move.w ($D422).w,d1 */
  lift_charge(x, 0xF8084);
  setw(&c->d[1], alu_addw(c, W(c->d[1]), W(c->d[1])));  /* add.w d1,d1 */
  lift_charge(x, 0xF8088);
  setw(&c->d[0], alu_subw(c, W(c->d[1]), W(c->d[0])));  /* sub.w d1,d0 */
  lift_charge(x, 0xF808A);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[0])));  /* move.w d0,($B02A).w */
  lift_charge(x, 0xF808C);

  for (;;)
  {
    int ge, gt;

    /* loc_F8090 */
    alu_cmpw(c, 0x0F, lift_r16(x, T_CURSOR_ROW)); /* cmp.w #$F,($B02A).w */
    lift_charge(x, 0xF8090);
    ge = (c->nf == c->vf);
    lift_charge_bcc(x, 0xF8096, ge);              /* bge.w loc_F80A0 */
    if (!ge)
    {
      uint32_t step = SEW(lift_r16(x, c->a[1]));  /* add.w (a1),a1 — adda */
      c->a[1] = c->a[1] + step;
      lift_charge(x, 0xF809A);
      lift_charge_bcc(x, 0xF809C, 1);             /* bra.w loc_F80C2 */
    }
    else
    {
      /* loc_F80A0 */
      alu_cmpw(c, 0x19, lift_r16(x, T_CURSOR_ROW));  /* cmp.w #$19,($B02A).w */
      lift_charge(x, 0xF80A0);
      gt = !c->zf && (c->nf == c->vf);
      lift_charge_bcc(x, 0xF80A6, gt);            /* bgt.w loc_F80C8 */
      if (gt) break;
      lift_w16(x, T_CURSOR_COL, alu_movew(c, 3)); /* move.w #3,($B028).w */
      lift_charge(x, 0xF80AA);
      lift_w16(x, 0xFFB02C, alu_movew(c, 0));     /* move.w #0,($B02C).w */
      lift_charge(x, 0xF80B0);
      lift_w16(x, T_PLANE_SEL, alu_movew(c, 0));  /* move.w #0,($B02E).w */
      lift_charge(x, 0xF80B6);
      lift_call(x, 0xF80BC, 6, Text_DrawTableString);   /* jsr sub_11A48 */
      if (x->declined) return;
    }

    /* loc_F80C2 */
    lift_w16(x, T_CURSOR_ROW,                     /* addq.w #2,($B02A).w */
             alu_addw(c, 2, W(lift_r16(x, T_CURSOR_ROW))));
    lift_charge(x, 0xF80C2);
    lift_charge_bcc(x, 0xF80C6, 1);               /* bra.s loc_F8090 */
  }

  /* loc_F80C8 */
  lift_w8(x, 0xFFC2F8, alu_bclr(c, lift_r8(x, 0xFFC2F8), 3));  /* bclr #3 */
  lift_charge(x, 0xF80C8);
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[7] += 8;
  lift_charge_movem(x, 0xF80CE);
  lift_charge(x, 0xF80D2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 3 ---- */

/*
 * Text_CursorToNameSlot (sub_FB7CA)
 *   in: d5 = flat slot index over a 6-column grid
 * Draw the inline cursor-home escape, then step the text cursor to the
 * slot's cell: two rows per row of six, two columns per column.
 * Returns d0 = d1 = 3 (the cell's width/height for the caller).
 */
void Text_CursorToNameSlot(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0xFB7CA, 6, Text_DrawInlineString);  /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, W(c->d[5])));       /* move.w d5,d0 */
  lift_charge(x, 0xFB7D6);
  c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
  lift_charge(x, 0xFB7D8);
  lift_charge_divu(x, 0xFB7DA, 6, c->d[0]);       /* divu.w #6,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 6, c->d[0]);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
  lift_charge(x, 0xFB7DE);
  lift_w16(x, T_CURSOR_ROW,                       /* add.w d0,($B02A).w */
           alu_addw(c, W(c->d[0]), W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFB7E0);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 — the remainder */
  lift_charge(x, 0xFB7E4);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));     /* asl.w #1,d0 */
  lift_charge(x, 0xFB7E6);
  lift_w16(x, T_CURSOR_COL,                       /* add.w d0,($B028).w */
           alu_addw(c, W(c->d[0]), W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0xFB7E8);
  c->d[0] = alu_moveql(c, 3);                     /* moveq #3,d0 */
  lift_charge(x, 0xFB7EC);
  c->d[1] = alu_moveql(c, 3);                     /* moveq #3,d1 */
  lift_charge(x, 0xFB7EE);
  lift_charge(x, 0xFB7F0);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawNameFieldCursor (sub_FB77A)
 *   in: d4 = column offset of the field
 * Position at the name field and, when the "editing" flag ($D6C4) is
 * set, draw the '<' insertion caret chunk after it.
 */
void Text_DrawNameFieldCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  lift_call(x, 0xFB77A, 6, Text_DrawInlineString);  /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL,                       /* add.w d4,($B028).w */
           alu_addw(c, W(c->d[4]), W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0xFB786);
  alu_tstw(c, lift_r16(x, 0xFFD6C4));             /* tst.w ($D6C4).w */
  lift_charge(x, 0xFB78A);
  t = c->zf;
  lift_charge_bcc(x, 0xFB78E, t);                 /* beq.w locret_FB79E */
  if (!t)
  {
    lift_call(x, 0xFB792, 6, Text_DrawInlineString);  /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;
  }
  lift_charge(x, 0xFB79E);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawShotTotalCell (sub_FDC28)
 *   in: a0 = period-totals cursor
 * Take the next period's shot count, accumulate it into the running
 * total ($D5A8), draw three spaces, back the column up over them and
 * print the count right-aligned (two digits under 100, three above).
 * Tail-calls Text_DrawString to emit the formatted buffer.
 */
void Stat_DrawShotTotalCell(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int lt;

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0xFDC28);
  lift_w16(x, 0xFFD5A8,                           /* add.w d0,($D5A8).w */
           alu_addw(c, W(c->d[0]), W(lift_r16(x, 0xFFD5A8))));
  lift_charge(x, 0xFDC2A);
  lift_call(x, 0xFDC2E, 6, Text_DrawInlineString);  /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL,                       /* subq.w #3,($B028).w */
           alu_subw(c, 3, W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0xFDC3A);
  setw(&c->d[1], alu_movew(c, 2));                /* move.w #2,d1 */
  lift_charge(x, 0xFDC3E);
  alu_cmpw(c, 0x64, W(c->d[0]));                  /* cmp.w #$64,d0 */
  lift_charge(x, 0xFDC42);
  lt = (c->nf != c->vf);
  lift_charge_bcc(x, 0xFDC46, lt);                /* blt.w loc_FDC4E */
  if (!lt)
  {
    setw(&c->d[1], alu_movew(c, 3));              /* move.w #3,d1 */
    lift_charge(x, 0xFDC4A);
  }
  /* loc_FDC4E */
  lift_call(x, 0xFDC4E, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
  if (x->declined) return;
  lift_charge(x, 0xFDC54);                        /* jmp sub_11BA4 — tail */
  Text_DrawString(x);
}

/*
 * Stat_DrawShotsByPeriodRow (sub_FDB8C)
 *   in: a2 = team block
 * Print the shots-on-goal row: one cell per period already played
 * ($C466 counts them, and the OT column additionally needs bit 1 of
 * $C2FC), then the running total accumulated in $D5A8 at column $21.
 * $342/$34A(a2) pick the home or away period array via $D598.
 */
void Stat_DrawShotsByPeriodRow(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, lt, done = 0;

  c->a[0] = c->a[2] + 0x342;                      /* lea $342(a2),a0 */
  lift_charge(x, 0xFDB8C);
  alu_tstw(c, lift_r16(x, 0xFFD598));             /* tst.w ($D598).w */
  lift_charge(x, 0xFDB90);
  t = c->zf;
  lift_charge_bcc(x, 0xFDB94, t);                 /* beq.w loc_FDB9C */
  if (!t)
  {
    c->a[0] = c->a[2] + 0x34A;                    /* lea $34A(a2),a0 */
    lift_charge(x, 0xFDB98);
  }

  /* loc_FDB9C */
  lift_w16(x, 0xFFD5A8, alu_movew(c, 0));         /* clr.w ($D5A8).w */
  lift_charge(x, 0xFDB9C);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x0B));  /* move.w #$B,($B028).w */
  lift_charge(x, 0xFDBA0);
  lift_call(x, 0xFDBA6, 4, Stat_DrawShotTotalCell);  /* bsr.w sub_FDC28 */
  if (x->declined) return;

  alu_cmpw(c, 1, lift_r16(x, 0xFFC466));          /* cmp.w #1,($C466).w */
  lift_charge(x, 0xFDBAA);
  lt = (c->nf != c->vf);
  lift_charge_bcc(x, 0xFDBB0, lt);                /* blt.w loc_FDBF0 */
  if (!lt)
  {
    lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x10));  /* move.w #$10,($B028).w */
    lift_charge(x, 0xFDBB4);
    lift_call(x, 0xFDBBA, 4, Stat_DrawShotTotalCell);
    if (x->declined) return;

    alu_cmpw(c, 2, lift_r16(x, 0xFFC466));        /* cmp.w #2,($C466).w */
    lift_charge(x, 0xFDBBE);
    lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xFDBC4, lt);              /* blt.w loc_FDBF0 */
    if (!lt)
    {
      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x15));  /* move.w #$15,($B028).w */
      lift_charge(x, 0xFDBC8);
      lift_call(x, 0xFDBCE, 4, Stat_DrawShotTotalCell);
      if (x->declined) return;

      alu_cmpw(c, 3, lift_r16(x, 0xFFC466));      /* cmp.w #3,($C466).w */
      lift_charge(x, 0xFDBD2);
      lt = (c->nf != c->vf);
      lift_charge_bcc(x, 0xFDBD8, lt);            /* blt.w loc_FDBF0 */
      if (!lt)
      {
        alu_btst(c, lift_r8(x, 0xFFC2FC), 1);     /* btst #1,($C2FC).w */
        lift_charge(x, 0xFDBDC);
        done = c->zf;
        lift_charge_bcc(x, 0xFDBE2, done);        /* beq.w loc_FDBF0 */
        if (!done)
        {
          lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x1A));  /* move.w #$1A,($B028).w */
          lift_charge(x, 0xFDBE6);
          lift_call(x, 0xFDBEC, 4, Stat_DrawShotTotalCell);
          if (x->declined) return;
        }
      }
    }
  }

  /* loc_FDBF0 */
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x21));  /* move.w #$21,($B028).w */
  lift_charge(x, 0xFDBF0);
  lift_call(x, 0xFDBF6, 6, Text_DrawInlineString);   /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL,                       /* subq.w #3,($B028).w */
           alu_subw(c, 3, W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0xFDC02);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5A8)));  /* move.w ($D5A8).w,d0 */
  lift_charge(x, 0xFDC06);
  setw(&c->d[1], alu_movew(c, 2));                /* move.w #2,d1 */
  lift_charge(x, 0xFDC0A);
  alu_cmpw(c, 0x64, W(c->d[0]));                  /* cmp.w #$64,d0 */
  lift_charge(x, 0xFDC0E);
  lt = (c->nf != c->vf);
  lift_charge_bcc(x, 0xFDC12, lt);                /* blt.w loc_FDC1A */
  if (!lt)
  {
    setw(&c->d[1], alu_movew(c, 3));              /* move.w #3,d1 */
    lift_charge(x, 0xFDC16);
  }
  /* loc_FDC1A */
  lift_call(x, 0xFDC1A, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
  if (x->declined) return;
  lift_call(x, 0xFDC20, 6, Text_DrawString);      /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_charge(x, 0xFDC26);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 4 ---- */

void Math_SqrtU32(lift_ctx *);   /* sub_110BE, math.c */
void sub_18AE8(lift_ctx *);      /* overlay.c */

/*
 * Records_DrawColumnHeader (sub_FBD86)
 * Pick the records screen's header chunk by page ($BF12): 0 = the win %
 * / win-loss-tie header, 2 = the goals/teams header, anything else the
 * third variant — then draw it.
 */
void Records_DrawColumnHeader(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  c->a[1] = 0x000FBDB2;                           /* movea.l #word_FBDB2,a1 */
  lift_charge(x, 0xFBD86);
  alu_tstw(c, lift_r16(x, 0xFFBF12));             /* tst.w ($BF12).w */
  lift_charge(x, 0xFBD8C);
  t = c->zf;
  lift_charge_bcc(x, 0xFBD90, t);                 /* beq.w loc_FBDAA */
  if (!t)
  {
    c->a[1] = 0x000FBE54;                         /* movea.l #word_FBE54,a1 */
    lift_charge(x, 0xFBD94);
    alu_cmpw(c, 2, lift_r16(x, 0xFFBF12));        /* cmp.w #2,($BF12).w */
    lift_charge(x, 0xFBD9A);
    t = c->zf;
    lift_charge_bcc(x, 0xFBDA0, t);               /* beq.w loc_FBDAA */
    if (!t)
    {
      c->a[1] = 0x000FBE04;                       /* movea.l #word_FBE04,a1 */
      lift_charge(x, 0xFBDA4);
    }
  }
  /* loc_FBDAA */
  lift_call(x, 0xFBDAA, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  lift_charge(x, 0xFBDB0);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawFrameWithSavedWidth (sub_FB97C)
 * Draw a frame box using the stored width at $D530 in place of the live
 * $B01E, restoring $B01E afterwards.
 */
void Text_DrawFrameWithSavedWidth(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved;

  saved = lift_r16(x, 0xFFB01E);                  /* move.w ($B01E).w,-(sp) */
  alu_movew(c, saved);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], saved);
  lift_charge(x, 0xFB97C);
  lift_w16(x, 0xFFB01E,                           /* move.w ($D530).w,($B01E).w */
           alu_movew(c, lift_r16(x, 0xFFD530)));
  lift_charge(x, 0xFB980);
  lift_call(x, 0xFB986, 6, Text_DrawFrame);       /* jsr sub_119B8 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B01E).w */
    c->a[7] += 2;
    lift_w16(x, 0xFFB01E, alu_movew(c, v));
    lift_charge(x, 0xFB98C);
  }
  lift_charge(x, 0xFB990);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawScaledLetterGrade (sub_9BB4)
 *   in: d0 = raw rating
 * sqrt(d0 * 4) biased by 'A' gives the letter grade; print it three
 * characters wide, then draw the trailing inline separator chunk.
 */
void Text_DrawScaledLetterGrade(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
  lift_charge(x, 0x9BB4);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));     /* asl.w #2,d0 */
  lift_charge(x, 0x9BB6);
  lift_call(x, 0x9BB8, 4, Math_SqrtU32);          /* bsr.w sub_110BE */
  if (x->declined) return;
  setw(&c->d[0], alu_addw(c, 0x41, W(c->d[0])));  /* add.w #$41,d0 */
  lift_charge(x, 0x9BBC);
  c->d[1] = alu_moveql(c, 3);                     /* moveq #3,d1 */
  lift_charge(x, 0x9BC0);
  lift_call(x, 0x9BC2, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
  if (x->declined) return;
  lift_call(x, 0x9BC8, 4, Text_DrawString);       /* bsr.w sub_11BA4 */
  if (x->declined) return;
  lift_call(x, 0x9BCC, 4, Text_DrawInlineString); /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;
  lift_charge(x, 0x9BD6);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_DrawStatColumnEntry (sub_9112)
 *   in: d0 = stat byte (negative = blank)
 * Format the byte through sub_18AE8 and draw it, then step to the next
 * row. A negative value draws nothing but still advances the row.
 */
void Roster_DrawStatColumnEntry(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int mi;

  setw(&c->d[0], alu_extw(c, c->d[0]));           /* ext.w d0 */
  lift_charge(x, 0x9112);
  mi = c->nf;
  lift_charge_bcc(x, 0x9114, mi);                 /* bmi.w loc_9124 */
  if (!mi)
  {
    lift_call(x, 0x9118, 6, sub_18AE8);           /* jsr sub_18AE8 */
    if (x->declined) return;
    lift_call(x, 0x911E, 6, Text_DrawString);     /* jsr sub_11BA4 */
    if (x->declined) return;
  }
  /* loc_9124 */
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0x9124);
  lift_charge(x, 0x9128);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 5 ---- */

void Clamp_HalveBelow50(lift_ctx *);   /* sub_FEF7C, game.c */
void sub_FA9F8(lift_ctx *);            /* game.c */
void sub_F998E(lift_ctx *);            /* overlay.c */

/*
 * Records_BuildNameChunk (sub_FB91A)
 *   in: a1 = the name-entry letter buffer
 * Assemble a $C-character chunk at $FFFFCF36 (length word $E first):
 * characters below the current edit length come from the buffer, the
 * rest are '-' placeholders. Then draw it.
 */
void Records_BuildNameChunk(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, lt;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFB91A);

  c->a[0] = 0xFFFFCF36;                           /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFB91E);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD4E8)));  /* move.w ($D4E8).w,d0 */
  lift_charge(x, 0xFB924);
  setw(&c->d[3], alu_movew(c, 0));                /* move.w #0,d3 */
  lift_charge(x, 0xFB928);
  lift_w16(x, c->a[0], alu_movew(c, 0x0E));       /* move.w #$E,(a0)+ */
  c->a[0] += 2;
  lift_charge(x, 0xFB92C);

  for (;;)
  {
    /* loc_FB930 */
    setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[3]))));  /* move.b (a1,d3.w),d1 */
    lift_charge(x, 0xFB930);
    alu_cmpw(c, lift_r16(x, 0xFFD4E8), W(c->d[3]));  /* cmp.w ($D4E8).w,d3 */
    lift_charge(x, 0xFB934);
    lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xFB938, lt);              /* blt.w loc_FB940 */
    if (!lt)
    {
      setb(&c->d[1], alu_moveb(c, 0x2D));         /* move.b #$2D,d1 */
      lift_charge(x, 0xFB93C);
    }
    /* loc_FB940 */
    lift_w8(x, c->a[0], alu_moveb(c, c->d[1]));   /* move.b d1,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0xFB940);
    setw(&c->d[3], alu_addw(c, 1, W(c->d[3])));   /* addq.w #1,d3 */
    lift_charge(x, 0xFB942);
    alu_cmpw(c, 0x0C, W(c->d[3]));                /* cmp.w #$C,d3 */
    lift_charge(x, 0xFB944);
    lt = (c->nf != c->vf);
    lift_charge_bcc(x, 0xFB948, lt);              /* blt.s loc_FB930 */
    if (!lt) break;
  }

  c->a[1] = 0xFFFFCF36;                           /* movea.l #$FFFFCF36,a1 */
  lift_charge(x, 0xFB94A);
  lift_call(x, 0xFB950, 6, Text_DrawString);      /* jsr sub_11BA4 */
  if (x->declined) return;
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFB956);
  lift_charge(x, 0xFB95A);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawTeamRatingBlock (sub_FABDC)
 *   in: d0 = column, d1 = row
 * Two stacked labels ("Team" / "Rating") followed by the selected team's
 * overall rating, two digits wide. $D4F6 picks the away team block.
 */
void Stat_DrawTeamRatingBlock(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFABDC);

  lift_w16(x, T_CURSOR_COL, alu_movew(c, W(c->d[0])));  /* move.w d0,($B028).w */
  lift_charge(x, 0xFABE0);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[1])));  /* move.w d1,($B02A).w */
  lift_charge(x, 0xFABE4);
  alu_movew(c, W(c->d[0]));                       /* move.w d0,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_charge(x, 0xFABE8);
  lift_call(x, 0xFABEA, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + 6 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFABF6);
  }
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFABFA);
  lift_call(x, 0xFABFE, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A */
  if (x->declined) return;

  c->a[0] = 0xFFFFC6CE;                           /* movea.l #$FFFFC6CE,a0 */
  lift_charge(x, 0xFAC0E);
  alu_tstw(c, lift_r16(x, 0xFFD4F6));             /* tst.w ($D4F6).w */
  lift_charge(x, 0xFAC14);
  t = c->zf;
  lift_charge_bcc(x, 0xFAC18, t);                 /* beq.w loc_FAC22 */
  if (!t)
  {
    c->a[0] = 0xFFFFCA32;                         /* movea.l #$FFFFCA32,a0 */
    lift_charge(x, 0xFAC1C);
  }
  /* loc_FAC22 */
  c->a[2] = c->a[0];                              /* movea.l a0,a2 */
  lift_charge(x, 0xFAC22);
  lift_call(x, 0xFAC24, 4, Lookup_TeamOverallRating);   /* bsr.w sub_FE172 */
  if (x->declined) return;
  setw(&c->d[1], alu_movew(c, 2));                /* move.w #2,d1 */
  lift_charge(x, 0xFAC28);
  lift_call(x, 0xFAC2C, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
  if (x->declined) return;
  lift_call(x, 0xFAC32, 6, Text_DrawTableString);          /* jsr sub_11A48 */
  if (x->declined) return;
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFAC38);
  lift_charge(x, 0xFAC3C);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Records_ClearListArea (sub_FBFA0)
 * Home the cursor via an inline escape chunk, blank $28 columns over
 * $12 rows, then restore the caller's column/row/plane triple.
 */
void Records_ClearListArea(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i;
  uint32_t v;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFBFA0);

  v = lift_r16(x, T_CURSOR_COL);                  /* move.w ($B028).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0xFBFA4);
  v = lift_r16(x, T_CURSOR_ROW);                  /* move.w ($B02A).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0xFBFA8);
  v = lift_r16(x, T_PLANE_SEL);                   /* move.w ($B02E).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0xFBFAC);
  lift_call(x, 0xFBFB0, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + 8 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, 0x28));             /* move.w #$28,d0 */
  lift_charge(x, 0xFBFBE);
  setw(&c->d[1], alu_movew(c, 0x12));             /* move.w #$12,d1 */
  lift_charge(x, 0xFBFC2);
  lift_call(x, 0xFBFC6, 6, Text_FillRows);        /* jsr sub_1197E */
  if (x->declined) return;

  v = lift_r16(x, c->a[7]); c->a[7] += 2;         /* move.w (sp)+,($B02E).w */
  lift_w16(x, T_PLANE_SEL, alu_movew(c, v));
  lift_charge(x, 0xFBFCC);
  v = lift_r16(x, c->a[7]); c->a[7] += 2;         /* move.w (sp)+,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, v));
  lift_charge(x, 0xFBFD0);
  v = lift_r16(x, c->a[7]); c->a[7] += 2;         /* move.w (sp)+,($B028).w */
  lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
  lift_charge(x, 0xFBFD4);
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFBFD8);
  lift_charge(x, 0xFBFDC);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawOverallRatingPercent (sub_FA8AC)
 *   in: d1 = column, d2 = row
 * "Overall" / "Rating" labels, then the team's rating as a percentage of
 * the league table's max (sub_FA9F8 returns numerator/denominator in
 * d0/d1), clamped by Clamp_HalveBelow50 and formatted into $FFFFBFA4.
 */
void Stat_DrawOverallRatingPercent(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFA8AC);

  lift_w16(x, T_CURSOR_COL, alu_movew(c, W(c->d[1])));  /* move.w d1,($B028).w */
  lift_charge(x, 0xFA8B0);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[2])));  /* move.w d2,($B02A).w */
  lift_charge(x, 0xFA8B4);
  alu_movew(c, W(c->d[1]));                       /* move.w d1,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[1]));
  lift_charge(x, 0xFA8B8);
  lift_call(x, 0xFA8BA, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFA8CA);
  }
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFA8CE);
  lift_call(x, 0xFA8D2, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $C */
  if (x->declined) return;

  c->a[2] = 0xFFFFC6CE;                           /* movea.l #$FFFFC6CE,a2 */
  lift_charge(x, 0xFA8E4);
  alu_tstw(c, lift_r16(x, 0xFFD4F6));             /* tst.w ($D4F6).w */
  lift_charge(x, 0xFA8EA);
  t = c->zf;
  lift_charge_bcc(x, 0xFA8EE, t);                 /* beq.w loc_FA8F8 */
  if (!t)
  {
    c->a[2] = 0xFFFFCA32;                         /* movea.l #$FFFFCA32,a2 */
    lift_charge(x, 0xFA8F2);
  }
  /* loc_FA8F8 */
  c->d[4] = alu_movel(c, lift_r32(x, 0x019420));  /* move.l (dword_19420).l,d4 */
  lift_charge(x, 0xFA8F8);
  alu_tstw(c, lift_r16(x, 0xFFD598));             /* tst.w ($D598).w */
  lift_charge(x, 0xFA8FE);
  t = c->zf;
  lift_charge_bcc(x, 0xFA902, t);                 /* beq.w loc_FA90C */
  if (!t)
  {
    c->d[4] = alu_movel(c, lift_r32(x, 0x019582)); /* move.l (dword_19582).l,d4 */
    lift_charge(x, 0xFA906);
  }
  /* loc_FA90C */
  lift_call(x, 0xFA90C, 4, sub_FA9F8);            /* bsr.w sub_FA9F8 */
  if (x->declined) return;
  lift_charge_mulu(x, 0xFA910, 0x64);             /* mulu.w #$64,d0 */
  c->d[0] = alu_mulu(c, 0x64, c->d[0]);
  lift_charge_divu(x, 0xFA914, W(c->d[1]), c->d[0]);   /* divu.w d1,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, W(c->d[1]), c->d[0]);
  lift_call(x, 0xFA916, 4, Clamp_HalveBelow50);   /* bsr.w sub_FEF7C */
  if (x->declined) return;
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA91A);
  lift_call(x, 0xFA920, 4, sub_F998E);            /* bsr.w sub_F998E */
  if (x->declined) return;
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA924);
  lift_call(x, 0xFA92A, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFA930);
  lift_charge(x, 0xFA934);                        /* rts (nullsub_6) */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 6 ---- */

void Text_AppendString(lift_ctx *);         /* sub_11D9E, overlay.c */
void Text_AppendIndexedString(lift_ctx *);  /* sub_FA880, game.c */
void Text_EmitTeamName(lift_ctx *);         /* sub_FA014, game.c */
void Team_SelectActiveBlock(lift_ctx *);    /* sub_FAF50, game.c */

static void Board_RenderCaptionedLine(lift_ctx *x);

/*
 * Board_DrawScoringLine (sub_FA75C, sharing the loc_FA7B0 tail)
 *   in: d0/d1 = cursor, d2 = count byte, d3/d4 = team indices, d5 = a
 *       third team index for the trailing name
 * Latch " Goal(s) [by]" as the board's caption ($BF18) — singular when
 * the count is 1, the "by" variant when d3 is non-zero — then fall into
 * the shared tail that renders "<n> Goals by <TEAM> vs. <TEAM> <TEAM>"
 * through the $FFFFBFA4 scratch buffer. A zero count draws nothing.
 * The tail is entered by bra from sub_FA6E8/sub_FA708 too; it lives here
 * because this is the only caller currently lifted.
 */
void Board_DrawScoringLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, ne;

  c->a[7] -= 12;                                  /* movem.l a1-a3,-(sp) */
  lift_w32(x, c->a[7],     c->a[1]);
  lift_w32(x, c->a[7] + 4, c->a[2]);
  lift_w32(x, c->a[7] + 8, c->a[3]);
  lift_charge_movem(x, 0xFA75C);

  c->a[1] = 0x000FA788;                           /* movea.l #word_FA788,a1 */
  lift_charge(x, 0xFA760);
  alu_tstw(c, W(c->d[3]));                        /* tst.w d3 */
  lift_charge(x, 0xFA766);
  t = c->zf;
  lift_charge_bcc(x, 0xFA768, t);                 /* beq.w loc_FA772 */
  if (!t)
  {
    c->a[1] = 0x000FA79A;                         /* movea.l #word_FA79A,a1 */
    lift_charge(x, 0xFA76C);
  }
  /* loc_FA772 */
  alu_cmpb(c, 1, c->d[2]);                        /* cmp.b #1,d2 */
  lift_charge(x, 0xFA772);
  ne = !c->zf;
  lift_charge_bcc(x, 0xFA776, ne);                /* bne.w loc_FA77C */
  if (!ne)
  {
    c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1])); /* add.w (a1),a1 — adda */
    lift_charge(x, 0xFA77A);
  }
  /* loc_FA77C */
  lift_w32(x, 0xFFBF18, alu_movel(c, c->a[1]));   /* move.l a1,($BF18).w */
  lift_charge(x, 0xFA77C);
  c->a[1] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,a1-a3 */
  c->a[2] = lift_r32(x, c->a[7] + 4);
  c->a[3] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0xFA780);
  lift_charge_bcc(x, 0xFA784, 1);                 /* bra.w loc_FA7B0 */
  Board_RenderCaptionedLine(x);
}

/*
 * Board_RenderCaptionedLine (loc_FA7B0)
 * The shared render tail of sub_FA6E8 / sub_FA708 / sub_FA75C: all three
 * only pick a caption chunk into $FFFFBF18 and bra here. Renders
 * "<n> <caption> <TEAM> vs. <TEAM> <TEAM>" through the $FFFFBFA4 scratch
 * buffer, one row per segment. A zero count byte in d2 draws nothing.
 * It is not a `sub_` of its own, so it is a static helper rather than a
 * registry entry — each entry point composes it directly.
 */
static void Board_RenderCaptionedLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int ne;

  alu_tstb(c, c->d[2]);                           /* tst.b d2 */
  lift_charge(x, 0xFA7B0);
  ne = !c->zf;
  lift_charge_bcc(x, 0xFA7B2, ne);                /* bne.w loc_FA7B8 */
  if (!ne)
  {
    lift_charge(x, 0xFA7B6);                      /* rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* loc_FA7B8 */
  lift_w16(x, T_CURSOR_COL, alu_movew(c, W(c->d[0])));  /* move.w d0,($B028).w */
  lift_charge(x, 0xFA7B8);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[1])));  /* move.w d1,($B02A).w */
  lift_charge(x, 0xFA7BC);
  setw(&c->d[0], alu_movew(c, W(c->d[2])));       /* move.w d2,d0 */
  lift_charge(x, 0xFA7C0);
  setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));  /* and.w #$FF,d0 */
  lift_charge(x, 0xFA7C2);
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA7C6);
  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0xFA7CC);
  lift_call(x, 0xFA7CE, 6, sub_F998E);            /* jsr sub_F998E */
  if (x->declined) return;
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFA7D4);
  c->a[3] = c->a[1];                              /* movea.l a1,a3 */
  lift_charge(x, 0xFA7D6);
  c->a[1] = lift_r32(x, 0xFFBF18);                /* movea.l ($BF18).w,a1 */
  lift_charge(x, 0xFA7D8);
  lift_call(x, 0xFA7DC, 6, Text_AppendString);    /* jsr sub_11D9E */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* move.w ($B028).w,-(sp) */
    alu_movew(c, v);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0xFA7E2);
  }
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA7E6);
  lift_call(x, 0xFA7EC, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFA7F2);
  }
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFA7F6);
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA7FA);
  setw(&c->d[2], alu_movew(c, W(c->d[3])));       /* move.w d3,d2 */
  lift_charge(x, 0xFA800);
  setw(&c->d[2], alu_extw(c, c->d[2]));           /* ext.w d2 */
  lift_charge(x, 0xFA802);
  lift_w8(x, 0xFFC2F8, alu_bset(c, lift_r8(x, 0xFFC2F8), 7));  /* bset #7 */
  lift_charge(x, 0xFA804);
  lift_call(x, 0xFA80A, 6, Text_EmitTeamName);    /* jsr sub_FA014 */
  if (x->declined) return;
  c->a[3] = c->a[1];                              /* movea.l a1,a3 */
  lift_charge(x, 0xFA810);
  c->a[1] = 0x000FA876;                           /* movea.l #word_FA876,a1 */
  lift_charge(x, 0xFA812);
  lift_call(x, 0xFA818, 6, Text_AppendString);    /* jsr sub_11D9E */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* move.w ($B028).w,-(sp) */
    alu_movew(c, v);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0xFA81E);
  }
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA822);
  lift_call(x, 0xFA828, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFA82E);
  }
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFA832);
  setw(&c->d[2], alu_movew(c, W(c->d[4])));       /* move.w d4,d2 */
  lift_charge(x, 0xFA836);
  setw(&c->d[2], alu_extw(c, c->d[2]));           /* ext.w d2 */
  lift_charge(x, 0xFA838);
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA83A);
  lift_w8(x, 0xFFC2F8, alu_bset(c, lift_r8(x, 0xFFC2F8), 7));  /* bset #7 */
  lift_charge(x, 0xFA840);
  lift_call(x, 0xFA846, 6, Text_EmitTeamName);    /* jsr sub_FA014 */
  if (x->declined) return;
  c->a[3] = c->a[1];                              /* movea.l a1,a3 */
  lift_charge(x, 0xFA84C);
  c->a[1] = 0x000FA87C;                           /* movea.l #word_FA87C,a1 */
  lift_charge(x, 0xFA84E);
  lift_call(x, 0xFA854, 6, Text_AppendString);    /* jsr sub_11D9E */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, W(c->d[5])));       /* move.w d5,d0 */
  lift_charge(x, 0xFA85A);
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA85C);
  lift_call(x, 0xFA862, 6, Text_AppendIndexedString);  /* jsr sub_FA880 */
  if (x->declined) return;
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFA868);
  lift_call(x, 0xFA86E, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  lift_charge(x, 0xFA874);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Stat_DrawGoalieSaves (sub_FAD84)
 *   in: d0 = goalie slot, d1 = column, d2 = row
 * From the active team block, shots-against ($E8 table) minus goals
 * ($B4 table) gives saves; print "Saves <n>" then "Save % <pct>" where
 * pct = saves*100/shots (zero when no shots faced).
 */
void Stat_DrawGoalieSaves(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, ne;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFAD84);

  lift_call(x, 0xFAD88, 4, Team_SelectActiveBlock);   /* bsr.w sub_FAF50 */
  if (x->declined) return;
  alu_movel(c, c->a[0]);                          /* move.l a0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[0]);
  lift_charge(x, 0xFAD8C);
  c->a[0] = c->a[0] + 0xB4;                       /* add.l #$B4,a0 — adda */
  lift_charge(x, 0xFAD8E);
  setb(&c->d[3], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[0]))));  /* move.b (a0,d0.w),d3 */
  lift_charge(x, 0xFAD94);
  setw(&c->d[3], alu_extw(c, c->d[3]));           /* ext.w d3 */
  lift_charge(x, 0xFAD98);
  c->a[0] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a0 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFAD9A);
  c->a[0] = c->a[0] + 0xE8;                       /* add.l #$E8,a0 — adda */
  lift_charge(x, 0xFAD9C);
  setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[0]))));  /* move.b (a0,d0.w),d4 */
  lift_charge(x, 0xFADA2);
  setw(&c->d[4], alu_extw(c, c->d[4]));           /* ext.w d4 */
  lift_charge(x, 0xFADA6);
  setw(&c->d[7], alu_movew(c, W(c->d[4])));       /* move.w d4,d7 */
  lift_charge(x, 0xFADA8);
  setw(&c->d[7], alu_subw(c, W(c->d[3]), W(c->d[7])));  /* sub.w d3,d7 */
  lift_charge(x, 0xFADAA);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, W(c->d[1])));  /* move.w d1,($B028).w */
  lift_charge(x, 0xFADAC);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[2])));  /* move.w d2,($B02A).w */
  lift_charge(x, 0xFADB0);
  alu_movew(c, W(c->d[1]));                       /* move.w d1,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[1]));
  lift_charge(x, 0xFADB4);
  lift_call(x, 0xFADB6, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $C */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, W(c->d[7])));       /* move.w d7,d0 */
  lift_charge(x, 0xFADC8);
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFADCA);
  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0xFADD0);
  lift_call(x, 0xFADD2, 4, sub_F998E);            /* bsr.w sub_F998E */
  if (x->declined) return;
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFADD6);
  lift_call(x, 0xFADD8, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($B028).w */
    c->a[7] += 2;
    lift_w16(x, T_CURSOR_COL, alu_movew(c, v));
    lift_charge(x, 0xFADDE);
  }
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0xFADE2);
  lift_call(x, 0xFADE6, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $C */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, W(c->d[7])));       /* move.w d7,d0 */
  lift_charge(x, 0xFADF8);
  lift_charge_mulu(x, 0xFADFA, 0x64);             /* mulu.w #$64,d0 */
  c->d[0] = alu_mulu(c, 0x64, c->d[0]);
  alu_tstw(c, W(c->d[4]));                        /* tst.w d4 */
  lift_charge(x, 0xFADFE);
  ne = !c->zf;
  lift_charge_bcc(x, 0xFAE00, ne);                /* bne.w loc_FAE0A */
  if (!ne)
  {
    setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
    lift_charge(x, 0xFAE04);
    lift_charge_bcc(x, 0xFAE06, 1);               /* bra.w loc_FAE0C */
  }
  else
  {
    /* loc_FAE0A */
    lift_charge_divu(x, 0xFAE0A, W(c->d[4]), c->d[0]);  /* divu.w d4,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, W(c->d[4]), c->d[0]);
  }
  /* loc_FAE0C */
  c->a[1] = 0xFFFFBFA4;                           /* movea.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFAE0C);
  alu_movel(c, c->a[1]);                          /* move.l a1,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->a[1]);
  lift_charge(x, 0xFAE12);
  lift_call(x, 0xFAE14, 4, sub_F998E);            /* bsr.w sub_F998E */
  if (x->declined) return;
  c->a[1] = lift_r32(x, c->a[7]);                 /* move.l (sp)+,a1 — movea */
  c->a[7] += 4;
  lift_charge(x, 0xFAE18);
  lift_call(x, 0xFAE1A, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFAE20);
  lift_charge(x, 0xFAE24);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 7 ---- */

void Text_FormatClock(lift_ctx *);   /* sub_11CA2, overlay.c (already declared above) */

/*
 * Text_DrawPeriodAndClock (sub_11C72)
 *   in: d0 = packed (period << 16) | clock ticks
 * Draw the period glyph from the off_11C92 table, step two columns, then
 * format the clock into the scratch buffer and tail into Text_DrawString.
 */
void Text_DrawPeriodAndClock(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x11C72);
  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x11C74);
  c->d[0] = alu_roll(c, c->d[0], 2);              /* rol.l #2,d0 */
  lift_charge(x, 0x11C76);
  c->a[1] = 0x00011C92;                           /* movea.l #off_11C92,a1 */
  lift_charge(x, 0x11C78);
  lift_call(x, 0x11C7E, 4, Text_SkipChunksThenDraw);  /* bsr.w sub_13508 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL,                       /* addq.w #2,($B028).w */
           alu_addw(c, 2, W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0x11C82);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x11C86);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 2));     /* lsr.w #2,d0 */
  lift_charge(x, 0x11C88);
  lift_call(x, 0x11C8A, 4, Text_FormatClock);     /* bsr.w sub_11CA2 */
  if (x->declined) return;
  lift_charge_bcc(x, 0x11C8E, 1);                 /* bra.w sub_11BA4 — tail */
  Text_DrawString(x);
}

/*
 * Roster_DrawPenaltyLine (sub_907C, falling through into sub_9112)
 *   in: d3 = byte offset of the penalty record inside the $FFFFC474 table
 * Blank four rows, print the period/clock, the offending player's name
 * from the team block ($364 further in for the away side per bit 7 of
 * the record), the infraction abbreviation from word_912A, and the three
 * stat bytes down the right-hand column. Falls through into
 * Roster_DrawStatColumnEntry for the last one.
 */
void Roster_DrawPenaltyLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;
  uint32_t v;

  v = lift_r16(x, T_CURSOR_ROW);                  /* move.w ($B02A).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0x907C);
  c->d[0] = alu_moveql(c, 0x28);                  /* moveq #$28,d0 */
  lift_charge(x, 0x9080);
  c->d[1] = alu_moveql(c, 4);                     /* moveq #4,d1 */
  lift_charge(x, 0x9082);
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0x9084);
  lift_call(x, 0x9088, 6, Text_FillRows);         /* jsr sub_1197E */
  if (x->declined) return;
  v = lift_r16(x, c->a[7]); c->a[7] += 2;         /* move.w (sp)+,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, v));
  lift_charge(x, 0x908E);
  c->a[0] = 0xFFFFC474;                           /* movea.w #$C474,a0 — sign-extends */
  lift_charge(x, 0x9092);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[3]))));  /* move.w (a0,d3.w),d0 */
  lift_charge(x, 0x9096);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 1));     /* move.w #1,($B028).w */
  lift_charge(x, 0x909A);
  lift_call(x, 0x90A0, 6, Text_DrawPeriodAndClock);  /* jsr sub_11C72 */
  if (x->declined) return;
  c->a[2] = 0xFFFFC6CE;                           /* movea.w #$C6CE,a2 — sign-extends */
  lift_charge(x, 0x90A6);
  alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 2), 7);  /* btst #7,2(a0,d3.w) */
  lift_charge(x, 0x90AA);
  t = c->zf;
  lift_charge_bcc(x, 0x90B0, t);                  /* beq.w loc_90B8 */
  if (!t)
  {
    c->a[2] = c->a[2] + 0x364;                    /* add.w #$364,a2 — adda */
    lift_charge(x, 0x90B4);
  }
  /* loc_90B8 */
  c->a[1] = lift_r32(x, c->a[2] + 0x1E);          /* movea.l $1E(a2),a1 */
  lift_charge(x, 0x90B8);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1] + 4));  /* add.w 4(a1),a1 — adda */
  lift_charge(x, 0x90BC);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1]));  /* add.w (a1),a1 — adda */
  lift_charge(x, 0x90C0);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x0C));  /* move.w #$C,($B028).w */
  lift_charge(x, 0x90C2);
  lift_call(x, 0x90C8, 6, Text_DrawString);       /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x23));  /* move.w #$23,($B028).w */
  lift_charge(x, 0x90CE);
  c->a[1] = 0x0000912A;                           /* lea word_912A(pc),a1 */
  lift_charge(x, 0x90D4);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 2)));  /* move.b 2(a0,d3.w),d0 */
  lift_charge(x, 0x90D8);
  setw(&c->d[0], alu_andw(c, 0x7F, W(c->d[0])));  /* and.w #$7F,d0 */
  lift_charge(x, 0x90DC);
  lift_call(x, 0x90E0, 6, Text_SkipChunksThenDraw);  /* jsr sub_13508 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x10));  /* move.w #$10,($B028).w */
  lift_charge(x, 0x90E6);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 3)));  /* move.b 3(a0,d3.w),d0 */
  lift_charge(x, 0x90EC);
  lift_call(x, 0x90F0, 4, Roster_DrawStatColumnEntry);  /* bsr.w sub_9112 */
  if (x->declined) return;
  lift_w16(x, 0xFFB02C, alu_movew(c, 0xE000));    /* move.w #$E000,($B02C).w */
  lift_charge(x, 0x90F4);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x12));  /* move.w #$12,($B028).w */
  lift_charge(x, 0x90FA);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 4)));  /* move.b 4(a0,d3.w),d0 */
  lift_charge(x, 0x9100);
  lift_call(x, 0x9104, 4, Roster_DrawStatColumnEntry);  /* bsr.w sub_9112 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x12));  /* move.w #$12,($B028).w */
  lift_charge(x, 0x9108);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 5)));  /* move.b 5(a0,d3.w),d0 */
  lift_charge(x, 0x910E);
  /* falls through into sub_9112 */
  Roster_DrawStatColumnEntry(x);
}

/*
 * Roster_DrawPenaltyLineAtRow (sub_9020)
 *   in: d0 = packed value whose high word scales the record offset
 * Compute the penalty record's byte offset (6 bytes per entry) into d3,
 * draw the inline home escape, derive the row from the game clock
 * ($D5AE) and render the line. d0 is preserved.
 */
void Roster_DrawPenaltyLineAtRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->d[0]);                          /* move.l d0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge(x, 0x9020);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x9022);
  c->d[3] = alu_moveql(c, 6);                     /* moveq #6,d3 */
  lift_charge(x, 0x9024);
  lift_charge_mulu(x, 0x9026, W(c->d[0]));        /* mulu.w d0,d3 */
  c->d[3] = alu_mulu(c, W(c->d[0]), c->d[3]);
  lift_call(x, 0x9028, 6, Text_DrawInlineString); /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d0 */
  lift_charge(x, 0x9034);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 3));     /* lsr.w #3,d0 */
  lift_charge(x, 0x9038);
  setw(&c->d[0], alu_subw(c, 3, W(c->d[0])));     /* subq.w #3,d0 */
  lift_charge(x, 0x903A);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));  /* and.w #$1F,d0 */
  lift_charge(x, 0x903C);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[0])));  /* move.w d0,($B02A).w */
  lift_charge(x, 0x9040);
  lift_call(x, 0x9044, 4, Roster_DrawPenaltyLine);      /* bsr.w sub_907C */
  if (x->declined) return;
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));   /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x9048);
  lift_charge(x, 0x904A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 8 ---- */

void Calc_HalvingAccumulator(lift_ctx *);   /* sub_1828A, math.c */
void Text_DrawLabelAndNumber(lift_ctx *);   /* sub_13454, this file */

/*
 * Board_DrawStatPanel (sub_133B2)
 *   in: d3 = panel selector (negative bails), a0 = the panel record
 * Set up the status banner, draw its frame, blank the caption row, then
 * centre the label from word_191E4 (indexed by 8(a0)-1) and render the
 * record's two label/number pairs underneath.
 */
void Board_DrawStatPanel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int mi, ne;

  alu_tstw(c, W(c->d[3]));                        /* tst.w d3 */
  lift_charge(x, 0x133B2);
  mi = c->nf;
  lift_charge_bcc(x, 0x133B4, mi);                /* bmi.w locret_15464 */
  if (mi)
  {
    lift_charge(x, 0x15464);                      /* the shared bare rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  lift_call(x, 0x133B8, 4, Text_SetupStatusBanner);   /* bsr.w sub_134E4 */
  if (x->declined) return;
  lift_call(x, 0x133BC, 4, Text_DrawFrame);           /* bsr.w sub_119B8 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL,                       /* addq.w #1,($B028).w */
           alu_addw(c, 1, W(lift_r16(x, T_CURSOR_COL))));
  lift_charge(x, 0x133C0);
  lift_w16(x, T_CURSOR_ROW,                       /* subq.w #4,($B02A).w */
           alu_subw(c, 4, W(lift_r16(x, T_CURSOR_ROW))));
  lift_charge(x, 0x133C4);
  {
    uint32_t v = lift_r16(x, T_CURSOR_COL);       /* move.w ($B028).w,-(sp) */
    alu_movew(c, v);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], v);
    lift_charge(x, 0x133C8);
  }
  lift_call(x, 0x133CC, 4, Calc_HalvingAccumulator);  /* bsr.w sub_1828A */
  if (x->declined) return;
  lift_w16(x, 0xFFB02C, alu_movew(c, 0xA000));    /* move.w #$A000,($B02C).w */
  lift_charge(x, 0x133D0);
  lift_call(x, 0x133D6, 4, Text_DrawInlineString);    /* bsr.w sub_11B92 + $1A */
  if (x->declined) return;

  /* loc_133F4 */
  lift_w16(x, T_CURSOR_COL,                       /* move.w (sp),($B028).w */
           alu_movew(c, lift_r16(x, c->a[7])));
  lift_charge(x, 0x133F4);
  c->d[0] = alu_moveql(c, 1);                     /* moveq #1,d0 */
  lift_charge(x, 0x133F8);
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFCEEC), W(c->d[0])));  /* add.w ($CEEC).w,d0 */
  lift_charge(x, 0x133FA);
  alu_tstw(c, lift_r16(x, 0xFFD048));             /* tst.w ($D048).w */
  lift_charge(x, 0x133FE);
  ne = !c->zf;
  lift_charge_bcc(x, 0x13402, ne);                /* bne.w loc_13408 */
  if (!ne)
  {
    setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
    lift_charge(x, 0x13406);
  }
  /* loc_13408 */
  c->a[1] = 0x00013488;                           /* lea word_13488(pc),a1 */
  lift_charge(x, 0x13408);
  lift_call(x, 0x1340C, 4, Text_SkipChunksThenDraw);  /* bsr.w sub_13508 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 8)));  /* move.w 8(a0),d0 */
  lift_charge(x, 0x13410);
  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));     /* subq.w #1,d0 */
  lift_charge(x, 0x13414);
  c->a[1] = 0x000191E4;                           /* movea.l #word_191E4,a1 */
  lift_charge(x, 0x13416);
  lift_call(x, 0x1341C, 4, Ptr_ChainAdd);         /* bsr.w sub_13510 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));  /* move.w (a1),d0 */
  lift_charge(x, 0x13420);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));     /* lsr.w #1,d0 */
  lift_charge(x, 0x13422);
  setw(&c->d[0], alu_negw(c, W(c->d[0])));        /* neg.w d0 */
  lift_charge(x, 0x13424);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[7]), W(c->d[0])));  /* add.w (sp),d0 */
  lift_charge(x, 0x13426);
  setw(&c->d[0], alu_addw(c, 0x17, W(c->d[0])));  /* add.w #$17,d0 */
  lift_charge(x, 0x13428);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, W(c->d[0])));  /* move.w d0,($B028).w */
  lift_charge(x, 0x1342C);
  lift_call(x, 0x13430, 4, Text_DrawString);      /* bsr.w sub_11BA4 */
  if (x->declined) return;
  lift_w16(x, 0xFFB02C, alu_movew(c, 0x8000));    /* move.w #$8000,($B02C).w */
  lift_charge(x, 0x13434);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + 2)));   /* move.w 2(a0),d0 */
  lift_charge(x, 0x1343A);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0xC)));  /* move.w $C(a0),d1 */
  lift_charge(x, 0x1343E);
  lift_call(x, 0x13442, 4, Text_DrawLabelAndNumber);   /* bsr.w sub_13454 */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));       /* move.w (a0),d0 */
  lift_charge(x, 0x13446);
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[0] + 0xA)));  /* move.w $A(a0),d1 */
  lift_charge(x, 0x13448);
  lift_call(x, 0x1344C, 4, Text_DrawLabelAndNumber);   /* bsr.w sub_13454 */
  if (x->declined) return;
  c->a[7] += 2;                                   /* addq.w #2,sp */
  lift_charge(x, 0x13450);
  lift_charge(x, 0x13452);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 43, batch 9 — the other two loc_FA7B0 entry points ---- */

/*
 * Board_SetCrowdCaption (sub_FA6E8)
 * Latch " dB Crowd Level" as the board caption and render through the
 * shared tail.
 */
void Board_SetCrowdCaption(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w32(x, 0xFFBF18, alu_movel(c, 0x000FA6F6));  /* move.l #word_FA6F6,($BF18).l */
  lift_charge(x, 0xFA6E8);
  lift_charge_bcc(x, 0xFA6F2, 1);                 /* bra.w loc_FA7B0 */
  Board_RenderCaptionedLine(x);
}

/*
 * Board_DrawSaveLine (sub_FA708)
 *   in: d2 = count byte, d3 = "by" selector
 * The saves twin of Board_DrawScoringLine: pick " Save(s) [by]" as the
 * caption — the singular variant is the next chunk along, reached by
 * stepping a1 over its own length word when the count is 1 — then render
 * through the shared tail.
 */
void Board_DrawSaveLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, ne;

  c->a[7] -= 12;                                  /* movem.l a1-a3,-(sp) */
  lift_w32(x, c->a[7],     c->a[1]);
  lift_w32(x, c->a[7] + 4, c->a[2]);
  lift_w32(x, c->a[7] + 8, c->a[3]);
  lift_charge_movem(x, 0xFA708);

  c->a[1] = 0x000FA734;                           /* movea.l #word_FA734,a1 */
  lift_charge(x, 0xFA70C);
  alu_tstw(c, W(c->d[3]));                        /* tst.w d3 */
  lift_charge(x, 0xFA712);
  t = c->zf;
  lift_charge_bcc(x, 0xFA714, t);                 /* beq.w loc_FA71E */
  if (!t)
  {
    c->a[1] = 0x000FA746;                         /* movea.l #word_FA746,a1 */
    lift_charge(x, 0xFA718);
  }
  /* loc_FA71E */
  alu_cmpb(c, 1, c->d[2]);                        /* cmp.b #1,d2 */
  lift_charge(x, 0xFA71E);
  ne = !c->zf;
  lift_charge_bcc(x, 0xFA722, ne);                /* bne.w loc_FA728 */
  if (!ne)
  {
    c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1]));  /* add.w (a1),a1 — adda */
    lift_charge(x, 0xFA726);
  }
  /* loc_FA728 */
  lift_w32(x, 0xFFBF18, alu_movel(c, c->a[1]));   /* move.l a1,($BF18).w */
  lift_charge(x, 0xFA728);
  c->a[1] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,a1-a3 */
  c->a[2] = lift_r32(x, c->a[7] + 4);
  c->a[3] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0xFA72C);
  lift_charge_bcc(x, 0xFA730, 1);                 /* bra.w loc_FA7B0 */
  Board_RenderCaptionedLine(x);
}

/* ---- wave 44, the Scc batch: the value-slider widget ---- */

/*
 * The slider widget behind the pre-game option screens: $FFFFD5AE is the
 * current value, $FFFFD5B2 its maximum, $FFFFD5B0 the pending step the
 * pad handler at $8F82 stores (+2 / -2). Redrawing a value repaints the
 * bar tiles through the data port and, on a 32-tick boundary, the two
 * range markers that grey out at either end of the travel.
 */

void Slider_SetValueAndRedraw(lift_ctx *);       /* sub_8FB8, below */
void Slider_DrawArrows(lift_ctx *);              /* sub_93C4, below */
void Roster_DrawPenaltyLineAtRowLower(lift_ctx *); /* sub_904C, below */

/*
 * Slider_ApplyPendingStep (sub_8FA0)
 * Add the pending step ($D5B0) to the value ($D5AE) and redraw, unless
 * there is no step, or the result would leave [0, $D5B2]. Every bail is
 * the shared bare rts at locret_A9D4.
 */
void Slider_ApplyPendingStep(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5B0)));  /* move.w ($D5B0).w,d0 */
  lift_charge(x, 0x8FA0);
  t = c->zf;
  lift_charge_bcc(x, 0x8FA4, t);                  /* beq.w locret_A9D4 */
  if (t) goto bail;

  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFD5AE), W(c->d[0])));  /* add.w ($D5AE).w,d0 */
  lift_charge(x, 0x8FA8);
  t = c->nf;
  lift_charge_bcc(x, 0x8FAC, t);                  /* bmi.w locret_A9D4 */
  if (t) goto bail;

  alu_cmpw(c, lift_r16(x, 0xFFD5B2), W(c->d[0])); /* cmp.w ($D5B2).w,d0 */
  lift_charge(x, 0x8FB0);
  t = (!c->zf && (c->nf == c->vf));
  lift_charge_bcc(x, 0x8FB4, t);                  /* bgt.w locret_A9D4 */
  if (t) goto bail;

  /* falls through into sub_8FB8 */
  Slider_SetValueAndRedraw(x);
  return;

bail:
  lift_charge(x, 0xA9D4);                         /* the shared bare rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Slider_SetValueAndRedraw (sub_8FB8)
 *   in: d0.w = the new slider value
 * Store it, and when it lands on a multiple of 32 repaint the range
 * markers and drop the pending step. When the PREVIOUS value was a
 * multiple of 32 the two penalty-box lines whose row the value selects
 * are repainted too (remainder $1E = the upper block, 2 = the lower).
 * Finally the bar tile itself is written straight out the data port
 * with the ISR guard bit held.
 */
void Slider_SetValueAndRedraw(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int ne;

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d1 */
  lift_charge(x, 0x8FB8);
  lift_w16(x, 0xFFD5AE, alu_movew(c, W(c->d[0])));      /* move.w d0,($D5AE).w */
  lift_charge(x, 0x8FBC);
  c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
  lift_charge(x, 0x8FC0);
  lift_charge_divs(x, 0x8FC2, 0x20, c->d[0]);     /* divs.w #$20,d0 */
  if (x->declined) return;
  c->d[0] = alu_divs(c, 0x20, c->d[0]);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 — remainder low */
  lift_charge(x, 0x8FC6);
  alu_tstw(c, W(c->d[0]));                        /* tst.w d0 */
  lift_charge(x, 0x8FC8);
  ne = !c->zf;
  lift_charge_bcc(x, 0x8FCA, ne);                 /* bne.w loc_8FD6 */
  if (!ne)
  {
    lift_call(x, 0x8FCE, 4, Slider_DrawArrows);   /* bsr.w sub_93C4 */
    if (x->declined) return;
    lift_w16(x, 0xFFD5B0, alu_movew(c, 0));       /* clr.w ($D5B0).w */
    lift_charge(x, 0x8FD2);
  }

  /* loc_8FD6 */
  setw(&c->d[1], alu_andw(c, 0x1F, W(c->d[1])));  /* and.w #$1F,d1 */
  lift_charge(x, 0x8FD6);
  ne = !c->zf;
  lift_charge_bcc(x, 0x8FDA, ne);                 /* bne.w loc_8FFA */
  if (!ne)
  {
    alu_movel(c, c->d[0]);                        /* move.l d0,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->d[0]);
    lift_charge(x, 0x8FDE);
    alu_cmpw(c, 0x1E, W(c->d[0]));                /* cmp.w #$1E,d0 */
    lift_charge(x, 0x8FE0);
    ne = !c->zf;
    lift_charge_bcc(x, 0x8FE4, ne);               /* bne.w loc_8FEC */
    if (!ne)
    {
      lift_call(x, 0x8FE8, 4, Roster_DrawPenaltyLineAtRow);  /* bsr.w sub_9020 */
      if (x->declined) return;
    }
    /* loc_8FEC */
    c->d[0] = alu_movel(c, lift_r32(x, c->a[7])); /* move.l (sp)+,d0 */
    c->a[7] += 4;
    lift_charge(x, 0x8FEC);
    alu_cmpw(c, 2, W(c->d[0]));                   /* cmp.w #2,d0 */
    lift_charge(x, 0x8FEE);
    ne = !c->zf;
    lift_charge_bcc(x, 0x8FF2, ne);               /* bne.w loc_8FFA */
    if (!ne)
    {
      lift_call(x, 0x8FF6, 4, Roster_DrawPenaltyLineAtRowLower);  /* bsr.w sub_904C */
      if (x->declined) return;
    }
  }

  /* loc_8FFA */
  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x8FFA);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x8FFE);
  }
  c->a[0] = 0x00C00000;                           /* move.l #$C00000,a0 — movea */
  lift_charge(x, 0x9004);

  alu_movel(c, 0x40020010);                       /* move.l #$40020010,4(a0) */
  lift_whw_ctrl32(x, 0x900A, 0x40020010);         /* ctrl port: address setup */
  if (x->declined) return;

  c->d[0] = alu_moveql(c, -0x50);                 /* moveq #$FFFFFFB0,d0 */
  lift_charge(x, 0x9012);
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFD5AE), W(c->d[0])));  /* add.w ($D5AE).w,d0 */
  lift_charge(x, 0x9014);

  alu_movew(c, W(c->d[0]));                       /* move.w d0,(a0) */
  lift_whw_data16(x, 0x9018, W(c->d[0]));
  if (x->declined) return;

  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x901A);
  }
  lift_charge(x, 0x901E);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_DrawPenaltyLineAtRowLower (sub_904C)
 *   in: d0 = packed value whose high word scales the record offset
 * The lower-block twin of Roster_DrawPenaltyLineAtRow: the same 6-byte
 * records, biased four entries in, drawn $10 rows further down (instead
 * of three rows up). d0 is preserved.
 */
void Roster_DrawPenaltyLineAtRowLower(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->d[0]);                          /* move.l d0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge(x, 0x904C);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x904E);
  setw(&c->d[0], alu_addw(c, 4, W(c->d[0])));     /* addq.w #4,d0 */
  lift_charge(x, 0x9050);
  c->d[3] = alu_moveql(c, 6);                     /* moveq #6,d3 */
  lift_charge(x, 0x9052);
  lift_charge_mulu(x, 0x9054, W(c->d[0]));        /* mulu.w d0,d3 */
  c->d[3] = alu_mulu(c, W(c->d[0]), c->d[3]);
  lift_call(x, 0x9056, 6, Text_DrawInlineString); /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d0 */
  lift_charge(x, 0x9062);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 3));     /* lsr.w #3,d0 */
  lift_charge(x, 0x9066);
  setw(&c->d[0], alu_addw(c, 0x10, W(c->d[0])));  /* add.w #$10,d0 */
  lift_charge(x, 0x9068);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));  /* and.w #$1F,d0 */
  lift_charge(x, 0x906C);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[0])));  /* move.w d0,($B02A).w */
  lift_charge(x, 0x9070);
  lift_call(x, 0x9074, 4, Roster_DrawPenaltyLine);      /* bsr.w sub_907C */
  if (x->declined) return;
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));   /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x9078);
  lift_charge(x, 0x907A);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Slider_DrawArrows (sub_93C4)
 * Draw the slider's caption (the inline table string at $93CE), then
 * pick one of the four marker chunks at word_9400 by whether the value
 * is above zero (+1) and below its maximum (+2) — so the pair greys out
 * independently at either end of the travel. d0/d1/a1 are preserved.
 */
void Slider_DrawArrows(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sc;

  c->a[7] -= 12;                                  /* movem.l d0-d1/a1,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[1]);
  lift_charge_movem(x, 0x93C4);

  lift_call(x, 0x93C8, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A inline */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x93D8);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d1 */
  lift_charge(x, 0x93DA);
  alu_tstw(c, W(c->d[1]));                        /* tst.w d1 */
  lift_charge(x, 0x93DE);
  sc = (!c->zf && (c->nf == c->vf)) ? 0xFF : 0x00;   /* sgt d0 */
  setb(&c->d[0], sc);
  lift_charge_scc(x, 0x93E0, sc);
  setb(&c->d[0], alu_negb(c, c->d[0]));           /* neg.b d0 */
  lift_charge(x, 0x93E2);
  alu_cmpw(c, lift_r16(x, 0xFFD5B2), W(c->d[1])); /* cmp.w ($D5B2).w,d1 */
  lift_charge(x, 0x93E4);
  sc = (c->nf != c->vf) ? 0xFF : 0x00;            /* slt d1 */
  setb(&c->d[1], sc);
  lift_charge_scc(x, 0x93E8, sc);
  setb(&c->d[1], alu_negb(c, c->d[1]));           /* neg.b d1 */
  lift_charge(x, 0x93EA);
  setb(&c->d[0], alu_addb(c, c->d[1], c->d[0]));  /* add.b d1,d0 */
  lift_charge(x, 0x93EC);
  setb(&c->d[0], alu_addb(c, c->d[1], c->d[0]));  /* add.b d1,d0 */
  lift_charge(x, 0x93EE);
  c->a[1] = 0x00009400;                           /* lea word_9400(pc),a1 */
  lift_charge(x, 0x93F0);

  lift_call(x, 0x93F4, 6, Text_SkipChunksThenDraw);  /* jsr sub_13508 */
  if (x->declined) return;

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d1/a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[1] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x93FA);
  lift_charge(x, 0x93FE);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Slider_DrawArrowsAlt (sub_99C6)
 * The same widget one screen over: identical logic, its own caption
 * ($99D0, cursor two columns left and two rows down) and its own marker
 * table at word_9A02.
 */
void Slider_DrawArrowsAlt(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sc;

  c->a[7] -= 12;                                  /* movem.l d0-d1/a1,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->d[1]);
  lift_w32(x, c->a[7] + 8, c->a[1]);
  lift_charge_movem(x, 0x99C6);

  lift_call(x, 0x99CA, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A inline */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x99DA);
  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d1 */
  lift_charge(x, 0x99DC);
  alu_tstw(c, W(c->d[1]));                        /* tst.w d1 */
  lift_charge(x, 0x99E0);
  sc = (!c->zf && (c->nf == c->vf)) ? 0xFF : 0x00;   /* sgt d0 */
  setb(&c->d[0], sc);
  lift_charge_scc(x, 0x99E2, sc);
  setb(&c->d[0], alu_negb(c, c->d[0]));           /* neg.b d0 */
  lift_charge(x, 0x99E4);
  alu_cmpw(c, lift_r16(x, 0xFFD5B2), W(c->d[1])); /* cmp.w ($D5B2).w,d1 */
  lift_charge(x, 0x99E6);
  sc = (c->nf != c->vf) ? 0xFF : 0x00;            /* slt d1 */
  setb(&c->d[1], sc);
  lift_charge_scc(x, 0x99EA, sc);
  setb(&c->d[1], alu_negb(c, c->d[1]));           /* neg.b d1 */
  lift_charge(x, 0x99EC);
  setb(&c->d[0], alu_addb(c, c->d[1], c->d[0]));  /* add.b d1,d0 */
  lift_charge(x, 0x99EE);
  setb(&c->d[0], alu_addb(c, c->d[1], c->d[0]));  /* add.b d1,d0 */
  lift_charge(x, 0x99F0);
  c->a[1] = 0x00009A02;                           /* lea word_9A02(pc),a1 */
  lift_charge(x, 0x99F2);

  lift_call(x, 0x99F6, 6, Text_SkipChunksThenDraw);  /* jsr sub_13508 */
  if (x->declined) return;

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d1/a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[1] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x99FC);
  lift_charge(x, 0x9A00);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 45, the cold leaves ---- */

/*
 * VDP_FlushPalette ($11044; the listing's own label)
 * Copy the whole 64-entry palette buffer at $FFFFBD28 into CRAM: one
 * ctrl-port long puts the VDP in CRAM-write mode, then 32 `move.l
 * (a1)+,(a0)` stream two colour words apiece out the data port (one
 * lift_whw_data32 each — never two data16 calls, which would charge the
 * instruction twice). $FFFFBD26 is set to $FF as the "palette is clean"
 * flag, under the $FFFFBF78 bit-2 ISR guard. d0/a0/a1 preserved.
 */
void VDP_FlushPalette(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 12;                                  /* movem.l d0/a0-a1,-(sp) */
  lift_w32(x, c->a[7],     c->d[0]);
  lift_w32(x, c->a[7] + 4, c->a[0]);
  lift_w32(x, c->a[7] + 8, c->a[1]);
  lift_charge_movem(x, 0x11044);

  {
    uint32_t guard = lift_r16(x, T_VDP_GUARD);    /* move.w ($BF78).w,-(sp) */
    alu_movew(c, guard);
    c->a[7] -= 2;
    lift_w16(x, c->a[7], guard);
    lift_charge(x, 0x11048);
  }
  {
    uint32_t b = lift_r8(x, T_VDP_GUARD);         /* bset #2,($BF78).w */
    lift_w8(x, T_VDP_GUARD, alu_bset(c, b, 2));
    lift_charge(x, 0x1104C);
  }
  c->a[1] = 0xFFFFBD28;                           /* move.w #$BD28,a1 — movea sign-extends */
  lift_charge(x, 0x11052);
  c->a[0] = 0x00C00000;                           /* move.l #$C00000,a0 — movea */
  lift_charge(x, 0x11056);

  alu_movel(c, 0xC0000000);                       /* move.l #$C0000000,4(a0) */
  lift_whw_ctrl32(x, 0x1105C, 0xC0000000);        /* ctrl port: CRAM write mode */
  if (x->declined) return;

  c->d[0] = alu_moveql(c, 0x1F);                  /* moveq #$1F,d0 */
  lift_charge(x, 0x11064);

  for (;;)
  {
    /* loc_11066 */
    uint32_t v = lift_r32(x, c->a[1]);            /* move.l (a1)+,(a0) */
    c->a[1] += 4;
    alu_movel(c, v);
    lift_whw_data32(x, 0x11066, v);
    if (x->declined) return;

    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_11066 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11068, taken, !taken);
      if (!taken) break;
    }
  }

  lift_w8(x, 0xFFBD26, 0xFF);                     /* st ($BD26).w — Scc sets no CCR */
  lift_charge(x, 0x1106C);
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,($BF78).w */
    c->a[7] += 2;
    alu_movew(c, v);
    lift_w16(x, T_VDP_GUARD, v);
    lift_charge(x, 0x11070);
  }
  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0/a0-a1 */
  c->a[0] = lift_r32(x, c->a[7] + 4);
  c->a[1] = lift_r32(x, c->a[7] + 8);
  c->a[7] += 12;
  lift_charge_movem(x, 0x11074);
  lift_charge(x, 0x11078);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Nibble_ExpandToWords (sub_10E88)
 *   in: a0 = packed-nibble stream, d0.w = how many nibbles to expand
 * Expand d0 nibbles (high nibble of each byte first) into consecutive
 * words at $FFFFD036. d2 is the half-byte phase: bchg #0 leaves Z set on
 * the high-nibble pass, which rewinds a0 and shifts the byte down.
 * Everything the routine touches is restored by the movem epilogue —
 * only the $D036 array changes.
 */
void Nibble_ExpandToWords(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 20;                                  /* movem.l d0-d2/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->a[0]);
  lift_w32(x, c->a[7] + 16, c->a[1]);
  lift_charge_movem(x, 0x10E88);

  c->a[1] = 0xFFFFD036;                           /* move.w #$D036,a1 — movea sign-extends */
  lift_charge(x, 0x10E8C);
  setw(&c->d[2], alu_movew(c, 0));                /* clr.w d2 */
  lift_charge(x, 0x10E90);
  lift_charge_bcc(x, 0x10E92, 1);                 /* bra.w loc_10EAA */
  if (W(c->d[0]) == 0xFFFF) { x->declined = 1; return; }  /* dbf would wrap 64K */

  for (;;)
  {
    /* loc_10EAA */
    setw(&c->d[0], W(c->d[0] - 1));               /* dbf d0,loc_10E96 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x10EAA, taken, !taken);
      if (!taken) break;
    }

    /* loc_10E96 */
    {
      int ne;
      setb(&c->d[1], alu_moveb(c, lift_r8(x, c->a[0])));  /* move.b (a0)+,d1 */
      c->a[0] += 1;
      lift_charge(x, 0x10E96);
      c->d[2] = alu_bchg(c, c->d[2], 0);           /* bchg #0,d2 — 32-bit on Dn, Z from the old bit */
      lift_charge(x, 0x10E98);
      ne = !c->zf;
      lift_charge_bcc(x, 0x10E9C, ne);             /* bne.w loc_10EA4 */
      if (!ne)
      {
        c->a[0] -= 1;                              /* subq.w #1,a0 — full 32 bits, no CCR */
        lift_charge(x, 0x10EA0);
        setw(&c->d[1], alu_lsrw(c, W(c->d[1]), 4)); /* lsr.w #4,d1 */
        lift_charge(x, 0x10EA2);
      }
      /* loc_10EA4 */
      setw(&c->d[1], alu_andw(c, 0xF, W(c->d[1])));  /* and.w #$F,d1 */
      lift_charge(x, 0x10EA4);
      lift_w16(x, c->a[1], alu_movew(c, W(c->d[1])));  /* move.w d1,(a1)+ */
      c->a[1] += 2;
      lift_charge(x, 0x10EA8);
    }
  }

  c->d[0] = lift_r32(x, c->a[7]);                 /* movem.l (sp)+,d0-d2/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->a[0] = lift_r32(x, c->a[7] + 12);
  c->a[1] = lift_r32(x, c->a[7] + 16);
  c->a[7] += 20;
  lift_charge_movem(x, 0x10EAE);
  lift_charge(x, 0x10EB2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawDashedDigitPair (sub_175FC)
 *   in: a2 = a record whose words at +4 and +6 are two single digits
 * Build the four-byte chunk "<d>-<d>" (plus its leading length word) in
 * the scratch buffer at $FFFFBFA4 and tail into Text_DrawString.
 */
void Text_DrawDashedDigitPair(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = 0xFFFFBFA4;                           /* move.w #$BFA4,a1 — movea sign-extends */
  lift_charge(x, 0x175FC);
  lift_w16(x, c->a[1], alu_movew(c, 6));          /* move.w #6,(a1)+ */
  c->a[1] += 2;
  lift_charge(x, 0x17600);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 4)));  /* move.w 4(a2),d0 */
  lift_charge(x, 0x17604);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));  /* add.w #$30,d0 */
  lift_charge(x, 0x17608);
  lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));     /* move.b d0,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x1760C);
  lift_w8(x, c->a[1], alu_moveb(c, 0x2D));        /* move.b #$2D,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x1760E);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 6)));  /* move.w 6(a2),d0 */
  lift_charge(x, 0x17612);
  setw(&c->d[0], alu_addw(c, 0x30, W(c->d[0])));  /* add.w #$30,d0 */
  lift_charge(x, 0x17616);
  lift_w8(x, c->a[1], alu_moveb(c, c->d[0]));     /* move.b d0,(a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x1761A);
  lift_w8(x, c->a[1], alu_moveb(c, 0));           /* clr.b (a1)+ */
  c->a[1] += 1;
  lift_charge(x, 0x1761C);
  c->a[1] = 0xFFFFBFA4;                           /* move.w #$BFA4,a1 — movea */
  lift_charge(x, 0x1761E);
  lift_charge_bcc(x, 0x17622, 1);                 /* bra.w sub_11BA4 */
  Text_DrawString(x);
}

/*
 * Text_HexToAscii8 (sub_18CDC)
 *   in: d0 = the value, a0 = the output buffer
 * Write d0 as eight ASCII hex digits, most significant first. Eight
 * `rol.l #4` leave d0 exactly as it was found; a0 ends 8 bytes on.
 */
void Text_HexToAscii8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[2] = alu_moveql(c, 7);                     /* moveq #7,d2 */
  lift_charge(x, 0x18CDC);

  for (;;)
  {
    /* loc_18CDE */
    int le;
    c->d[0] = alu_roll(c, c->d[0], 4);            /* rol.l #4,d0 */
    lift_charge(x, 0x18CDE);
    setw(&c->d[1], alu_movew(c, W(c->d[0])));     /* move.w d0,d1 */
    lift_charge(x, 0x18CE0);
    setw(&c->d[1], alu_andw(c, 0xF, W(c->d[1]))); /* and.w #$F,d1 */
    lift_charge(x, 0x18CE2);
    setw(&c->d[1], alu_addw(c, 0x30, W(c->d[1])));/* add.w #$30,d1 */
    lift_charge(x, 0x18CE6);
    alu_cmpw(c, 0x39, W(c->d[1]));                /* cmp.w #$39,d1 */
    lift_charge(x, 0x18CEA);
    le = (c->zf || (c->nf != c->vf));
    lift_charge_bcc(x, 0x18CEE, le);              /* ble.w loc_18CF4 */
    if (!le)
    {
      setw(&c->d[1], alu_addw(c, 7, W(c->d[1]))); /* addq.w #7,d1 */
      lift_charge(x, 0x18CF2);
    }
    /* loc_18CF4 */
    lift_w8(x, c->a[0], alu_moveb(c, c->d[1]));   /* move.b d1,(a0)+ */
    c->a[0] += 1;
    lift_charge(x, 0x18CF4);

    setw(&c->d[2], W(c->d[2] - 1));               /* dbf d2,loc_18CDE */
    {
      int taken = (W(c->d[2]) != 0xFFFF);
      lift_charge_dbcc(x, 0x18CF6, taken, !taken);
      if (!taken) break;
    }
  }

  lift_charge(x, 0x18CFA);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ---- wave 47, the text-family cold tail ---- */

void Roster_DrawStatusLine(lift_ctx *);          /* sub_9316, below */

/*
 * Roster_DrawStatusLine (sub_9316)
 *   in: d3 = the $C5DE record's byte offset
 * The $C5DE twin of Roster_DrawPenaltyLine: blank three rows, print the
 * period/clock, the player's name from the team block ($364 further in
 * for the away side per bit 7 of the record), then the label the
 * word_18E0C table selects for the record's code — its count byte
 * through sub_11D06, its name, the player number through sub_18AE8 —
 * and tail into Text_DrawString for the label's trailing text.
 */
void Roster_DrawStatusLine(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;
  uint32_t v;

  v = lift_r16(x, T_CURSOR_ROW);                  /* move.w ($B02A).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0x9316);
  c->d[0] = alu_moveql(c, 0x28);                  /* moveq #$28,d0 */
  lift_charge(x, 0x931A);
  c->d[1] = alu_moveql(c, 3);                     /* moveq #3,d1 */
  lift_charge(x, 0x931C);
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0x931E);
  lift_call(x, 0x9322, 6, Text_FillRows);         /* jsr sub_1197E */
  if (x->declined) return;
  v = lift_r16(x, c->a[7]); c->a[7] += 2;         /* move.w (sp)+,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, v));
  lift_charge(x, 0x9328);
  c->a[0] = 0xFFFFC5DE;                           /* movea.w #$C5DE,a0 — sign-extends */
  lift_charge(x, 0x932C);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0] + SEW(c->d[3]))));  /* move.w (a0,d3.w),d0 */
  lift_charge(x, 0x9330);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 1));     /* move.w #1,($B028).w */
  lift_charge(x, 0x9334);
  lift_call(x, 0x933A, 6, Text_DrawPeriodAndClock);  /* jsr sub_11C72 */
  if (x->declined) return;
  c->a[2] = 0xFFFFC6CE;                           /* movea.w #$C6CE,a2 — sign-extends */
  lift_charge(x, 0x9340);
  alu_btst(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 2), 7);  /* btst #7,2(a0,d3.w) */
  lift_charge(x, 0x9344);
  t = c->zf;
  lift_charge_bcc(x, 0x934A, t);                  /* beq.w loc_9352 */
  if (!t)
  {
    c->a[2] = c->a[2] + 0x364;                    /* add.w #$364,a2 — adda */
    lift_charge(x, 0x934E);
  }

  /* loc_9352 */
  c->a[1] = lift_r32(x, c->a[2] + 0x1E);          /* movea.l $1E(a2),a1 */
  lift_charge(x, 0x9352);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1] + 4));  /* add.w 4(a1),a1 — adda */
  lift_charge(x, 0x9356);
  c->a[1] = c->a[1] + SEW(lift_r16(x, c->a[1]));  /* add.w (a1),a1 — adda */
  lift_charge(x, 0x935A);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x0C));  /* move.w #$C,($B028).w */
  lift_charge(x, 0x935C);
  lift_call(x, 0x9362, 6, Text_DrawString);       /* jsr sub_11BA4 */
  if (x->declined) return;
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 2)));  /* move.b 2(a0,d3.w),d0 */
  lift_charge(x, 0x9368);
  setw(&c->d[0], alu_andw(c, 0x7F, W(c->d[0])));  /* and.w #$7F,d0 */
  lift_charge(x, 0x936C);
  c->a[3] = 0x00018E0C;                           /* movea.l #word_18E0C,a3 */
  lift_charge(x, 0x9370);
  c->a[3] = c->a[3] + SEW(lift_r16(x, c->a[3] + SEW(c->d[0])));  /* add.w (a3,d0.w),a3 — adda */
  lift_charge(x, 0x9376);
  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x937A);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[3] + 1)));  /* move.b 1(a3),d0 */
  lift_charge(x, 0x937C);
  lift_call(x, 0x9380, 6, sub_11D06);             /* jsr sub_11D06 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x23));  /* move.w #$23,($B028).w */
  lift_charge(x, 0x9386);
  lift_call(x, 0x938C, 6, Text_DrawString);       /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x10));  /* move.w #$10,($B028).w */
  lift_charge(x, 0x9392);
  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x9398);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] + SEW(c->d[3]) + 3)));  /* move.b 3(a0,d3.w),d0 */
  lift_charge(x, 0x939A);
  lift_call(x, 0x939E, 6, sub_18AE8);             /* jsr sub_18AE8 */
  if (x->declined) return;
  lift_call(x, 0x93A4, 6, Text_DrawString);       /* jsr sub_11BA4 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_ROW,                       /* addq.w #1,($B02A).w — sets CCR */
           alu_addw(c, 1, lift_r16(x, T_CURSOR_ROW)));
  lift_charge(x, 0x93AA);
  lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x13));  /* move.w #$13,($B028).w */
  lift_charge(x, 0x93AE);
  lift_w16(x, 0xFFB02C, alu_movew(c, 0xE000));    /* move.w #$E000,($B02C).w */
  lift_charge(x, 0x93B4);
  c->a[1] = c->a[3] + 2;                          /* lea 2(a3),a1 — no CCR */
  lift_charge(x, 0x93BA);
  lift_charge(x, 0x93BE);                         /* jmp sub_11BA4 — tail */
  Text_DrawString(x);
}

/*
 * Roster_DrawStatusLineAtRow (sub_92BA)
 *   in: d0 = packed value whose high word scales the record offset
 * Compute the $C5DE record's byte offset (4 bytes per entry) into d3,
 * draw the inline home escape, derive the row from the game clock
 * ($D5AE) and render the line. d0 is preserved.
 */
void Roster_DrawStatusLineAtRow(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->d[0]);                          /* move.l d0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge(x, 0x92BA);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x92BC);
  setw(&c->d[3], alu_movew(c, W(c->d[0])));       /* move.w d0,d3 */
  lift_charge(x, 0x92BE);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));     /* asl.w #2,d3 */
  lift_charge(x, 0x92C0);
  lift_call(x, 0x92C2, 6, Text_DrawInlineString); /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d0 */
  lift_charge(x, 0x92CE);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 3));     /* lsr.w #3,d0 */
  lift_charge(x, 0x92D2);
  setw(&c->d[0], alu_subw(c, 2, W(c->d[0])));     /* subq.w #2,d0 */
  lift_charge(x, 0x92D4);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));  /* and.w #$1F,d0 */
  lift_charge(x, 0x92D6);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[0])));  /* move.w d0,($B02A).w */
  lift_charge(x, 0x92DA);
  lift_call(x, 0x92DE, 4, Roster_DrawStatusLine); /* bsr.w sub_9316 */
  if (x->declined) return;
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));   /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x92E2);
  lift_charge(x, 0x92E4);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Roster_DrawStatusLineAtRowLower (sub_92E6)
 * The lower-block twin of Roster_DrawStatusLineAtRow: five entries
 * further into the $C5DE table, drawn $F rows down instead of two up.
 */
void Roster_DrawStatusLineAtRowLower(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, c->d[0]);                          /* move.l d0,-(sp) */
  c->a[7] -= 4;
  lift_w32(x, c->a[7], c->d[0]);
  lift_charge(x, 0x92E6);
  c->d[0] = alu_swap(c, c->d[0]);                 /* swap d0 */
  lift_charge(x, 0x92E8);
  setw(&c->d[0], alu_addw(c, 5, W(c->d[0])));     /* addq.w #5,d0 */
  lift_charge(x, 0x92EA);
  setw(&c->d[3], alu_movew(c, W(c->d[0])));       /* move.w d0,d3 */
  lift_charge(x, 0x92EC);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));     /* asl.w #2,d3 */
  lift_charge(x, 0x92EE);
  lift_call(x, 0x92F0, 6, Text_DrawInlineString); /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD5AE)));  /* move.w ($D5AE).w,d0 */
  lift_charge(x, 0x92FC);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 3));     /* lsr.w #3,d0 */
  lift_charge(x, 0x9300);
  setw(&c->d[0], alu_addw(c, 0xF, W(c->d[0])));   /* add.w #$F,d0 */
  lift_charge(x, 0x9302);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));  /* and.w #$1F,d0 */
  lift_charge(x, 0x9306);
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, W(c->d[0])));  /* move.w d0,($B02A).w */
  lift_charge(x, 0x930A);
  lift_call(x, 0x930E, 4, Roster_DrawStatusLine); /* bsr.w sub_9316 */
  if (x->declined) return;
  c->d[0] = alu_movel(c, lift_r32(x, c->a[7]));   /* move.l (sp)+,d0 */
  c->a[7] += 4;
  lift_charge(x, 0x9312);
  lift_charge(x, 0x9314);                         /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_DrawFlagLabel (sub_FB6F0)
 * Pick word_FB7F2 or word_FB80A by whether the byte $C * ($D4EA) into
 * the $FFFFD45A table is set, and draw it. Every register is saved and
 * restored by the movem pair.
 */
void Text_DrawFlagLabel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFB6F0);

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFFFD4EA)));  /* move.w ($D4EA).w,d0 */
  lift_charge(x, 0xFB6F4);
  lift_charge_mulu(x, 0xFB6F8, 0xC);              /* mulu.w #$C,d0 */
  c->d[0] = alu_mulu(c, 0xC, c->d[0]);
  c->a[0] = 0xFFFFD45A;                           /* move.l #$FFFFD45A,a0 — movea */
  lift_charge(x, 0xFB6FC);

  /* loc_FB702 */
  c->a[1] = 0x000FB7F2;                           /* move.l #word_FB7F2,a1 — movea */
  lift_charge(x, 0xFB702);
  alu_tstb(c, lift_r8(x, c->a[0] + SEW(c->d[0])));  /* tst.b (a0,d0.w) */
  lift_charge(x, 0xFB708);
  t = c->zf;
  lift_charge_bcc(x, 0xFB70C, t);                 /* beq.w loc_FB716 */
  if (!t)
  {
    c->a[1] = 0x000FB80A;                         /* move.l #word_FB80A,a1 — movea */
    lift_charge(x, 0xFB710);
  }

  /* loc_FB716 */
  lift_call(x, 0xFB716, 6, Text_DrawString);      /* jsr sub_11BA4 */
  if (x->declined) return;

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);  /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFB71C);
  lift_charge(x, 0xFB720);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Text_ClearListBody (sub_FCA04)
 * Draw the inline chunk at $FCA0A, then blank ten rows of 40 columns
 * with tile $7FF by tailing into Text_FillRows.
 */
void Text_ClearListBody(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0xFCA04, 6, Text_DrawInlineString);  /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  c->d[0] = alu_moveql(c, 0x28);                  /* moveq #$28,d0 */
  lift_charge(x, 0xFCA10);
  c->d[1] = alu_moveql(c, 0xA);                   /* moveq #$A,d1 */
  lift_charge(x, 0xFCA12);
  setw(&c->d[2], alu_movew(c, 0x7FF));            /* move.w #$7FF,d2 */
  lift_charge(x, 0xFCA14);
  lift_charge(x, 0xFCA18);                        /* jmp sub_1197E — tail */
  Text_FillRows(x);
}

/*
 * Text_ClearPeriodLabelBlock (sub_18A56; called from sub_B92E:loc_B946)
 * The short branch of the period-label overlay: when the cursor row sits
 * high enough on the plane ($B02A < $F), reposition with the inline
 * chunk at $18A5A and blank a $13 x 8 block with tile $7FF by tailing
 * into Text_FillRows.
 */
void Text_ClearPeriodLabelBlock(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_call(x, 0x18A56, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;
  c->d[0] = alu_moveql(c, 0x13);                  /* moveq #$13,d0 */
  lift_charge(x, 0x18A60);
  c->d[1] = alu_moveql(c, 8);                     /* moveq #8,d1 */
  lift_charge(x, 0x18A62);
  c->d[2] = alu_movel(c, 0x7FF);                  /* move.l #$7FF,d2 */
  lift_charge(x, 0x18A64);
  lift_charge_bcc(x, 0x18A6A, 1);                 /* bra.w sub_1197E — tail */
  Text_FillRows(x);
}

/* ===========================================================================
 * The sub_1169A/sub_11738 sprawl — the RLE unpacker chain
 * (endgame Item 2, owner decision (b+) 2026-08-04: the mode-split lift).
 *
 * $FFFFCF32 is a cache-LUT pointer: Unpack_BlockCached (sub_1172C) points
 * it at its caller's inline 8-byte nibble LUT before decoding;
 * Unpack_BlockDirect (sub_11738) clears it and falls through into
 * Unpack_Block. Every flush site branches on it — cache set flushes go
 * through unpack_blit_nibble_page (loc_10EE0: VDP_SetAddress + a pure
 * data-port PIO stream), cache null flushes go loc_113D0 -> sub_113E4 ->
 * VDP_TransferDMA, which is runtime-core (HW-STAGING.md ADDENDUM (2)) —
 * the lift DECLINES on that branch, by design.
 *
 * The RLE interpreter (sub_1177A) dispatches the high nibble of each
 * stream byte through the static ROM offset table word_117B2 (16 entries,
 * 11 unique handlers — unlabeled chunks, lifted as static functions per
 * the Board_RenderCaptionedLine precedent). Output is the 256-byte ring
 * page at $FFFFCF36 (byte index d1, wrapping via addq.b), flushed on
 * every wrap and on a nonempty partial page at the terminator opcode
 * (high nibble E with a zero distance byte). d3 carries the VRAM byte
 * cursor across flushes. No iteration cap is needed: every non-terminator
 * opcode stages at least one ring write, so LIFT_WLOG_MAX bounds the
 * interpreter loop (logw declines at the cap).
 */

/*
 * loc_10EE0 — the cache flush chunk (no registry entry: reachable only
 * through the composed pea/jsr paths). In: d0 = source word count,
 * d1 = VRAM byte address (exg'd immediately), a0 = source words,
 * a1 = nibble LUT. Each source word's four nibbles select LUT half-bytes
 * (even nibble value -> high half of the LUT byte) rotated together into
 * one output word streamed out the data port.
 */
static void unpack_blit_nibble_page(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t tmp;

  tmp = lift_r16(x, T_VDP_GUARD);               /* move.w ($BF78).w,-(sp) */
  alu_movew(c, tmp);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], tmp);
  lift_charge(x, 0x10EE0);
  tmp = lift_r8(x, T_VDP_GUARD);                /* bset #2,($BF78).w */
  lift_w8(x, T_VDP_GUARD, alu_bset(c, tmp, 2));
  lift_charge(x, 0x10EE4);

  c->a[7] -= 32;                                /* movem.l d0-d4/a0-a2,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->d[3]);
  lift_w32(x, c->a[7] + 16, c->d[4]);
  lift_w32(x, c->a[7] + 20, c->a[0]);
  lift_w32(x, c->a[7] + 24, c->a[1]);
  lift_w32(x, c->a[7] + 28, c->a[2]);
  lift_charge_movem(x, 0x10EEA);

  tmp = c->d[1];                                /* exg d1,d0 — no flags */
  c->d[1] = c->d[0];
  c->d[0] = tmp;
  lift_charge(x, 0x10EEE);
  c->a[2] = c->a[0];                            /* movea.l a0,a2 */
  lift_charge(x, 0x10EF0);
  lift_call(x, 0x10EF2, 4, VDP_SetAddress);     /* bsr.w VDP_SetAddress */
  if (x->declined) return;
  setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));   /* subq.w #1,d1 */
  lift_charge(x, 0x10EF6);

  for (;;)
  {
    /* loc_10EF8 */
    c->d[0] = alu_moveql(c, 3);                 /* moveq #3,d0 */
    lift_charge(x, 0x10EF8);
    setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[2])));  /* move.w (a2)+,d2 */
    c->a[2] += 2;
    lift_charge(x, 0x10EFA);
    setw(&c->d[3], alu_movew(c, 0));            /* clr.w d3 */
    lift_charge(x, 0x10EFC);
    for (;;)
    {
      /* loc_10EFE */
      setw(&c->d[4], alu_movew(c, W(c->d[2])));      /* move.w d2,d4 */
      lift_charge(x, 0x10EFE);
      setw(&c->d[4], alu_andw(c, 0xF, W(c->d[4])));  /* and.w #$F,d4 */
      lift_charge(x, 0x10F00);
      setw(&c->d[4], alu_lsrw(c, W(c->d[4]), 1));    /* lsr.w #1,d4 */
      lift_charge(x, 0x10F04);
      setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[1] + SEW(c->d[4]))));  /* move.b (a1,d4.w),d4 */
      lift_charge(x, 0x10F06);
      alu_btst(c, c->d[2], 0);                       /* btst #0,d2 */
      lift_charge(x, 0x10F0A);
      lift_charge_bcc(x, 0x10F0E, !c->zf);           /* bne.w loc_10F14 */
      if (c->zf)
      {
        setw(&c->d[4], alu_lsrw(c, W(c->d[4]), 4));  /* lsr.w #4,d4 */
        lift_charge(x, 0x10F12);
      }
      /* loc_10F14 */
      setw(&c->d[4], alu_andw(c, 0xF, W(c->d[4])));  /* and.w #$F,d4 */
      lift_charge(x, 0x10F14);
      setb(&c->d[3], alu_orb(c, c->d[4], c->d[3]));  /* or.b d4,d3 */
      lift_charge(x, 0x10F18);
      setw(&c->d[3], alu_rorw(c, W(c->d[3]), 4));    /* ror.w #4,d3 */
      lift_charge(x, 0x10F1A);
      setw(&c->d[2], alu_rorw(c, W(c->d[2]), 4));    /* ror.w #4,d2 */
      lift_charge(x, 0x10F1C);
      setw(&c->d[0], W(c->d[0] - 1));                /* dbf d0,loc_10EFE */
      {
        int taken = (W(c->d[0]) != 0xFFFF);
        lift_charge_dbcc(x, 0x10F1E, taken, !taken);
        if (!taken) break;
      }
    }
    alu_movew(c, W(c->d[3]));                   /* move.w d3,(a0) — data port */
    lift_whw_data16(x, 0x10F22, W(c->d[3]));
    if (x->declined) return;
    setw(&c->d[1], W(c->d[1] - 1));             /* dbf d1,loc_10EF8 */
    {
      int taken = (W(c->d[1]) != 0xFFFF);
      lift_charge_dbcc(x, 0x10F24, taken, !taken);
      if (!taken) break;
    }
  }

  c->d[0] = lift_r32(x, c->a[7]);               /* movem.l (sp)+,d0-d4/a0-a2 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->d[4] = lift_r32(x, c->a[7] + 16);
  c->a[0] = lift_r32(x, c->a[7] + 20);
  c->a[1] = lift_r32(x, c->a[7] + 24);
  c->a[2] = lift_r32(x, c->a[7] + 28);
  c->a[7] += 32;
  lift_charge_movem(x, 0x10F28);
  tmp = lift_r16(x, c->a[7]);                   /* move.w (sp)+,($BF78).w */
  alu_movew(c, tmp);
  lift_w16(x, T_VDP_GUARD, tmp);
  c->a[7] += 2;
  lift_charge(x, 0x10F2C);
  lift_charge(x, 0x10F30);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Unpack_FlushPage (sub_11924) — flush the ring page (d1 bytes, or a
 * full $100 when d1 wrapped to 0) to VRAM at cursor d3, advancing d3.
 * Cache set: nibble-expand through loc_10EE0. Cache null: the DMA path
 * (jsr (a6) -> loc_113D0) — runtime-core, declined by design.
 */
void Unpack_FlushPage(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 16;                                /* movem.l d0-d1/a0-a1,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_charge_movem(x, 0x11924);

  setw(&c->d[0], alu_movew(c, W(c->d[1])));     /* move.w d1,d0 */
  lift_charge(x, 0x11928);
  lift_charge_bcc(x, 0x1192A, !c->zf);          /* bne.w loc_11932 */
  if (c->zf)
  {
    setw(&c->d[0], alu_movew(c, 0x100));        /* move.w #$100,d0 */
    lift_charge(x, 0x1192E);
  }
  /* loc_11932 */
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 1));   /* lsr.w #1,d0 */
  lift_charge(x, 0x11932);
  setw(&c->d[1], alu_movew(c, W(c->d[3])));     /* move.w d3,d1 */
  lift_charge(x, 0x11934);
  setw(&c->d[3], alu_addw(c, W(c->d[0]), W(c->d[3])));  /* add.w d0,d3 */
  lift_charge(x, 0x11936);
  setw(&c->d[3], alu_addw(c, W(c->d[0]), W(c->d[3])));  /* add.w d0,d3 */
  lift_charge(x, 0x11938);
  c->a[0] = c->a[1];                            /* movea.l a1,a0 */
  lift_charge(x, 0x1193A);
  alu_tstl(c, lift_r32(x, c->a[4]));            /* tst.l (a4) */
  lift_charge(x, 0x1193C);
  /* beq.w loc_1194A — null cache flushes via jsr (a6) = loc_113D0 ->
   * sub_113E4 -> VDP_TransferDMA: runtime-core, not modeled */
  if (c->zf) { x->declined = 1; return; }
  lift_charge_bcc(x, 0x1193E, c->zf);
  c->a[1] = lift_r32(x, c->a[4]);               /* movea.l (a4),a1 */
  lift_charge(x, 0x11942);
  if ((c->a[5] & 0xFFFFFF) != 0x010EE0) { x->declined = 1; return; }
  c->a[7] -= 4;                                 /* jsr (a5) */
  lift_w32(x, c->a[7], 0x011946);
  lift_charge(x, 0x11944);
  unpack_blit_nibble_page(x);
  if (x->declined) return;
  lift_charge_bcc(x, 0x11946, 1);               /* bra.w loc_1194C */

  c->d[0] = lift_r32(x, c->a[7]);               /* movem.l (sp)+,d0-d1/a0-a1 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[7] += 16;
  lift_charge_movem(x, 0x1194C);
  lift_charge(x, 0x11950);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * loc_11840 — the shared forward back-reference tail: d0 = dbf count,
 * d2 = distance. Copies ring[d1-d2] -> ring[d1], both indexes
 * incrementing bytewise, flushing on every page wrap.
 */
static void unpack_op_copy_fwd(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[2], alu_negb(c, c->d[2]));         /* neg.b d2 */
  lift_charge(x, 0x11840);
  setb(&c->d[2], alu_addb(c, c->d[1], c->d[2])); /* add.b d1,d2 */
  lift_charge(x, 0x11842);
  for (;;)
  {
    /* loc_11844: move.b (a1,d2.w),(a1,d1.w) */
    uint32_t b = lift_r8(x, c->a[1] + SEW(c->d[2]));
    alu_moveb(c, b);
    lift_w8(x, c->a[1] + SEW(c->d[1]), b);
    lift_charge(x, 0x11844);
    if (x->declined) return;
    setb(&c->d[2], alu_addb(c, 1, c->d[2]));    /* addq.b #1,d2 */
    lift_charge(x, 0x1184A);
    setb(&c->d[1], alu_addb(c, 1, c->d[1]));    /* addq.b #1,d1 */
    lift_charge(x, 0x1184C);
    lift_charge_bcc(x, 0x1184E, !c->zf);        /* bne.w loc_11856 */
    if (c->zf)
    {
      lift_call(x, 0x11852, 4, Unpack_FlushPage);  /* bsr.w sub_11924 */
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));             /* loc_11856: dbf d0,loc_11844 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11856, taken, !taken);
      if (!taken) break;
    }
  }
  lift_charge(x, 0x1185A);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * loc_118CE — the mirrored tail: source index d2 DECREMENTS while the
 * destination increments (reversed copy), same wrap-flush shape.
 */
static void unpack_op_copy_rev(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[2], alu_negb(c, c->d[2]));         /* neg.b d2 */
  lift_charge(x, 0x118CE);
  setb(&c->d[2], alu_addb(c, c->d[1], c->d[2])); /* add.b d1,d2 */
  lift_charge(x, 0x118D0);
  for (;;)
  {
    /* loc_118D2: move.b (a1,d2.w),(a1,d1.w) */
    uint32_t b = lift_r8(x, c->a[1] + SEW(c->d[2]));
    alu_moveb(c, b);
    lift_w8(x, c->a[1] + SEW(c->d[1]), b);
    lift_charge(x, 0x118D2);
    if (x->declined) return;
    setb(&c->d[2], alu_subb(c, 1, c->d[2]));    /* subq.b #1,d2 */
    lift_charge(x, 0x118D8);
    setb(&c->d[1], alu_addb(c, 1, c->d[1]));    /* addq.b #1,d1 */
    lift_charge(x, 0x118DA);
    lift_charge_bcc(x, 0x118DC, !c->zf);        /* bne.w loc_118E4 */
    if (c->zf)
    {
      lift_call(x, 0x118E0, 4, Unpack_FlushPage);  /* bsr.w sub_11924 */
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));             /* loc_118E4: dbf d0,loc_118D2 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x118E4, taken, !taken);
      if (!taken) break;
    }
  }
  lift_charge(x, 0x118E8);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* $117D2 (nibbles 0-1) — literal run: (op & $1F)+1 stream bytes into the ring */
static void unpack_op_literal(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x117D2);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));          /* and.w #$1F,d0 */
  lift_charge(x, 0x117D6);
  for (;;)
  {
    /* loc_117DA: move.b (a0)+,(a1,d1.w) */
    uint32_t b = lift_r8(x, c->a[0]);
    alu_moveb(c, b);
    c->a[0] += 1;
    lift_w8(x, c->a[1] + SEW(c->d[1]), b);
    lift_charge(x, 0x117DA);
    if (x->declined) return;
    setb(&c->d[1], alu_addb(c, 1, c->d[1]));    /* addq.b #1,d1 */
    lift_charge(x, 0x117DE);
    lift_charge_bcc(x, 0x117E0, !c->zf);        /* bne.w loc_117E8 */
    if (c->zf)
    {
      lift_call(x, 0x117E4, 4, Unpack_FlushPage);
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));             /* loc_117E8: dbf d0,loc_117DA */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x117E8, taken, !taken);
      if (!taken) break;
    }
  }
  lift_charge(x, 0x117EC);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* $117EE (nibble 2) — zero run: (op & $F)+1 cleared ring bytes */
static void unpack_op_zeros(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x117EE);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));           /* and.w #$F,d0 */
  lift_charge(x, 0x117F2);
  for (;;)
  {
    /* loc_117F6: clr.b (a1,d1.w) */
    alu_moveb(c, 0);
    lift_w8(x, c->a[1] + SEW(c->d[1]), 0);
    lift_charge(x, 0x117F6);
    if (x->declined) return;
    setb(&c->d[1], alu_addb(c, 1, c->d[1]));    /* addq.b #1,d1 */
    lift_charge(x, 0x117FA);
    lift_charge_bcc(x, 0x117FC, !c->zf);        /* bne.w loc_11804 */
    if (c->zf)
    {
      lift_call(x, 0x11800, 4, Unpack_FlushPage);
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));             /* loc_11804: dbf d0,loc_117F6 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11804, taken, !taken);
      if (!taken) break;
    }
  }
  lift_charge(x, 0x11808);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* $1180A (nibble 3) — repeat run: (op & $F)+3 copies of the next byte */
static void unpack_op_repeat(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1180A);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));           /* and.w #$F,d0 */
  lift_charge(x, 0x1180E);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x11812);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x11814);
  for (;;)
  {
    /* loc_11816: move.b d2,(a1,d1.w) */
    alu_moveb(c, c->d[2] & 0xFF);
    lift_w8(x, c->a[1] + SEW(c->d[1]), c->d[2] & 0xFF);
    lift_charge(x, 0x11816);
    if (x->declined) return;
    setb(&c->d[1], alu_addb(c, 1, c->d[1]));    /* addq.b #1,d1 */
    lift_charge(x, 0x1181A);
    lift_charge_bcc(x, 0x1181C, !c->zf);        /* bne.w loc_11824 */
    if (c->zf)
    {
      lift_call(x, 0x11820, 4, Unpack_FlushPage);
      if (x->declined) return;
    }
    setw(&c->d[0], W(c->d[0] - 1));             /* loc_11824: dbf d0,loc_11816 */
    {
      int taken = (W(c->d[0]) != 0xFFFF);
      lift_charge_dbcc(x, 0x11824, taken, !taken);
      if (!taken) break;
    }
  }
  lift_charge(x, 0x11828);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* $1182A (nibbles 4-7) — short back-ref: count/distance packed in the opcode */
static void unpack_op_ref_short(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1182A);
  setw(&c->d[0], alu_andw(c, 7, W(c->d[0])));             /* and.w #7,d0 */
  lift_charge(x, 0x1182E);
  setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));             /* addq.w #1,d0 */
  lift_charge(x, 0x11832);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d2 */
  lift_charge(x, 0x11834);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 3));             /* lsr.w #3,d2 */
  lift_charge(x, 0x11838);
  setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));             /* and.w #7,d2 */
  lift_charge(x, 0x1183A);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x1183E);
  unpack_op_copy_fwd(x);                        /* falls into loc_11840 */
}

/* $1185C (nibble 8) — back-ref, byte distance operand */
static void unpack_op_ref_dist8(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1185C);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));           /* and.w #$F,d0 */
  lift_charge(x, 0x11860);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x11864);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x11866);
  lift_charge_bcc(x, 0x11868, 1);               /* bra.s loc_11840 */
  unpack_op_copy_fwd(x);
}

/* $1186A (nibble 9) — back-ref, 5-bit count spanning both bytes (the
 * operand byte's top bit rides into the count via X), 7-bit distance */
static void unpack_op_ref_c5d7(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0),d0 */
  lift_charge(x, 0x1186A);
  setb(&c->d[0], alu_aslb(c, c->d[0], 1));                /* asl.b #1,d0 */
  lift_charge(x, 0x1186C);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1186E);
  setb(&c->d[0], alu_roxlb(c, c->d[0], 1));               /* roxl.b #1,d0 */
  lift_charge(x, 0x11872);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));          /* and.w #$1F,d0 */
  lift_charge(x, 0x11874);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x11878);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x1187A);
  setw(&c->d[2], alu_andw(c, 0x7F, W(c->d[2])));          /* and.w #$7F,d2 */
  lift_charge(x, 0x1187C);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x11880);
  lift_charge_bcc(x, 0x11882, 1);               /* bra.s loc_11840 */
  unpack_op_copy_fwd(x);
}

/* $11884 (nibble A) — back-ref, 6-bit count over both bytes, 6-bit distance */
static void unpack_op_ref_c6d6(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x11884);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 8));             /* asl.w #8,d0 */
  lift_charge(x, 0x11888);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0),d0 */
  lift_charge(x, 0x1188A);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 6));             /* lsr.w #6,d0 */
  lift_charge(x, 0x1188C);
  setw(&c->d[0], alu_andw(c, 0x3F, W(c->d[0])));          /* and.w #$3F,d0 */
  lift_charge(x, 0x1188E);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x11892);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x11894);
  setw(&c->d[2], alu_andw(c, 0x3F, W(c->d[2])));          /* and.w #$3F,d2 */
  lift_charge(x, 0x11896);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x1189A);
  lift_charge_bcc(x, 0x1189C, 1);               /* bra.s loc_11840 */
  unpack_op_copy_fwd(x);
}

/* $1189E (nibble B) — back-ref, 7-bit count over both bytes, 5-bit distance */
static void unpack_op_ref_c7d5(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1189E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 8));             /* asl.w #8,d0 */
  lift_charge(x, 0x118A2);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0),d0 */
  lift_charge(x, 0x118A4);
  setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 5));             /* lsr.w #5,d0 */
  lift_charge(x, 0x118A6);
  setw(&c->d[0], alu_andw(c, 0x7F, W(c->d[0])));          /* and.w #$7F,d0 */
  lift_charge(x, 0x118A8);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x118AC);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x118AE);
  setw(&c->d[2], alu_andw(c, 0x1F, W(c->d[2])));          /* and.w #$1F,d2 */
  lift_charge(x, 0x118B0);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x118B4);
  lift_charge_bcc(x, 0x118B6, 1);               /* bra.s loc_11840 */
  unpack_op_copy_fwd(x);
}

/* $118B8 (nibbles C-D) — short reversed back-ref, packed in the opcode */
static void unpack_op_rev_short(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x118B8);
  setw(&c->d[0], alu_andw(c, 3, W(c->d[0])));             /* and.w #3,d0 */
  lift_charge(x, 0x118BC);
  setw(&c->d[0], alu_addw(c, 1, W(c->d[0])));             /* addq.w #1,d0 */
  lift_charge(x, 0x118C0);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d2 */
  lift_charge(x, 0x118C2);
  setw(&c->d[2], alu_lsrw(c, W(c->d[2]), 2));             /* lsr.w #2,d2 */
  lift_charge(x, 0x118C6);
  setw(&c->d[2], alu_andw(c, 7, W(c->d[2])));             /* and.w #7,d2 */
  lift_charge(x, 0x118C8);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x118CC);
  unpack_op_copy_rev(x);                        /* falls into loc_118CE */
}

/* $118EA (nibble E) — reversed back-ref with byte distance, OR the stream
 * terminator when that distance byte is zero: flush a nonempty partial
 * page, discard the dispatch return, pop the interpreter frame and return
 * to Unpack_RleStream's caller. */
static void unpack_op_rev_or_end(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x118EA);
  setw(&c->d[0], alu_andw(c, 0xF, W(c->d[0])));           /* and.w #$F,d0 */
  lift_charge(x, 0x118EE);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x118F2);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x118F4);
  lift_charge_bcc(x, 0x118F6, !c->zf);          /* bne.s loc_118CE */
  if (!c->zf)
  {
    unpack_op_copy_rev(x);
    return;
  }
  alu_tstw(c, W(c->d[1]));                      /* tst.w d1 */
  lift_charge(x, 0x118F8);
  lift_charge_bcc(x, 0x118FA, c->zf);           /* beq.w loc_11902 */
  if (!c->zf)
  {
    lift_call(x, 0x118FE, 4, Unpack_FlushPage); /* bsr.w sub_11924 */
    if (x->declined) return;
  }
  /* loc_11902: addq.w #4,sp — drop the dispatch jsr's return; no flags */
  c->a[7] += 4;
  lift_charge(x, 0x11902);
  c->d[0] = lift_r32(x, c->a[7]);               /* movem.l (sp)+,d0-d3/a0-a2 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->d[2] = lift_r32(x, c->a[7] + 8);
  c->d[3] = lift_r32(x, c->a[7] + 12);
  c->a[0] = lift_r32(x, c->a[7] + 16);
  c->a[1] = lift_r32(x, c->a[7] + 20);
  c->a[2] = lift_r32(x, c->a[7] + 24);
  c->a[7] += 28;
  lift_charge_movem(x, 0x11904);
  lift_charge(x, 0x11908);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* $1190A (nibble F) — reversed back-ref, 5-bit spanning count / 7-bit distance */
static void unpack_op_rev_c5d7(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0),d0 */
  lift_charge(x, 0x1190A);
  setb(&c->d[0], alu_aslb(c, c->d[0], 1));                /* asl.b #1,d0 */
  lift_charge(x, 0x1190C);
  setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d0 */
  lift_charge(x, 0x1190E);
  setb(&c->d[0], alu_roxlb(c, c->d[0], 1));               /* roxl.b #1,d0 */
  lift_charge(x, 0x11912);
  setw(&c->d[0], alu_andw(c, 0x1F, W(c->d[0])));          /* and.w #$1F,d0 */
  lift_charge(x, 0x11914);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));             /* addq.w #2,d0 */
  lift_charge(x, 0x11918);
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0])));      /* move.b (a0)+,d2 */
  c->a[0] += 1;
  lift_charge(x, 0x1191A);
  setw(&c->d[2], alu_andw(c, 0x7F, W(c->d[2])));          /* and.w #$7F,d2 */
  lift_charge(x, 0x1191C);
  setw(&c->d[2], alu_addw(c, 1, W(c->d[2])));             /* addq.w #1,d2 */
  lift_charge(x, 0x11920);
  lift_charge_bcc(x, 0x11922, 1);               /* bra.s loc_118CE */
  unpack_op_copy_rev(x);
}

/*
 * Unpack_RleStream (sub_1177A) — the bytecode interpreter. In: a0 =
 * compressed stream, d1 = VRAM byte address for the flush cursor.
 * Dispatches each stream byte's high nibble through word_117B2 (static
 * ROM — enumerated, the sub_11A48 pattern). Exits only through the
 * terminator path of unpack_op_rev_or_end, which pops this frame itself.
 */
void Unpack_RleStream(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = 0xFFFFCF36;                         /* movea.w #$CF36,a1 — sign-extends */
  lift_charge(x, 0x1177A);
  c->a[3] = 0xFFFFCF36;                         /* movea.w #$CF36,a3 */
  lift_charge(x, 0x1177E);
  c->a[4] = 0xFFFFCF32;                         /* movea.w #$CF32,a4 */
  lift_charge(x, 0x11782);
  c->a[5] = 0x00010EE0;                         /* movea.l #loc_10EE0,a5 */
  lift_charge(x, 0x11786);
  c->a[6] = 0x000113D0;                         /* movea.l #loc_113D0,a6 */
  lift_charge(x, 0x1178C);
  c->a[7] -= 28;                                /* movem.l d0-d3/a0-a2,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->d[2]);
  lift_w32(x, c->a[7] + 12, c->d[3]);
  lift_w32(x, c->a[7] + 16, c->a[0]);
  lift_w32(x, c->a[7] + 20, c->a[1]);
  lift_w32(x, c->a[7] + 24, c->a[2]);
  lift_charge_movem(x, 0x11792);
  setw(&c->d[3], alu_movew(c, W(c->d[1])));     /* move.w d1,d3 */
  lift_charge(x, 0x11796);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0x11798);
  setw(&c->d[2], alu_movew(c, 0));              /* clr.w d2 */
  lift_charge(x, 0x1179A);

  for (;;)
  {
    /* loc_1179C: fetch and dispatch one opcode */
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));    /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0x1179C);
    setw(&c->d[0], alu_andw(c, 0xF0, W(c->d[0])));        /* and.w #$F0,d0 */
    lift_charge(x, 0x1179E);
    setw(&c->d[0], alu_lsrw(c, W(c->d[0]), 3));           /* lsr.w #3,d0 */
    lift_charge(x, 0x117A2);
    c->a[2] = 0x000117B2;                                 /* lea word_117B2(pc),a2 */
    lift_charge(x, 0x117A4);
    setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + SEW(c->d[0]))));  /* move.w (a2,d0.w),d0 */
    lift_charge(x, 0x117A8);

    /* jsr (a2,d0.w) — the offset table is static ROM, so enumerate it */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], 0x0117B0);
    lift_charge(x, 0x117AC);
    switch ((0x117B2 + SEW(c->d[0])) & 0xFFFFFF)
    {
    case 0x0117D2: unpack_op_literal(x);   break;
    case 0x0117EE: unpack_op_zeros(x);     break;
    case 0x01180A: unpack_op_repeat(x);    break;
    case 0x01182A: unpack_op_ref_short(x); break;
    case 0x01185C: unpack_op_ref_dist8(x); break;
    case 0x01186A: unpack_op_ref_c5d7(x);  break;
    case 0x011884: unpack_op_ref_c6d6(x);  break;
    case 0x01189E: unpack_op_ref_c7d5(x);  break;
    case 0x0118B8: unpack_op_rev_short(x); break;
    case 0x0118EA: unpack_op_rev_or_end(x); break;
    case 0x01190A: unpack_op_rev_c5d7(x);  break;
    default: x->declined = 1; return;             /* off-table target */
    }
    if (x->declined) return;
    if ((c->pc & 0xFFFFFF) != 0x0117B0) return;   /* terminator popped out */
    lift_charge_bcc(x, 0x117B0, 1);               /* bra.s loc_1179C */
  }
}

/*
 * Unpack_Block (sub_1173C) — decode one block from the a2 stream: leading
 * word 0 = empty; positive = raw tile words (count<<4 words nibble-blitted
 * when the cache is set, DMA'd when not — the DMA raw path declines);
 * negative = &$7FFF tile count, body RLE-decoded by Unpack_RleStream.
 * d4 (the tile cursor, VRAM address = d4<<5) advances by the block's
 * count and is the routine's only register output.
 */
void Unpack_Block(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[7] -= 36;                                /* movem.l d0-d1/a0-a6,-(sp) */
  lift_w32(x, c->a[7],      c->d[0]);
  lift_w32(x, c->a[7] + 4,  c->d[1]);
  lift_w32(x, c->a[7] + 8,  c->a[0]);
  lift_w32(x, c->a[7] + 12, c->a[1]);
  lift_w32(x, c->a[7] + 16, c->a[2]);
  lift_w32(x, c->a[7] + 20, c->a[3]);
  lift_w32(x, c->a[7] + 24, c->a[4]);
  lift_w32(x, c->a[7] + 28, c->a[5]);
  lift_w32(x, c->a[7] + 32, c->a[6]);
  lift_charge_movem(x, 0x1173C);

  c->a[0] = c->a[2];                            /* movea.l a2,a0 */
  lift_charge(x, 0x11740);
  setw(&c->d[1], alu_movew(c, W(c->d[4])));     /* move.w d4,d1 */
  lift_charge(x, 0x11742);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 5));   /* asl.w #5,d1 */
  lift_charge(x, 0x11744);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[0])));  /* move.w (a0)+,d0 */
  c->a[0] += 2;
  lift_charge(x, 0x11746);
  lift_charge_bcc(x, 0x11748, c->zf);           /* beq.w loc_11774 */
  if (!c->zf)
  {
    lift_charge_bcc(x, 0x1174C, c->nf);         /* bmi.w loc_1176A */
    if (!c->nf)
    {
      /* raw tile words */
      setw(&c->d[4], alu_addw(c, W(c->d[0]), W(c->d[4])));  /* add.w d0,d4 */
      lift_charge(x, 0x11750);
      setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));           /* asl.w #4,d0 */
      lift_charge(x, 0x11752);
      c->a[7] -= 4;                             /* pea (loc_11774).l */
      lift_w32(x, c->a[7], 0x00011774);
      lift_charge(x, 0x11754);
      alu_tstl(c, lift_r32(x, 0xFFFFCF32));     /* tst.l ($CF32).w */
      lift_charge(x, 0x1175A);
      /* beq.w loc_113D0 — the null-cache raw path DMAs the words
       * (sub_113E4 -> VDP_TransferDMA): runtime-core, declined */
      if (c->zf) { x->declined = 1; return; }
      lift_charge_bcc(x, 0x1175E, c->zf);
      c->a[1] = lift_r32(x, 0xFFFFCF32);        /* movea.l ($CF32).w,a1 */
      lift_charge(x, 0x11762);
      lift_charge_bcc(x, 0x11766, 1);           /* bra.w loc_10EE0 */
      unpack_blit_nibble_page(x);               /* its rts pops the pea */
      if (x->declined) return;
    }
    else
    {
      /* loc_1176A: RLE block */
      setw(&c->d[0], alu_andw(c, 0x7FFF, W(c->d[0])));      /* and.w #$7FFF,d0 */
      lift_charge(x, 0x1176A);
      setw(&c->d[4], alu_addw(c, W(c->d[0]), W(c->d[4])));  /* add.w d0,d4 */
      lift_charge(x, 0x1176E);
      lift_call(x, 0x11770, 4, Unpack_RleStream);           /* bsr.w sub_1177A */
      if (x->declined) return;
    }
  }
  /* loc_11774 */
  c->d[0] = lift_r32(x, c->a[7]);               /* movem.l (sp)+,d0-d1/a0-a6 */
  c->d[1] = lift_r32(x, c->a[7] + 4);
  c->a[0] = lift_r32(x, c->a[7] + 8);
  c->a[1] = lift_r32(x, c->a[7] + 12);
  c->a[2] = lift_r32(x, c->a[7] + 16);
  c->a[3] = lift_r32(x, c->a[7] + 20);
  c->a[4] = lift_r32(x, c->a[7] + 24);
  c->a[5] = lift_r32(x, c->a[7] + 28);
  c->a[6] = lift_r32(x, c->a[7] + 32);
  c->a[7] += 36;
  lift_charge_movem(x, 0x11774);
  lift_charge(x, 0x11778);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Unpack_BlockDirect (sub_11738) — clear the cache pointer (all flushes
 * DMA) and fall through into Unpack_Block. Nonempty blocks decline at
 * their first flush; the empty-block path verifies.
 */
void Unpack_BlockDirect(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movel(c, 0);                              /* clr.l ($CF32).w */
  lift_w32(x, 0xFFFFCF32, 0);
  lift_charge(x, 0x11738);
  Unpack_Block(x);                              /* falls through into sub_1173C */
}

/*
 * Unpack_BlockCached (sub_1172C) — point the cache pointer at this
 * call's inline 8-byte nibble LUT (the return address), decode the
 * block through Unpack_Block, then step the return address past the LUT.
 */
void Unpack_BlockCached(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;

  v = lift_r32(x, c->a[7]);                     /* move.l (sp),($CF32).w */
  alu_movel(c, v);
  lift_w32(x, 0xFFFFCF32, v);
  lift_charge(x, 0x1172C);
  lift_call(x, 0x11730, 4, Unpack_Block);       /* bsr.w sub_1173C */
  if (x->declined) return;
  v = alu_addl(c, 8, lift_r32(x, c->a[7]));     /* addq.l #8,(sp) */
  lift_w32(x, c->a[7], v);
  lift_charge(x, 0x11734);
  lift_charge(x, 0x11736);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Tilemap_DrawRegion (sub_1169A) — the "possibly a decompression
 * algorithm" entry at $1169A, and the last big unlifted consumer of the
 * unpacker chain.
 *
 *   in:  d0.w = first map column      d1.w = first map row
 *        d2.w = columns to emit       d3.w = rows to emit
 *        d4.w = tile-index bias (also the unpacker's block index)
 *        d5.w = 32-byte-block copy mask (LSB first, 0 bit terminates)
 *        a0   = source pattern bytes  a1   = tilemap header
 *        a2   = packed tile block for the trailing unpack
 *   out: every register it touches restored (movem epilogue).
 *
 * Two halves. First a mask-driven staging copy: each set bit of d5 copies
 * one 32-byte pattern from a0 into the $FFFFBD28 buffer (the same buffer
 * Copy_5605ABlockToBD28 fills), each bit — set or clear — advancing both
 * pointers by $20, and a clear bit with nothing left above it ending the
 * walk. Then the map blit: for each of d3 rows, re-derive the VRAM
 * address from the text cursor (Text_SetCursorVramAddr), index the map at
 * 4(a1) by row*width+column (width is the header word at (a1)) and stream
 * d2 name-table words out the data port, each biased by d4 and eor'd with
 * the attribute bits ($FFFFB02C & $F800). Finally, unless $FFFFC2F8 bit 0
 * is set, the packed block at a2 is decompressed through
 * Unpack_BlockDirect — a null-cache (DMA-mode) flush, so nonempty blocks
 * decline there by design (HW-STAGING.md ADDENDUM (2)).
 *
 * Zero column/row counts would dbf-wrap into 64K-iteration loops —
 * decline and let the interpreter have them.
 */
void Tilemap_DrawRegion(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp, v;

  if (W(c->d[2]) == 0 || W(c->d[3]) == 0) { x->declined = 1; return; }

  v = lift_r16(x, T_CURSOR_ROW);                /* move.w ($B02A).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0x1169A);

  v = lift_r16(x, T_VDP_GUARD);                 /* move.w ($BF78).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0x1169E);

  v = lift_r8(x, T_VDP_GUARD);                  /* bset #2,($BF78).w — byte op */
  lift_w8(x, T_VDP_GUARD, alu_bset(c, v, 2));
  lift_charge(x, 0x116A2);

  c->a[7] -= 40;                                /* movem.l d0-d3/d5-d6/a0-a3,-(sp) */
  sp = c->a[7];
  lift_w32(x, sp + 0x00, c->d[0]);
  lift_w32(x, sp + 0x04, c->d[1]);
  lift_w32(x, sp + 0x08, c->d[2]);
  lift_w32(x, sp + 0x0C, c->d[3]);
  lift_w32(x, sp + 0x10, c->d[5]);
  lift_w32(x, sp + 0x14, c->d[6]);
  lift_w32(x, sp + 0x18, c->a[0]);
  lift_w32(x, sp + 0x1C, c->a[1]);
  lift_w32(x, sp + 0x20, c->a[2]);
  lift_w32(x, sp + 0x24, c->a[3]);
  lift_charge_movem(x, 0x116A8);

  setw(&c->d[6], alu_movew(c, W(c->d[1])));     /* move.w d1,d6 — map row cursor */
  lift_charge(x, 0x116AC);
  c->a[3] = 0xFFFFBD28;                         /* movea.w #$BD28,a3 — sign-extends */
  lift_charge(x, 0x116AE);
  lift_charge(x, 0x116B2);                      /* bra.w loc_116CC */

  for (;;)
  {
    int copy;

    /* loc_116CC */
    c->d[0] = alu_moveql(c, 0x1F);              /* moveq #$1F,d0 */
    lift_charge(x, 0x116CC);
    setw(&c->d[5], alu_lsrw(c, W(c->d[5]), 1)); /* lsr.w #1,d5 */
    lift_charge(x, 0x116CE);

    copy = c->cf;
    lift_charge_bcc(x, 0x116D0, copy);          /* bcs.s loc_116B6 */
    if (!copy)
    {
      int more = !c->zf;
      lift_charge_bcc(x, 0x116D2, more);        /* bne.s loc_116C0 */
      if (!more) break;
    }
    else
    {
      do
      {
        /* loc_116B6: move.b (a0,d0.w),(a3,d0.w) */
        uint32_t idx = SEW(W(c->d[0]));
        lift_w8(x, c->a[3] + idx, alu_moveb(c, lift_r8(x, c->a[0] + idx)));
        lift_charge(x, 0x116B6);
        setw(&c->d[0], W(c->d[0] - 1));         /* dbf d0,loc_116B6 */
        {
          int taken = (W(c->d[0]) != 0xFFFF);
          lift_charge_dbcc(x, 0x116BC, taken, !taken);
          if (!taken) break;
        }
      } while (1);
    }

    /* loc_116C0 */
    c->a[0] += 0x20;                            /* adda.l #$20,a0 — no CCR */
    lift_charge(x, 0x116C0);
    c->a[3] += 0x20;                            /* adda.l #$20,a3 */
    lift_charge(x, 0x116C6);
  }

  setw(&c->d[5], alu_movew(c, lift_r16(x, 0xFFB02C)));  /* move.w ($B02C).w,d5 */
  lift_charge(x, 0x116D4);
  setw(&c->d[5], alu_andw(c, 0xF800, W(c->d[5])));      /* and.w #$F800,d5 */
  lift_charge(x, 0x116D8);
  setw(&c->d[2], alu_movew(c, lift_r16(x, sp + 0x0E))); /* move.w var_1E(sp),d2 — row count */
  lift_charge(x, 0x116DC);
  setw(&c->d[2], alu_subw(c, 1, W(c->d[2])));   /* subq.w #1,d2 */
  lift_charge(x, 0x116E0);

  do
  {
    /* loc_116E2 */
    lift_call(x, 0x116E2, 4, Text_SetCursorVramAddr);   /* bsr.w sub_11952 */
    if (x->declined) return;

    setw(&c->d[0], alu_movew(c, W(c->d[6])));   /* move.w d6,d0 */
    lift_charge(x, 0x116E6);
    {
      uint32_t src = lift_r16(x, c->a[1]);      /* mulu.w (a1),d0 — row * map width */
      c->d[0] = alu_mulu(c, src, c->d[0]);
      lift_charge_mulu(x, 0x116E8, src);
    }
    setw(&c->d[0], alu_addw(c, lift_r16(x, sp + 0x02), W(c->d[0]))); /* add.w var_2A(sp),d0 */
    lift_charge(x, 0x116EA);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1)); /* asl.w #1,d0 — word index */
    lift_charge(x, 0x116EE);
    setw(&c->d[1], alu_movew(c, lift_r16(x, sp + 0x0A))); /* move.w var_22(sp),d1 */
    lift_charge(x, 0x116F0);
    setw(&c->d[1], alu_subw(c, 1, W(c->d[1])));  /* subq.w #1,d1 — column count */
    lift_charge(x, 0x116F4);

    do
    {
      /* loc_116F6 */
      setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1] + 4 + SEW(W(c->d[0])))));
      lift_charge(x, 0x116F6);
      setw(&c->d[3], alu_addw(c, W(c->d[4]), W(c->d[3])));  /* add.w d4,d3 */
      lift_charge(x, 0x116FA);
      setw(&c->d[3], alu_eorw(c, W(c->d[5]), W(c->d[3])));  /* eor.w d5,d3 */
      lift_charge(x, 0x116FC);

      /* move.w d3,(a0) — the data port, via the VDP_SetAddress a0 convention */
      if ((c->a[0] & 0xFFFFFF) != 0xC00000) { x->declined = 1; return; }
      alu_movew(c, W(c->d[3]));
      lift_whw_data16(x, 0x116FE, W(c->d[3]));
      if (x->declined) return;

      setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));  /* addq.w #2,d0 */
      lift_charge(x, 0x11700);
      setw(&c->d[1], W(c->d[1] - 1));               /* dbf d1,loc_116F6 */
      {
        int taken = (W(c->d[1]) != 0xFFFF);
        lift_charge_dbcc(x, 0x11702, taken, !taken);
        if (!taken) break;
      }
    } while (1);

    v = lift_r16(x, T_CURSOR_ROW);              /* addq.w #1,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, alu_addw(c, 1, v));
    lift_charge(x, 0x11706);
    setw(&c->d[6], alu_addw(c, 1, W(c->d[6])));  /* addq.w #1,d6 */
    lift_charge(x, 0x1170A);
    setw(&c->d[2], W(c->d[2] - 1));              /* dbf d2,loc_116E2 */
    {
      int taken = (W(c->d[2]) != 0xFFFF);
      lift_charge_dbcc(x, 0x1170C, taken, !taken);
      if (!taken) break;
    }
  } while (1);

  alu_btst(c, lift_r8(x, 0xFFC2F8), 0);         /* btst #0,($C2F8).w */
  lift_charge(x, 0x11710);
  {
    int skip = !c->zf;
    lift_charge_bcc(x, 0x11716, skip);          /* bne.w loc_1171E */
    if (!skip)
    {
      lift_call(x, 0x1171A, 4, Unpack_BlockDirect);   /* bsr.w sub_11738 */
      if (x->declined) return;
    }
  }

  /* loc_1171E */
  c->d[0] = lift_r32(x, sp + 0x00);             /* movem.l (sp)+,d0-d3/d5-d6/a0-a3 */
  c->d[1] = lift_r32(x, sp + 0x04);
  c->d[2] = lift_r32(x, sp + 0x08);
  c->d[3] = lift_r32(x, sp + 0x0C);
  c->d[5] = lift_r32(x, sp + 0x10);
  c->d[6] = lift_r32(x, sp + 0x14);
  c->a[0] = lift_r32(x, sp + 0x18);
  c->a[1] = lift_r32(x, sp + 0x1C);
  c->a[2] = lift_r32(x, sp + 0x20);
  c->a[3] = lift_r32(x, sp + 0x24);
  c->a[7] = sp + 40;
  lift_charge_movem(x, 0x1171E);

  v = lift_r16(x, c->a[7]);                     /* move.w (sp)+,($BF78).w */
  c->a[7] += 2;
  lift_w16(x, T_VDP_GUARD, alu_movew(c, v));
  lift_charge(x, 0x11722);
  v = lift_r16(x, c->a[7]);                     /* move.w (sp)+,($B02A).w */
  c->a[7] += 2;
  lift_w16(x, T_CURSOR_ROW, alu_movew(c, v));
  lift_charge(x, 0x11726);

  lift_charge(x, 0x1172A);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_DrawPanel (sub_12D70) — draw one caption + tilemap panel, unless
 * bit 5 of $FFFFC2EE says the panel is suppressed (then out through the
 * shared far rts at $15464). The caption goes through
 * Text_DrawInlineString's 6-byte inline block at $12D7E; the panel body
 * is the tilemap at unk_B3530 + its own 4(a1) self-relative offset,
 * blitted by Tilemap_DrawRegion with the map's own header words as the
 * column/row counts, the tile bias from $FFFFB020 and no staging copy
 * (d5 = 0). Ends with a tail call — Tilemap_DrawRegion's rts returns to
 * OUR caller.
 */
void Board_DrawPanel(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_btst(c, lift_r8(x, 0xFFC2EE), 5);         /* btst #5,($C2EE).w */
  lift_charge(x, 0x12D70);
  {
    int suppressed = !c->zf;
    lift_charge_bcc(x, 0x12D76, suppressed);    /* bne.w locret_15464 */
    if (suppressed)
    {
      lift_charge(x, 0x15464);                  /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }

  lift_call(x, 0x12D7A, 4, Text_DrawInlineString);   /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;

  c->a[1] = 0x000B3530;                         /* movea.l #unk_B3530,a1 */
  lift_charge(x, 0x12D84);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* adda.l 4(a1),a1 */
  lift_charge(x, 0x12D8A);
  c->a[2] = SEW(0x030A);                        /* movea.w #$30A,a2 */
  lift_charge(x, 0x12D8E);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0x12D92);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0x12D94);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1])));      /* move.w (a1),d2 */
  lift_charge(x, 0x12D96);
  setw(&c->d[3], alu_movew(c, lift_r16(x, c->a[1] + 2)));  /* move.w 2(a1),d3 */
  lift_charge(x, 0x12D98);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB020)));     /* move.w ($B020).w,d4 */
  lift_charge(x, 0x12D9C);
  setw(&c->d[5], alu_movew(c, 0));              /* clr.w d5 */
  lift_charge(x, 0x12DA0);

  lift_charge(x, 0x12DA2);                      /* bra.w sub_1169A — tail call */
  Tilemap_DrawRegion(x);
}

/*
 * Board_DrawDeferredPanel (sub_12B32) — consume the "panel dirty" latch
 * ($FFFFC2EE bit 5) and, if it was set, draw the panel. Note the sense
 * flip against Board_DrawPanel: this one clears the bit first, so the
 * tail call always finds it clear and never bails at $12D76.
 */
void Board_DrawDeferredPanel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;

  v = lift_r8(x, 0xFFC2EE);                     /* bclr #5,($C2EE).w — byte op */
  lift_w8(x, 0xFFC2EE, alu_bclr(c, v, 5));
  lift_charge(x, 0x12B32);
  {
    int wasClear = c->zf;
    lift_charge_bcc(x, 0x12B38, wasClear);      /* beq.w locret_15464 */
    if (wasClear)
    {
      lift_charge(x, 0x15464);                  /* shared far rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }
  }
  lift_charge(x, 0x12B3C);                      /* bra.w sub_12D70 — tail call */
  Board_DrawPanel(x);
}

/* ===================================================================
 * Wave 52 (2026-08-05) — the second tier of Unpack / Tilemap_DrawRegion
 * callers, thawed by the sprawl session's chain lift.
 * =================================================================== */

/*
 * Gfx_UnpackAAC5A_SetBaseB012 (sub_11F04) — record the incoming tile
 * cursor in the $FFFFB012 VRAM-base table, then unpack the $AAC5A block
 * there (DMA mode: Unpack_BlockDirect clears the nibble cache first).
 * d4 comes back advanced past the block; a2 is left at the block.
 */
void Gfx_UnpackAAC5A_SetBaseB012(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movew(c, W(c->d[4]));                     /* move.w d4,($B012).w */
  lift_w16(x, 0xFFB012u, W(c->d[4]));
  lift_charge(x, 0x11F04);
  c->a[2] = 0x000AAC5A;                         /* move.l #unk_AAC5A,a2 */
  lift_charge(x, 0x11F08);
  lift_charge_bcc(x, 0x11F0E, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/*
 * Gfx_Unpack55B86_SetBaseB01E (sub_11F12) — the $B01E sibling of
 * Gfx_UnpackAAC5A_SetBaseB012 (the pointer load comes first here).
 */
void Gfx_Unpack55B86_SetBaseB01E(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[2] = 0x00055B86;                         /* move.l #unk_55B86,a2 */
  lift_charge(x, 0x11F12);
  alu_movew(c, W(c->d[4]));                     /* move.w d4,($B01E).w */
  lift_w16(x, 0xFFB01Eu, W(c->d[4]));
  lift_charge(x, 0x11F18);
  lift_charge_bcc(x, 0x11F1C, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/*
 * Gfx_UnpackABA1C_AtBase2 (sub_11F20) — unpack $ABA1C at the fixed tile
 * cursor 2. Records nothing.
 */
void Gfx_UnpackABA1C_AtBase2(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[4] = alu_moveql(c, 2);                   /* moveq #2,d4 */
  lift_charge(x, 0x11F20);
  c->a[2] = 0x000ABA1C;                         /* move.l #unk_ABA1C,a2 */
  lift_charge(x, 0x11F22);
  lift_charge_bcc(x, 0x11F28, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/*
 * Gfx_UnpackAB928_AtBaseB016 (sub_16CC4) — reload the tile cursor a
 * previous pass recorded in $FFFFB016 and unpack $AB928 over it.
 */
void Gfx_UnpackAB928_AtBaseB016(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB016u)));  /* move.w ($B016).w,d4 */
  lift_charge(x, 0x16CC4);
  c->a[2] = 0x000AB928;                         /* move.l #unk_AB928,a2 */
  lift_charge(x, 0x16CC8);
  lift_charge_bcc(x, 0x16CCE, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/* Gfx_UnpackA4B5C_AtBaseB01A (sub_16CD2) — the $B01A / $A4B5C sibling. */
void Gfx_UnpackA4B5C_AtBaseB01A(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB01Au)));  /* move.w ($B01A).w,d4 */
  lift_charge(x, 0x16CD2);
  c->a[2] = 0x000A4B5C;                         /* move.l #unk_A4B5C,a2 */
  lift_charge(x, 0x16CD6);
  lift_charge_bcc(x, 0x16CDC, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/* Gfx_Unpack5C410_AtBaseB026 (sub_16CEE) — the $B026 / $5C410 sibling. */
void Gfx_Unpack5C410_AtBaseB026(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB026u)));  /* move.w ($B026).w,d4 */
  lift_charge(x, 0x16CEE);
  c->a[2] = 0x0005C410;                         /* move.l #unk_5C410,a2 */
  lift_charge(x, 0x16CF2);
  lift_charge_bcc(x, 0x16CF8, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/* Gfx_Unpack5CF6C_AtBaseB026 (sub_16CE0) — the $B026 / $5CF6C sibling. */
void Gfx_Unpack5CF6C_AtBaseB026(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB026u)));  /* move.w ($B026).w,d4 */
  lift_charge(x, 0x16CE0);
  c->a[2] = 0x0005CF6C;                         /* move.l #unk_5CF6C,a2 */
  lift_charge(x, 0x16CE4);
  lift_charge_bcc(x, 0x16CEA, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/* Gfx_UnpackA78B6_AtBaseB01C (sub_16CFC) — the $B01C / $A78B6 sibling. */
void Gfx_UnpackA78B6_AtBaseB01C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB01Cu)));  /* move.w ($B01C).w,d4 */
  lift_charge(x, 0x16CFC);
  c->a[2] = 0x000A78B6;                         /* move.l #unk_A78B6,a2 */
  lift_charge(x, 0x16D00);
  lift_charge_bcc(x, 0x16D06, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/* Gfx_Unpack55BFE_AtBaseB026 (sub_16D0A) — the $B026 / $55BFE sibling. */
void Gfx_Unpack55BFE_AtBaseB026(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB026u)));  /* move.w ($B026).w,d4 */
  lift_charge(x, 0x16D0A);
  c->a[2] = 0x00055BFE;                         /* move.l #unk_55BFE,a2 */
  lift_charge(x, 0x16D0E);
  lift_charge_bcc(x, 0x16D14, 1);               /* bra.w sub_11738 — tail */
  Unpack_BlockDirect(x);
}

/*
 * Gfx_UnpackAFE1ADualMode (sub_F84D0) — unpack $AFE1A at cursor 2 in DMA
 * mode, record where that left the cursor in $FFFFD43A, then unpack the
 * SAME block again through Unpack_BlockCached, whose inline 8-byte nibble
 * LUT lives at $F84EE (the bytes IDA disassembles as bchg/move.l/muls.w —
 * they are data, and the cached entry's addq.l #8,(sp) steps the return
 * address over them to the rts at $F84F6).
 */
void Gfx_UnpackAFE1ADualMode(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->d[4] = alu_moveql(c, 2);                   /* moveq #2,d4 */
  lift_charge(x, 0xF84D0);
  c->a[2] = 0x000AFE1A;                         /* move.l #unk_AFE1A,a2 */
  lift_charge(x, 0xF84D2);
  lift_call(x, 0xF84D8, 6, Unpack_BlockDirect); /* jsr sub_11738 */
  if (x->declined) return;

  alu_movew(c, W(c->d[4]));                     /* move.w d4,($D43A).w */
  lift_w16(x, 0xFFD43Au, W(c->d[4]));
  lift_charge(x, 0xF84DE);
  c->a[2] = 0x000AFE1A;                         /* move.l #unk_AFE1A,a2 */
  lift_charge(x, 0xF84E2);
  lift_call(x, 0xF84E8, 6, Unpack_BlockCached); /* jsr sub_1172C + 8 inline */
  if (x->declined) return;

  lift_charge(x, 0xF84F6);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Gfx_UnpackIndexedTeamBlob (sub_FEA52) — unpack the block whose pointer
 * sits at index ($FFFFC330) of the $FEA74 table, at the caller's cursor
 * biased down by $18. The +8 skips each entry's two header longs. d0 is
 * preserved through a word push; d4 comes back advanced.
 */
void Gfx_UnpackIndexedTeamBlob(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movew(c, W(c->d[0]));                     /* move.w d0,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[0]));
  lift_charge(x, 0xFEA52);

  setw(&c->d[4], alu_subw(c, 0x18, W(c->d[4])));        /* sub.w #$18,d4 */
  lift_charge(x, 0xFEA54);
  c->a[2] = 0x000FEA74;                                 /* move.l #off_FEA74,a2 */
  lift_charge(x, 0xFEA58);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFC330u))); /* move.w ($C330).w,d0 */
  lift_charge(x, 0xFEA5E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 2));           /* asl.w #2,d0 */
  lift_charge(x, 0xFEA62);
  c->a[2] = lift_r32(x, c->a[2] + SEW(c->d[0]));        /* move.l (a2,d0.w),a2 — movea */
  lift_charge(x, 0xFEA64);
  c->a[2] += 8;                                         /* addq.w #8,a2 — adda, no CCR */
  lift_charge(x, 0xFEA68);

  lift_call(x, 0xFEA6A, 6, Unpack_BlockDirect);         /* jsr sub_11738 */
  if (x->declined) return;

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[7])));   /* move.w (sp)+,d0 */
  c->a[7] += 2;
  lift_charge(x, 0xFEA70);
  lift_charge(x, 0xFEA72);                              /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Tilemap_DrawTwoRowStripAFE12 (sub_17B78) — two rows of the $AFE12
 * tilemap at column d0=0, row d1*2, width taken from the map header.
 * d4 (the tile bias) is the caller's; d5=0 skips the pattern-staging
 * copy, so a0 never matters. Tail-calls Tilemap_DrawRegion, which
 * restores everything it touches.
 */
void Tilemap_DrawTwoRowStripAFE12(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0x17B78);
  setw(&c->d[1], alu_aslw(c, W(c->d[1]), 1));   /* asl.w #1,d1 */
  lift_charge(x, 0x17B7A);
  c->a[0] = 0x000AFE12;                         /* move.l #off_AFE12,a0 */
  lift_charge(x, 0x17B7C);
  c->a[1] = c->a[0];                            /* move.l a0,a1 — movea */
  lift_charge(x, 0x17B82);
  c->a[0] += lift_r32(x, c->a[0]);              /* add.l (a0),a0 — adda */
  lift_charge(x, 0x17B84);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0x17B86);
  c->a[2] = 0x0000030A;                         /* move.w #$30A,a2 — movea.w */
  lift_charge(x, 0x17B8A);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d2 */
  lift_charge(x, 0x17B8E);
  c->d[3] = alu_moveql(c, 2);                   /* moveq #2,d3 */
  lift_charge(x, 0x17B90);
  c->d[5] = alu_moveql(c, 0);                   /* moveq #0,d5 */
  lift_charge(x, 0x17B92);
  lift_charge_bcc(x, 0x17B94, 1);               /* bra.w sub_1169A — tail */
  Tilemap_DrawRegion(x);
}

/*
 * Tilemap_DrawB3640Panel (sub_17626) — a $17-row, 2-column strip of the
 * $B3640 tilemap at the $FFFFB026 tile base. Everything d0-a3 is restored
 * by the movem epilogue.
 */
void Tilemap_DrawB3640Panel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;
  int i;

  c->a[7] -= 48;                                /* movem.l d0-a3,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 4; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0x17626);

  c->a[0] = 0x000B3640;                         /* move.l #unk_B3640,a0 */
  lift_charge(x, 0x1762A);
  c->a[1] = c->a[0];                            /* move.l a0,a1 */
  lift_charge(x, 0x17630);
  c->a[0] += lift_r32(x, c->a[0]);              /* add.l (a0),a0 */
  lift_charge(x, 0x17632);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 */
  lift_charge(x, 0x17634);
  c->a[2] = 0x0000030A;                         /* move.w #$30A,a2 */
  lift_charge(x, 0x17638);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0x1763C);
  c->d[2] = alu_moveql(c, 2);                   /* moveq #2,d2 */
  lift_charge(x, 0x1763E);
  c->d[3] = alu_moveql(c, 0x17);                /* moveq #$17,d3 */
  lift_charge(x, 0x17640);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB026u))); /* move.w ($B026).w,d4 */
  lift_charge(x, 0x17642);
  c->d[5] = alu_moveql(c, 0);                   /* moveq #0,d5 */
  lift_charge(x, 0x17646);

  lift_call(x, 0x17648, 4, Tilemap_DrawRegion); /* bsr.w sub_1169A */
  if (x->declined) return;

  sp = c->a[7];                                 /* movem.l (sp)+,d0-a3 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, sp + 4 * i);
  for (i = 0; i < 4; i++) c->a[i] = lift_r32(x, sp + 32 + 4 * i);
  c->a[7] = sp + 48;
  lift_charge_movem(x, 0x1764C);
  lift_charge(x, 0x17650);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Tilemap_DrawABA14Panel (sub_17652) — a 2x2 patch of the $ABA14 tilemap
 * at tile base 2. First it samples the $FFFFCEF4 byte table at index
 * ($FFFFCEEE), doubles it, and — when that matches the caller's d1 byte —
 * forces the name-table attribute word ($FFFFB02C) to $6000 (palette line
 * 3) for the draw. d0-a3 restored by the movem epilogue.
 */
void Tilemap_DrawABA14Panel(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;
  int i;

  c->a[7] -= 48;                                /* movem.l d0-a3,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 4; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0x17652);

  c->a[0] = 0xFFFFCEF4;                         /* move.w #$CEF4,a0 — movea.w sign-extends */
  lift_charge(x, 0x17656);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFCEEEu))); /* move.w ($CEEE).w,d0 */
  lift_charge(x, 0x1765A);
  setb(&c->d[0], alu_moveb(x->c, lift_r8(x, c->a[0] + SEW(c->d[0]))));  /* move.b (a0,d0.w),d0 */
  lift_charge(x, 0x1765E);
  setb(&c->d[0], alu_addb(c, c->d[0] & 0xFF, c->d[0] & 0xFF));          /* add.b d0,d0 */
  lift_charge(x, 0x17662);
  alu_cmpb(c, c->d[0] & 0xFF, c->d[1] & 0xFF);  /* cmp.b d0,d1 — d1 - d0 */
  lift_charge(x, 0x17664);
  lift_charge_bcc(x, 0x17666, !c->zf);          /* bne.w loc_17670 */
  if (c->zf)
  {
    alu_movew(c, 0x6000);                       /* move.w #$6000,($B02C).w */
    lift_w16(x, 0xFFB02Cu, 0x6000);
    lift_charge(x, 0x1766A);
  }

  /* loc_17670 */
  c->a[1] = 0x000ABA14;                         /* move.l #off_ABA14,a1 */
  lift_charge(x, 0x17670);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0x17676);
  c->a[2] = 0x0000030A;                         /* move.w #$30A,a2 */
  lift_charge(x, 0x1767A);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0x1767E);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d2 */
  lift_charge(x, 0x17680);
  c->d[3] = alu_moveql(c, 2);                   /* moveq #2,d3 */
  lift_charge(x, 0x17682);
  c->d[4] = alu_moveql(c, 2);                   /* moveq #2,d4 */
  lift_charge(x, 0x17684);
  c->d[5] = alu_moveql(c, 0);                   /* moveq #0,d5 */
  lift_charge(x, 0x17686);

  lift_call(x, 0x17688, 4, Tilemap_DrawRegion); /* bsr.w sub_1169A */
  if (x->declined) return;

  sp = c->a[7];                                 /* movem.l (sp)+,d0-a3 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, sp + 4 * i);
  for (i = 0; i < 4; i++) c->a[i] = lift_r32(x, sp + 32 + 4 * i);
  c->a[7] = sp + 48;
  lift_charge_movem(x, 0x1768C);
  lift_charge(x, 0x17690);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Card_DrawPortraitAndClearCaption (sub_FD17E)
 *   in: a0 = a two-long header (map offset, pattern offset)
 *       d3 = portrait index   d5 = 4 selects the tighter $20 stride
 * Draw the 6x6 portrait at the top-left of the text plane — the pattern
 * bytes come from $FF462 + (d3<<3 - $20 or - $40), the tilemap from the
 * header's second long — then park the text cursor at column 2 or $20
 * (whichever side of column $14 it was already on), row $19, and clear a
 * 8x3 patch with Text_FillRows. Saves nothing: d0-d3 and a0-a2 are all
 * clobbered on the way out.
 */
static void card_draw_portrait_tail(lift_ctx *x);

void Card_DrawPortraitAndClearCaption(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = c->a[0];                            /* move.l a0,a1 — movea */
  lift_charge(x, 0xFD17E);
  c->a[2] = c->a[0];                            /* move.l a0,a2 — movea */
  lift_charge(x, 0xFD180);
  c->a[0] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a0 — adda */
  c->a[2] += 4;
  lift_charge(x, 0xFD182);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 3));   /* asl.w #3,d3 */
  lift_charge(x, 0xFD184);
  alu_cmpw(c, 4, W(c->d[5]));                   /* cmpi.w #4,d5 */
  lift_charge(x, 0xFD186);
  lift_charge_bcc(x, 0xFD18A, c->zf);           /* beq.w loc_FD196 */
  if (!c->zf)
  {
    setw(&c->d[3], alu_subw(c, 0x20, W(c->d[3])));      /* sub.w #$20,d3 */
    lift_charge(x, 0xFD18E);
    lift_charge_bcc(x, 0xFD192, 1);             /* bra.w loc_FD19A */
  }
  else
  {
    setw(&c->d[3], alu_subw(c, 0x40, W(c->d[3])));      /* loc_FD196: sub.w #$40,d3 */
    lift_charge(x, 0xFD196);
  }

  /* loc_FD19A */
  c->a[0] = 0x000FF462;                         /* move.l #unk_FF462,a0 */
  lift_charge(x, 0xFD19A);
  c->a[0] += SEW(c->d[3]);                      /* add.w d3,a0 — adda */
  lift_charge(x, 0xFD1A0);
  c->a[1] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a1 — adda */
  c->a[2] += 4;
  lift_charge(x, 0xFD1A2);

  card_draw_portrait_tail(x);                   /* falls through into loc_FD1A4 */
}

/*
 * card_draw_portrait_tail (loc_FD1A4) — the draw half shared by
 * Card_DrawPortraitAndClearCaption (falls through) and
 * Card_DrawPortraitWithFallback (`bra.w` from $FD17A). Draws the 6x6
 * portrait at (0,0), then parks the cursor at column 2 or $20 — whichever
 * side of column $14 it was already on — row $19, and clears an 8x3 patch
 * with Text_FillRows. Its rts returns to whichever entry's caller.
 */
static void card_draw_portrait_tail(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[3], alu_movew(c, 6));              /* move.w #6,d3 */
  lift_charge(x, 0xFD1A4);
  setw(&c->d[2], alu_movew(c, 6));              /* move.w #6,d2 */
  lift_charge(x, 0xFD1A8);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0xFD1AC);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0xFD1AE);

  lift_call(x, 0xFD1B0, 6, Tilemap_DrawRegion); /* jsr sub_1169A */
  if (x->declined) return;

  alu_cmpw(c, 0x14, lift_r16(x, T_CURSOR_COL)); /* cmp.w #$14,($B028).w */
  lift_charge(x, 0xFD1B6);
  lift_charge_bcc(x, 0xFD1BC, !c->zf && c->nf == c->vf);  /* bgt.w loc_FD1CC */
  if (c->zf || c->nf != c->vf)
  {
    alu_movew(c, 2);                            /* move.w #2,($FFFFB028).l */
    lift_w16(x, T_CURSOR_COL, 2);
    lift_charge(x, 0xFD1C0);
    lift_charge_bcc(x, 0xFD1C8, 1);             /* bra.w loc_FD1D4 */
  }
  else
  {
    alu_movew(c, 0x20);                         /* loc_FD1CC: move.w #$20,($FFFFB028).l */
    lift_w16(x, T_CURSOR_COL, 0x20);
    lift_charge(x, 0xFD1CC);
  }

  /* loc_FD1D4 */
  alu_movew(c, 0x19);                           /* move.w #$19,($FFFFB02A).l */
  lift_w16(x, T_CURSOR_ROW, 0x19);
  lift_charge(x, 0xFD1D4);
  setw(&c->d[0], alu_movew(c, 8));              /* move.w #8,d0 */
  lift_charge(x, 0xFD1DC);
  setw(&c->d[1], alu_movew(c, 3));              /* move.w #3,d1 */
  lift_charge(x, 0xFD1E0);
  setw(&c->d[2], alu_movew(c, 0x7FF));          /* move.w #$7FF,d2 */
  lift_charge(x, 0xFD1E4);

  lift_call(x, 0xFD1E8, 6, Text_FillRows);      /* jsr sub_1197E */
  if (x->declined) return;

  lift_charge(x, 0xFD1EE);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * menu_draw_panel_tail (loc_F8796) — the shared body behind FOUR entry
 * points (blind spot 5: it is not a `sub_` of its own, so triage sees the
 * three siblings below only as "far-branches into mid-routine"). Each
 * entry picks its own tile base and text column and then `bra.w`s here;
 * this body draws the 8x8 $BF702 tilemap at row 5 with $FFFFC2F8 bit 0
 * held set across the call so Tilemap_DrawRegion skips its trailing
 * unpack, and its rts returns to the entry's caller.
 */
static void menu_draw_panel_tail(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;

  alu_movew(c, 0);                              /* clr.w ($B02C).w */
  lift_w16(x, 0xFFB02Cu, 0);
  lift_charge(x, 0xF8796);
  alu_movew(c, 5);                              /* move.w #5,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, 5);
  lift_charge(x, 0xF879A);
  c->a[0] = 0x000BF702;                         /* move.l #off_BF702,a0 */
  lift_charge(x, 0xF87A0);
  c->a[1] = c->a[0];                            /* move.l a0,a1 */
  lift_charge(x, 0xF87A6);
  c->a[2] = c->a[0];                            /* move.l a0,a2 */
  lift_charge(x, 0xF87A8);
  c->a[0] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a0 */
  c->a[2] += 4;
  lift_charge(x, 0xF87AA);
  c->a[1] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a1 */
  c->a[2] += 4;
  lift_charge(x, 0xF87AC);
  setw(&c->d[3], alu_movew(c, 8));              /* move.w #8,d3 */
  lift_charge(x, 0xF87AE);
  setw(&c->d[2], alu_movew(c, 8));              /* move.w #8,d2 */
  lift_charge(x, 0xF87B2);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0xF87B6);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0xF87B8);
  setw(&c->d[5], alu_movew(c, 0));              /* move.w #0,d5 */
  lift_charge(x, 0xF87BA);
  v = lift_r8(x, 0xFFC2F8u);                    /* bset #0,($C2F8).w — byte op */
  lift_w8(x, 0xFFC2F8u, alu_bset(c, v, 0));
  lift_charge(x, 0xF87BE);

  lift_call(x, 0xF87C4, 6, Tilemap_DrawRegion); /* jsr sub_1169A */
  if (x->declined) return;

  v = lift_r8(x, 0xFFC2F8u);                    /* bclr #0,($C2F8).w */
  lift_w8(x, 0xFFC2F8u, alu_bclr(c, v, 0));
  lift_charge(x, 0xF87CA);
  lift_charge(x, 0xF87D0);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_DrawPanelBackdropAt1C (sub_F8762) — the $FFFFD436 / column $1C
 * entry of menu_draw_panel_tail.
 */
void Menu_DrawPanelBackdropAt1C(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD436u)));  /* move.w ($D436).w,d4 */
  lift_charge(x, 0xF8762);
  alu_movew(c, 0x1C);                           /* move.w #$1C,($B028).w */
  lift_w16(x, T_CURSOR_COL, 0x1C);
  lift_charge(x, 0xF8766);
  lift_charge_bcc(x, 0xF876C, 1);               /* bra.w loc_F8796 */
  menu_draw_panel_tail(x);
}

/*
 * Menu_DrawPanelBackdropAt18 (sub_F8770) — the $FFFFD436 / column $18
 * entry; the one Menu_DrawOptionCardPane calls.
 */
void Menu_DrawPanelBackdropAt18(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD436u)));  /* move.w ($D436).w,d4 */
  lift_charge(x, 0xF8770);
  alu_movew(c, 0x18);                           /* move.w #$18,($B028).w */
  lift_w16(x, T_CURSOR_COL, 0x18);
  lift_charge(x, 0xF8774);
  lift_charge_bcc(x, 0xF877A, 1);               /* bra.w loc_F8796 */
  menu_draw_panel_tail(x);
}

/*
 * Menu_DrawPanelBackdropAt3 (sub_F877E) — the $FFFFD438 / column 3 entry.
 */
void Menu_DrawPanelBackdropAt3(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD438u)));  /* move.w ($D438).w,d4 */
  lift_charge(x, 0xF877E);
  alu_movew(c, 3);                              /* move.w #3,($B028).w */
  lift_w16(x, T_CURSOR_COL, 3);
  lift_charge(x, 0xF8782);
  lift_charge_bcc(x, 0xF8788, 1);               /* bra.w loc_F8796 */
  menu_draw_panel_tail(x);
}

/*
 * Menu_DrawPanelBackdrop (sub_F878C) — the $FFFFD438 / column 8 entry,
 * the one Menu_DrawTeamCardPane calls.
 */
void Menu_DrawPanelBackdrop(lift_ctx *x)
{
  rcpu_t *c = x->c;

  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD438u)));  /* move.w ($D438).w,d4 */
  lift_charge(x, 0xF878C);
  alu_movew(c, 8);                              /* move.w #8,($B028).w */
  lift_w16(x, T_CURSOR_COL, 8);
  lift_charge(x, 0xF8790);
  menu_draw_panel_tail(x);                      /* falls through into loc_F8796 */
}

/*
 * Menu_DrawTeamCardPane (sub_F8608) — the full option-screen team card:
 * clear the option panes, mark $FFFFD42E bits 3/7, paint the backdrop,
 * then (only when bit 2 was set — bclr both tests and clears it) draw the
 * first captioned line before the second one, and finally the 6x6 team
 * portrait picked by ($FFFFD428) out of the $F86F2 pointer table. The
 * camera longs $FFFFBD4A/$BD4E are parked on the stack across the draw.
 * All of d0-a6 restored by the movem epilogue.
 */
static void menu_team_card_tail(lift_ctx *x);

void Menu_DrawTeamCardPane(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp, v;
  int i;

  c->a[7] -= 60;                                /* movem.l d0-a6,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xF8608);

  lift_call(x, 0xF860C, 4, Menu_ClearOptionPanes);      /* bsr.w sub_F84FC */
  if (x->declined) return;

  v = lift_r8(x, 0xFFD42Eu);                    /* bset #3,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bset(c, v, 3));
  lift_charge(x, 0xF8610);
  v = lift_r8(x, 0xFFD42Eu);                    /* bset #7,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bset(c, v, 7));
  lift_charge(x, 0xF8616);

  lift_call(x, 0xF861C, 4, Menu_DrawPanelBackdrop);     /* bsr.w sub_F878C */
  if (x->declined) return;

  v = lift_r8(x, 0xFFD42Eu);                    /* bclr #2,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bclr(c, v, 2));
  lift_charge(x, 0xF8620);
  lift_charge_bcc(x, 0xF8626, c->zf);           /* beq.w loc_F864A */
  if (!c->zf)
  {
    lift_call(x, 0xF862A, 6, Text_DrawInlineString);    /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;
    alu_movew(c, 0x18);                         /* move.w #$18,($B028).w */
    lift_w16(x, T_CURSOR_COL, 0x18);
    lift_charge(x, 0xF8636);
    alu_movew(c, 5);                            /* move.w #5,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 5);
    lift_charge(x, 0xF863C);
    c->d[0] = alu_moveql(c, 8);                 /* moveq #8,d0 */
    lift_charge(x, 0xF8642);
    c->d[1] = alu_moveql(c, 8);                 /* moveq #8,d1 */
    lift_charge(x, 0xF8644);
    setw(&c->d[2], alu_movew(c, 0x7FF));        /* move.w #$7FF,d2 */
    lift_charge(x, 0xF8646);
  }

  /* loc_F864A */
  lift_call(x, 0xF864A, 6, Text_DrawInlineString);      /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  alu_movew(c, 9);                              /* move.w #9,($B028).w */
  lift_w16(x, T_CURSOR_COL, 9);
  lift_charge(x, 0xF8656);

  menu_team_card_tail(x);                       /* falls through into loc_F865C */
}

/*
 * menu_team_card_tail (loc_F865C) — the portrait half shared by
 * Menu_DrawTeamCardPane (falls through) and Menu_DrawOptionCardPane
 * (`bra.w` from $F8604). Picks the 6x6 team portrait by ($FFFFD428) out
 * of the $F86F2 pointer table, parks the camera longs $FFFFBD4A/$BD4E on
 * the stack across the draw, and ends with the movem epilogue that
 * restores the 60-byte frame BOTH entries pushed at their own entry.
 */
static void menu_team_card_tail(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp, v;
  int i, bit2;

  alu_movew(c, 0xB4);                           /* move.w #$B4,($D440).w */
  lift_w16(x, 0xFFD440u, 0xB4);
  lift_charge(x, 0xF865C);
  alu_movew(c, 6);                              /* move.w #6,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, 6);
  lift_charge(x, 0xF8662);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD430u))); /* move.w ($D430).w,d4 */
  lift_charge(x, 0xF8668);
  alu_btst(c, lift_r8(x, 0xFFD42Eu), 2);        /* btst #2,($D42E).w */
  lift_charge(x, 0xF866C);
  lift_charge_bcc(x, 0xF8672, !c->zf);          /* bne.w loc_F867A */
  if (c->zf)
  {
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD432u))); /* move.w ($D432).w,d4 */
    lift_charge(x, 0xF8676);
  }

  /* loc_F867A */
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFD428u))); /* move.w ($D428).w,d3 */
  lift_charge(x, 0xF867A);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));   /* asl.w #2,d3 */
  lift_charge(x, 0xF867E);
  c->a[0] = 0x000F86F2;                         /* move.l #off_F86F2,a0 */
  lift_charge(x, 0xF8680);
  c->a[0] = lift_r32(x, c->a[0] + SEW(c->d[3]));        /* move.l (a0,d3.w),a0 — movea */
  lift_charge(x, 0xF8686);
  c->a[1] = c->a[0];                            /* move.l a0,a1 */
  lift_charge(x, 0xF868A);
  c->a[2] = c->a[0];                            /* move.l a0,a2 */
  lift_charge(x, 0xF868C);
  c->a[0] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a0 */
  c->a[2] += 4;
  lift_charge(x, 0xF868E);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 3));   /* asl.w #3,d3 */
  lift_charge(x, 0xF8690);
  c->a[0] = 0x000FF462;                         /* move.l #unk_FF462,a0 */
  lift_charge(x, 0xF8692);
  alu_btst(c, lift_r8(x, 0xFFD42Eu), 2);        /* btst #2,($D42E).w */
  lift_charge(x, 0xF8698);
  bit2 = !c->zf;
  lift_charge_bcc(x, 0xF869E, c->zf);           /* beq.w loc_F86AA */
  if (bit2)
  {
    setw(&c->d[3], alu_subw(c, 0x20, W(c->d[3])));      /* sub.w #$20,d3 */
    lift_charge(x, 0xF86A2);
    lift_charge_bcc(x, 0xF86A6, 1);             /* bra.w loc_F86AE */
  }
  else
  {
    setw(&c->d[3], alu_subw(c, 0x40, W(c->d[3])));      /* loc_F86AA: sub.w #$40,d3 */
    lift_charge(x, 0xF86AA);
  }

  /* loc_F86AE */
  c->a[0] += SEW(c->d[3]);                      /* add.w d3,a0 — adda */
  lift_charge(x, 0xF86AE);
  c->a[1] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a1 */
  c->a[2] += 4;
  lift_charge(x, 0xF86B0);
  setw(&c->d[3], alu_movew(c, 6));              /* move.w #6,d3 */
  lift_charge(x, 0xF86B2);
  setw(&c->d[2], alu_movew(c, 6));              /* move.w #6,d2 */
  lift_charge(x, 0xF86B6);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0xF86BA);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0xF86BC);
  v = lift_r32(x, 0xFFBD4Au);                   /* move.l ($BD4A).w,-(sp) */
  alu_movel(c, v);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], v);
  lift_charge(x, 0xF86BE);
  v = lift_r32(x, 0xFFBD4Eu);                   /* move.l ($BD4E).w,-(sp) */
  alu_movel(c, v);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], v);
  lift_charge(x, 0xF86C2);
  setw(&c->d[5], alu_movew(c, 4));              /* move.w #4,d5 */
  lift_charge(x, 0xF86C6);
  alu_btst(c, lift_r8(x, 0xFFD42Eu), 2);        /* btst #2,($D42E).w */
  lift_charge(x, 0xF86CA);
  lift_charge_bcc(x, 0xF86D0, c->zf);           /* beq.w loc_F86D8 */
  if (!c->zf)
  {
    setw(&c->d[5], alu_movew(c, 2));            /* move.w #2,d5 */
    lift_charge(x, 0xF86D4);
  }

  /* loc_F86D8 */
  lift_call(x, 0xF86D8, 6, Tilemap_DrawRegion); /* jsr sub_1169A */
  if (x->declined) return;

  v = lift_r32(x, c->a[7]);                     /* move.l (sp)+,($BD4E).w */
  c->a[7] += 4;
  alu_movel(c, v);
  lift_w32(x, 0xFFBD4Eu, v);
  lift_charge(x, 0xF86DE);
  v = lift_r32(x, c->a[7]);                     /* move.l (sp)+,($BD4A).w */
  c->a[7] += 4;
  alu_movel(c, v);
  lift_w32(x, 0xFFBD4Au, v);
  lift_charge(x, 0xF86E2);
  alu_movew(c, 0x64);                           /* move.w #$64,($BD26).w */
  lift_w16(x, 0xFFBD26u, 0x64);
  lift_charge(x, 0xF86E6);

  sp = c->a[7];                                 /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, sp + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, sp + 32 + 4 * i);
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xF86EC);
  lift_charge(x, 0xF86F0);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_DrawTeamPortraitAtCursor (sub_FA66E) — draw the inline caption,
 * park the text cursor at the caller's (d0,d1), and paint the 6x6 team
 * portrait for ($FFFFC330) — or ($FFFFC332) when ($FFFFD4F6) is nonzero —
 * out of the same $F86F2 pointer table Menu_DrawTeamCardPane uses, always
 * with the $20 pattern stride and d5=2. Camera longs parked across the
 * draw; all of d0-a6 restored.
 */
void Menu_DrawTeamPortraitAtCursor(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp, v;
  int i;

  c->a[7] -= 60;                                /* movem.l d0-a6,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFA66E);

  lift_call(x, 0xFA672, 6, Text_DrawInlineString);      /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;

  alu_movew(c, W(c->d[0]));                     /* move.w d0,($B028).w */
  lift_w16(x, T_CURSOR_COL, W(c->d[0]));
  lift_charge(x, 0xFA67E);
  alu_movew(c, W(c->d[1]));                     /* move.w d1,($B02A).w */
  lift_w16(x, T_CURSOR_ROW, W(c->d[1]));
  lift_charge(x, 0xFA682);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD450u))); /* move.w ($D450).w,d4 */
  lift_charge(x, 0xFA686);
  setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFC330u))); /* move.w ($C330).w,d3 */
  lift_charge(x, 0xFA68A);
  alu_tstw(c, lift_r16(x, 0xFFD4F6u));          /* tst.w ($D4F6).w */
  lift_charge(x, 0xFA68E);
  lift_charge_bcc(x, 0xFA692, c->zf);           /* beq.w loc_FA69A */
  if (!c->zf)
  {
    setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFC332u))); /* move.w ($C332).w,d3 */
    lift_charge(x, 0xFA696);
  }

  /* loc_FA69A */
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 2));   /* asl.w #2,d3 */
  lift_charge(x, 0xFA69A);
  c->a[0] = 0x000F86F2;                         /* move.l #off_F86F2,a0 */
  lift_charge(x, 0xFA69C);
  c->a[0] = lift_r32(x, c->a[0] + SEW(c->d[3]));        /* move.l (a0,d3.w),a0 — movea */
  lift_charge(x, 0xFA6A2);
  c->a[1] = c->a[0];                            /* move.l a0,a1 */
  lift_charge(x, 0xFA6A6);
  c->a[2] = c->a[0];                            /* move.l a0,a2 */
  lift_charge(x, 0xFA6A8);
  c->a[0] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a0 */
  c->a[2] += 4;
  lift_charge(x, 0xFA6AA);
  setw(&c->d[3], alu_aslw(c, W(c->d[3]), 3));   /* asl.w #3,d3 */
  lift_charge(x, 0xFA6AC);
  c->a[0] = 0x000FF462;                         /* move.l #unk_FF462,a0 */
  lift_charge(x, 0xFA6AE);
  setw(&c->d[3], alu_subw(c, 0x20, W(c->d[3]))); /* sub.w #$20,d3 */
  lift_charge(x, 0xFA6B4);
  c->a[0] += SEW(c->d[3]);                      /* add.w d3,a0 — adda */
  lift_charge(x, 0xFA6B8);
  c->a[1] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a1 */
  c->a[2] += 4;
  lift_charge(x, 0xFA6BA);
  setw(&c->d[3], alu_movew(c, 6));              /* move.w #6,d3 */
  lift_charge(x, 0xFA6BC);
  setw(&c->d[2], alu_movew(c, 6));              /* move.w #6,d2 */
  lift_charge(x, 0xFA6C0);
  setw(&c->d[0], alu_movew(c, 0));              /* clr.w d0 */
  lift_charge(x, 0xFA6C4);
  setw(&c->d[1], alu_movew(c, 0));              /* clr.w d1 */
  lift_charge(x, 0xFA6C6);
  v = lift_r32(x, 0xFFBD4Au);                   /* move.l ($BD4A).w,-(sp) */
  alu_movel(c, v);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], v);
  lift_charge(x, 0xFA6C8);
  v = lift_r32(x, 0xFFBD4Eu);                   /* move.l ($BD4E).w,-(sp) */
  alu_movel(c, v);
  c->a[7] -= 4;
  lift_w32(x, c->a[7], v);
  lift_charge(x, 0xFA6CC);
  setw(&c->d[5], alu_movew(c, 2));              /* move.w #2,d5 */
  lift_charge(x, 0xFA6D0);

  lift_call(x, 0xFA6D4, 6, Tilemap_DrawRegion); /* jsr sub_1169A */
  if (x->declined) return;

  v = lift_r32(x, c->a[7]);                     /* move.l (sp)+,($BD4E).w */
  c->a[7] += 4;
  alu_movel(c, v);
  lift_w32(x, 0xFFBD4Eu, v);
  lift_charge(x, 0xFA6DA);
  v = lift_r32(x, c->a[7]);                     /* move.l (sp)+,($BD4A).w */
  c->a[7] += 4;
  alu_movel(c, v);
  lift_w32(x, 0xFFBD4Au, v);
  lift_charge(x, 0xFA6DE);

  sp = c->a[7];                                 /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, sp + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, sp + 32 + 4 * i);
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFA6E2);
  lift_charge(x, 0xFA6E6);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Board_DrawTitleBannerF0 (sub_A448) — flag $FFFFDEB4 = $F0, draw the
 * inline caption, then tail into Tilemap_DrawRegion for an 8x4 patch at
 * column $20 / row $55 from the $5605A tilemap — or $B5180 when
 * $FFFFC2F4 bit 4 is set. d5 = 0, so a0 (never set here) is unused.
 */
void Board_DrawTitleBannerF0(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movew(c, 0xF0);                           /* move.w #$F0,($DEB4).w */
  lift_w16(x, 0xFFDEB4u, 0xF0);
  lift_charge(x, 0xA448);
  lift_call(x, 0xA44E, 4, Text_DrawInlineString);       /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;

  c->d[0] = alu_moveql(c, 0x20);                /* moveq #$20,d0 */
  lift_charge(x, 0xA458);
  c->d[1] = alu_moveql(c, 0x55);                /* moveq #$55,d1 */
  lift_charge(x, 0xA45A);
  c->d[2] = alu_moveql(c, 8);                   /* moveq #8,d2 */
  lift_charge(x, 0xA45C);
  c->d[3] = alu_moveql(c, 4);                   /* moveq #4,d3 */
  lift_charge(x, 0xA45E);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB018u))); /* move.w ($B018).w,d4 */
  lift_charge(x, 0xA460);
  setw(&c->d[5], alu_movew(c, 0));              /* move.w #0,d5 */
  lift_charge(x, 0xA464);
  c->a[1] = 0x0005605A;                         /* move.l #off_5605A,a1 */
  lift_charge(x, 0xA468);
  alu_btst(c, lift_r8(x, 0xFFC2F4u), 4);        /* btst #4,($C2F4).w */
  lift_charge(x, 0xA46E);
  lift_charge_bcc(x, 0xA474, c->zf);            /* beq.w loc_A47E */
  if (!c->zf)
  {
    c->a[1] = 0x000B5180;                       /* move.l #off_B5180,a1 */
    lift_charge(x, 0xA478);
  }

  /* loc_A47E */
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0xA47E);
  c->a[2] = 0x0000030A;                         /* move.w #$30A,a2 */
  lift_charge(x, 0xA482);
  lift_charge_bcc(x, 0xA486, 1);                /* bra.w sub_1169A — tail */
  Tilemap_DrawRegion(x);
}

/*
 * Board_DrawFullPanelFF (sub_A4A8) — the `st` sibling of
 * Board_DrawTitleBannerF0: flag $FFFFDEB4 = $FF, inline caption, then a
 * $10x$B patch of the $BB4EE tilemap at (0,0) on the $FFFFB026 base.
 */
void Board_DrawFullPanelFF(lift_ctx *x)
{
  rcpu_t *c = x->c;

  lift_w8(x, 0xFFDEB4u, 0xFF);                  /* st ($DEB4).w — Scc sets no CCR */
  lift_charge(x, 0xA4A8);
  lift_call(x, 0xA4AC, 4, Text_DrawInlineString);       /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;

  c->d[0] = alu_moveql(c, 0);                   /* moveq #0,d0 */
  lift_charge(x, 0xA4B6);
  c->d[1] = alu_moveql(c, 0);                   /* moveq #0,d1 */
  lift_charge(x, 0xA4B8);
  c->d[2] = alu_moveql(c, 0x10);                /* moveq #$10,d2 */
  lift_charge(x, 0xA4BA);
  c->d[3] = alu_moveql(c, 0x0B);                /* moveq #$B,d3 */
  lift_charge(x, 0xA4BC);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB026u))); /* move.w ($B026).w,d4 */
  lift_charge(x, 0xA4BE);
  setw(&c->d[5], alu_movew(c, 0));              /* move.w #0,d5 */
  lift_charge(x, 0xA4C2);
  c->a[1] = 0x000BB4EE;                         /* move.l #off_BB4EE,a1 */
  lift_charge(x, 0xA4C6);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0xA4CC);
  c->a[2] = 0x0000030A;                         /* move.w #$30A,a2 */
  lift_charge(x, 0xA4D0);
  lift_charge_bcc(x, 0xA4D4, 1);                /* bra.w sub_1169A — tail */
  Tilemap_DrawRegion(x);
}

/*
 * Menu_DrawOptionCardPane (sub_F85B0) — wave 53. The sibling of
 * Menu_DrawTeamCardPane on the other option-screen flag pair: clear the
 * option panes, mark $FFFFD42E bits 2/6, paint the column-$18 backdrop,
 * then (only when bit 3 was set — the `bclr` both tests and clears it)
 * draw the first captioned line before the second, park the cursor at
 * column $19, and `bra.w` into the shared portrait tail at loc_F865C.
 * Its 60-byte movem frame is the one that tail's epilogue pops.
 */
void Menu_DrawOptionCardPane(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp, v;
  int i;

  c->a[7] -= 60;                                /* movem.l d0-a6,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xF85B0);

  lift_call(x, 0xF85B4, 4, Menu_ClearOptionPanes);      /* bsr.w sub_F84FC */
  if (x->declined) return;

  v = lift_r8(x, 0xFFD42Eu);                    /* bset #2,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bset(c, v, 2));
  lift_charge(x, 0xF85B8);
  v = lift_r8(x, 0xFFD42Eu);                    /* bset #6,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bset(c, v, 6));
  lift_charge(x, 0xF85BE);

  lift_call(x, 0xF85C4, 4, Menu_DrawPanelBackdropAt18); /* bsr.w sub_F8770 */
  if (x->declined) return;

  v = lift_r8(x, 0xFFD42Eu);                    /* bclr #3,($D42E).w */
  lift_w8(x, 0xFFD42Eu, alu_bclr(c, v, 3));
  lift_charge(x, 0xF85C8);
  lift_charge_bcc(x, 0xF85CE, c->zf);           /* beq.w loc_F85F2 */
  if (!c->zf)
  {
    lift_call(x, 0xF85D2, 6, Text_DrawInlineString);    /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;
    alu_movew(c, 8);                            /* move.w #8,($B028).w */
    lift_w16(x, T_CURSOR_COL, 8);
    lift_charge(x, 0xF85DE);
    alu_movew(c, 5);                            /* move.w #5,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 5);
    lift_charge(x, 0xF85E4);
    c->d[0] = alu_moveql(c, 8);                 /* moveq #8,d0 */
    lift_charge(x, 0xF85EA);
    c->d[1] = alu_moveql(c, 8);                 /* moveq #8,d1 */
    lift_charge(x, 0xF85EC);
    setw(&c->d[2], alu_movew(c, 0x7FF));        /* move.w #$7FF,d2 */
    lift_charge(x, 0xF85EE);
  }

  /* loc_F85F2 */
  lift_call(x, 0xF85F2, 6, Text_DrawInlineString);      /* jsr sub_11B92 + 6 inline */
  if (x->declined) return;
  alu_movew(c, 0x19);                           /* move.w #$19,($B028).w */
  lift_w16(x, T_CURSOR_COL, 0x19);
  lift_charge(x, 0xF85FE);

  lift_charge_bcc(x, 0xF8604, 1);               /* bra.w loc_F865C */
  menu_team_card_tail(x);
}

/* wave 54 callees defined in game.c */
void Text_ExpandDigitStream(lift_ctx *);
void Lookup_JumpTableEntry(lift_ctx *);
void Lookup_PenaltyOrTierTable(lift_ctx *);

/*
 * Card_DrawPortraitWithFallback (sub_FD14A) — wave 54. The fallback
 * variant of Card_DrawPortraitAndClearCaption: a0 arrives pointing at a
 * two-long header, and a2 walks it. The pattern pointer is always the
 * $C63F8 blob plus its own first long (the a0 the header would have given
 * is computed and then discarded at $FD150 — faithful, and dead). The
 * tilemap is the header's second long when it is nonzero, otherwise
 * $C63F8's own 4(a1) offset. Text_ExpandDigitStream then fills the digit
 * buffer, a2 is pointed at $FFFFDA1E, and it `bra.w`s into the shared
 * draw half at loc_FD1A4.
 *
 * The old skip row said loc_FD1A4 "is not a self-owned chunk trick; it
 * jsr sub_1169A and jsr sub_1197E, both hw, on every path" — that was
 * true when it was written and is stale now: both are lifted
 * (Tilemap_DrawRegion wave 51, Text_FillRows earlier), and the tail is
 * exactly the blind-spot-5 shared-body shape.
 */
void Card_DrawPortraitWithFallback(lift_ctx *x)
{
  rcpu_t *c = x->c;

  c->a[1] = c->a[0];                            /* move.l a0,a1 — movea */
  lift_charge(x, 0xFD14A);
  c->a[2] = c->a[0];                            /* move.l a0,a2 — movea */
  lift_charge(x, 0xFD14C);
  c->a[0] += lift_r32(x, c->a[2]);              /* add.l (a2)+,a0 — adda */
  c->a[2] += 4;
  lift_charge(x, 0xFD14E);
  c->a[0] = 0x000C63F8;                         /* move.l #off_C63F8,a0 */
  lift_charge(x, 0xFD150);
  c->a[0] += lift_r32(x, c->a[0]);              /* add.l (a0),a0 — adda */
  lift_charge(x, 0xFD156);
  alu_tstl(c, lift_r32(x, c->a[2]));            /* tst.l (a2) */
  lift_charge(x, 0xFD158);
  lift_charge_bcc(x, 0xFD15A, !c->zf);          /* bne.w loc_FD16E */
  if (c->zf)
  {
    c->a[1] = 0x000C63F8;                       /* move.l #off_C63F8,a1 */
    lift_charge(x, 0xFD15E);
    c->a[1] += lift_r32(x, c->a[1] + 4);        /* add.l 4(a1),a1 — adda */
    lift_charge(x, 0xFD164);
    alu_tstl(c, lift_r32(x, c->a[2]));          /* tst.l (a2)+ */
    c->a[2] += 4;
    lift_charge(x, 0xFD168);
    lift_charge_bcc(x, 0xFD16A, 1);             /* bra.w loc_FD170 */
  }
  else
  {
    c->a[1] += lift_r32(x, c->a[2]);            /* loc_FD16E: add.l (a2)+,a1 — adda */
    c->a[2] += 4;
    lift_charge(x, 0xFD16E);
  }

  /* loc_FD170 */
  lift_call(x, 0xFD170, 4, Text_ExpandDigitStream);     /* bsr.w sub_FE98A */
  if (x->declined) return;

  c->a[2] = 0xFFFFDA1E;                         /* move.l #$FFFFDA1E,a2 — movea */
  lift_charge(x, 0xFD174);
  lift_charge_bcc(x, 0xFD17A, 1);               /* bra.w loc_FD1A4 */
  card_draw_portrait_tail(x);
}

/*
 * Card_DrawBothTeamPortraits (sub_FD084) — wave 54. Draws the away card
 * at column 2 and the home card at column $20, both on row $F. With
 * ($FFFFD598).w clear it takes the plain path: Lookup_JumpTableEntry on
 * the team id ($C332 / $C330) into a0, tile bases $D432 / $D430, d5 = 4
 * then 2, and the away card additionally forces the attribute word
 * $FFFFB02C to $6000 (palette line 3) — drawn by
 * Card_DrawPortraitAndClearCaption. With $D598 set it takes the
 * penalty/tier path instead: Lookup_PenaltyOrTierTable keyed by
 * ($FFFFD59E / $D59C) and the fallback drawer. Either way it finishes by
 * setting $FFFFBD26 to $64. All of d0-a6 restored by the movem epilogue.
 */
void Card_DrawBothTeamPortraits(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t sp;
  int i;

  c->a[7] -= 60;                                /* movem.l d0-a6,-(sp) */
  sp = c->a[7];
  for (i = 0; i < 8; i++) lift_w32(x, sp + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, sp + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFD084);

  alu_tstw(c, lift_r16(x, 0xFFD598u));          /* tst.w ($D598).w */
  lift_charge(x, 0xFD088);
  lift_charge_bcc(x, 0xFD08C, !c->zf);          /* bne.w loc_FD0E6 */
  if (c->zf)
  {
    setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFC332u)));       /* move.w ($C332).w,d3 */
    lift_charge(x, 0xFD090);
    lift_call(x, 0xFD094, 4, Lookup_JumpTableEntry);            /* bsr.w sub_FD1F0 */
    if (x->declined) return;
    alu_movew(c, 2);                            /* move.w #2,($B028).w */
    lift_w16(x, T_CURSOR_COL, 2);
    lift_charge(x, 0xFD098);
    alu_movew(c, 0xF);                          /* move.w #$F,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 0xF);
    lift_charge(x, 0xFD09E);
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD432u)));       /* move.w ($D432).w,d4 */
    lift_charge(x, 0xFD0A4);
    setw(&c->d[5], alu_movew(c, 4));            /* move.w #4,d5 */
    lift_charge(x, 0xFD0A8);
    alu_movew(c, 0x6000);                       /* move.w #$6000,($B02C).w */
    lift_w16(x, 0xFFB02Cu, 0x6000);
    lift_charge(x, 0xFD0AC);
    lift_call(x, 0xFD0B2, 4, Card_DrawPortraitAndClearCaption); /* bsr.w sub_FD17E */
    if (x->declined) return;

    setw(&c->d[3], alu_movew(c, lift_r16(x, 0xFFC330u)));       /* move.w ($C330).w,d3 */
    lift_charge(x, 0xFD0B6);
    lift_call(x, 0xFD0BA, 4, Lookup_JumpTableEntry);            /* bsr.w sub_FD1F0 */
    if (x->declined) return;
    alu_movew(c, 0x20);                         /* move.w #$20,($B028).w */
    lift_w16(x, T_CURSOR_COL, 0x20);
    lift_charge(x, 0xFD0BE);
    alu_movew(c, 0xF);                          /* move.w #$F,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 0xF);
    lift_charge(x, 0xFD0C4);
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD430u)));       /* move.w ($D430).w,d4 */
    lift_charge(x, 0xFD0CA);
    alu_movew(c, 0);                            /* move.w #0,($B02C).w */
    lift_w16(x, 0xFFB02Cu, 0);
    lift_charge(x, 0xFD0CE);
    setw(&c->d[5], alu_movew(c, 2));            /* move.w #2,d5 */
    lift_charge(x, 0xFD0D4);
    lift_call(x, 0xFD0D8, 4, Card_DrawPortraitAndClearCaption); /* bsr.w sub_FD17E */
    if (x->declined) return;
    alu_movew(c, 0x64);                         /* move.w #$64,($BD26).w */
    lift_w16(x, 0xFFBD26u, 0x64);
    lift_charge(x, 0xFD0DC);
    lift_charge_bcc(x, 0xFD0E2, 1);             /* bra.w loc_FD144 */
  }
  else
  {
    /* loc_FD0E6 */
    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFC332u)));       /* move.w ($C332).w,d1 */
    lift_charge(x, 0xFD0E6);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD59Eu)));       /* move.w ($D59E).w,d0 */
    lift_charge(x, 0xFD0EA);
    lift_call(x, 0xFD0EE, 6, Lookup_PenaltyOrTierTable);        /* jsr sub_FAE26 */
    if (x->declined) return;
    alu_movew(c, 2);                            /* move.w #2,($B028).w */
    lift_w16(x, T_CURSOR_COL, 2);
    lift_charge(x, 0xFD0F4);
    alu_movew(c, 0xF);                          /* move.w #$F,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 0xF);
    lift_charge(x, 0xFD0FA);
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD432u)));       /* move.w ($D432).w,d4 */
    lift_charge(x, 0xFD100);
    setw(&c->d[5], alu_movew(c, 2));            /* move.w #2,d5 */
    lift_charge(x, 0xFD104);
    alu_movew(c, 0);                            /* move.w #0,($B02C).w */
    lift_w16(x, 0xFFB02Cu, 0);
    lift_charge(x, 0xFD108);
    lift_call(x, 0xFD10E, 4, Card_DrawPortraitWithFallback);    /* bsr.w sub_FD14A */
    if (x->declined) return;

    setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFC330u)));       /* move.w ($C330).w,d1 */
    lift_charge(x, 0xFD112);
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD59Cu)));       /* move.w ($D59C).w,d0 */
    lift_charge(x, 0xFD116);
    lift_call(x, 0xFD11A, 6, Lookup_PenaltyOrTierTable);        /* jsr sub_FAE26 */
    if (x->declined) return;
    alu_movew(c, 0x20);                         /* move.w #$20,($B028).w */
    lift_w16(x, T_CURSOR_COL, 0x20);
    lift_charge(x, 0xFD120);
    alu_movew(c, 0xF);                          /* move.w #$F,($B02A).w */
    lift_w16(x, T_CURSOR_ROW, 0xF);
    lift_charge(x, 0xFD126);
    setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD430u)));       /* move.w ($D430).w,d4 */
    lift_charge(x, 0xFD12C);
    setw(&c->d[5], alu_movew(c, 0));            /* move.w #0,d5 */
    lift_charge(x, 0xFD130);
    alu_movew(c, 0);                            /* move.w #0,($B02C).w */
    lift_w16(x, 0xFFB02Cu, 0);
    lift_charge(x, 0xFD134);
    lift_call(x, 0xFD13A, 4, Card_DrawPortraitWithFallback);    /* bsr.w sub_FD14A */
    if (x->declined) return;
    alu_movew(c, 0x64);                         /* move.w #$64,($BD26).w */
    lift_w16(x, 0xFFBD26u, 0x64);
    lift_charge(x, 0xFD13E);
  }

  /* loc_FD144 */
  sp = c->a[7];                                 /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, sp + 4 * i);
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, sp + 32 + 4 * i);
  c->a[7] = sp + 60;
  lift_charge_movem(x, 0xFD144);
  lift_charge(x, 0xFD148);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

void Sprite_BuildPieceHeader(lift_ctx *);   /* sub_16468, render.c */

/*
 * Clock_RenderGameTime (sub_162FE) — wave 55. Two completely different
 * halves picked by bit 7 of $FFFFC2EC, and the second one is an IDA
 * FUNCTION CHUNK at loc_163BE ($AA bytes) that belongs to this routine —
 * blind spot 4, not 5, which is why triage only ever reported
 * "far-branches into mid-routine loc_163BE".
 *
 * Bit 7 SET — the text path. Unless $FFFFC2FA bit 0 says the clock is
 * suppressed, it brackets the draw with two inline captions (a1 parked on
 * the stack across them) and prints the tick count at $FFFFC468 as
 * mm:ss.t: divu $258 (600 ticks = a minute) gives the minutes, and each
 * following stage swaps the remainder down and divides again by $3C then
 * $A. Each digit goes through Text_DrawIndexedChunk; a zero minutes digit
 * is skipped and the cursor simply advances instead.
 *
 * Bit 7 CLEAR — the sprite path (loc_163BE). It consumes the "clock
 * dirty" latch (`bclr #3,($BF78).w`) and bails out through the shared far
 * rts at $15464 if it was already clear, if $FFFFC2EE bit 3 is set, or if
 * ($FFFFC466).w is exactly 4. Otherwise it builds the clock's sprite
 * pieces backwards from $FFFFBF8C: the colon/base tile comes from
 * Art_BoardText's $78(a1) entry biased by ($FFFFB012).w with the priority
 * bit forced on, then four Sprite_BuildPieceHeader calls emit the digits
 * of ($FFFFC468) — or ($FFFFD454) when $FFFFC2FA bit 1 selects the
 * alternate counter — via divu $A / 6 / $A, with a zero high digit
 * replaced by the blank glyph ($FFFFFFF0 swapped in). Finally the piece
 * list at (a5) gets the pointer, a count of 5, and a packed position word
 * computed from the $FFFFB004 or $FFFFB008 plane entry.
 */
void Clock_RenderGameTime(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;
  int cnt;

  alu_btst(c, lift_r8(x, 0xFFC2ECu), 7);        /* btst #7,($C2EC).w */
  lift_charge(x, 0x162FE);
  lift_charge_bcc(x, 0x16304, c->zf);           /* beq.w loc_163BE */
  if (!c->zf)
  {
    alu_btst(c, lift_r8(x, 0xFFC2FAu), 0);      /* btst #0,($C2FA).w */
    lift_charge(x, 0x16308);
    lift_charge_bcc(x, 0x1630E, !c->zf);        /* bne.w locret_16382 */
    if (!c->zf)
    {
      lift_charge(x, 0x16382);                  /* locret_16382: rts */
      c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
      c->a[7] += 4;
      return;
    }

    alu_movel(c, c->a[1]);                      /* move.l a1,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->a[1]);
    lift_charge(x, 0x16312);

    lift_call(x, 0x16314, 6, Text_DrawInlineString);    /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;

    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFC468u)));       /* move.w ($C468).w,d0 */
    lift_charge(x, 0x16320);
    c->d[0] = alu_extl(c, c->d[0]);             /* ext.l d0 */
    lift_charge(x, 0x16324);
    lift_charge_divu(x, 0x16326, 0x258, c->d[0]);               /* divu.w #$258,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, 0x258, c->d[0]);

    alu_movel(c, c->d[0]);                      /* move.l d0,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->d[0]);
    lift_charge(x, 0x1632A);
    alu_tstw(c, W(c->d[0]));                    /* tst.w d0 */
    lift_charge(x, 0x1632C);
    lift_charge_bcc(x, 0x1632E, !c->zf);        /* bne.w loc_1633A */
    if (c->zf)
    {
      v = lift_r16(x, T_CURSOR_COL);            /* addq.w #1,($B028).w */
      lift_w16(x, T_CURSOR_COL, alu_addw(c, 1, v));
      lift_charge(x, 0x16332);
      lift_charge_bcc(x, 0x16336, 1);           /* bra.w loc_16340 */
    }
    else
    {
      lift_call(x, 0x1633A, 6, Text_DrawIndexedChunk);  /* loc_1633A: jsr sub_16384 */
      if (x->declined) return;
    }

    /* loc_16340 */
    v = lift_r32(x, c->a[7]);                   /* move.l (sp)+,d0 */
    c->d[0] = alu_movel(c, v);
    c->a[7] += 4;
    lift_charge(x, 0x16340);
    c->d[0] = alu_swap(c, c->d[0]);             /* swap d0 */
    lift_charge(x, 0x16342);
    c->d[0] = alu_extl(c, c->d[0]);             /* ext.l d0 */
    lift_charge(x, 0x16344);
    lift_charge_divu(x, 0x16346, 0x3C, c->d[0]);                /* divu.w #$3C,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, 0x3C, c->d[0]);

    alu_movel(c, c->d[0]);                      /* move.l d0,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->d[0]);
    lift_charge(x, 0x1634A);
    lift_call(x, 0x1634C, 6, Text_DrawIndexedChunk);            /* jsr sub_16384 */
    if (x->declined) return;

    v = lift_r32(x, c->a[7]);                   /* move.l (sp)+,d0 */
    c->d[0] = alu_movel(c, v);
    c->a[7] += 4;
    lift_charge(x, 0x16352);
    c->d[0] = alu_swap(c, c->d[0]);             /* swap d0 */
    lift_charge(x, 0x16354);
    c->d[0] = alu_extl(c, c->d[0]);             /* ext.l d0 */
    lift_charge(x, 0x16356);
    v = lift_r16(x, T_CURSOR_COL);              /* addq.w #2,($B028).w */
    lift_w16(x, T_CURSOR_COL, alu_addw(c, 2, v));
    lift_charge(x, 0x16358);
    lift_charge_divu(x, 0x1635C, 0xA, c->d[0]);                 /* divu.w #$A,d0 */
    if (x->declined) return;
    c->d[0] = alu_divu(c, 0xA, c->d[0]);

    alu_movel(c, c->d[0]);                      /* move.l d0,-(sp) */
    c->a[7] -= 4;
    lift_w32(x, c->a[7], c->d[0]);
    lift_charge(x, 0x16360);
    lift_call(x, 0x16362, 6, Text_DrawIndexedChunk);            /* jsr sub_16384 */
    if (x->declined) return;

    v = lift_r32(x, c->a[7]);                   /* move.l (sp)+,d0 */
    c->d[0] = alu_movel(c, v);
    c->a[7] += 4;
    lift_charge(x, 0x16368);
    c->d[0] = alu_swap(c, c->d[0]);             /* swap d0 */
    lift_charge(x, 0x1636A);
    c->d[0] = alu_extl(c, c->d[0]);             /* ext.l d0 */
    lift_charge(x, 0x1636C);
    lift_call(x, 0x1636E, 6, Text_DrawIndexedChunk);            /* jsr sub_16384 */
    if (x->declined) return;

    c->a[1] = lift_r32(x, c->a[7]);             /* move.l (sp)+,a1 — movea, no CCR */
    c->a[7] += 4;
    lift_charge(x, 0x16374);
    lift_call(x, 0x16376, 6, Text_DrawInlineString);            /* jsr sub_11B92 + 6 inline */
    if (x->declined) return;

    lift_charge(x, 0x16382);                    /* locret_16382: rts */
    c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
    c->a[7] += 4;
    return;
  }

  /* ---- loc_163BE: the sprite path (this routine's own function chunk) ---- */
  v = lift_r8(x, 0xFFBF78u);                    /* bclr #3,($BF78).w — byte op */
  lift_w8(x, 0xFFBF78u, alu_bclr(c, v, 3));
  lift_charge(x, 0x163BE);
  lift_charge_bcc(x, 0x163C4, c->zf);           /* beq.w locret_15464 */
  if (c->zf) goto far_rts;

  alu_btst(c, lift_r8(x, 0xFFC2EEu), 3);        /* btst #3,($C2EE).w */
  lift_charge(x, 0x163C8);
  lift_charge_bcc(x, 0x163CE, !c->zf);          /* bne.w locret_15464 */
  if (!c->zf) goto far_rts;

  alu_cmpw(c, 4, lift_r16(x, 0xFFC466u));       /* cmp.w #4,($C466).w */
  lift_charge(x, 0x163D2);
  lift_charge_bcc(x, 0x163D8, c->zf);           /* beq.w locret_15464 */
  if (c->zf) goto far_rts;

  c->a[0] = 0xFFFFBF8C;                         /* move.w #$BF8C,a0 — movea.w */
  lift_charge(x, 0x163DC);
  c->a[1] = 0x000AAC52;                         /* move.l #Art_BoardText,a1 */
  lift_charge(x, 0x163E0);
  c->a[1] += lift_r32(x, c->a[1] + 4);          /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0x163E6);
  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1] + 0x78)));    /* move.w $78(a1),d0 */
  lift_charge(x, 0x163EA);
  setw(&c->d[0], alu_addw(c, lift_r16(x, 0xFFB012u), W(c->d[0])));  /* add.w ($B012).w,d0 */
  lift_charge(x, 0x163EE);
  setw(&c->d[0], alu_orw(c, 0x8000, W(c->d[0])));              /* or.w #$8000,d0 */
  lift_charge(x, 0x163F2);
  alu_movew(c, W(c->d[0]));                     /* move.w d0,($BF86).w */
  lift_w16(x, 0xFFBF86u, W(c->d[0]));
  lift_charge(x, 0x163F6);
  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFC468u)));        /* move.w ($C468).w,d0 */
  lift_charge(x, 0x163FA);
  alu_btst(c, lift_r8(x, 0xFFC2FAu), 1);        /* btst #1,($C2FA).w */
  lift_charge(x, 0x163FE);
  lift_charge_bcc(x, 0x16404, c->zf);           /* beq.w loc_1640C */
  if (!c->zf)
  {
    setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFD454u)));      /* move.w ($D454).w,d0 */
    lift_charge(x, 0x16408);
  }

  /* loc_1640C */
  c->d[0] = alu_extl(c, c->d[0]);               /* ext.l d0 */
  lift_charge(x, 0x1640C);
  lift_charge_divu(x, 0x1640E, 0xA, c->d[0]);   /* divu.w #$A,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0xA, c->d[0]);
  lift_call(x, 0x16412, 4, Sprite_BuildPieceHeader);           /* bsr.w sub_16468 */
  if (x->declined) return;

  lift_charge_divu(x, 0x16416, 6, c->d[0]);     /* divu.w #6,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 6, c->d[0]);
  lift_call(x, 0x1641A, 4, Sprite_BuildPieceHeader);           /* bsr.w sub_16468 */
  if (x->declined) return;

  c->a[0] -= 2;                                 /* subq.w #2,a0 — no CCR */
  lift_charge(x, 0x1641E);
  lift_charge_divu(x, 0x16420, 0xA, c->d[0]);   /* divu.w #$A,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0xA, c->d[0]);
  lift_call(x, 0x16424, 4, Sprite_BuildPieceHeader);           /* bsr.w sub_16468 */
  if (x->declined) return;

  c->d[0] = alu_swap(c, c->d[0]);               /* swap d0 */
  lift_charge(x, 0x16428);
  alu_tstl(c, c->d[0]);                         /* tst.l d0 */
  lift_charge(x, 0x1642A);
  lift_charge_bcc(x, 0x1642C, !c->zf);          /* bne.w loc_16434 */
  if (c->zf)
  {
    c->d[0] = alu_moveql(c, -16);               /* moveq #$F0,d0 */
    lift_charge(x, 0x16430);
    c->d[0] = alu_swap(c, c->d[0]);             /* swap d0 */
    lift_charge(x, 0x16432);
  }

  /* loc_16434 */
  lift_call(x, 0x16434, 4, Sprite_BuildPieceHeader);           /* bsr.w sub_16468 */
  if (x->declined) return;

  alu_movel(c, c->a[0]);                        /* move.l a0,(a5)+ */
  lift_w32(x, c->a[5], c->a[0]);
  c->a[5] += 4;
  lift_charge(x, 0x16438);
  alu_movew(c, 5);                              /* move.w #5,(a5)+ */
  lift_w16(x, c->a[5], 5);
  c->a[5] += 2;
  lift_charge(x, 0x1643A);
  c->a[1] = 0xFFFFB004;                         /* move.w #$B004,a1 — movea.w */
  lift_charge(x, 0x1643E);
  c->d[0] = alu_moveql(c, 0x18);                /* moveq #$18,d0 */
  lift_charge(x, 0x16442);
  c->d[2] = alu_moveql(c, 3);                   /* moveq #3,d2 */
  lift_charge(x, 0x16444);
  alu_btst(c, lift_r8(x, 0xFFC2ECu), 7);        /* btst #7,($C2EC).w */
  lift_charge(x, 0x16446);
  lift_charge_bcc(x, 0x1644C, c->zf);           /* beq.w loc_16458 */
  if (!c->zf)
  {
    c->a[1] = 0xFFFFB008;                       /* move.w #$B008,a1 — movea.w */
    lift_charge(x, 0x16450);
    c->d[0] = alu_moveql(c, 5);                 /* moveq #5,d0 */
    lift_charge(x, 0x16454);
    c->d[2] = alu_moveql(c, 0xD);               /* moveq #$D,d2 */
    lift_charge(x, 0x16456);
  }

  /* loc_16458 */
  setw(&c->d[1], alu_movew(c, lift_r16(x, c->a[1] + 2)));      /* move.w 2(a1),d1 */
  lift_charge(x, 0x16458);
  cnt = (int)(c->d[1] & 63);                    /* asl.w d1,d0 — dynamic count */
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), cnt));
  lift_charge_shift_reg(x, 0x1645C, cnt);
  setw(&c->d[0], alu_addw(c, W(c->d[2]), W(c->d[0])));         /* add.w d2,d0 */
  lift_charge(x, 0x1645E);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 1));   /* asl.w #1,d0 */
  lift_charge(x, 0x16460);
  setw(&c->d[0], alu_addw(c, lift_r16(x, c->a[1]), W(c->d[0])));  /* add.w (a1),d0 */
  lift_charge(x, 0x16462);
  alu_movew(c, W(c->d[0]));                     /* move.w d0,(a5)+ */
  lift_w16(x, c->a[5], W(c->d[0]));
  c->a[5] += 2;
  lift_charge(x, 0x16464);
  lift_charge(x, 0x16466);                      /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
  return;

far_rts:
  lift_charge(x, 0x15464);                      /* shared far rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/* ===========================================================================
 * Wave 57 — the expired-skip re-triages (sub_15EA4, sub_17AF4) and the
 * cold-front rows (sub_16CE0/16CFC/16D0A, sub_12E66, sub_B92E).
 * See QUEUE.md's WAVE 57 note: the first two carry real profiled coverage,
 * the last five have ZERO entries on all nine scripts and ship
 * done-unverified with that justification on their Done bullets.
 * =========================================================================== */

void Team_RefreshDataCache(lift_ctx *);        /* game.c — sub_17190 */
void Lineup_AverageLineRating(lift_ctx *);     /* game.c — sub_12EB4 */

/*
 * VDP_CommitScrollPair (sub_15EA4)
 * Push the scroll pair sub_15F34 staged in $FFFFBD1E/$FFFFBD20 into the
 * VDP: the horizontal word goes to the VRAM slot two entries past the
 * base index kept in $FFFFB000, the vertical word to VSRAM $0002 (what
 * the $40020010 control long selects). a0 is left at the data port by
 * VDP_SetAddress, exactly as the rest of the chain expects.
 */
void VDP_CommitScrollPair(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t v;

  setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFB000u)));  /* move.w ($B000).w,d0 */
  lift_charge(x, 0x15EA4);
  setw(&c->d[0], alu_addw(c, 2, W(c->d[0])));     /* addq.w #2,d0 */
  lift_charge(x, 0x15EA8);

  lift_call(x, 0x15EAA, 4, VDP_SetAddress);       /* bsr.w VDP_SetAddress */
  if (x->declined) return;

  v = lift_r16(x, 0xFFBD1Eu);                     /* move.w ($BD1E).w,(a0) */
  alu_movew(c, v);
  lift_whw_data16(x, 0x15EAE, v);
  if (x->declined) return;

  alu_movel(c, 0x40020010);                       /* move.l #$40020010,4(a0) */
  lift_whw_ctrl32(x, 0x15EB2, 0x40020010);        /* ctrl port: VSRAM $0002 */
  if (x->declined) return;

  v = lift_r16(x, 0xFFBD20u);                     /* move.w ($BD20).w,(a0) */
  alu_movew(c, v);
  lift_whw_data16(x, 0x15EBA, v);
  if (x->declined) return;

  lift_charge(x, 0x15EBE);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Menu_DrawMatchupTeamStrips (sub_17AF4)
 * The matchup screen's two team banners. When the screen step ($D048) is
 * 0 or 4 the pending pair ($D04C/$D04E) is latched into the live pair
 * ($C330/$C332); then each team's $40-byte entry in the table at $F8BF4
 * supplies a palette long ($26(entry) for the visitor slot, $2(entry) for
 * the home slot) and Tilemap_DrawTwoRowStripAFE12 paints its strip.
 * Team_RefreshDataCache reloads the roster cache for the new pair.
 */
void Menu_DrawMatchupTeamStrips(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t, latch;

  c->a[7] -= 44;                                  /* movem.l d0-a2,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 3; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0x17AF4);

  alu_cmpw(c, 4, lift_r16(x, 0xFFD048u));         /* cmp.w #4,($D048).w */
  lift_charge(x, 0x17AF8);
  latch = c->zf;
  lift_charge_bcc(x, 0x17AFE, latch);             /* beq.w loc_17B0A */
  if (!latch)
  {
    alu_tstw(c, lift_r16(x, 0xFFD048u));          /* tst.w ($D048).w */
    lift_charge(x, 0x17B02);
    t = !c->zf;
    lift_charge_bcc(x, 0x17B06, t);               /* bne.w loc_17B16 */
    latch = !t;
  }
  if (latch)
  {
    /* loc_17B0A */
    lift_w16(x, 0xFFC330u,                        /* move.w ($D04C).w,($C330).w */
             alu_movew(c, lift_r16(x, 0xFFD04Cu)));
    lift_charge(x, 0x17B0A);
    lift_w16(x, 0xFFC332u,                        /* move.w ($D04E).w,($C332).w */
             alu_movew(c, lift_r16(x, 0xFFD04Eu)));
    lift_charge(x, 0x17B10);
  }

  /* loc_17B16 */
  lift_call(x, 0x17B16, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFC332u)));  /* move.w ($C332).w,d1 */
  lift_charge(x, 0x17B20);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));       /* move.w d1,d0 */
  lift_charge(x, 0x17B24);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 6));     /* asl.w #6,d0 */
  lift_charge(x, 0x17B26);
  c->a[0] = 0x000F8BF4;                           /* move.l #word_F8BF4,a0 */
  lift_charge(x, 0x17B28);
  lift_w32(x, 0xFFBD4Eu,                          /* move.l $26(a0,d0.w),($BD4E).w */
           alu_movel(c, lift_r32(x, c->a[0] + SEW(c->d[0]) + 0x26)));
  lift_charge(x, 0x17B2E);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFD43Au)));  /* move.w ($D43A).w,d4 */
  lift_charge(x, 0x17B34);
  lift_call(x, 0x17B38, 4, Tilemap_DrawTwoRowStripAFE12); /* bsr.w sub_17B78 */
  if (x->declined) return;

  lift_call(x, 0x17B3C, 4, Text_DrawInlineString);  /* bsr.w sub_11B92 + 6 inline */
  if (x->declined) return;

  setw(&c->d[1], alu_movew(c, lift_r16(x, 0xFFC330u)));  /* move.w ($C330).w,d1 */
  lift_charge(x, 0x17B46);
  setw(&c->d[0], alu_movew(c, W(c->d[1])));       /* move.w d1,d0 */
  lift_charge(x, 0x17B4A);
  setw(&c->d[0], alu_aslw(c, W(c->d[0]), 6));     /* asl.w #6,d0 */
  lift_charge(x, 0x17B4C);
  c->a[0] = 0x000F8BF4;                           /* move.l #word_F8BF4,a0 */
  lift_charge(x, 0x17B4E);
  lift_w32(x, 0xFFBD4Au,                          /* move.l 2(a0,d0.w),($BD4A).w */
           alu_movel(c, lift_r32(x, c->a[0] + SEW(c->d[0]) + 2)));
  lift_charge(x, 0x17B54);
  lift_w16(x, 0xFFBD52u, alu_movew(c, 0xEEE));    /* move.w #$EEE,($BD52).w */
  lift_charge(x, 0x17B5A);
  setw(&c->d[4], alu_movew(c, 2));                /* move.w #2,d4 */
  lift_charge(x, 0x17B60);
  lift_call(x, 0x17B64, 4, Tilemap_DrawTwoRowStripAFE12); /* bsr.w sub_17B78 */
  if (x->declined) return;
  lift_call(x, 0x17B68, 4, Team_RefreshDataCache);       /* bsr.w sub_17190 */
  if (x->declined) return;
  lift_w16(x, 0xFFBD26u, alu_movew(c, 0x64));     /* move.w #$64,($BD26).w */
  lift_charge(x, 0x17B6C);

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);  /* movem.l (sp)+,d0-a2 */
  for (i = 0; i < 3; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 44;
  lift_charge_movem(x, 0x17B72);
  lift_charge(x, 0x17B76);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Lineup_DrawRatingMeter (sub_12E66)
 * Draw one line's strength meter: Lineup_AverageLineRating's d0 scaled
 * down by $100 and clamped to $F picks how much of the 16-step bar at
 * $AB920 to paint (d1 = 15 - level). $B02C is forced to $8000 across the
 * draw and restored, so the meter ignores the caller's tile bias.
 */
void Lineup_DrawRatingMeter(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;
  uint32_t v;

  c->a[7] -= 36;                                  /* movem.l d0-d5/a0-a2,-(sp) */
  for (i = 0; i < 6; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 3; i++) lift_w32(x, c->a[7] + 24 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0x12E66);

  lift_call(x, 0x12E6A, 4, Lineup_AverageLineRating);  /* bsr.w sub_12EB4 */
  if (x->declined) return;

  v = lift_r16(x, 0xFFB02Cu);                     /* move.w ($B02C).w,-(sp) */
  alu_movew(c, v);
  c->a[7] -= 2;
  lift_w16(x, c->a[7], v);
  lift_charge(x, 0x12E6E);
  lift_w16(x, 0xFFB02Cu, alu_movew(c, 0x8000));   /* move.w #$8000,($B02C).w */
  lift_charge(x, 0x12E72);
  c->d[0] = alu_extl(c, c->d[0]);                 /* ext.l d0 */
  lift_charge(x, 0x12E78);
  lift_charge_divu(x, 0x12E7A, 0x100, c->d[0]);   /* divu.w #$100,d0 */
  if (x->declined) return;
  c->d[0] = alu_divu(c, 0x100, c->d[0]);
  alu_cmpw(c, 0xF, W(c->d[0]));                   /* cmp.w #$F,d0 */
  lift_charge(x, 0x12E7E);
  t = (c->cf || c->zf);                           /* bls.w loc_12E88 */
  lift_charge_bcc(x, 0x12E82, t);
  if (!t)
  {
    c->d[0] = alu_moveql(c, 0xF);                 /* moveq #$F,d0 */
    lift_charge(x, 0x12E86);
  }

  /* loc_12E88 */
  c->d[1] = alu_moveql(c, 0xF);                   /* moveq #$F,d1 */
  lift_charge(x, 0x12E88);
  setw(&c->d[1], alu_subw(c, W(c->d[0]), W(c->d[1])));  /* sub.w d0,d1 */
  lift_charge(x, 0x12E8A);
  setw(&c->d[0], alu_movew(c, 0));                /* clr.w d0 */
  lift_charge(x, 0x12E8C);
  c->a[1] = 0x000AB920;                           /* move.l #unk_AB920,a1 */
  lift_charge(x, 0x12E8E);
  c->a[1] += lift_r32(x, c->a[1] + 4);            /* add.l 4(a1),a1 — adda */
  lift_charge(x, 0x12E94);
  c->a[2] = 0x0000030A;                           /* move.w #$30A,a2 — movea.w */
  lift_charge(x, 0x12E98);
  setw(&c->d[2], alu_movew(c, lift_r16(x, c->a[1])));   /* move.w (a1),d2 */
  lift_charge(x, 0x12E9C);
  c->d[3] = alu_moveql(c, 1);                     /* moveq #1,d3 */
  lift_charge(x, 0x12E9E);
  setw(&c->d[4], alu_movew(c, lift_r16(x, 0xFFB016u)));  /* move.w ($B016).w,d4 */
  lift_charge(x, 0x12EA0);
  c->d[5] = alu_moveql(c, 0);                     /* moveq #0,d5 */
  lift_charge(x, 0x12EA4);
  lift_call(x, 0x12EA6, 4, Tilemap_DrawRegion);   /* bsr.w sub_1169A */
  if (x->declined) return;

  v = lift_r16(x, c->a[7]);                       /* move.w (sp)+,($B02C).w */
  c->a[7] += 2;
  lift_w16(x, 0xFFB02Cu, alu_movew(c, v));
  lift_charge(x, 0x12EAA);
  for (i = 0; i < 6; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);  /* movem.l (sp)+ */
  for (i = 0; i < 3; i++) c->a[i] = lift_r32(x, c->a[7] + 24 + 4 * i);
  c->a[7] += 36;
  lift_charge_movem(x, 0x12EAE);
  lift_charge(x, 0x12EB2);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Overlay_DrawLineChangeList (sub_B92E)
 * The line-change overlay: the period label, a frame, then three rows —
 * lines 'C', 'B', 'A' counted down in d4 — each showing sub_BA04's line
 * record, the name chunk from $191A6 and Lineup_DrawRatingMeter's bar.
 * The single-character label is built in place at $FFFFBFA4 as the
 * 4-byte chunk $00044120 with 'A' + d4 patched into byte 2. Tails into
 * Text_DrawString for the footer chunk the team block points at.
 */
void Overlay_DrawLineChangeList(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t, iter;

  lift_call(x, 0xB92E, 4, Overlay_DrawPeriodLabel);  /* bsr.w sub_B9C2 */
  if (x->declined) return;
  alu_cmpw(c, 0xF, lift_r16(x, T_CURSOR_ROW));    /* cmp.w #$F,($B02A).w */
  lift_charge(x, 0xB932);
  t = (c->nf != c->vf);                           /* blt.w loc_B946 */
  lift_charge_bcc(x, 0xB938, t);
  if (!t)
  {
    uint32_t b = lift_r8(x, 0xFFC2F0u);           /* bset #0,($C2F0).w */
    lift_w8(x, 0xFFC2F0u, alu_bset(c, b, 0));
    lift_charge(x, 0xB93C);
    lift_charge_bcc(x, 0xB942, 1);                /* bra.w loc_B950 */
  }
  else
  {
    /* loc_B946 */
    lift_call(x, 0xB946, 6, Text_ClearPeriodLabelBlock);  /* jsr sub_18A56 */
    if (x->declined) return;
    lift_call(x, 0xB94C, 4, Overlay_DrawPeriodLabel);     /* bsr.w sub_B9C2 */
    if (x->declined) return;
  }

  /* loc_B950 */
  lift_call(x, 0xB950, 4, Text_DrawFrame);        /* bsr.w sub_119B8 */
  if (x->declined) return;
  lift_w16(x, T_CURSOR_ROW,                       /* subq.w #2,($B02A).w */
           alu_subw(c, 2, lift_r16(x, T_CURSOR_ROW)));
  lift_charge(x, 0xB954);
  lift_w16(x, T_CURSOR_COL,                       /* addq.w #1,($B028).w */
           alu_addw(c, 1, lift_r16(x, T_CURSOR_COL)));
  lift_charge(x, 0xB958);
  c->d[4] = alu_moveql(c, 2);                     /* moveq #2,d4 */
  lift_charge(x, 0xB95C);

  for (iter = 0;; iter++)
  {
    int skip = 0;

    if (iter > 64) { x->declined = 1; return; }   /* dbf can only wrap if a
                                                   * callee clobbered d4 */
    /* loc_B95E */
    setw(&c->d[0], alu_movew(c, W(c->d[4])));     /* move.w d4,d0 */
    lift_charge(x, 0xB95E);
    lift_call(x, 0xB960, 4, sub_BA04);            /* bsr.w sub_BA04 */
    if (x->declined) return;
    alu_tstw(c, W(c->d[0]));                      /* tst.w d0 */
    lift_charge(x, 0xB964);
    t = c->nf;                                    /* bmi.w loc_B9AC */
    lift_charge_bcc(x, 0xB966, t);
    if (!t)
    {
      int direct;

      alu_btst(c, lift_r8(x, c->a[2] + 0x30), 1); /* btst #1,$30(a2) */
      lift_charge(x, 0xB96A);
      direct = !c->zf;
      lift_charge_bcc(x, 0xB970, direct);         /* bne.w loc_B980 */
      if (!direct)
      {
        alu_cmpw(c, lift_r16(x, c->a[2] + 0x2E), W(c->d[4]));  /* cmp.w $2E(a2),d4 */
        lift_charge(x, 0xB974);
        skip = !c->zf;
        lift_charge_bcc(x, 0xB978, skip);         /* bne.w loc_B9A8 */
        if (!skip)
        {
          setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[2] + 0x16)));  /* move.w $16(a2),d0 */
          lift_charge(x, 0xB97C);
        }
      }
      if (!skip)
      {
        /* loc_B980 */
        c->a[1] = 0xFFFFBFA4;                     /* move.w #$BFA4,a1 — movea.w */
        lift_charge(x, 0xB980);
        alu_movel(c, 0x00044120);                 /* move.l #unk_44120,(a1) */
        lift_w32(x, c->a[1], 0x00044120);
        lift_charge(x, 0xB984);
        lift_w8(x, c->a[1] + 2,                   /* add.b d4,2(a1) */
                alu_addb(c, c->d[4], lift_r8(x, c->a[1] + 2)));
        lift_charge(x, 0xB98A);
        lift_call(x, 0xB98E, 4, Text_DrawString); /* bsr.w sub_11BA4 */
        if (x->declined) return;
        alu_movew(c, W(c->d[0]));                 /* move.w d0,-(sp) */
        c->a[7] -= 2;
        lift_w16(x, c->a[7], W(c->d[0]));
        lift_charge(x, 0xB992);
        c->a[1] = 0x000191A6;                     /* move.l #word_191A6,a1 */
        lift_charge(x, 0xB994);
        lift_call(x, 0xB99A, 4, Text_SkipChunksThenDraw);  /* bsr.w sub_13508 */
        if (x->declined) return;
        {
          uint32_t v = lift_r16(x, c->a[7]);      /* move.w (sp)+,d0 */
          c->a[7] += 2;
          setw(&c->d[0], alu_movew(c, v));
          lift_charge(x, 0xB99E);
        }
        lift_call(x, 0xB9A0, 4, Lineup_DrawRatingMeter);   /* bsr.w sub_12E66 */
        if (x->declined) return;
        lift_w16(x, T_CURSOR_COL,                 /* subq.w #5,($B028).w */
                 alu_subw(c, 5, lift_r16(x, T_CURSOR_COL)));
        lift_charge(x, 0xB9A4);
      }
      /* loc_B9A8 */
      lift_w16(x, T_CURSOR_ROW,                   /* subq.w #1,($B02A).w */
               alu_subw(c, 1, lift_r16(x, T_CURSOR_ROW)));
      lift_charge(x, 0xB9A8);
    }
    /* loc_B9AC */
    setw(&c->d[4], W(c->d[4] - 1));               /* dbf d4,loc_B95E */
    {
      int taken = (W(c->d[4]) != 0xFFFF);
      lift_charge_dbcc(x, 0xB9AC, taken, !taken);
      if (!taken) break;
    }
  }

  c->a[1] = lift_r32(x, c->a[2] + 0x1E);          /* move.l $1E(a2),a1 — movea */
  lift_charge(x, 0xB9B0);
  c->a[1] += SEW(lift_r16(x, c->a[1] + 4));       /* add.w 4(a1),a1 — adda.w */
  lift_charge(x, 0xB9B4);
  c->a[1] += SEW(lift_r16(x, c->a[1]));           /* add.w (a1),a1 — adda.w */
  lift_charge(x, 0xB9B8);
  lift_w16(x, T_CURSOR_COL,                       /* addq.w #2,($B028).w */
           alu_addw(c, 2, lift_r16(x, T_CURSOR_COL)));
  lift_charge(x, 0xB9BA);
  lift_charge_bcc(x, 0xB9BE, 1);                  /* bra.w sub_11BA4 — tail */
  Text_DrawString(x);
}

/* ===========================================================================
 * Wave 58 — sub_FBD7A, the last row on the board.
 *
 * The 2026-08-05 handoff read this as two blind-spot-5 splits of ALREADY
 * LIFTED routines; the listing says otherwise. loc_FBEA4 sits AFTER
 * Records_DrawColumnHeader's rts ($FBDB0) with its header data in
 * between, and loc_FBFDE sits AFTER Records_ClearListArea's rts
 * ($FBFDC). Neither is inside a lifted body, so no split is needed:
 * they are two unnamed but function-shaped bsr targets (movem.l d0-a6
 * prologue, rts epilogue), lifted here as static helpers. The one real
 * blocker was their shared callee sub_FC136, which triage flags
 * `own:jmp-out` for its closing `jmp sub_11A48` — a tail call into a
 * lifted routine, the same shape Text_ClearListBody already ships.
 * =========================================================================== */

void Text_FormatFixedWidthDecimal(lift_ctx *);  /* game.c — sub_11D3A */
void Text_AppendString(lift_ctx *);             /* game.c — sub_11D9E */
void Buf_CopyBytes(lift_ctx *);                 /* game.c — sub_F997A */
void Text_AppendIndexedString(lift_ctx *);      /* game.c — sub_FA880 */
void Text_EmitTeamName(lift_ctx *);             /* game.c — sub_FA014 */

/*
 * Records_DrawRankPrefix (sub_FC136)
 * Emit one list row's "N. " rank prefix — d7 counts 6..0, so the digit is
 * '0' + (7 - d7) — then the team name for the record index the caller
 * just consumed from (a0). Tails into Text_DrawTableString for it.
 */
void Records_DrawRankPrefix(lift_ctx *x)
{
  rcpu_t *c = x->c;

  alu_movew(c, W(c->d[7]));                       /* move.w d7,-(sp) */
  c->a[7] -= 2;
  lift_w16(x, c->a[7], W(c->d[7]));
  lift_charge(x, 0xFC136);
  setw(&c->d[7], alu_negw(c, W(c->d[7])));        /* neg.w d7 */
  lift_charge(x, 0xFC138);
  setw(&c->d[7], alu_addw(c, 7, W(c->d[7])));     /* addq.w #7,d7 */
  lift_charge(x, 0xFC13A);
  setw(&c->d[7], alu_addw(c, 0x30, W(c->d[7])));  /* add.w #$30,d7 */
  lift_charge(x, 0xFC13C);
  c->a[1] = 0xFFFFBFA4;                           /* move.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFC140);
  lift_w16(x, c->a[1], alu_movew(c, 6));          /* move.w #6,(a1) */
  lift_charge(x, 0xFC146);
  lift_w8(x, c->a[1] + 2, alu_moveb(c, c->d[7])); /* move.b d7,2(a1) */
  lift_charge(x, 0xFC14A);
  lift_w8(x, c->a[1] + 3, alu_moveb(c, 0x2E));    /* move.b #$2E,3(a1) */
  lift_charge(x, 0xFC14E);
  lift_w8(x, c->a[1] + 4, alu_moveb(c, 0x20));    /* move.b #$20,4(a1) */
  lift_charge(x, 0xFC154);
  lift_w8(x, c->a[1] + 5, alu_moveb(c, 0));       /* move.b #0,5(a1) */
  lift_charge(x, 0xFC15A);
  lift_call(x, 0xFC160, 6, Text_DrawTableString); /* jsr sub_11A48 */
  if (x->declined) return;
  {
    uint32_t v = lift_r16(x, c->a[7]);            /* move.w (sp)+,d7 */
    c->a[7] += 2;
    setw(&c->d[7], alu_movew(c, v));
    lift_charge(x, 0xFC166);
  }
  setb(&c->d[2], alu_moveb(c, lift_r8(x, c->a[0] - 1)));  /* move.b -1(a0),d2 */
  lift_charge(x, 0xFC168);
  setw(&c->d[2], alu_extw(c, c->d[2]));           /* ext.w d2 */
  lift_charge(x, 0xFC16C);
  c->a[1] = 0xFFFFBFA4;                           /* move.l #$FFFFBFA4,a1 */
  lift_charge(x, 0xFC16E);
  {
    uint32_t b = lift_r8(x, 0xFFC2F8u);           /* bclr #7,($C2F8).w */
    lift_w8(x, 0xFFC2F8u, alu_bclr(c, b, 7));
    lift_charge(x, 0xFC174);
  }
  lift_call(x, 0xFC17A, 4, Text_EmitTeamName);    /* bsr.w sub_FA014 */
  if (x->declined) return;
  lift_charge(x, 0xFC17E);                        /* jmp sub_11A48 — tail */
  Text_DrawTableString(x);
}

/*
 * loc_FBEA4 — the win-record page of the Record Holders screen. Seven
 * rows, each: the index list at $FFFFD542 picks a team slot, $FFFFD54A
 * gives its win %, and the $10-byte block at $FFFFCF36 supplies the
 * wins ($A/$B), losses ($C/$D) and games ($8/$9) big-endian byte pairs —
 * ties fall out as -(games + losses - wins). A zero win count draws the
 * blank chunk at $FC100 instead.
 */
static void records_draw_win_page(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFBEA4);

  lift_call(x, 0xFBEA8, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A */
  if (x->declined) return;

  setw(&c->d[7], alu_movew(c, 6));                /* move.w #6,d7 */
  lift_charge(x, 0xFBEB8);
  c->a[0] = 0xFFFFD542;                           /* move.l #$FFFFD542,a0 */
  lift_charge(x, 0xFBEBC);
  c->a[2] = 0xFFFFD54A;                           /* move.l #$FFFFD54A,a2 */
  lift_charge(x, 0xFBEC2);
  c->a[5] = 0xFFFFCF36;                           /* move.l #$FFFFCF36,a5 */
  lift_charge(x, 0xFBEC8);

  for (;;)
  {
    /* loc_FBECE */
    lift_w16(x, T_CURSOR_COL, alu_movew(c, 2));   /* move.w #2,($B028).w */
    lift_charge(x, 0xFBECE);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0xFBED4);
    setw(&c->d[0], alu_extw(c, c->d[0]));         /* ext.w d0 */
    lift_charge(x, 0xFBED6);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]))));  /* move.b (a2,d0.w),d5 */
    lift_charge(x, 0xFBED8);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));   /* asl.w #4,d0 */
    lift_charge(x, 0xFBEDC);

    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 0xA)));  /* move.b $A(a5,d0.w),d4 */
    lift_charge(x, 0xFBEDE);
    setw(&c->d[4], alu_lslw(c, W(c->d[4]), 8));   /* lsl.w #8,d4 */
    lift_charge(x, 0xFBEE2);
    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 0xB)));  /* move.b $B(a5,d0.w),d4 */
    lift_charge(x, 0xFBEE4);
    alu_movew(c, W(c->d[4]));                     /* move.w d4,-(sp) */
    c->a[7] -= 2;
    lift_w16(x, c->a[7], W(c->d[4]));
    lift_charge(x, 0xFBEE8);

    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 0xC)));  /* move.b $C(a5,d0.w),d4 */
    lift_charge(x, 0xFBEEA);
    setw(&c->d[4], alu_lslw(c, W(c->d[4]), 8));   /* lsl.w #8,d4 */
    lift_charge(x, 0xFBEEE);
    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 0xD)));  /* move.b $D(a5,d0.w),d4 */
    lift_charge(x, 0xFBEF0);
    lift_w16(x, 0xFFBF4Au, alu_movew(c, W(c->d[4])));   /* move.w d4,($BF4A).w */
    lift_charge(x, 0xFBEF4);

    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 8)));    /* move.b 8(a5,d0.w),d4 */
    lift_charge(x, 0xFBEF8);
    setw(&c->d[4], alu_lslw(c, W(c->d[4]), 8));   /* lsl.w #8,d4 */
    lift_charge(x, 0xFBEFC);
    setb(&c->d[4], alu_moveb(c, lift_r8(x, c->a[5] + SEW(c->d[0]) + 9)));    /* move.b 9(a5,d0.w),d4 */
    lift_charge(x, 0xFBEFE);
    lift_w16(x, 0xFFBF4Eu, alu_movew(c, W(c->d[4])));   /* move.w d4,($BF4E).w */
    lift_charge(x, 0xFBF02);

    setw(&c->d[4], alu_addw(c, lift_r16(x, 0xFFBF4Au), W(c->d[4])));  /* add.w ($BF4A).w,d4 */
    lift_charge(x, 0xFBF06);
    setw(&c->d[4], alu_subw(c, lift_r16(x, c->a[7]), W(c->d[4])));    /* sub.w (sp),d4 */
    lift_charge(x, 0xFBF0A);
    setw(&c->d[4], alu_negw(c, W(c->d[4])));      /* neg.w d4 */
    lift_charge(x, 0xFBF0C);
    lift_w16(x, 0xFFBF4Cu, alu_movew(c, W(c->d[4])));   /* move.w d4,($BF4C).w */
    lift_charge(x, 0xFBF0E);
    {
      uint32_t v = lift_r16(x, c->a[7]);          /* move.w (sp)+,d4 */
      c->a[7] += 2;
      setw(&c->d[4], alu_movew(c, v));
      lift_charge(x, 0xFBF12);
    }
    alu_tstw(c, W(c->d[4]));                      /* tst.w d4 */
    lift_charge(x, 0xFBF14);
    t = c->zf;
    lift_charge_bcc(x, 0xFBF16, t);               /* beq.w loc_FBF86 */
    if (!t)
    {
      lift_call(x, 0xFBF1A, 4, Records_DrawRankPrefix);  /* bsr.w sub_FC136 */
      if (x->declined) return;
      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x13));     /* move.w #$13,($B028).w */
      lift_charge(x, 0xFBF1E);
      alu_movew(c, W(c->d[0]));                   /* move.w d0,-(sp) */
      c->a[7] -= 2;
      lift_w16(x, c->a[7], W(c->d[0]));
      lift_charge(x, 0xFBF24);
      setw(&c->d[0], alu_movew(c, W(c->d[5])));   /* move.w d5,d0 */
      lift_charge(x, 0xFBF26);
      setw(&c->d[1], alu_movew(c, 3));            /* move.w #3,d1 */
      lift_charge(x, 0xFBF28);
      lift_call(x, 0xFBF2C, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
      if (x->declined) return;
      lift_call(x, 0xFBF32, 6, Text_DrawTableString);          /* jsr sub_11A48 */
      if (x->declined) return;

      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x1A));           /* move.w #$1A,($B028).w */
      lift_charge(x, 0xFBF38);
      setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFBF4Eu)));    /* move.w ($BF4E).w,d0 */
      lift_charge(x, 0xFBF3E);
      setw(&c->d[1], alu_movew(c, 4));            /* move.w #4,d1 */
      lift_charge(x, 0xFBF42);
      lift_call(x, 0xFBF46, 6, Text_FormatFixedWidthDecimal);
      if (x->declined) return;
      lift_call(x, 0xFBF4C, 6, Text_DrawTableString);
      if (x->declined) return;

      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x1F));           /* move.w #$1F,($B028).w */
      lift_charge(x, 0xFBF52);
      setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFBF4Cu)));    /* move.w ($BF4C).w,d0 */
      lift_charge(x, 0xFBF58);
      setw(&c->d[1], alu_movew(c, 4));            /* move.w #4,d1 */
      lift_charge(x, 0xFBF5C);
      lift_call(x, 0xFBF60, 6, Text_FormatFixedWidthDecimal);
      if (x->declined) return;
      lift_call(x, 0xFBF66, 6, Text_DrawTableString);
      if (x->declined) return;

      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x24));           /* move.w #$24,($B028).w */
      lift_charge(x, 0xFBF6C);
      setw(&c->d[0], alu_movew(c, lift_r16(x, 0xFFBF4Au)));    /* move.w ($BF4A).w,d0 */
      lift_charge(x, 0xFBF72);
      setw(&c->d[1], alu_movew(c, 3));            /* move.w #3,d1 */
      lift_charge(x, 0xFBF76);
      lift_call(x, 0xFBF7A, 6, Text_FormatFixedWidthDecimal);
      if (x->declined) return;
      {
        uint32_t v = lift_r16(x, c->a[7]);        /* move.w (sp)+,d0 */
        c->a[7] += 2;
        setw(&c->d[0], alu_movew(c, v));
        lift_charge(x, 0xFBF80);
      }
      lift_charge_bcc(x, 0xFBF82, 1);             /* bra.w loc_FBF8C */
    }
    else
    {
      /* loc_FBF86 */
      c->a[1] = 0x000FC100;                       /* move.l #word_FC100,a1 */
      lift_charge(x, 0xFBF86);
    }

    /* loc_FBF8C */
    lift_call(x, 0xFBF8C, 6, Text_DrawTableString);   /* jsr sub_11A48 */
    if (x->declined) return;
    lift_w16(x, T_CURSOR_ROW,                     /* addq.w #2,($B02A).w */
             alu_addw(c, 2, lift_r16(x, T_CURSOR_ROW)));
    lift_charge(x, 0xFBF92);
    if (W(c->d[7]) == 0xFFFF) { x->declined = 1; return; }  /* dbf would wrap 64K */
    setw(&c->d[7], W(c->d[7] - 1));               /* dbf d7,loc_FBECE */
    {
      int taken = (W(c->d[7]) != 0xFFFF);
      lift_charge_dbcc(x, 0xFBF96, taken, !taken);
      if (!taken) break;
    }
  }

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);  /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFBF9A);
  lift_charge(x, 0xFBF9E);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * loc_FBFDE — the goals / saves leader page. $BF12 = 1 selects the goals
 * index list at $FFFFD532 (and byte offsets 0/1/2 of each $10-byte
 * $FFFFCF36 block), anything else the saves list at $FFFFD53A (offsets
 * 4/5/6). Each row prints the count, then "by <scorer>" and " vs. <goalie>"
 * assembled in the $FFFFBFA4 scratch chunk. A zero count draws the blank
 * chunk at $FC100.
 */
static void records_draw_leader_page(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int i, t;

  c->a[7] -= 60;                                  /* movem.l d0-a6,-(sp) */
  for (i = 0; i < 8; i++) lift_w32(x, c->a[7] + 4 * i, c->d[i]);
  for (i = 0; i < 7; i++) lift_w32(x, c->a[7] + 32 + 4 * i, c->a[i]);
  lift_charge_movem(x, 0xFBFDE);

  lift_call(x, 0xFBFE2, 6, Text_DrawInlineTableString);  /* jsr sub_11A36 + $A */
  if (x->declined) return;

  setw(&c->d[7], alu_movew(c, 6));                /* move.w #6,d7 */
  lift_charge(x, 0xFBFF2);
  c->a[0] = 0xFFFFD532;                           /* move.l #$FFFFD532,a0 */
  lift_charge(x, 0xFBFF6);
  alu_cmpw(c, 1, lift_r16(x, 0xFFBF12u));         /* cmp.w #1,($BF12).w */
  lift_charge(x, 0xFBFFC);
  t = c->zf;
  lift_charge_bcc(x, 0xFC002, t);                 /* beq.w loc_FC00C */
  if (!t)
  {
    c->a[0] = 0xFFFFD53A;                         /* move.l #$FFFFD53A,a0 */
    lift_charge(x, 0xFC006);
  }
  /* loc_FC00C */
  c->a[2] = 0xFFFFCF36;                           /* move.l #$FFFFCF36,a2 */
  lift_charge(x, 0xFC00C);

  for (;;)
  {
    /* loc_FC012 */
    lift_w16(x, T_CURSOR_COL, alu_movew(c, 2));   /* move.w #2,($B028).w */
    lift_charge(x, 0xFC012);
    setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[0])));   /* move.b (a0)+,d0 */
    c->a[0] += 1;
    lift_charge(x, 0xFC018);
    setw(&c->d[0], alu_extw(c, c->d[0]));         /* ext.w d0 */
    lift_charge(x, 0xFC01A);
    setw(&c->d[0], alu_aslw(c, W(c->d[0]), 4));   /* asl.w #4,d0 */
    lift_charge(x, 0xFC01C);
    setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]))));  /* move.b (a2,d0.w),d5 */
    lift_charge(x, 0xFC01E);
    alu_cmpw(c, 1, lift_r16(x, 0xFFBF12u));       /* cmp.w #1,($BF12).w */
    lift_charge(x, 0xFC022);
    t = c->zf;
    lift_charge_bcc(x, 0xFC028, t);               /* beq.w loc_FC030 */
    if (!t)
    {
      setb(&c->d[5], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 4)));  /* move.b 4(a2,d0.w),d5 */
      lift_charge(x, 0xFC02C);
    }

    /* loc_FC030 */
    alu_tstb(c, c->d[5]);                         /* tst.b d5 */
    lift_charge(x, 0xFC030);
    t = c->zf;
    lift_charge_bcc(x, 0xFC032, t);               /* beq.w loc_FC0E6 */
    if (!t)
    {
      int alt;

      lift_call(x, 0xFC036, 4, Records_DrawRankPrefix);  /* bsr.w sub_FC136 */
      if (x->declined) return;
      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x12));     /* move.w #$12,($B028).w */
      lift_charge(x, 0xFC03A);
      alu_movew(c, W(c->d[0]));                   /* move.w d0,-(sp) */
      c->a[7] -= 2;
      lift_w16(x, c->a[7], W(c->d[0]));
      lift_charge(x, 0xFC040);
      alu_cmpw(c, 1, lift_r16(x, 0xFFBF12u));     /* cmp.w #1,($BF12).w */
      lift_charge(x, 0xFC042);
      alt = !c->zf;
      lift_charge_bcc(x, 0xFC048, alt);           /* bne.w loc_FC054 */
      if (!alt)
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]))));    /* move.b (a2,d0.w),d0 */
        lift_charge(x, 0xFC04C);
        lift_charge_bcc(x, 0xFC050, 1);           /* bra.w loc_FC058 */
      }
      else
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 4)));  /* move.b 4(a2,d0.w),d0 */
        lift_charge(x, 0xFC054);
      }
      /* loc_FC058 */
      setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));   /* and.w #$FF,d0 */
      lift_charge(x, 0xFC058);
      setw(&c->d[1], alu_movew(c, 3));            /* move.w #3,d1 */
      lift_charge(x, 0xFC05C);
      lift_call(x, 0xFC060, 6, Text_FormatFixedWidthDecimal);  /* jsr sub_11D3A */
      if (x->declined) return;
      lift_call(x, 0xFC066, 6, Text_DrawTableString);          /* jsr sub_11A48 */
      if (x->declined) return;
      {
        uint32_t v = lift_r16(x, c->a[7]);        /* move.w (sp)+,d0 */
        c->a[7] += 2;
        setw(&c->d[0], alu_movew(c, v));
        lift_charge(x, 0xFC06C);
      }
      lift_w16(x, T_CURSOR_COL, alu_movew(c, 0x19));   /* move.w #$19,($B028).w */
      lift_charge(x, 0xFC06E);
      c->a[1] = 0x000FC128;                       /* move.l #word_FC128,a1 */
      lift_charge(x, 0xFC074);
      c->a[3] = 0xFFFFBFA4;                       /* move.l #$FFFFBFA4,a3 */
      lift_charge(x, 0xFC07A);
      lift_call(x, 0xFC080, 4, Buf_CopyBytes);    /* bsr.w sub_F997A */
      if (x->declined) return;
      alu_movew(c, W(c->d[0]));                   /* move.w d0,-(sp) */
      c->a[7] -= 2;
      lift_w16(x, c->a[7], W(c->d[0]));
      lift_charge(x, 0xFC084);
      alu_cmpw(c, 1, lift_r16(x, 0xFFBF12u));     /* cmp.w #1,($BF12).w */
      lift_charge(x, 0xFC086);
      alt = !c->zf;
      lift_charge_bcc(x, 0xFC08C, alt);           /* bne.w loc_FC098 */
      if (!alt)
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 1)));  /* move.b 1(a2,d0.w),d0 */
        lift_charge(x, 0xFC090);
        lift_charge_bcc(x, 0xFC094, 1);           /* bra.w loc_FC09C */
      }
      else
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 5)));  /* move.b 5(a2,d0.w),d0 */
        lift_charge(x, 0xFC098);
      }
      /* loc_FC09C */
      setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));   /* and.w #$FF,d0 */
      lift_charge(x, 0xFC09C);
      c->a[1] = 0xFFFFBFA4;                       /* move.l #$FFFFBFA4,a1 */
      lift_charge(x, 0xFC0A0);
      lift_call(x, 0xFC0A6, 4, Text_AppendIndexedString);  /* bsr.w sub_FA880 */
      if (x->declined) return;
      c->a[1] = 0x000FC12E;                       /* move.l #word_FC12E,a1 */
      lift_charge(x, 0xFC0AA);
      c->a[3] = 0xFFFFBFA4;                       /* move.l #$FFFFBFA4,a3 */
      lift_charge(x, 0xFC0B0);
      lift_call(x, 0xFC0B6, 6, Text_AppendString);     /* jsr sub_11D9E */
      if (x->declined) return;
      c->a[1] = 0xFFFFBFA4;                       /* move.l #$FFFFBFA4,a1 */
      lift_charge(x, 0xFC0BC);
      {
        uint32_t v = lift_r16(x, c->a[7]);        /* move.w (sp)+,d0 */
        c->a[7] += 2;
        setw(&c->d[0], alu_movew(c, v));
        lift_charge(x, 0xFC0C2);
      }
      alu_cmpw(c, 1, lift_r16(x, 0xFFBF12u));     /* cmp.w #1,($BF12).w */
      lift_charge(x, 0xFC0C4);
      alt = !c->zf;
      lift_charge_bcc(x, 0xFC0CA, alt);           /* bne.w loc_FC0D6 */
      if (!alt)
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 2)));  /* move.b 2(a2,d0.w),d0 */
        lift_charge(x, 0xFC0CE);
        lift_charge_bcc(x, 0xFC0D2, 1);           /* bra.w loc_FC0DA */
      }
      else
      {
        setb(&c->d[0], alu_moveb(c, lift_r8(x, c->a[2] + SEW(c->d[0]) + 6)));  /* move.b 6(a2,d0.w),d0 */
        lift_charge(x, 0xFC0D6);
      }
      /* loc_FC0DA */
      setw(&c->d[0], alu_andw(c, 0xFF, W(c->d[0])));   /* and.w #$FF,d0 */
      lift_charge(x, 0xFC0DA);
      lift_call(x, 0xFC0DE, 4, Text_AppendIndexedString);  /* bsr.w sub_FA880 */
      if (x->declined) return;
      lift_charge_bcc(x, 0xFC0E2, 1);             /* bra.w loc_FC0EC */
    }
    else
    {
      /* loc_FC0E6 */
      c->a[1] = 0x000FC100;                       /* move.l #word_FC100,a1 */
      lift_charge(x, 0xFC0E6);
    }

    /* loc_FC0EC */
    lift_call(x, 0xFC0EC, 6, Text_DrawTableString);  /* jsr sub_11A48 */
    if (x->declined) return;
    lift_w16(x, T_CURSOR_ROW,                     /* addq.w #2,($B02A).w */
             alu_addw(c, 2, lift_r16(x, T_CURSOR_ROW)));
    lift_charge(x, 0xFC0F2);
    if (W(c->d[7]) == 0xFFFF) { x->declined = 1; return; }  /* dbf would wrap 64K */
    setw(&c->d[7], W(c->d[7] - 1));               /* dbf d7,loc_FC012 */
    {
      int taken = (W(c->d[7]) != 0xFFFF);
      lift_charge_dbcc(x, 0xFC0F6, taken, !taken);
      if (!taken) break;
    }
  }

  for (i = 0; i < 8; i++) c->d[i] = lift_r32(x, c->a[7] + 4 * i);  /* movem.l (sp)+,d0-a6 */
  for (i = 0; i < 7; i++) c->a[i] = lift_r32(x, c->a[7] + 32 + 4 * i);
  c->a[7] += 60;
  lift_charge_movem(x, 0xFC0FA);
  lift_charge(x, 0xFC0FE);                        /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Records_DrawListPage (sub_FBD7A)
 * Three instructions that pick the Record Holders page body for the
 * current page number ($BF12): 0 draws the win-record page, anything
 * else the goals/saves leader page. Both are tail branches, so the
 * helper's own rts returns to this routine's caller.
 */
void Records_DrawListPage(lift_ctx *x)
{
  rcpu_t *c = x->c;
  int t;

  alu_tstw(c, lift_r16(x, 0xFFBF12u));            /* tst.w ($BF12).w */
  lift_charge(x, 0xFBD7A);
  t = c->zf;
  lift_charge_bcc(x, 0xFBD7E, t);                 /* beq.w loc_FBEA4 */
  if (t)
  {
    records_draw_win_page(x);
    return;
  }
  lift_charge_bcc(x, 0xFBD82, 1);                 /* bra.w loc_FBFDE */
  records_draw_leader_page(x);
}
