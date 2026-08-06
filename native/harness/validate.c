/*
 * Lockstep validator: before the interpreter executes each instruction, run
 * the generated semantic function on a snapshot of CPU state (reads hit live
 * memory, writes go to a log). After the interpreter has executed it (i.e. on
 * the next hook), compare predicted regs/flags/PC/memory against reality.
 */
#include "shared.h"
#include "gen_insns.h"
#include "m68k.h"

/* interpreter state access (Musashi layout) */

#define IN_D(n) m68k.dar[n]
#define IN_A(n) m68k.dar[8 + (n)]
#define IN_X() ((m68k.x_flag >> 8) & 1)
#define IN_N() ((m68k.n_flag >> 7) & 1)
#define IN_Z() (m68k.not_z_flag == 0)
#define IN_V() ((m68k.v_flag >> 7) & 1)
#define IN_C() ((m68k.c_flag >> 8) & 1)

static int enabled = 0;
static rcpu_t pred;
static int pending = 0;
static unsigned int pend_addr = 0;
static int pend_entry = -1;
static const char *pend_mnem = "";

static struct {
  unsigned long executed, predicted, verified, unimpl, unpred, int_skip;
  unsigned long divergences, unknown_pc, unk_ram, unk_checksum;
} st;

static struct { unsigned int page; unsigned long n; } upages[32];
static int n_upage = 0;

/* unique static-instruction coverage: verified-at-least-once bit per entry */
static unsigned char *hit;      /* indexed like rc_table */
static const char *covout = 0;

/* dynamic execution counts per instruction (for lift-candidate ranking) */
static unsigned long *prof;
static const char *profout = 0;

/* dynamic counts for unimplemented table entries */
#define UN_MAX 64
static struct { const rc_entry *e; unsigned long n; } un[UN_MAX];
static int n_un = 0;

static void e_unimpl_count(const rc_entry *e)
{
  int i;
  for (i = 0; i < n_un; i++)
    if (un[i].e == e) { un[i].n++; return; }
  if (n_un < UN_MAX) { un[n_un].e = e; un[n_un].n = 1; n_un++; }
}

#define MAX_DIV_REPORT 25
static int div_reported = 0;

/* --- memory helpers: ROM / RAM / SRAM readable, all else unpredictable --- */

static int mem_class(uint32_t addr)
{
  addr &= 0xFFFFFF;
  if (addr < 0x100000) return 1;                     /* ROM */
  if (addr >= 0xE00000) return 2;                    /* work RAM (mirrored) */
  if (addr >= 0x200000 && addr < 0x204000) return 3; /* cart SRAM */
  return 0;
}

uint32_t rc_real_read8(uint32_t addr)
{
  switch (mem_class(addr))
  {
    case 1: return cart.rom[(addr & 0xFFFFFF) ^ 1];
    case 2: return work_ram[(addr & 0xFFFF) ^ 1];
    case 3: return sram.sram[addr & 0xFFFF];
  }
  return 0;
}

uint32_t rc_read8(rcpu_t *c, uint32_t addr)
{
  if (!mem_class(addr)) { c->unpred = 1; return 0; }
  /* read-after-logged-write consistency */
  for (int i = c->nw - 1; i >= 0; i--)
  {
    uint32_t a = c->wlog[i].addr & 0xFFFFFF, sz = c->wlog[i].sz;
    uint32_t q = addr & 0xFFFFFF;
    if (q >= a && q < a + sz)
      return (c->wlog[i].val >> (8 * (sz - 1 - (q - a)))) & 0xFF;
  }
  return rc_real_read8(addr);
}

uint32_t rc_read16(rcpu_t *c, uint32_t addr) { return (rc_read8(c, addr) << 8) | rc_read8(c, addr + 1); }
uint32_t rc_read32(rcpu_t *c, uint32_t addr) { return (rc_read16(c, addr) << 16) | rc_read16(c, addr + 2); }

static void logw(rcpu_t *c, uint32_t addr, uint32_t v, int sz)
{
  if (!mem_class(addr) || mem_class(addr) == 1) { c->unpred = 1; return; }
  if (c->nw >= RC_WLOG_MAX) { c->unpred = 1; return; }
  c->wlog[c->nw].addr = addr & 0xFFFFFF;
  c->wlog[c->nw].val = v;
  c->wlog[c->nw].sz = sz;
  c->nw++;
}
void rc_write8(rcpu_t *c, uint32_t addr, uint32_t v) { logw(c, addr, v & 0xFF, 1); }
void rc_write16(rcpu_t *c, uint32_t addr, uint32_t v) { logw(c, addr, v & 0xFFFF, 2); }
void rc_write32(rcpu_t *c, uint32_t addr, uint32_t v) { logw(c, addr, v, 4); }

/* --- table lookup --- */

void rc_validate_covout(const char *path) { covout = path; }
void rc_validate_profout(const char *path) { profout = path; }

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

/* --- compare --- */

static void report_div(const char *what, unsigned int detail_a, unsigned int detail_b)
{
  st.divergences++;
  if (div_reported < MAX_DIV_REPORT)
  {
    printf("DIVERGE @%06X %-10s %s: predicted=%08X actual=%08X\n",
           pend_addr, pend_mnem, what, detail_a, detail_b);
    div_reported++;
  }
}

static void check_pending(unsigned int now_pc)
{
  int i;
  if (pred.pc != (now_pc & 0xFFFFFF))
  {
    /* interrupt entry looks like a wrong PC prediction — detect and skip */
    if ((now_pc & 0xFFFFFF) == 0x0076B2) { st.int_skip++; return; }
    report_div("pc", pred.pc, now_pc & 0xFFFFFF);
    return;
  }
  for (i = 0; i < 8; i++)
  {
    if (pred.d[i] != IN_D(i)) { report_div("dreg", pred.d[i], IN_D(i)); return; }
    if (pred.a[i] != IN_A(i)) { report_div("areg", pred.a[i], IN_A(i)); return; }
  }
  if (pred.nf != IN_N() || pred.zf != IN_Z() || pred.vf != IN_V() ||
      pred.cf != IN_C() || pred.xf != IN_X())
  {
    unsigned int p = (pred.xf << 4) | (pred.nf << 3) | (pred.zf << 2) | (pred.vf << 1) | pred.cf;
    unsigned int a = (IN_X() << 4) | (IN_N() << 3) | (IN_Z() << 2) | (IN_V() << 1) | IN_C();
    report_div("flags", p, a);
    return;
  }
  for (i = 0; i < pred.nw; i++)
  {
    uint32_t addr = pred.wlog[i].addr, sz = pred.wlog[i].sz, j, actual = 0;
    for (j = 0; j < sz; j++) actual = (actual << 8) | rc_real_read8(addr + j);
    if (actual != pred.wlog[i].val) { report_div("mem", pred.wlog[i].val, actual); return; }
  }
  st.verified++;
  if (hit && pend_entry >= 0) hit[pend_entry] = 1;
}

void rc_validate_step(unsigned int pc)
{
  if (!enabled) return;
  if (pending) { check_pending(pc); pending = 0; }

  const rc_entry *e = lookup(pc & 0xFFFFFF);
  st.executed++;
  if (e && prof) prof[e - rc_table]++;
  if (!e || !e->fn)
  {
    st.unimpl++;
    if (e) e_unimpl_count(e);
    else
    {
      st.unknown_pc++;
      if ((pc & 0xFF0000) == 0xFF0000 || pc >= 0xE00000) st.unk_ram++;
      else if ((pc & 0xFFFFFF) >= 0xFF000 && (pc & 0xFFFFFF) < 0xFFB40)
        st.unk_checksum++;
      else
      {
        int i;
        unsigned int page = pc & 0xFFFFFF;
        for (i = 0; i < n_upage; i++) if (upages[i].page == page) { upages[i].n++; break; }
        if (i == n_upage && n_upage < 32) { upages[n_upage].page = page; upages[n_upage].n = 1; n_upage++; }
      }
    }
    return;
  }

  int i;
  memset(&pred, 0, sizeof(pred));
  for (i = 0; i < 8; i++) { pred.d[i] = IN_D(i); pred.a[i] = IN_A(i); }
  pred.xf = IN_X(); pred.nf = IN_N(); pred.zf = IN_Z();
  pred.vf = IN_V(); pred.cf = IN_C();
  pred.sr_high = m68k_get_reg(M68K_REG_SR) & 0xFFE0;
  pred.usp = m68k_get_reg(M68K_REG_USP);
  e->fn(&pred);
  if (pred.unpred) { st.unpred++; return; }
  st.predicted++;
  pend_addr = pc & 0xFFFFFF;
  pend_entry = (int)(e - rc_table);
  pend_mnem = e->m;
  pending = 1;
}

void rc_validate_enable(int on)
{
  enabled = on;
  if (on && !hit) hit = calloc(rc_table_n, 1);
  if (on && !prof) prof = calloc(rc_table_n, sizeof(unsigned long));
}

void rc_validate_report(void)
{
  printf("validate: executed=%lu predicted=%lu verified=%lu divergences=%lu\n"
         "          unimplemented=%lu unpredictable=%lu interrupt-skips=%lu\n",
         st.executed, st.predicted, st.verified, st.divergences,
         st.unimpl, st.unpred, st.int_skip);
  if (st.predicted)
    printf("          coverage=%.2f%% of executed, accuracy=%.4f%%\n",
           100.0 * st.predicted / st.executed,
           st.verified + st.divergences ?
             100.0 * st.verified / (st.verified + st.divergences) : 0.0);
  if (n_un)
  {
    int i, j;
    printf("          top unimplemented (dynamic):\n");
    for (i = 0; i < 12; i++)
    {
      int best = -1;
      for (j = 0; j < n_un; j++)
        if (un[j].n && (best < 0 || un[j].n > un[best].n)) best = j;
      if (best < 0) break;
      printf("            %06X %-10s x%lu\n", un[best].e->addr, un[best].e->m, un[best].n);
      un[best].n = 0;
    }
  }
  if (prof && profout)
  {
    FILE *f = fopen(profout, "w");
    if (f)
    {
      int i;
      for (i = 0; i < rc_table_n; i++)
        if (prof[i]) fprintf(f, "%06X %lu\n", rc_table[i].addr, prof[i]);
      fclose(f);
      printf("          profile -> %s\n", profout);
    }
  }
  if (st.unknown_pc)
  {
    int i;
    printf("          executed PCs not in table: %lu (RAM: %lu, checksum routine: %lu)\n",
           st.unknown_pc, st.unk_ram, st.unk_checksum);
    for (i = 0; i < n_upage; i++)
      printf("            stray PC %06X x%lu\n", upages[i].page, upages[i].n);
  }
  if (hit)
  {
    int i;
    long u = 0;
    for (i = 0; i < rc_table_n; i++) u += hit[i];
    printf("          unique instructions verified: %ld / %d (%.2f%%)\n",
           u, rc_table_n, 100.0 * u / rc_table_n);
    if (covout)
    {
      FILE *f = fopen(covout, "w");
      if (f)
      {
        for (i = 0; i < rc_table_n; i++)
          if (hit[i]) fprintf(f, "%06X\n", rc_table[i].addr);
        fclose(f);
        printf("          coverage list -> %s\n", covout);
      }
    }
  }
}
