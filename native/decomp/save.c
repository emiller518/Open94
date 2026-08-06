/*
 * save.c — the save-game subsystem, lifted from Source.asm $1A14E..$1A25C.
 *
 * How NHL '94 saves: a $2000-byte buffer in work RAM ($FF0000) is the working
 * copy of the save. Cartridge SRAM is byte-wide on a 16-bit bus, so only the
 * odd byte of each SRAM word exists — the copy routines therefore stride two
 * SRAM addresses per useful byte. The last two buffer bytes are an integrity
 * pair: buf[$1FFF] = sum of the first $1FFE bytes, buf[$1FFE] = ~sum.
 *
 * Each routine below replaces one original 68k routine. It must be exact, not
 * merely equivalent: results, scratch-register end states, condition flags,
 * memory writes and cycle timing are all compared against the original on
 * every call in --lift-verify mode.
 */
#include "../harness/lift.h"
#include "util68k.h"

#define SAVE_BUF      0xFFFF0000u   /* work-RAM save buffer (68k $FF0000) */
#define SAVE_BUF_LEN  0x1FFE        /* bytes covered by the checksum */
#define SAVE_SIZE     0x2000        /* full buffer, incl. the check pair */
#define SRAM_BASE     0x200000u     /* cart SRAM, odd bytes only */
#define R_SAVE_BAD    0xFFD458u     /* nonzero = save failed verification */

/* SRAM byte n lives at SRAM_BASE + 2n + 1 */
#define SRAM_ADDR(n)  (SRAM_BASE + ((n) * 2) + 1)

static void rts(lift_ctx *x)
{
  rcpu_t *c = x->c;
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Both copy loops end with `addq.w #2,d0` — neither dbf nor the movem
 * restore touches the condition codes, so that addition's flags are what
 * the caller sees on return. d0's word runs 2*index + 2*iteration.
 */
static void copy_loop_exit_flags(rcpu_t *c, unsigned int index, unsigned int count)
{
  unsigned int d = (2 * index + 2 * (count - 1)) & 0xFFFF;
  unsigned int r = (d + 2) & 0xFFFF;
  c->cf = ((d + 2) >> 16) & 1;
  c->xf = c->cf;
  c->vf = ((~(2u ^ d) & (2u ^ r)) >> 15) & 1;
  c->nf = (r >> 15) & 1;
  c->zf = (r == 0);
}

/*
 * SRAM_ReadBytes ($1A244) — copy `count` bytes out of SRAM into RAM.
 *   d0 = first SRAM byte index, d1 = count, a0 = destination
 * Preserves d0-d2/a0-a1 (movem save/restore) and leaves flags untouched
 * apart from the loop's dbf.
 */
void SRAM_ReadBytes(lift_ctx *x)
{
  rcpu_t *c = x->c;
  unsigned int index = c->d[0], count = c->d[1], dst = c->a[0];
  unsigned int i;

  if (count == 0) { x->declined = 1; return; }   /* dbf would wrap 65536x */

  /* movem.l d0-d2/a0-a1,-(sp): the push/pop nets out, but the 20 transient
   * bytes below sp are really written — staged so live mode leaves the
   * same stale stack bytes as the original */
  lift_w32(x, c->a[7] - 20, c->d[0]);
  lift_w32(x, c->a[7] - 16, c->d[1]);
  lift_w32(x, c->a[7] - 12, c->d[2]);
  lift_w32(x, c->a[7] - 8,  c->a[0]);
  lift_w32(x, c->a[7] - 4,  c->a[1]);

  for (i = 0; i < count; i++)
    lift_w8(x, dst + i, lift_r8(x, SRAM_ADDR(index + i)));

  /* ---- cycles + ABI (original instruction stream) ---- */
  lift_charge_movem(x, 0x1A244);              /* movem.l d0-d2/a0-a1,-(sp) */
  lift_charge(x, 0x1A248);                    /* move.l #$200000,a1 */
  lift_charge(x, 0x1A24E);                    /* add.l d0,d0 */
  lift_charge(x, 0x1A250);                    /* subq.l #1,d1 */
  for (i = 0; i < count; i++)
  {
    lift_charge(x, 0x1A252);                  /* move.b 1(a1,d0.w),d2 */
    lift_charge(x, 0x1A256);                  /* move.b d2,(a0)+ */
    lift_charge(x, 0x1A258);                  /* addq.w #2,d0 */
    lift_charge_dbcc(x, 0x1A25A, i != count - 1, i == count - 1);
  }
  lift_charge_movem(x, 0x1A25E);              /* movem.l (sp)+,d0-d2/a0-a1 */
  lift_charge(x, 0x1A262);                    /* rts */
  /* movem restores d0-d2/a0-a1, so every register is unchanged on return */
  copy_loop_exit_flags(c, index, count);
  rts(x);
}

/*
 * SRAM_WriteBytes ($1A1E4) — copy `count` bytes from RAM into SRAM.
 *   d0 = first SRAM byte index, d1 = count, a0 = source
 * Writes a full word to each SRAM slot (only the odd byte is stored).
 */
void SRAM_WriteBytes(lift_ctx *x)
{
  rcpu_t *c = x->c;
  unsigned int index = c->d[0], count = c->d[1], src = c->a[0];
  unsigned int i;

  if (count == 0) { x->declined = 1; return; }

  /* movem.l d0-d2/a0-a1,-(sp): stage the 20 transient stack bytes (see
   * SRAM_ReadBytes) */
  lift_w32(x, c->a[7] - 20, c->d[0]);
  lift_w32(x, c->a[7] - 16, c->d[1]);
  lift_w32(x, c->a[7] - 12, c->d[2]);
  lift_w32(x, c->a[7] - 8,  c->a[0]);
  lift_w32(x, c->a[7] - 4,  c->a[1]);

  for (i = 0; i < count; i++)
    lift_w8(x, SRAM_ADDR(index + i), lift_r8(x, src + i));

  lift_charge_movem(x, 0x1A1E4);              /* movem.l d0-d2/a0-a1,-(sp) */
  lift_charge(x, 0x1A1E8);                    /* move.l #$200000,a1 */
  lift_charge(x, 0x1A1EE);                    /* add.l d0,d0 */
  lift_charge(x, 0x1A1F0);                    /* subq.l #1,d1 */
  lift_charge(x, 0x1A1F2);                    /* clr.w d2 */
  for (i = 0; i < count; i++)
  {
    lift_charge(x, 0x1A1F4);                  /* move.b (a0)+,d2 */
    lift_charge(x, 0x1A1F6);                  /* move.w d2,(a1,d0.w) */
    lift_charge(x, 0x1A1FA);                  /* addq.w #2,d0 */
    lift_charge_dbcc(x, 0x1A1FC, i != count - 1, i == count - 1);
  }
  lift_charge_movem(x, 0x1A200);              /* movem.l (sp)+,d0-d2/a0-a1 */
  lift_charge(x, 0x1A204);                    /* rts */
  copy_loop_exit_flags(c, index, count);
  rts(x);
}

/*
 * SaveBuf_VerifyChecksum ($1A14E) — validate the buffer's integrity pair.
 * Sets R_SAVE_BAD: word 0 when all three relationships hold, byte $FF if any
 * fails (the original's write widths differ per path; preserved).
 */
void SaveBuf_VerifyChecksum(lift_ctx *x)
{
  rcpu_t *c = x->c;
  unsigned int i, errors = 0, sum = 0;

  for (i = 0; i < SAVE_BUF_LEN; i++)
    sum = (sum + lift_r8(x, SAVE_BUF + i)) & 0xFF;

  unsigned int chk_not = lift_r8(x, SAVE_BUF + 0x1FFE);   /* expect ~sum */
  unsigned int chk_sum = lift_r8(x, SAVE_BUF + 0x1FFF);   /* expect  sum */

  if (chk_sum != sum) errors++;
  if (chk_not != ((~sum) & 0xFF)) errors++;
  if (chk_sum != ((~chk_not) & 0xFF)) errors++;

  if (errors == 0) lift_w16(x, R_SAVE_BAD, 0);
  else             lift_w8(x, R_SAVE_BAD, 0xFF);

  /* ---- ABI: scratch state the original leaves behind ---- */
  c->d[0] = ((((c->d[0] >> 16) & 0xFF00) | ((~chk_not) & 0xFF)) << 16)
            | 0xFF00 | ((~sum) & 0xFF);
  c->d[1] = (c->d[1] & 0xFFFF0000) | (errors & 0xFFFF);
  c->a[0] = 0xFFFF1FFE;
  c->nf = 0; c->vf = 0; c->cf = 0;
  c->zf = (errors == 0);

  /* ---- cycles ---- */
  lift_charge(x, 0x1A14E);
  lift_charge(x, 0x1A152);
  lift_charge(x, 0x1A154);
  for (i = 0; i < SAVE_BUF_LEN; i++)
  {
    lift_charge(x, 0x1A15A);
    lift_charge_dbcc(x, 0x1A15C, i != SAVE_BUF_LEN - 1, i == SAVE_BUF_LEN - 1);
  }
  lift_charge(x, 0x1A160);
  lift_charge(x, 0x1A162);
  lift_charge_bcc(x, 0x1A166, chk_sum == sum);
  if (chk_sum != sum) lift_charge(x, 0x1A16A);
  lift_charge(x, 0x1A16C);
  lift_charge(x, 0x1A16E);
  lift_charge_bcc(x, 0x1A170, chk_not == ((~sum) & 0xFF));
  if (chk_not != ((~sum) & 0xFF)) lift_charge(x, 0x1A174);
  lift_charge(x, 0x1A176);
  lift_charge(x, 0x1A178);
  lift_charge(x, 0x1A17A);
  lift_charge(x, 0x1A17C);
  lift_charge_bcc(x, 0x1A180, chk_sum == ((~chk_not) & 0xFF));
  if (chk_sum != ((~chk_not) & 0xFF)) lift_charge(x, 0x1A184);
  lift_charge(x, 0x1A186);
  lift_charge(x, 0x1A188);
  lift_charge_bcc(x, 0x1A18A, errors != 0);
  if (errors == 0) { lift_charge(x, 0x1A18E); lift_charge(x, 0x1A192); }
  else lift_charge(x, 0x1A196);
  lift_charge(x, 0x1A19A);
  rts(x);
}

/*
 * SRAM_RecalcChecksum ($1A206) — re-read the save from SRAM, recompute the
 * integrity pair over the fresh copy, and write just those two bytes back.
 */
void SRAM_RecalcChecksum(lift_ctx *x)
{
  rcpu_t *c = x->c;
  unsigned int i, sum = 0;

  /* pull the whole save back into the buffer */
  c->a[0] = SAVE_BUF;
  c->d[1] = SAVE_SIZE;
  c->d[0] = 0;
  lift_charge(x, 0x1A206);                    /* lea SAVE_BUF,a0 */
  lift_charge(x, 0x1A20C);                    /* move.l #$2000,d1 */
  lift_charge(x, 0x1A212);                    /* clr.l d0 */
  lift_call(x, 0x1A214, 4, SRAM_ReadBytes);

  for (i = 0; i < SAVE_BUF_LEN; i++)
    sum = (sum + lift_r8(x, SAVE_BUF + i)) & 0xFF;

  lift_w8(x, SAVE_BUF + 0x1FFF, sum);                 /* sum  */
  lift_w8(x, SAVE_BUF + 0x1FFE, (~sum) & 0xFF);       /* ~sum */

  lift_charge(x, 0x1A218);                    /* lea SAVE_BUF,a0 */
  lift_charge(x, 0x1A21E);                    /* clr.w d0 */
  lift_charge(x, 0x1A220);                    /* move.w #$1FFD,d1 */
  for (i = 0; i < SAVE_BUF_LEN; i++)
  {
    lift_charge(x, 0x1A224);                  /* add.b (a0)+,d0 */
    lift_charge_dbcc(x, 0x1A226, i != SAVE_BUF_LEN - 1, i == SAVE_BUF_LEN - 1);
  }
  lift_charge(x, 0x1A22A);                    /* move.b d0,1(a0) */
  lift_charge(x, 0x1A22E);                    /* not.w d0 */
  lift_charge(x, 0x1A230);                    /* move.b d0,(a0) */
  lift_charge(x, 0x1A232);                    /* move.l #$FFFF1FFE,a0 */
  lift_charge(x, 0x1A238);                    /* moveq #2,d1 */
  lift_charge(x, 0x1A23A);                    /* move.l #$1FFE,d0 */

  /* write the pair back to SRAM; the callee restores d0-d2/a0-a1 and
   * leaves the condition codes the caller returns with */
  c->d[0] = 0x1FFE;
  c->d[1] = 2;
  c->a[0] = 0xFFFF1FFE;
  lift_call(x, 0x1A240, 2, SRAM_WriteBytes);  /* bsr.s */

  lift_charge(x, 0x1A242);                    /* rts */
  rts(x);
}

/*
 * SRAM_FormatSave ($1A19C) — create a fresh save: zero the buffer, mark
 * byte $1FFE as $FF, push the cleared buffer to SRAM, then write a 1 to
 * byte 1 of SRAM (the "formatted" signature) and rebuild the checksum.
 */
void SRAM_FormatSave(lift_ctx *x)
{
  rcpu_t *c = x->c;
  unsigned int i;

  for (i = 0; i < SAVE_SIZE; i++)
    lift_w8(x, SAVE_BUF + i, 0);
  lift_w8(x, SAVE_BUF + 0x1FFE, 0xFF);

  lift_charge(x, 0x1A19C);                    /* lea SAVE_BUF,a0 */
  lift_charge(x, 0x1A1A2);                    /* move.w #$1FFF,d0 */
  lift_charge(x, 0x1A1A6);                    /* clr.l d1 */
  for (i = 0; i < SAVE_SIZE; i++)
  {
    lift_charge(x, 0x1A1A8);                  /* move.b d1,(a0)+ */
    lift_charge_dbcc(x, 0x1A1AA, i != SAVE_SIZE - 1, i == SAVE_SIZE - 1);
  }
  lift_charge(x, 0x1A1AE);                    /* move.b #$FF,$FFFF1FFE */
  lift_charge(x, 0x1A1B6);                    /* moveq #0,d0 */
  lift_charge(x, 0x1A1B8);                    /* move.l #$2000,d1 */
  lift_charge(x, 0x1A1BE);                    /* move.l #$FFFF0000,a0 */

  c->d[0] = 0;
  c->d[1] = SAVE_SIZE;
  c->a[0] = SAVE_BUF;
  lift_call(x, 0x1A1C4, 4, SRAM_WriteBytes);  /* whole buffer -> SRAM */

  lift_w8(x, SAVE_BUF, 1);                    /* signature byte */
  lift_charge(x, 0x1A1C8);                    /* moveq #1,d0 */
  lift_charge(x, 0x1A1CA);                    /* moveq #1,d1 */
  lift_charge(x, 0x1A1CC);                    /* move.b #1,$FFFF0000 */
  lift_charge(x, 0x1A1D4);                    /* move.l #$FFFF0000,a0 */

  c->d[0] = 1;
  c->d[1] = 1;
  c->a[0] = SAVE_BUF;
  lift_call(x, 0x1A1DA, 4, SRAM_WriteBytes);  /* one byte at index 1 */

  lift_call(x, 0x1A1DE, 4, SRAM_RecalcChecksum);

  lift_charge(x, 0x1A1E2);                    /* rts */
  rts(x);
}

/*
 * Buf_CopyBytes (sub_F997A; called from sub_F98C6/sub_F9A64)
 *   in:  a1 = source (its leading word is the byte count, read but NOT
 *        advanced past — the count word itself is copied as data too),
 *        a3 = destination
 *   Copies count bytes from (a1)+ to (a3)+. d0 saved/restored.
 */
void Buf_CopyBytes(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0];

  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);         /* movem.l d0,-(sp) */
  lift_charge_movem(x, 0xF997A);

  setw(&c->d[0], alu_movew(c, lift_r16(x, c->a[1])));    /* move.w (a1),d0 */
  lift_charge(x, 0xF997E);
  setw(&c->d[0], alu_subw(c, 1, W(c->d[0])));             /* subq.w #1,d0 */
  lift_charge(x, 0xF9980);

  for (;;)
  {
    lift_w8(x, c->a[3], alu_moveb(c, lift_r8(x, c->a[1])));  /* move.b (a1)+,(a3)+ */
    c->a[1] += 1;
    c->a[3] += 1;
    lift_charge(x, 0xF9982);
    uint32_t nd0 = W(W(c->d[0]) - 1);                        /* dbf d0,loc_F9982 */
    setw(&c->d[0], nd0);
    int taken = (nd0 != 0xFFFF);
    lift_charge_dbcc(x, 0xF9984, taken, !taken);
    if (!taken) break;
  }

  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;            /* movem.l (sp)+,d0 */
  lift_charge_movem(x, 0xF9988);

  lift_charge(x, 0xF998C);                                  /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * Sram_LoadSnapshotBuffer (sub_FE660; called from ROM:7718/sub_F739E)
 * Reads $100 bytes from SRAM (starting at index $1EF6) into RAM at
 * $FFFFD076 via SRAM_ReadBytes, clears R_UNK_C2EC bit4, resets
 * $FFFFC016 to $FFFF, and resets the snapshot ring-buffer cursor
 * (Game_RecordSnapshot's R_SNAP_CURSOR) to $FFFF0000. d0/d1/a0
 * saved/restored.
 */
void Sram_LoadSnapshotBuffer(lift_ctx *x)
{
  rcpu_t *c = x->c;
  uint32_t saved_d0 = c->d[0], saved_d1 = c->d[1], saved_a0 = c->a[0];

  /* movem.l d0-d1/a0,-(sp): a0 pushed first (high addr), d1, d0 lands lowest */
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_a0);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d1);
  c->a[7] -= 4; lift_w32(x, c->a[7], saved_d0);
  lift_charge_movem(x, 0xFE660);

  c->d[1] = alu_movel(c, 0x100);                          /* move.l #$100,d1 */
  lift_charge(x, 0xFE664);
  c->d[0] = alu_movel(c, 0x1EF6);                          /* move.l #$1EF6,d0 */
  lift_charge(x, 0xFE66A);
  c->a[0] = 0xFFFFD076u;                                    /* movea.l #$FFFFD076,a0 */
  lift_charge(x, 0xFE670);
  lift_call(x, 0xFE676, 6, SRAM_ReadBytes);                 /* jsr SRAM_ReadBytes */
  if (x->declined) return;

  lift_w8(x, 0xFFFFC2ECu, alu_bclr(c, lift_r8(x, 0xFFFFC2ECu), 4));  /* bclr #4,(abs) */
  lift_charge(x, 0xFE67C);
  lift_w16(x, 0xFFFFC016u, alu_movew(c, 0xFFFF));           /* move.w #$FFFF,(abs) */
  lift_charge(x, 0xFE682);
  lift_w32(x, 0xFFFFB036u, alu_movel(c, 0xFFFF0000u));      /* move.l #$FFFF0000,(abs) */
  lift_charge(x, 0xFE688);

  /* movem.l (sp)+,d0-d1/a0 */
  c->d[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->d[1] = lift_r32(x, c->a[7]); c->a[7] += 4;
  c->a[0] = lift_r32(x, c->a[7]); c->a[7] += 4;
  lift_charge_movem(x, 0xFE690);

  lift_charge(x, 0xFE694);                                   /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}

/*
 * sub_FE6D2 — "clear ALL win records" (Record Holders screen, A+C prompt;
 * called from ROM:$FBD28).
 *
 * The season/all-time record table is eight 16-byte entries at $FFFFCF36.
 * Bytes 8..$B of each entry hold the record itself; the loop zeroes just
 * those four bytes per entry, leaving the name field ahead of them
 * untouched, then flushes the whole $80-byte table to SRAM at offset
 * $D20 and recomputes the save checksum.
 *
 * All of d0-a6 are restored by the movem, so the routine is invisible to
 * its caller apart from the RAM/SRAM writes.
 */
void sub_FE6D2(lift_ctx *x)
{
  rcpu_t *c = x->c;

  /* movem.l d0-a6,-(sp) — pushed a6..a0 then d7..d0 */
  uint32_t saved[15];
  for (int i = 0; i < 7; i++) saved[i] = c->a[6 - i];
  for (int i = 0; i < 8; i++) saved[7 + i] = c->d[7 - i];
  for (int i = 0; i < 15; i++) { c->a[7] -= 4; lift_w32(x, c->a[7], saved[i]); }
  lift_charge_movem(x, 0xFE6D2);

  c->a[0] = 0xFFFFCF36u;                            /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFE6D6);
  setw(&c->d[7], alu_movew(c, 7));                  /* move.w #7,d7 */
  lift_charge(x, 0xFE6DC);

  for (;;)
  {
    /* loc_FE6E0 */
    lift_w8(x, c->a[0] + 8, alu_moveb(c, 0));       /* clr.b 8(a0) */
    lift_charge(x, 0xFE6E0);
    lift_w8(x, c->a[0] + 9, alu_moveb(c, 0));       /* clr.b 9(a0) */
    lift_charge(x, 0xFE6E4);
    lift_w8(x, c->a[0] + 0xA, alu_moveb(c, 0));     /* clr.b $A(a0) */
    lift_charge(x, 0xFE6E8);
    lift_w8(x, c->a[0] + 0xB, alu_moveb(c, 0));     /* clr.b $B(a0) */
    lift_charge(x, 0xFE6EC);
    c->a[0] += 0x10;                                /* adda.w #$10,a0 */
    lift_charge(x, 0xFE6F0);

    {
      uint32_t nd7 = W(W(c->d[7]) - 1);             /* dbf d7,loc_FE6E0 */
      setw(&c->d[7], nd7);
      int taken = (nd7 != 0xFFFF);
      lift_charge_dbcc(x, 0xFE6F4, taken, !taken);
      if (!taken) break;
    }
  }

  c->d[0] = alu_movel(c, 0xD20);                    /* move.l #$D20,d0 */
  lift_charge(x, 0xFE6F8);
  c->d[1] = alu_movel(c, 0x80);                     /* move.l #$80,d1 */
  lift_charge(x, 0xFE6FE);
  c->a[0] = 0xFFFFCF36u;                            /* movea.l #$FFFFCF36,a0 */
  lift_charge(x, 0xFE704);

  lift_call(x, 0xFE70A, 6, SRAM_WriteBytes);        /* jsr SRAM_WriteBytes */
  if (x->declined) return;
  lift_call(x, 0xFE710, 6, SRAM_RecalcChecksum);    /* jsr SRAM_RecalcChecksum */
  if (x->declined) return;

  /* movem.l (sp)+,d0-a6 */
  for (int i = 0; i < 8; i++) { c->d[i] = lift_r32(x, c->a[7]); c->a[7] += 4; }
  for (int i = 0; i < 7; i++) { c->a[i] = lift_r32(x, c->a[7]); c->a[7] += 4; }
  lift_charge_movem(x, 0xFE716);

  lift_charge(x, 0xFE71A);                          /* rts */
  c->pc = lift_r32(x, c->a[7]) & 0xFFFFFF;
  c->a[7] += 4;
}
