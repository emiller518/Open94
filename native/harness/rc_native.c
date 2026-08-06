/*
 * Phase 2: native execution dispatcher. Called from the patched interpreter
 * loop for every instruction; on success the generated semantic function has
 * updated all CPU + memory state and the interpreter skips the instruction,
 * charged the exact cycles it would itself have charged. Any doubt -> -1,
 * interpreter runs the instruction (per-instruction fallback).
 */
#include "shared.h"
#include "gen_insns.h"
#include "lift.h"
#include "m68k.h"

extern m68ki_cpu_core m68k;
extern unsigned int rc_cycles_for_opcode(unsigned int op);

static int enabled = 0;
static struct { unsigned long ran, fb_kind, fb_nofn, fb_unpred, fb_wmem, fb_notable; } st;

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

static int rc_cc_true(int cc, const rcpu_t *c)
{
  switch (cc)
  {
    case 0x2: return !c->cf && !c->zf;          /* hi */
    case 0x3: return c->cf || c->zf;            /* ls */
    case 0x4: return !c->cf;                    /* cc */
    case 0x5: return c->cf;                     /* cs */
    case 0x6: return !c->zf;                    /* ne */
    case 0x7: return c->zf;                     /* eq */
    case 0x8: return !c->vf;                    /* vc */
    case 0x9: return c->vf;                     /* vs */
    case 0xA: return !c->nf;                    /* pl */
    case 0xB: return c->nf;                     /* mi */
    case 0xC: return c->nf == c->vf;            /* ge */
    case 0xD: return c->nf != c->vf;            /* lt */
    case 0xE: return !c->zf && c->nf == c->vf;  /* gt */
    case 0xF: return c->zf || c->nf != c->vf;   /* le */
  }
  return 1;
}

int rc_native_try(unsigned int pc)
{
  int i, j;
  rcpu_t c;
  if (!enabled) return -1;
  pc &= 0xFFFFFF;

  /* semantic-lifted routines get first claim (verify or live) */
  {
    int lr = lift_step(pc);
    if (lr >= 0) return lr;
  }

  const rc_entry *e = lookup(pc);
  if (!e) { st.fb_notable++; return -1; }
  if (!e->fn) { st.fb_nofn++; return -1; }
  if (e->kind == 0) { st.fb_kind++; return -1; }

  /* bisection aid: RC_EXCL=prefix,prefix,... forces interpreter fallback */
  {
    static const char *excl = (const char *)-1;
    if (excl == (const char *)-1) excl = getenv("RC_EXCL");
    if (excl)
    {
      const char *p = excl;
      while (*p)
      {
        const char *q = p;
        while (*q && *q != ',') q++;
        if (!strncmp(e->m, p, q - p)) { st.fb_kind++; return -1; }
        p = *q ? q + 1 : q;
      }
    }
  }

  memset(&c, 0, sizeof(c));
  for (i = 0; i < 8; i++)
  {
    c.d[i] = m68k.dar[i];
    c.a[i] = m68k.dar[8 + i];
  }
  c.xf = (m68k.x_flag >> 8) & 1;
  c.nf = (m68k.n_flag >> 7) & 1;
  c.zf = (m68k.not_z_flag == 0);
  c.vf = (m68k.v_flag >> 7) & 1;
  c.cf = (m68k.c_flag >> 8) & 1;
  c.usp = m68k_get_reg(M68K_REG_USP);
  c.sr_high = m68k_get_reg(M68K_REG_SR) & 0xFFE0;

  /* bit-ops with register bit number (kind 7): GPGX charges 2 extra cycles
   * when the bit (mod 32, read before execution) is in the upper word */
  uint32_t pre_bit = 0;
  if (e->kind == 7)
    pre_bit = m68k.dar[(rom_word(e->addr) >> 9) & 7] & 0x1F;

  e->fn(&c);
  if (c.unpred) { st.fb_unpred++; return -1; }

  /* all writes must land in work RAM; anything else -> interpreter */
  for (i = 0; i < c.nw; i++)
  {
    uint32_t a = c.wlog[i].addr;
    if (!((a & 0xFFFFFF) >= 0xE00000)) { st.fb_wmem++; return -1; }
  }

  /* commit: memory */
  for (i = 0; i < c.nw; i++)
  {
    uint32_t a = c.wlog[i].addr, sz = c.wlog[i].sz, v = c.wlog[i].val;
    for (j = 0; j < (int)sz; j++)
      work_ram[((a + j) & 0xFFFF) ^ 1] = (v >> (8 * (sz - 1 - j))) & 0xFF;
  }
  /* commit: registers + flags + pc */
  for (i = 0; i < 8; i++)
  {
    m68k.dar[i] = c.d[i];
    m68k.dar[8 + i] = c.a[i];
  }
  m68k.x_flag = c.xf << 8;
  m68k.n_flag = c.nf << 7;
  m68k.not_z_flag = c.zf ? 0 : 1;
  m68k.v_flag = c.vf << 7;
  m68k.c_flag = c.cf << 8;
  m68k.pc = c.pc;

  /* cycles: replicate the interpreter loop's refresh delay, the handler's
   * charge ordering, and SKIP_BUS_REFRESH for long instructions (movem). */
  {
    uint32_t op = rom_word(e->addr);
    int base = (int)rc_cycles_for_opcode(op);
    uint32_t fall = e->addr + e->len;

    if (m68k.cycles >= m68k.refresh_cycles)
    {
      m68k.refresh_cycles = m68k.cycles + (128 * 7);
      m68k.cycles += (2 * 7);
    }
    /* a branch whose target IS the fall-through (b<cc> *+4) can't be
     * discriminated by c.pc — evaluate the condition from the flags
     * (bcc leaves CCR untouched, so post-exec flags are the ones tested) */
    int taken;
    switch (e->kind)
    {
      case 2:                                               /* bcc.s */
        taken = (e->addr + 2 + (int32_t)(int8_t)(op & 0xFF) == fall)
                ? rc_cc_true((op >> 8) & 0xF, &c) : (c.pc != fall);
        if (!taken) base += -14;                            /* not taken */
        break;
      case 3:                                               /* bcc.w */
        taken = (e->addr + 2 + (int32_t)(int16_t)rom_word(e->addr + 2) == fall)
                ? rc_cc_true((op >> 8) & 0xF, &c) : (c.pc != fall);
        if (taken == 0) base += 14;                         /* not taken */
        break;
      case 4:                                               /* dbcc */
        taken = (e->addr + 2 + (int32_t)(int16_t)rom_word(e->addr + 2) == fall)
                ? ((c.d[op & 7] & 0xFFFF) != 0xFFFF) : (c.pc != fall);
        if (taken) base += -14;                             /* branch taken */
        else if ((c.d[op & 7] & 0xFFFF) == 0xFFFF) base += 14; /* expired */
        break;                                              /* cc true: base */
      case 5:                                               /* scc on dreg */
        if ((c.d[op & 7] & 0xFF) == 0xFF) base += 14;
        break;
      case 6:                                               /* movem.l */
        m68k.cycles += e->extra;                            /* per-reg, charged first */
        if (m68k.cycles >= m68k.refresh_cycles)             /* SKIP_BUS_REFRESH */
          m68k.refresh_cycles += (128 * 7);
        break;
      case 7:                                               /* bset/bclr/bchg Dn,Dm */
        if (pre_bit >= 16) base += 14;
        break;
      default: base += e->extra; break;
    }
    m68k.cycles += base;
    st.ran++;
    return 0;
  }
}

void rc_native_enable(int on) { enabled = on; }

void rc_native_report(void)
{
  unsigned long total = st.ran + st.fb_kind + st.fb_nofn + st.fb_unpred +
                        st.fb_wmem + st.fb_notable;
  if (!total) return;
  printf("native:   ran=%lu (%.2f%%) fallbacks: excluded=%lu hw=%lu "
         "non-ram-write=%lu no-fn=%lu no-table=%lu\n",
         st.ran, 100.0 * st.ran / total, st.fb_kind, st.fb_unpred,
         st.fb_wmem, st.fb_nofn, st.fb_notable);
}
