/*
 * Headless harness frontend for Genesis Plus GX.
 * Loads a ROM, runs N frames with scripted controller input, dumps
 * screenshots (PPM) at chosen frames, and prints RAM bytes/words at
 * chosen 68k addresses on exit. Exit code 0 on success.
 *
 * Usage:
 *   harness ROM --frames N [--shot f1,f2,...] [--out DIR]
 *               [--ram addr1,addr2,...]           (hex 68k addrs, e.g. FFC6DA)
 *               [--script FILE]                   (lines: start end BUTTONS)
 *               [--sram FILE]                     (load/save SRAM here)
 * BUTTONS: string of U D L R A B C S (e.g. "S" = Start held start..end frames)
 */
#include "shared.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "hooks.h"
#include "lift.h"
#include <stdint.h>

int debug_on = 0;
int log_error = 0;

/* NTSC filter instances (owned by frontend, unused headless) */
md_ntsc_t *md_ntsc = NULL;
sms_ntsc_t *sms_ntsc = NULL;

static uint8 framebuf[720 * 576 * 2];
static int16 soundbuf[8192];

typedef struct { int start, end; uint16 mask; } input_ev;
static input_ev evs[512];
static int n_evs = 0;
static int cur_frame = 0;

/* replay: (frame, mask) deltas recorded by the app; overrides --script */
typedef struct { int frame; uint16 mask; } replay_ev;
static replay_ev rev[65536];
static int n_rev = 0;

static uint16 buttons_mask(const char *s)
{
  uint16 m = 0;
  for (; *s; s++)
  {
    switch (*s)
    {
      case 'U': m |= INPUT_UP; break;
      case 'D': m |= INPUT_DOWN; break;
      case 'L': m |= INPUT_LEFT; break;
      case 'R': m |= INPUT_RIGHT; break;
      case 'A': m |= INPUT_A; break;
      case 'B': m |= INPUT_B; break;
      case 'C': m |= INPUT_C; break;
      case 'S': m |= INPUT_START; break;
      default: break;
    }
  }
  return m;
}

int harness_input_update(void)
{
  int i;
  uint16 m = 0;
  if (n_rev)
  {
    for (i = 0; i < n_rev && rev[i].frame <= cur_frame; i++)
      m = rev[i].mask;
    input.pad[0] = m;
    return 1;
  }
  for (i = 0; i < n_evs; i++)
  {
    if (cur_frame >= evs[i].start && cur_frame < evs[i].end)
      m |= evs[i].mask;
  }
  input.pad[0] = m;
  return 1;
}

/* 68k RAM byte at address (work_ram is byteswapped on LSB_FIRST builds) */
static uint8 ram_byte(uint32 addr)
{
#ifdef LSB_FIRST
  return work_ram[(addr & 0xFFFF) ^ 1];
#else
  return work_ram[addr & 0xFFFF];
#endif
}

static void dump_ppm(const char *dir, int frame)
{
  char path[512];
  int x, y;
  int w = bitmap.viewport.w, h = bitmap.viewport.h;
  snprintf(path, sizeof(path), "%s/frame%06d.ppm", dir, frame);
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "harness: cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (y = 0; y < h; y++)
  {
    uint16 *line = (uint16 *)(bitmap.data + (y * bitmap.pitch));
    for (x = 0; x < w; x++)
    {
      uint16 p = line[x];                    /* RGB565 */
      uint8 rgb[3];
      rgb[0] = ((p >> 11) & 0x1F) << 3;
      rgb[1] = ((p >> 5) & 0x3F) << 2;
      rgb[2] = (p & 0x1F) << 3;
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
  printf("shot: %s (%dx%d)\n", path, w, h);
}

int main(int argc, char **argv)
{
  const char *rom = NULL, *outdir = ".", *script = NULL, *sramfile = NULL;
  char shots[1024] = "", rams[1024] = "";
  int frames = 600, i, log_hooks = 0, ramhash_every = 0, ramdump_frame = 0;
  struct { unsigned int addr; unsigned char data[64]; int len; } cli_patches[32];
  int n_cli_patches = 0;

  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--log-hooks")) log_hooks = 1;
    else if (!strcmp(argv[i], "--validate")) rc_validate_enable(1);
    else if (!strcmp(argv[i], "--covout") && i + 1 < argc) rc_validate_covout(argv[++i]);
    else if (!strcmp(argv[i], "--profout") && i + 1 < argc) rc_validate_profout(argv[++i]);
    else if (!strcmp(argv[i], "--native")) rc_native_enable(1);
    else if (!strcmp(argv[i], "--lift-verify")) { rc_native_enable(1); lift_set_mode(1); }
    else if (!strcmp(argv[i], "--lift")) { rc_native_enable(1); lift_set_mode(2); }
    else if (!strcmp(argv[i], "--ramhash") && i + 1 < argc) ramhash_every = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--ramdump") && i + 1 < argc) ramdump_frame = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--patch") && i + 1 < argc)
    {
      /* --patch ADDR:HEXBYTES  (applied post-checksum) */
      char *arg = argv[++i], *colon = strchr(arg, ':');
      if (colon && n_cli_patches < 32)
      {
        *colon = 0;
        cli_patches[n_cli_patches].addr = (unsigned int)strtoul(arg, NULL, 16);
        int len = 0;
        for (char *p = colon + 1; p[0] && p[1] && len < 64; p += 2)
        {
          char b[3] = {p[0], p[1], 0};
          cli_patches[n_cli_patches].data[len++] = (unsigned char)strtoul(b, NULL, 16);
        }
        cli_patches[n_cli_patches].len = len;
        n_cli_patches++;
      }
    }
    else if (!strcmp(argv[i], "--replay") && i + 1 < argc)
    {
      const char *rf = argv[++i];
      FILE *f = fopen(rf, "r");
      if (!f) { fprintf(stderr, "harness: cannot read %s\n", rf); return 2; }
      int fr; unsigned int mk;
      while (n_rev < 65536 && fscanf(f, "%d %x", &fr, &mk) == 2)
      {
        rev[n_rev].frame = fr;
        rev[n_rev].mask = (uint16)mk;
        n_rev++;
      }
      fclose(f);
      /* SRAM sidecar for deterministic start state */
      if (!sramfile)
      {
        static char sidecar[512];
        snprintf(sidecar, sizeof(sidecar), "%s.srm", rf);
        FILE *s = fopen(sidecar, "rb");
        if (s) { fclose(s); sramfile = sidecar; }
      }
      printf("replay: %d input changes\n", n_rev);
    }
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) snprintf(shots, sizeof(shots), "%s", argv[++i]);
    else if (!strcmp(argv[i], "--ram") && i + 1 < argc) snprintf(rams, sizeof(rams), "%s", argv[++i]);
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
    else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--sram") && i + 1 < argc) sramfile = argv[++i];
    else if (!rom) rom = argv[i];
  }
  if (!rom)
  {
    fprintf(stderr, "usage: harness ROM [--frames N] [--shot f1,f2] [--ram a1,a2] [--script FILE] [--sram FILE] [--out DIR]\n");
    return 2;
  }

  if (script)
  {
    FILE *f = fopen(script, "r");
    if (!f) { fprintf(stderr, "harness: cannot read %s\n", script); return 2; }
    char btn[64];
    int s, e;
    while (n_evs < 512 && fscanf(f, "%d %d %63s", &s, &e, btn) == 3)
    {
      evs[n_evs].start = s;
      evs[n_evs].end = e;
      evs[n_evs].mask = buttons_mask(btn);
      n_evs++;
    }
    fclose(f);
    printf("script: %d input events\n", n_evs);
  }

  set_config_defaults();

  memset(&bitmap, 0, sizeof(bitmap));
  bitmap.width = 720;
  bitmap.height = 576;
  bitmap.pitch = 720 * 2;
  bitmap.data = framebuf;
  bitmap.viewport.changed = 3;

  if (!load_rom((char *)rom))
  {
    fprintf(stderr, "harness: failed to load ROM %s\n", rom);
    return 1;
  }
  printf("rom: %s [%s]\n", rominfo.international, rominfo.product);

  audio_init(SOUND_FREQUENCY, 0);
  system_init();

  hooks_init();
  if (log_hooks)
  {
    hooks_add_log(SYM_SRAM_InitAndLoadSave, "SRAM_InitAndLoadSave", 10);
    hooks_add_log(SYM_SRAM_FormatSave, "SRAM_FormatSave", 10);
    hooks_add_log(SYM_SRAM_RecalcChecksum, "SRAM_RecalcChecksum", 10);
    hooks_add_log(SYM_Menu_DrawOptionValue, "Menu_DrawOptionValue", 5);
    hooks_add_log(SYM_VBlankInt, "VBlankInt", 3);
  }
  for (i = 0; i < n_cli_patches; i++)
    hooks_rom_patch(cli_patches[i].addr, cli_patches[i].data, cli_patches[i].len);

  if (sram.on && sramfile)
  {
    FILE *f = fopen(sramfile, "rb");
    if (f)
    {
      if (fread(sram.sram, 0x10000, 1, f) != 1) { /* partial ok */ }
      fclose(f);
      printf("sram: loaded %s\n", sramfile);
    }
  }

  system_reset();

  for (cur_frame = 0; cur_frame < frames; cur_frame++)
  {
    harness_input_update();
    system_frame_gen(0);
    audio_update(soundbuf);

    if (ramhash_every && (cur_frame % ramhash_every) == 0)
      printf("ramhash %06d %08lX\n", cur_frame,
             (unsigned long)crc32(0, work_ram, 0x10000));

    if (ramdump_frame && cur_frame == ramdump_frame)
    {
      char path[1024];
      snprintf(path, sizeof(path), "%s/ramdump%06d.bin", outdir, cur_frame);
      FILE *rf = fopen(path, "wb");
      if (rf)
      {
        int k;
        for (k = 0; k < 0x10000; k++) fputc(ram_byte(0xFF0000 + k), rf);
        fclose(rf);
        printf("ramdump: %s\n", path);
      }
    }

    /* dump screenshot if requested for this frame */
    if (shots[0])
    {
      char want[16];
      snprintf(want, sizeof(want), "%d", cur_frame);
      char *tok, tmp[1024];
      snprintf(tmp, sizeof(tmp), "%s", shots);
      for (tok = strtok(tmp, ","); tok; tok = strtok(NULL, ","))
        if (!strcmp(tok, want)) dump_ppm(outdir, cur_frame);
    }
  }

  /* RAM report */
  if (rams[0])
  {
    char *tok, tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", rams);
    for (tok = strtok(tmp, ","); tok; tok = strtok(NULL, ","))
    {
      uint32 a = (uint32)strtoul(tok, NULL, 16);
      printf("ram: %06X byte=%02X word=%02X%02X\n",
             a, ram_byte(a), ram_byte(a), ram_byte(a + 1));
    }
  }

  if (sram.on && sramfile)
  {
    FILE *f = fopen(sramfile, "wb");
    if (f)
    {
      fwrite(sram.sram, 0x10000, 1, f);
      fclose(f);
      printf("sram: saved %s\n", sramfile);
    }
  }

  rc_validate_report();
  rc_native_report();
  lift_report();
  printf("done: %d frames\n", frames);
  return 0;
}
