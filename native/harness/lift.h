/* Semantic-lifting runtime: routine-level replacement of original code with
 * readable C (the decomp proper, under native/decomp/).
 *
 * Modes: 0 off; 1 shadow-verify (lifted code runs on a copy at every call,
 * compared against real execution at the return address; nested lifted
 * calls each get their own arm slot); 2 live (lifted code replaces the
 * routine, cycle-exact, RAM + SRAM writes committed).
 */
#ifndef _LIFT_H_
#define _LIFT_H_

#include "rcpu.h"

#define LIFT_WLOG_MAX 65536
#define LIFT_HWLOG_MAX 4096   /* a 32-row x 64-word tilemap fill stages ~2100 */

typedef struct { uint32_t addr; uint32_t val; uint8_t sz; } lift_write;

/* staged VDP port write: ts = the m68k.cycles value the interpreter's
 * handler would observe (post refresh-check, pre base-charge) */
typedef struct {
  uint32_t port; uint16_t val;
  long long ts;              /* pre-handler m68k.cycles (what observe sees) */
  long long ts_after;        /* post-handler: == ts unless a modeled FIFO
                              * stall bumped it (vdp_68k_data_w_m5's
                              * round-up-to-7 jump) — live replay checks
                              * m68k.cycles against THIS after the handler */
} lift_hw_write;

/* execution context handed to lifted routines */
typedef struct {
  rcpu_t *c;                 /* registers/flags (write-log unused here) */
  long long cycles;          /* simulated m68k.cycles */
  long long refresh;         /* simulated m68k.refresh_cycles */
  lift_write *wl;            /* routine-level write log */
  int nw;
  int declined;              /* set to fall back to per-instruction path */
  int hw_declined;           /* subset flag: declined by the hw predicate */
  int hw_xslice;             /* verify only: a staged hw write ran past the
                              * slice, so the frozen line snapshot is stale —
                              * the arm drops to the SEQUENCE tier (ports+
                              * values compared, per-write ts and total
                              * cycles unchecked; counted hw-xslice). Live
                              * mode still declines these. */
  lift_hw_write *hwl;        /* staged VDP port writes (see HW-STAGING.md) */
  int nhw;
  struct {                   /* read-only oracle mirroring vdp_ctrl.c state;
                              * inited lazily on the first staged hw write */
    int inited;
    int pending, code;
    int m5;                  /* reg[1] & 4 — what the handler sets pending to */
    int fifo_active;         /* !(status&8) && (reg[1]&0x40) */
    unsigned int fifo_cycles[4];
    int fifo_idx;
    const int *fifo_timing;
    int fifo_byte_access;
    unsigned int mcycles_vdp;
  } hwsim;
} lift_ctx;

typedef void (*lift_fn)(lift_ctx *);

typedef struct {
  unsigned int entry;
  lift_fn fn;
  const char *name;
} lift_routine;

/* the decomp registry (native/decomp/registry.c) */
extern const lift_routine lift_routines[];
extern const int lift_routines_n;

/* --- memory access for lifted code (reads see staged writes) --- */
uint32_t lift_r8(lift_ctx *x, uint32_t addr);
uint32_t lift_r16(lift_ctx *x, uint32_t addr);
uint32_t lift_r32(lift_ctx *x, uint32_t addr);
void lift_w8(lift_ctx *x, uint32_t addr, uint32_t v);
void lift_w16(lift_ctx *x, uint32_t addr, uint32_t v);
void lift_w32(lift_ctx *x, uint32_t addr, uint32_t v);

/* --- cycle helpers (exact interpreter accounting) --- */
void lift_charge(lift_ctx *x, unsigned int insn_addr);              /* fixed */
void lift_charge_bcc(lift_ctx *x, unsigned int insn_addr, int taken);
void lift_charge_dbcc(lift_ctx *x, unsigned int insn_addr, int taken, int expired);
void lift_charge_movem(lift_ctx *x, unsigned int insn_addr);
/* register-count shift/rotate: count = the dynamic count (reg & 63) */
void lift_charge_shift_reg(lift_ctx *x, unsigned int insn_addr, int count);
/* bset/bclr/bchg with a register bit number on a data register: +14 when
 * the dynamic bit (mod 32, read before execution) lands in the upper word
 * (rc_native.c kind 7). Pass the raw bit-number register value. */
void lift_charge_bitop_reg(lift_ctx *x, unsigned int insn_addr, uint32_t bit);
/* Scc on a data register: +14 when the condition held, i.e. the result
 * byte is $FF (rc_native.c kind 5, which reads the destination byte
 * after execution). Pass the just-computed result byte — never a
 * caller-meaningful boolean (same trap family as lift_charge_bcc's
 * taken flag). Scc itself does not touch CCR. */
void lift_charge_scc(lift_ctx *x, unsigned int insn_addr, uint32_t result);
/* mul/div: data-dependent (GPGX Use*Cycles). src = 16-bit source operand,
 * dst = 32-bit destination register value BEFORE the division. The div
 * helpers set x->declined on a zero divisor (interpreter would trap) —
 * check and bail before using the quotient. */
void lift_charge_mulu(lift_ctx *x, unsigned int insn_addr, uint32_t src);
void lift_charge_muls(lift_ctx *x, unsigned int insn_addr, uint32_t src);
void lift_charge_divu(lift_ctx *x, unsigned int insn_addr, uint32_t src, uint32_t dst);
void lift_charge_divs(lift_ctx *x, unsigned int insn_addr, uint32_t src, uint32_t dst);

/* --- staged VDP port writes (design: native/decomp/HW-STAGING.md) ---
 * Each helper is the COMBINED model of one hw-writing instruction:
 * refresh check, timestamp + stage the port write(s), then the base
 * cycle charge. Call it INSTEAD of lift_w* + lift_charge for that
 * instruction — never follow it with lift_charge for the same address.
 * ctrl32 stages both 16-bit halves of a move.l to $C00004 at one ts.
 * The stageability predicate declines (x->declined + x->hw_declined):
 * register writes ($8xxx first words), DMA triggers (CD5), any entry
 * state with DMA in flight, and data writes that would stall on a full
 * FIFO. Reads, byte-size port writes and non-VDP hw stay unliftable. */
void lift_whw_ctrl32(lift_ctx *x, unsigned int insn_addr, uint32_t val);
void lift_whw_ctrl16(lift_ctx *x, unsigned int insn_addr, uint32_t val);
void lift_whw_data16(lift_ctx *x, unsigned int insn_addr, uint32_t val);
/* data32: a move.l to $C00000 — TWO data-port words at ONE timestamp,
 * one refresh check, one base charge (never spell it as two data16
 * calls: that double-charges both). */
void lift_whw_data32(lift_ctx *x, unsigned int insn_addr, uint32_t val);

/* call another lifted routine with original bsr semantics */
void lift_call(lift_ctx *x, unsigned int bsr_addr, int bsr_len, lift_fn fn);

/* harness control */
void lift_set_mode(int mode);
int lift_step(unsigned int pc);
void lift_report(void);

#endif
