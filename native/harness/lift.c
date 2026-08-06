#include "shared.h"
#include "lift.h"
#include "gen_insns.h"
#include "m68k.h"

extern m68ki_cpu_core m68k;
extern unsigned int rc_cycles_for_opcode(unsigned int op);

static int mode = 0;
static long live_max = -1;    /* RC_LIFT_MAX: cap on live commits (debug bisect) */

/* shadow-verify arm stack: nested lifted calls verified LIFO.
 * 8 deep = ~7.7MB static (each slot carries wl[65536]*12B + two
 * hw logs of 4096*24B); raised from 4 on 2026-08-04 — the text
 * chains nest 5+ and were skipped as arm-overflow, not verified */
#define ARM_MAX 8
#define ISR_ENTRY 0x0076B2   /* VBlank handler (same detection as validate.c) */
#define ISR_HBLANK 0x015E6C  /* HBlank handler (level-4 autovector, ROM $70) */

static struct {
  unsigned int entry, ret_pc;
  rcpu_t pred;
  long long cycles_pred, refresh_pred;
  const char *name;
  int interrupted;           /* ISR fired mid-call: prediction unusable */
  unsigned long isr_at_arm;  /* DEBUG: global ISR count when armed */
  lift_write wl[LIFT_WLOG_MAX];
  int nw;
  unsigned int e_a3, e_a7;             /* entry-time a3/a7 (RC_LIFT_DEBUG dumps) */
  lift_hw_write hwl[LIFT_HWLOG_MAX];   /* staged VDP port writes */
  int nhw;
  lift_hw_write obs[LIFT_HWLOG_MAX];   /* interpreter's observed port writes */
  int nobs, obs_over;
  int hw_xslice;             /* sequence-tier arm: hw staged past the slice */
  int ridx;                  /* index into lift_routines[] */
} arm[ARM_MAX];

/* per-routine verified-call counts (RC_LIFT_STATS=1 prints them at report
 * time) — the instrument the [unverified]-promotion sweep reads: a row
 * promotes only on nonzero verified calls, and the split shows whether
 * they were exact-tier or sequence-tier (hw-xslice) */
#define LIFT_MAX_ROUTINES 1024
static unsigned long per_verified[LIFT_MAX_ROUTINES];
static unsigned long per_xslice[LIFT_MAX_ROUTINES];
/* the decline instrument (2026-08-04): a profiled entry that never
 * yields a checked call is either declined, skipped for arm depth, or
 * had its arm invalidated by an ISR — these split the three apart,
 * where per_verified alone could not */
static unsigned long per_declined[LIFT_MAX_ROUTINES];
static unsigned long per_overflow[LIFT_MAX_ROUTINES];
static unsigned long per_intskip[LIFT_MAX_ROUTINES];
static int n_arm = 0;
static unsigned long isr_seen = 0;  /* DEBUG */

static lift_write live_wl[LIFT_WLOG_MAX];
static lift_hw_write live_hwl[LIFT_HWLOG_MAX];

static struct {
  unsigned long verified, divergences, live_runs, arm_overflow,
                declined, cyc_unchecked, slice_spill, int_skip,
                hw_shipped, hw_declined, hw_replay_stall,
                hw_xslice, hw_stall_model;
} st;

#define MAX_DIV 10
static int reported = 0;

static const rc_entry *lookup(unsigned int addr)
{
  int lo = 0, hi = rc_table_n - 1;
  while (lo <= hi)
  {
    int mid = (lo + hi) / 2;
    if (rc_table[mid].addr < addr) lo = mid + 1;
    else if (rc_table[mid].addr > addr) hi = mid - 1;
    else return &rc_table[mid];
  }
  return 0;
}

static uint32_t rom_word(uint32_t addr)
{
  return (cart.rom[addr ^ 1] << 8) | cart.rom[(addr + 1) ^ 1];
}

static int mem_writable(uint32_t addr)
{
  addr &= 0xFFFFFF;
  if (addr >= 0xE00000) return 2;                    /* work RAM */
  if (addr >= 0x200000 && addr < 0x204000) return 3; /* cart SRAM */
  return 0;
}

/* --- memory access: reads see the routine's staged writes --- */

uint32_t lift_r8(lift_ctx *x, uint32_t addr)
{
  int i;
  uint32_t q = addr & 0xFFFFFF;
  for (i = x->nw - 1; i >= 0; i--)
  {
    uint32_t a = x->wl[i].addr, sz = x->wl[i].sz;
    if (q >= a && q < a + sz)
      return (x->wl[i].val >> (8 * (sz - 1 - (q - a)))) & 0xFF;
  }
  return rc_real_read8(addr);
}
uint32_t lift_r16(lift_ctx *x, uint32_t addr) { return (lift_r8(x, addr) << 8) | lift_r8(x, addr + 1); }
uint32_t lift_r32(lift_ctx *x, uint32_t addr) { return (lift_r16(x, addr) << 16) | lift_r16(x, addr + 2); }

static void logw(lift_ctx *x, uint32_t addr, uint32_t v, int sz)
{
  if (!mem_writable(addr) || x->nw >= LIFT_WLOG_MAX) { x->declined = 1; return; }
  x->wl[x->nw].addr = addr & 0xFFFFFF;
  x->wl[x->nw].val = v;
  x->wl[x->nw].sz = sz;
  x->nw++;
}
void lift_w8(lift_ctx *x, uint32_t addr, uint32_t v) { logw(x, addr, v & 0xFF, 1); }
void lift_w16(lift_ctx *x, uint32_t addr, uint32_t v) { logw(x, addr, v & 0xFFFF, 2); }
void lift_w32(lift_ctx *x, uint32_t addr, uint32_t v) { logw(x, addr, v, 4); }

/* --- exact cycle accounting (mirrors rc_native.c / the interpreter) --- */

static void charge(lift_ctx *x, int amount)
{
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  x->cycles += amount;
}

void lift_charge(lift_ctx *x, unsigned int a)
{
  const rc_entry *e = lookup(a);
  charge(x, (int)rc_cycles_for_opcode(rom_word(a)) + ((e && e->kind == 1) ? e->extra : 0));
}

void lift_charge_bcc(lift_ctx *x, unsigned int a, int taken)
{
  const rc_entry *e = lookup(a);
  int cyc = (int)rc_cycles_for_opcode(rom_word(a));
  if (!taken) cyc += (e && e->kind == 2) ? -14 : 14;
  charge(x, cyc);
}

void lift_charge_dbcc(lift_ctx *x, unsigned int a, int taken, int expired)
{
  int cyc = (int)rc_cycles_for_opcode(rom_word(a));
  if (taken) cyc += -14;
  else if (expired) cyc += 14;
  charge(x, cyc);
}

void lift_charge_movem(lift_ctx *x, unsigned int a)
{
  const rc_entry *e = lookup(a);
  /* only movem.l (kind 6) has GPGX's SKIP_BUS_REFRESH; movem.w charges
   * base+extra like any fixed-cycle instruction */
  if (!e || e->kind != 6) { lift_charge(x, a); return; }
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  x->cycles += e->extra;                      /* per-register, charged first */
  if (x->cycles >= x->refresh)                /* SKIP_BUS_REFRESH */
    x->refresh += (128 * 7);
  x->cycles += (int)rc_cycles_for_opcode(rom_word(a));
}

/* register-count shifts/rotates: the GPGX handler charges count*CYC_SHIFT
 * plus SKIP_BUS_REFRESH (when count != 0) BEFORE the main loop adds the
 * base table cycles — the extra-then-base order matters when a bus-refresh
 * boundary lands between the two charges (static-count shift forms instead
 * fold their extra into the rc_table and have no refresh skip) */
void lift_charge_shift_reg(lift_ctx *x, unsigned int a, int count)
{
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  if (count)
  {
    x->cycles += 14 * count;                    /* CYC_SHIFT = 2*MUL, MUL=7 */
    if (x->cycles >= x->refresh)                /* SKIP_BUS_REFRESH */
      x->refresh += (128 * 7);
  }
  x->cycles += (int)rc_cycles_for_opcode(rom_word(a));
}

/* bit-ops with a register bit number on a data register (rc_native.c
 * kind 7): GPGX charges +14 when the dynamic bit (mod 32, read before
 * execution) lands in the destination's upper word. Ordinary refresh-
 * checked charge otherwise — no SKIP_BUS_REFRESH here. */
void lift_charge_bitop_reg(lift_ctx *x, unsigned int a, uint32_t bit)
{
  charge(x, (int)rc_cycles_for_opcode(rom_word(a)) + (((bit & 31) >= 16) ? 14 : 0));
}

void lift_charge_scc(lift_ctx *x, unsigned int a, uint32_t result)
{
  charge(x, (int)rc_cycles_for_opcode(rom_word(a)) + (((result & 0xFF) == 0xFF) ? 14 : 0));
}

/* --- mul/div: data-dependent cycles, mirroring GPGX's Use*Cycles helpers
 * (m68kops.h). The interpreter's order per instruction is: bus-refresh
 * check, handler (which charges the data-dependent part; div also skips
 * one refresh period), then the base table cycles. mul/div register-direct
 * opcodes have base 0 in the table; EA forms carry only the EA cost, so
 * charging mcycles + base in that order is exact for every addressing
 * mode. src is the 16-bit source operand (cycles depend on it, not the
 * destination). A zero divisor would trap in the interpreter — the div
 * helpers decline instead; callers must check x->declined and bail
 * before using the quotient. */

void lift_charge_mulu(lift_ctx *x, unsigned int a, uint32_t src)
{
  int mc = 38 * 7;
  src &= 0xFFFF;
  while (src) { if (src & 1) mc += 2 * 7; src >>= 1; }
  charge(x, mc + (int)rc_cycles_for_opcode(rom_word(a)));
}

void lift_charge_muls(lift_ctx *x, unsigned int a, uint32_t src)
{
  int mc = 38 * 7;
  uint32_t tmp = ((src << 1) ^ src) & 0xFFFF;   /* 01/10 bit patterns */
  while (tmp) { if (tmp & 1) mc += 2 * 7; tmp >>= 1; }
  charge(x, mc + (int)rc_cycles_for_opcode(rom_word(a)));
}

void lift_charge_divu(lift_ctx *x, unsigned int a, uint32_t src, uint32_t dst)
{
  src &= 0xFFFF;
  if (src == 0) { x->declined = 1; return; }    /* zero-divide trap */
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  if (dst / src < 0x10000)
  {
    int i, mc = 76 * 7;
    uint32_t s = src << 16, d = dst;
    for (i = 0; i < 15; i++)
    {
      if ((int32_t)d < 0) { d <<= 1; d -= s; }
      else
      {
        d <<= 1; mc += 4 * 7;
        if (d >= s) { d -= s; mc -= 2 * 7; }
      }
    }
    x->cycles += mc;
    if (x->cycles >= x->refresh)                /* SKIP_BUS_REFRESH */
      x->refresh += (128 * 7);
  }
  else
    x->cycles += 10 * 7;                        /* overflow: no refresh skip */
  x->cycles += (int)rc_cycles_for_opcode(rom_word(a));
}

void lift_charge_divs(lift_ctx *x, unsigned int a, uint32_t src, uint32_t dst)
{
  int32_t ss = (int16_t)(src & 0xFFFF), sd = (int32_t)dst;
  int32_t abs_d, abs_s;
  int mc = 12 * 7;
  if (ss == 0) { x->declined = 1; return; }     /* zero-divide trap */
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  if (sd < 0) mc += 2 * 7;
  /* GPGX takes abs() of the sint32 dividend; INT32_MIN stays INT32_MIN
   * there, making the >>16 guard negative (and thus true) — replicate */
  abs_d = (sd < 0) ? (int32_t)(0u - (uint32_t)sd) : sd;
  abs_s = (ss < 0) ? -ss : ss;
  if ((abs_d >> 16) < abs_s)
  {
    int i;
    uint32_t quotient = (uint32_t)(abs_d / abs_s);
    mc += 110 * 7;
    if (ss >= 0) mc += (sd >= 0) ? -2 * 7 : 2 * 7;
    for (i = 0; i < 15; i++)
    {
      quotient >>= 1;
      if (!(quotient & 1)) mc += 2 * 7;
    }
  }
  else
    mc += 4 * 7;                                /* absolute overflow */
  x->cycles += mc;
  if (x->cycles >= x->refresh)                  /* SKIP_BUS_REFRESH */
    x->refresh += (128 * 7);
  x->cycles += (int)rc_cycles_for_opcode(rom_word(a));
}

/* --- staged VDP port writes (design: native/decomp/HW-STAGING.md) --- */

/* base+extra charge WITHOUT the refresh check — the hw helpers run the
 * check themselves before timestamping, mirroring the interpreter's
 * refresh-check -> execute(handler sees cycles) -> USE_CYCLES order */
static void charge_base(lift_ctx *x, unsigned int a)
{
  const rc_entry *e = lookup(a);
  x->cycles += (int)rc_cycles_for_opcode(rom_word(a)) + ((e && e->kind == 1) ? e->extra : 0);
}

static long long hw_refresh_check(lift_ctx *x)
{
  if (x->cycles >= x->refresh)
  {
    x->refresh = x->cycles + (128 * 7);
    x->cycles += (2 * 7);
  }
  return x->cycles;
}

static void hw_decline(lift_ctx *x)
{
  x->declined = 1;
  x->hw_declined = 1;
}

static void hw_sim_init(lift_ctx *x)
{
  rc_vdp_state_t s;
  rc_vdp_get_state(&s);
  x->hwsim.inited = 1;
  /* DMA in flight at entry: a cached ctrl word could replay mid-sequence
   * and dmafill would rearm on the first data write — not stageable */
  if (s.dma_length || s.dmafill) { hw_decline(x); return; }
  x->hwsim.pending = s.pending;
  x->hwsim.code = s.code;
  x->hwsim.m5 = (s.reg1 & 4) ? 1 : 0;
  x->hwsim.fifo_active = (!(s.status & 8) && (s.reg1 & 0x40));
  memcpy(x->hwsim.fifo_cycles, s.fifo_cycles_, sizeof x->hwsim.fifo_cycles);
  x->hwsim.fifo_idx = s.fifo_idx;
  x->hwsim.fifo_timing = s.fifo_timing_;
  x->hwsim.fifo_byte_access = s.fifo_byte_access;
  x->hwsim.mcycles_vdp = s.mcycles_vdp_;
}

static void hw_stage(lift_ctx *x, uint32_t port, uint32_t val, long long ts)
{
  long long ts_after = ts;
  if (x->declined) return;
  if (!x->hwsim.inited) hw_sim_init(x);
  if (x->declined) return;
  if (x->nhw >= LIFT_HWLOG_MAX) { hw_decline(x); return; }
  /* Past cycle_end the frozen entry snapshot is stale (per-line FIFO
   * drain, status/mcycles_vdp advance, frame-end cycle rebase), so the
   * exact-cycle tier ends at the slice boundary.
   *   Live: decline as before — an atomic commit can't cross the slice
   *   (slice-spill), and replay needs the exact model.
   *   Verify: drop to the SEQUENCE tier (2026-08-04 hw-granularity):
   *   keep staging ports+values in program order — the interpreter's
   *   observed writes are still compared one-for-one — but stop the
   *   FIFO/cycle sim; compare_at_return skips the per-write ts and the
   *   total-cycle/refresh checks for this arm (counted hw-xslice, the
   *   hw analogue of cyc-unchecked). Register/flag/RAM prediction stays
   *   at full strength. */
  if (ts >= (long long)m68k.cycle_end)
  {
    if (mode == 2) { hw_decline(x); return; }
    x->hw_xslice = 1;
  }
  if (port & 4)   /* control port: mirror vdp_68k_ctrl_w's state machine */
  {
    if (!x->hwsim.pending)
    {
      x->hwsim.code = (x->hwsim.code & 0x3C) | ((val >> 14) & 3);
      if ((val & 0xC000) == 0x8000)          /* VDP register write: IRQ-visible */
      { hw_decline(x); return; }
      x->hwsim.pending = x->hwsim.m5;
    }
    else
    {
      x->hwsim.pending = 0;
      x->hwsim.code = (x->hwsim.code & 0x03) | ((val >> 2) & 0x3C);
      if (x->hwsim.code & 0x20)              /* CD5: would trigger DMA */
      { hw_decline(x); return; }
    }
  }
  else            /* data port: exact vdp_68k_data_w_m5 FIFO arithmetic */
  {
    x->hwsim.pending = 0;
    if (x->hwsim.fifo_active && !x->hw_xslice)
    {
      unsigned int cycles = (unsigned int)ts;
      int slot = 0, wi = x->hwsim.fifo_idx;
      if (cycles < x->hwsim.fifo_cycles[(wi + 3) & 3])
      {
        if (cycles < x->hwsim.fifo_cycles[wi])
        {
          /* FIFO full: the 68k stalls until the oldest entry drains.
           * Mirror vdp_68k_data_w_m5 EXACTLY: m68k.cycles jumps to the
           * drain time rounded UP to a multiple of 7, mid-instruction
           * (before the base charge). ts stays pre-stall — that is what
           * rc_hw_observe records at the handler top; ts_after carries
           * the post-stall value for live replay's check. */
          long long stalled =
            (((long long)x->hwsim.fifo_cycles[wi] + 6) / 7) * 7;
          if (stalled >= (long long)m68k.cycle_end)
          {
            /* the stall itself crosses the slice: the frozen drain time
             * may be rewritten by the next line's FIFO processing, so
             * the exact tier ends here. Live: decline. Verify: sequence
             * tier (the bump below is best-effort — cycles go unchecked
             * for this arm anyway). */
            if (mode == 2) { hw_decline(x); return; }
            x->hw_xslice = 1;
          }
          x->cycles = stalled;
          ts_after = stalled;
          st.hw_stall_model++;
        }
        cycles = x->hwsim.fifo_cycles[(wi + 3) & 3];
      }
      cycles -= x->hwsim.mcycles_vdp;
      while ((int)cycles >= x->hwsim.fifo_timing[slot]) slot++;
      x->hwsim.fifo_cycles[wi] = x->hwsim.mcycles_vdp +
        x->hwsim.fifo_timing[slot + x->hwsim.fifo_byte_access];
      x->hwsim.fifo_idx = (wi + 1) & 3;
    }
  }
  x->hwl[x->nhw].port = port;
  x->hwl[x->nhw].val = val & 0xFFFF;
  x->hwl[x->nhw].ts = ts;
  x->hwl[x->nhw].ts_after = ts_after;
  x->nhw++;
}

void lift_whw_ctrl32(lift_ctx *x, unsigned int a, uint32_t val)
{
  long long ts = hw_refresh_check(x);
  hw_stage(x, 0xC00004, (val >> 16) & 0xFFFF, ts);
  hw_stage(x, 0xC00004, val & 0xFFFF, ts);   /* same ts: one instruction */
  charge_base(x, a);
}

void lift_whw_ctrl16(lift_ctx *x, unsigned int a, uint32_t val)
{
  long long ts = hw_refresh_check(x);
  hw_stage(x, 0xC00004, val, ts);
  charge_base(x, a);
}

void lift_whw_data16(lift_ctx *x, unsigned int a, uint32_t val)
{
  long long ts = hw_refresh_check(x);
  hw_stage(x, 0xC00000, val, ts);
  charge_base(x, a);
}

void lift_whw_data32(lift_ctx *x, unsigned int a, uint32_t val)
{
  long long ts = hw_refresh_check(x);
  hw_stage(x, 0xC00000, (val >> 16) & 0xFFFF, ts);
  if (x->declined) return;
  /* one instruction, two bus writes, no charge between — but if the
   * FIRST word stalled on a full FIFO the handler bumped m68k.cycles,
   * and the SECOND write's handler observes the post-stall time */
  hw_stage(x, 0xC00000, val & 0xFFFF, x->hwl[x->nhw - 1].ts_after);
  charge_base(x, a);
}

/* verify-mode capture of the interpreter's real port writes: appended to
 * every armed slot — an inner lifted call's port writes belong to the
 * outer routine's staged sequence too (lift_call shares the log) */
static void hw_observe(unsigned int port, unsigned int data)
{
  int i;
  for (i = 0; i < n_arm; i++)
  {
    if (arm[i].nobs >= LIFT_HWLOG_MAX) { arm[i].obs_over = 1; continue; }
    arm[i].obs[arm[i].nobs].port = port;
    arm[i].obs[arm[i].nobs].val = data & 0xFFFF;
    arm[i].obs[arm[i].nobs].ts = m68k.cycles;
    arm[i].nobs++;
  }
}

/* --- composition: call a lifted routine with original bsr semantics --- */

void lift_call(lift_ctx *x, unsigned int bsr_addr, int bsr_len, lift_fn fn)
{
  lift_charge(x, bsr_addr);
  x->c->a[7] -= 4;
  lift_w32(x, x->c->a[7], (bsr_addr + bsr_len) & 0xFFFFFF);
  fn(x);          /* callee's rts sets c->pc and pops a7 */
}

/* --- snapshot / compare --- */

static void snapshot(rcpu_t *c)
{
  int i;
  memset(c, 0, sizeof(*c));
  for (i = 0; i < 8; i++)
  {
    c->d[i] = m68k.dar[i];
    c->a[i] = m68k.dar[8 + i];
  }
  c->xf = (m68k.x_flag >> 8) & 1;
  c->nf = (m68k.n_flag >> 7) & 1;
  c->zf = (m68k.not_z_flag == 0);
  c->vf = (m68k.v_flag >> 7) & 1;
  c->cf = (m68k.c_flag >> 8) & 1;
}

static void compare_at_return(int slot)
{
  int i, j, bad = 0;
  char what[128] = "";
  rcpu_t cur;
  snapshot(&cur);
  for (i = 0; i < 8 && !bad; i++)
  {
    if (cur.d[i] != arm[slot].pred.d[i]) { snprintf(what, 64, "d%d %08X!=%08X", i, arm[slot].pred.d[i], cur.d[i]); bad = 1; }
    else if (cur.a[i] != arm[slot].pred.a[i]) { snprintf(what, 64, "a%d %08X!=%08X", i, arm[slot].pred.a[i], cur.a[i]); bad = 1; }
  }
  if (!bad && (cur.xf != arm[slot].pred.xf || cur.nf != arm[slot].pred.nf ||
               cur.zf != arm[slot].pred.zf || cur.vf != arm[slot].pred.vf ||
               cur.cf != arm[slot].pred.cf))
  {
    snprintf(what, 64, "flags XNZVC pred=%d%d%d%d%d real=%d%d%d%d%d",
             arm[slot].pred.xf, arm[slot].pred.nf, arm[slot].pred.zf,
             arm[slot].pred.vf, arm[slot].pred.cf,
             cur.xf, cur.nf, cur.zf, cur.vf, cur.cf);
    bad = 1;
  }
  for (i = 0; i < arm[slot].nw && !bad; i++)
  {
    uint32_t a = arm[slot].wl[i].addr, sz = arm[slot].wl[i].sz;
    int b;
    /* only the final value at each BYTE survives: a later write may
     * partially overlap this one (e.g. a word push landing inside an
     * earlier long push at a different call depth — the tail-call
     * pattern that produced sub_161D0's false mem@ divergences), so
     * coverage must be judged per byte, not per whole write */
    for (b = 0; b < (int)sz && !bad; b++)
    {
      uint32_t ba = a + b;
      uint8_t expect;
      int covered = 0;
      for (j = i + 1; j < arm[slot].nw && !covered; j++)
        if (ba >= arm[slot].wl[j].addr &&
            ba < arm[slot].wl[j].addr + arm[slot].wl[j].sz)
          covered = 1;
      if (covered) continue;
      expect = (arm[slot].wl[i].val >> (8 * (sz - 1 - b))) & 0xFF;
      if (rc_real_read8(ba) != expect)
      {
        snprintf(what, 64, "mem@%06X staged=%02X real=%02X wi=%d/%d isr=%lu",
                 ba, expect, rc_real_read8(ba), i, arm[slot].nw,
                 isr_seen - arm[slot].isr_at_arm);
        bad = 1;
      }
    }
  }
  /* staged hw sequence vs the interpreter's observed port writes: count,
   * order, port, value AND per-write timestamp must all match — the ts
   * compare verifies the cycle model at every port write inside the
   * routine, not just the total at return */
  if (!bad && (arm[slot].obs_over || arm[slot].nobs != arm[slot].nhw))
  {
    snprintf(what, sizeof what, "hw count staged=%d real=%d%s",
             arm[slot].nhw, arm[slot].nobs, arm[slot].obs_over ? " (ovf)" : "");
    bad = 1;
  }
  for (i = 0; i < arm[slot].nhw && !bad; i++)
  {
    if (arm[slot].hwl[i].port != arm[slot].obs[i].port ||
        arm[slot].hwl[i].val  != arm[slot].obs[i].val  ||
        (!arm[slot].hw_xslice && arm[slot].hwl[i].ts != arm[slot].obs[i].ts))
    {
      snprintf(what, sizeof what,
               "hw#%d staged %06X=%04X@%lld real %06X=%04X@%lld", i,
               arm[slot].hwl[i].port, arm[slot].hwl[i].val, arm[slot].hwl[i].ts,
               arm[slot].obs[i].port, arm[slot].obs[i].val, arm[slot].obs[i].ts);
      bad = 1;
    }
  }
  if (arm[slot].hw_xslice)
  {
    /* SEQUENCE tier (hw ran past the slice): ports+values compared
     * above at full strength; the per-write ts and the total-cycle /
     * refresh predictions are unchecked — the frozen line snapshot the
     * cycle model needs went stale mid-routine. The hw analogue of
     * cyc-unchecked; count it honestly. */
    if (!bad) st.hw_xslice++;
  }
  else
  {
    if (!bad && m68k.cycles != arm[slot].cycles_pred)
    {
      if (m68k.cycles < arm[slot].cycles_pred) st.cyc_unchecked++;
      else { snprintf(what, 64, "cycles +%lld", m68k.cycles - arm[slot].cycles_pred); bad = 1; }
    }
    if (!bad && m68k.cycles == arm[slot].cycles_pred &&
        (long long)m68k.refresh_cycles != arm[slot].refresh_pred)
    {
      snprintf(what, 64, "refresh pred=%lld real=%d",
               arm[slot].refresh_pred, m68k.refresh_cycles);
      bad = 1;
    }
  }
  if (bad)
  {
    st.divergences++;
    if (reported < MAX_DIV)
    {
      printf("LIFT DIVERGE %s: %s\n", arm[slot].name, what);
      reported++;
    }
    /* RC_LIFT_DEBUG=<name-substring>: dump the diverging call's entry
     * pointers, full staged write log and the real bytes around the
     * stack top — the $CB50 stack-aliasing investigation instrument */
    {
      const char *dbg = getenv("RC_LIFT_DEBUG");
      if (dbg && strstr(arm[slot].name, dbg))
      {
        printf("DBG %s entry_a3=%08X entry_a7=%08X ret_pc=%06X pred_a7=%08X "
               "real_a7=%08X nw=%d isr=%lu\n",
               arm[slot].name, arm[slot].e_a3, arm[slot].e_a7,
               arm[slot].ret_pc, arm[slot].pred.a[7],
               (unsigned int)m68k.dar[15], arm[slot].nw,
               isr_seen - arm[slot].isr_at_arm);
        for (i = 0; i < arm[slot].nw; i++)
          printf("DBG wl[%02d] %06X=%08X sz=%d\n", i, arm[slot].wl[i].addr,
                 arm[slot].wl[i].val, arm[slot].wl[i].sz);
        printf("DBG real FFFF70..FFFFA0:");
        for (i = 0xFFFF70; i <= 0xFFFFA0; i++)
          printf(" %02X", rc_real_read8((unsigned int)i));
        printf("\n");
      }
    }
  }
  else
  {
    st.verified++;
    if (arm[slot].ridx < LIFT_MAX_ROUTINES)
    {
      per_verified[arm[slot].ridx]++;
      if (arm[slot].hw_xslice) per_xslice[arm[slot].ridx]++;
    }
  }
}

/* --- per-instruction step --- */

int lift_step(unsigned int pc)
{
  int i;
  pc &= 0xFFFFFF;

  /* an interrupt taken mid-call invalidates every armed prediction: the
   * ISR's cycles and stack frame land inside the routine's span. This
   * ROM has TWO live handlers — VBlank ($76B2) and HBlank ($15E6C);
   * missing the HBlank one produced call-to-call-varying mem@/cycles
   * noise on otherwise-correct lifts (sub_15FF0/sub_161D0, 2026-07-31) */
  if (mode == 1 && (pc == ISR_ENTRY || pc == ISR_HBLANK))
  {
    isr_seen++;
    for (i = 0; i < n_arm; i++) arm[i].interrupted = 1;
  }

  /* pop every arm expiring here: a routine that tail-falls into another
   * lifted routine shares its return address with it, so two arms can
   * legitimately expire on the same instruction */
  while (mode == 1 && n_arm && pc == arm[n_arm - 1].ret_pc)
  {
    n_arm--;
    if (arm[n_arm].interrupted)
    {
      st.int_skip++;
      if (arm[n_arm].ridx < LIFT_MAX_ROUTINES) per_intskip[arm[n_arm].ridx]++;
    }
    else compare_at_return(n_arm);
  }

  for (i = 0; i < lift_routines_n; i++)
  {
    if (lift_routines[i].entry != pc) continue;
    lift_ctx x;
    rcpu_t c;
    snapshot(&c);
    x.c = &c;
    x.cycles = m68k.cycles;
    x.refresh = m68k.refresh_cycles;
    x.wl = (mode == 2) ? live_wl : (n_arm < ARM_MAX ? arm[n_arm].wl : NULL);
    x.nw = 0;
    x.declined = 0;
    x.hw_declined = 0;
    x.hw_xslice = 0;
    x.hwl = (mode == 2) ? live_hwl : (n_arm < ARM_MAX ? arm[n_arm].hwl : NULL);
    x.nhw = 0;
    x.hwsim.inited = 0;
    if (!x.wl)
    {
      st.arm_overflow++;
      if (i < LIFT_MAX_ROUTINES) per_overflow[i]++;
      return -1;
    }
    lift_routines[i].fn(&x);
    if (x.declined)
    {
      st.declined++;
      if (x.hw_declined) st.hw_declined++;
      if (i < LIFT_MAX_ROUTINES) per_declined[i]++;
      return -1;
    }

    /* live: an atomic commit must not overrun the current m68k_run slice —
     * the interpreter would have paused at cycle_end mid-routine (VDP line
     * events, IRQ assertion), so a routine that crosses it runs interpreted */
    if (mode == 2 && x.cycles > (long long)m68k.cycle_end)
    {
      st.slice_spill++;
      return -1;
    }
    if (mode == 2 && live_max >= 0 && (long)st.live_runs >= live_max)
      return -1;

    if (mode == 2)                        /* live: commit everything */
    {
      int j, k;
      /* replay staged hw writes through the REAL port handlers, each at
       * the m68k.cycles its handler would have observed — VDP internal
       * state (addr autoincrement, pending, FIFO) is only ever mutated
       * by GPGX's own code. The predicate guarantees no handler bumps
       * m68k.cycles (FIFO stall); the counter proves it stays true.
       * Order vs the RAM commit below is immaterial: no stageable VDP
       * op reads RAM (DMA is declined). */
      for (j = 0; j < x.nhw; j++)
      {
        int ts = (int)live_hwl[j].ts;
        m68k.cycles = ts;
        if (live_hwl[j].port & 4) vdp_68k_ctrl_w(live_hwl[j].val);
        else vdp_68k_data_w(live_hwl[j].val);
        /* a modeled FIFO stall legitimately bumps m68k.cycles inside the
         * handler — the prediction carried the post-stall value in
         * ts_after; anything else is a predicate bug */
        if (m68k.cycles != (int)live_hwl[j].ts_after)
        {
          st.hw_replay_stall++;
          printf("LIFT HW REPLAY STALL %s #%d port=%06X val=%04X ts=%d "
                 "expect=%d now=%d\n",
                 lift_routines[i].name, j, live_hwl[j].port, live_hwl[j].val,
                 ts, (int)live_hwl[j].ts_after, m68k.cycles);
        }
      }
      if (x.nhw) st.hw_shipped++;
      for (j = 0; j < x.nw; j++)
      {
        uint32_t a = x.wl[j].addr, sz = x.wl[j].sz, v = x.wl[j].val;
        int cls = mem_writable(a);
        for (k = 0; k < (int)sz; k++)
        {
          uint8_t b = (v >> (8 * (sz - 1 - k))) & 0xFF;
          if (cls == 2) work_ram[((a + k) & 0xFFFF) ^ 1] = b;
          else sram.sram[(a + k) & 0xFFFF] = b;
        }
      }
      for (j = 0; j < 8; j++)
      {
        m68k.dar[j] = c.d[j];
        m68k.dar[8 + j] = c.a[j];
      }
      m68k.x_flag = c.xf << 8;
      m68k.n_flag = c.nf << 7;
      m68k.not_z_flag = c.zf ? 0 : 1;
      m68k.v_flag = c.vf << 7;
      m68k.c_flag = c.cf << 8;
      m68k.pc = c.pc;
      m68k.cycles = (int)x.cycles;
      m68k.refresh_cycles = (int)x.refresh;
      st.live_runs++;
      if (live_max >= 0)
        printf("lift-live #%lu %s entry=%06X ret=%06X cyc=%lld nw=%d "
               "a7=%08X\n", st.live_runs, lift_routines[i].name, pc,
               c.pc, x.cycles, x.nw, c.a[7]);
      return 0;
    }
    /* verify: arm prediction (nested arms allowed, LIFO) */
    arm[n_arm].entry = pc;
    arm[n_arm].ret_pc = c.pc;
    arm[n_arm].pred = c;
    arm[n_arm].cycles_pred = x.cycles;
    arm[n_arm].refresh_pred = x.refresh;
    arm[n_arm].interrupted = 0;
    arm[n_arm].isr_at_arm = isr_seen;
    arm[n_arm].name = lift_routines[i].name;
    arm[n_arm].nw = x.nw;
    arm[n_arm].e_a3 = (unsigned int)m68k.dar[11];   /* entry a3/a7: the C ctx */
    arm[n_arm].e_a7 = (unsigned int)m68k.dar[15];   /* is mutated by the run  */
    arm[n_arm].nhw = x.nhw;
    arm[n_arm].nobs = 0;
    arm[n_arm].obs_over = 0;
    arm[n_arm].hw_xslice = x.hw_xslice;
    arm[n_arm].ridx = i;
    if (x.nhw) st.hw_shipped++;
    n_arm++;
    return -1;
  }
  return -1;
}

void lift_set_mode(int m)
{
  const char *e = getenv("RC_LIFT_MAX");
  if (e) live_max = atol(e);
  mode = m;
  if (m == 1) rc_hw_observe = hw_observe;   /* verify: capture real bus writes */
}

void lift_report(void)
{
  if (!mode) return;
  printf("lift:     mode=%s verified=%lu divergences=%lu live=%lu "
         "declined=%lu slice-spill=%lu int-skips=%lu cyc-unchecked=%lu "
         "arm-overflow=%lu hw=%lu hw-declined=%lu hw-stall=%lu "
         "hw-xslice=%lu hw-stall-model=%lu\n",
         mode == 2 ? "live" : "verify", st.verified, st.divergences,
         st.live_runs, st.declined, st.slice_spill, st.int_skip,
         st.cyc_unchecked, st.arm_overflow,
         st.hw_shipped, st.hw_declined, st.hw_replay_stall,
         st.hw_xslice, st.hw_stall_model);
  if (mode == 1 && getenv("RC_LIFT_STATS"))
  {
    int i;
    for (i = 0; i < lift_routines_n && i < LIFT_MAX_ROUTINES; i++)
      if (per_verified[i] || per_declined[i] || per_overflow[i] || per_intskip[i])
        printf("lift-stat %-40s verified=%lu xslice=%lu declined=%lu "
               "overflow=%lu int-skip=%lu\n",
               lift_routines[i].name, per_verified[i], per_xslice[i],
               per_declined[i], per_overflow[i], per_intskip[i]);
  }
}
