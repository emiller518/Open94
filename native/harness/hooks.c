/*
 * Address-hook layer over Genesis Plus GX's cpu_hook API.
 * Exec hooks fire on instruction fetch at registered PCs. ROM patches are
 * queued and applied the first time RunGame executes — after the game's
 * boot checksum has passed, so patched bytes never trip it.
 */
#include "hooks.h"
#include "cpuhook.h"
#include "rcpu.h"
#include "m68k.h"

#define MAX_HOOKS 64
#define MAX_PATCH 128

typedef struct { unsigned int addr; nhl_hook_fn fn; const char *name; int log_left; } hook_ent;
static hook_ent hooks[MAX_HOOKS];
static int n_hooks = 0;

typedef struct { unsigned int addr; unsigned char data[64]; int len; } patch_ent;
static patch_ent patches[MAX_PATCH];
static int n_patches = 0;
static int patches_applied = 0;

/* ROM bytes are stored 16-bit byteswapped on little-endian builds */
static void rom_write_byte(unsigned int addr, unsigned char v)
{
#ifdef LSB_FIRST
  cart.rom[(addr & 0xFFFFFF) ^ 1] = v;
#else
  cart.rom[addr & 0xFFFFFF] = v;
#endif
}

static void apply_patches(unsigned int pc)
{
  int i, j;
  if (patches_applied) return;
  patches_applied = 1;
  for (i = 0; i < n_patches; i++)
    for (j = 0; j < patches[i].len; j++)
      rom_write_byte(patches[i].addr + j, patches[i].data[j]);
  if (n_patches)
    printf("hooks: applied %d ROM patch(es) post-checksum at PC=%06X\n", n_patches, pc);
}

extern m68ki_cpu_core m68k;
static FILE *trace_f;
static int trace_init;

static void dispatcher(hook_type_t type, int width, unsigned int address, unsigned int value)
{
  int i;
  if (type != HOOK_M68K_E) return;
  if (!trace_init)
  {
    const char *t = getenv("RC_TRACE");
    if (t) trace_f = fopen(t, "w");
    trace_init = 1;
  }
  if (trace_f) fprintf(trace_f, "%X %d %d\n", address, m68k.cycles, m68k.refresh_cycles);
#ifdef RC_NATIVE
  rc_validate_step(address);   /* harness-only: validator + native + lift */
#endif
  for (i = 0; i < n_hooks; i++)
  {
    if (hooks[i].addr == address)
    {
      if (hooks[i].log_left != 0)
      {
        if (hooks[i].log_left > 0) hooks[i].log_left--;
        printf("hooks: %s (PC=%06X)\n", hooks[i].name ? hooks[i].name : "?", address);
      }
      if (hooks[i].fn) hooks[i].fn(address);
    }
  }
}

void hooks_init(void)
{
  set_cpu_hook(dispatcher);
  hooks_add_exec(SYM_RunGame, apply_patches, NULL);
}

void hooks_add_exec(unsigned int addr, nhl_hook_fn fn, const char *name)
{
  if (n_hooks >= MAX_HOOKS) return;
  hooks[n_hooks].addr = addr;
  hooks[n_hooks].fn = fn;
  hooks[n_hooks].name = name;
  hooks[n_hooks].log_left = name ? -1 : 0;
  n_hooks++;
}

void hooks_add_log(unsigned int addr, const char *name, int limit)
{
  if (n_hooks >= MAX_HOOKS) return;
  hooks[n_hooks].addr = addr;
  hooks[n_hooks].fn = NULL;
  hooks[n_hooks].name = name;
  hooks[n_hooks].log_left = limit;
  n_hooks++;
}

void hooks_rom_patch(unsigned int addr, const unsigned char *data, int len)
{
  if (n_patches >= MAX_PATCH || len > 64) return;
  patches[n_patches].addr = addr;
  memcpy(patches[n_patches].data, data, len);
  patches[n_patches].len = len;
  n_patches++;
}
