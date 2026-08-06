/*
 * NHL Hockey '94 — native windowed shell (SDL2) around Genesis Plus GX.
 *
 * Usage: nhl94 ROM [--sram FILE]
 * Controls: arrows = d-pad, Z/X/C = A/B/C, Enter = Start, Esc = quit.
 * First connected game controller is used automatically
 * (d-pad/left stick, A/B/X = A/B/C, Start = Start).
 */
#include "shared.h"
#include "md_ntsc.h"
#include "sms_ntsc.h"
#include "hooks.h"
#include <SDL.h>

int debug_on = 0;
int log_error = 0;
md_ntsc_t *md_ntsc = NULL;
sms_ntsc_t *sms_ntsc = NULL;

static uint8 framebuf[720 * 576 * 2];
static int16 soundbuf[8192];
static SDL_GameController *pad = NULL;

int harness_input_update(void)
{
  const Uint8 *k = SDL_GetKeyboardState(NULL);
  uint16 m = 0;
  if (k[SDL_SCANCODE_UP]) m |= INPUT_UP;
  if (k[SDL_SCANCODE_DOWN]) m |= INPUT_DOWN;
  if (k[SDL_SCANCODE_LEFT]) m |= INPUT_LEFT;
  if (k[SDL_SCANCODE_RIGHT]) m |= INPUT_RIGHT;
  if (k[SDL_SCANCODE_Z]) m |= INPUT_A;
  if (k[SDL_SCANCODE_X]) m |= INPUT_B;
  if (k[SDL_SCANCODE_C]) m |= INPUT_C;
  if (k[SDL_SCANCODE_RETURN]) m |= INPUT_START;
  if (pad)
  {
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= INPUT_UP;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= INPUT_DOWN;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= INPUT_LEFT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= INPUT_RIGHT;
    Sint16 ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -12000) m |= INPUT_LEFT;
    if (ax > 12000) m |= INPUT_RIGHT;
    if (ay < -12000) m |= INPUT_UP;
    if (ay > 12000) m |= INPUT_DOWN;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) m |= INPUT_A;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) m |= INPUT_B;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) m |= INPUT_C;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) m |= INPUT_START;
  }
  input.pad[0] = m;
  return 1;
}

int main(int argc, char **argv)
{
  const char *rom = NULL, *sramfile = "nhl94.srm", *recfile = NULL;
  FILE *rec = NULL;
  int i;
  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--sram") && i + 1 < argc) sramfile = argv[++i];
    else if (!strcmp(argv[i], "--record") && i + 1 < argc) recfile = argv[++i];
    else if (!rom) rom = argv[i];
  }
  if (!rom)
  {
    fprintf(stderr, "usage: nhl94 ROM [--sram FILE]\n");
    return 2;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
  {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  for (i = 0; i < SDL_NumJoysticks(); i++)
  {
    if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
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
    fprintf(stderr, "failed to load ROM %s\n", rom);
    return 1;
  }

  audio_init(48000, 0);
  system_init();
  hooks_init();

  if (sram.on)
  {
    FILE *f = fopen(sramfile, "rb");
    if (f)
    {
      if (fread(sram.sram, 0x10000, 1, f) != 1) { /* partial ok */ }
      fclose(f);
    }
  }

  if (recfile)
  {
    /* input log + SRAM snapshot sidecar so replays start from identical state */
    rec = fopen(recfile, "w");
    char sidecar[512];
    snprintf(sidecar, sizeof(sidecar), "%s.srm", recfile);
    FILE *f = fopen(sidecar, "wb");
    if (f) { fwrite(sram.sram, 0x10000, 1, f); fclose(f); }
  }

  system_reset();

  SDL_Window *win = SDL_CreateWindow("NHL Hockey '94 — native",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 448,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
      SDL_TEXTUREACCESS_STREAMING, 720, 576);

  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  SDL_PauseAudioDevice(adev, 0);

  int running = 1;
  int frame = 0;
  unsigned int last_mask = 0xFFFFFFFF;
  SDL_Rect src = {0, 0, 256, 224};
  while (running)
  {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
      if (ev.type == SDL_QUIT) running = 0;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
      if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad)
        pad = SDL_GameControllerOpen(ev.cdevice.which);
    }

    harness_input_update();
    if (rec && input.pad[0] != last_mask)
    {
      fprintf(rec, "%d %04X\n", frame, input.pad[0]);
      last_mask = input.pad[0];
    }
    system_frame_gen(0);
    frame++;

    int n = audio_update(soundbuf);
    SDL_QueueAudio(adev, soundbuf, n * 4);

    if (bitmap.viewport.changed & 1)
    {
      bitmap.viewport.changed &= ~1;
      src.w = bitmap.viewport.w;
      src.h = bitmap.viewport.h;
      SDL_RenderSetLogicalSize(ren, src.w, src.h);
    }
    SDL_UpdateTexture(tex, NULL, bitmap.data, bitmap.pitch);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, &src, NULL);
    SDL_RenderPresent(ren);

    /* pace emulation to the audio clock (~4 frames of queued audio) */
    while (SDL_GetQueuedAudioSize(adev) > 48000 / 60 * 4 * 4)
      SDL_Delay(1);
  }

  if (rec) fclose(rec);
  if (sram.on)
  {
    FILE *f = fopen(sramfile, "wb");
    if (f) { fwrite(sram.sram, 0x10000, 1, f); fclose(f); }
  }
  SDL_Quit();
  return 0;
}
