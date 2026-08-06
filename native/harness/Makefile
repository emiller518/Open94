# Headless harness build for Genesis Plus GX (adapted from Makefile.sdl2, SDL removed)
NAME      = harness
CC        = gcc
CFLAGS    = -O2 -fomit-frame-pointer -Wall -Wno-strict-aliasing -std=c99 -fcommon
DEFINES   = -DLSB_FIRST -DUSE_16BPP_RENDERING -DMAXROMSIZE=33554432 \
            -DHAVE_YM3438_CORE -DHAVE_OPLL_CORE -DHOOK_CPU -DRC_NATIVE -DHAVE_ALLOCA_H

GPGX      = ../vendor/Genesis-Plus-GX
SRCDIR    = $(GPGX)/core
SDLDIR    = $(GPGX)/sdl
INCLUDES  = -I. -I$(SRCDIR) -I$(SRCDIR)/z80 -I$(SRCDIR)/m68k -I$(SRCDIR)/sound \
            -I$(SRCDIR)/sound/minimp3 \
            -I$(SRCDIR)/input_hw -I$(SRCDIR)/cart_hw -I$(SRCDIR)/cart_hw/svp \
            -I$(SRCDIR)/cd_hw -I$(SRCDIR)/ntsc -I$(SRCDIR)/debug -I$(SDLDIR)
LIBS      = -lz -lm

OBJDIR = ./build

OBJECTS = $(OBJDIR)/z80.o \
          $(OBJDIR)/m68kcpu.o $(OBJDIR)/s68kcpu.o \
          $(OBJDIR)/genesis.o $(OBJDIR)/vdp_ctrl.o $(OBJDIR)/vdp_render.o \
          $(OBJDIR)/system.o $(OBJDIR)/io_ctrl.o $(OBJDIR)/mem68k.o \
          $(OBJDIR)/memz80.o $(OBJDIR)/membnk.o $(OBJDIR)/state.o \
          $(OBJDIR)/loadrom.o \
          $(OBJDIR)/input.o $(OBJDIR)/gamepad.o $(OBJDIR)/lightgun.o \
          $(OBJDIR)/mouse.o $(OBJDIR)/activator.o $(OBJDIR)/xe_1ap.o \
          $(OBJDIR)/teamplayer.o $(OBJDIR)/paddle.o $(OBJDIR)/smash.o \
          $(OBJDIR)/sportspad.o $(OBJDIR)/terebi_oekaki.o $(OBJDIR)/graphic_board.o \
          $(OBJDIR)/sound.o $(OBJDIR)/psg.o $(OBJDIR)/ym2413.o $(OBJDIR)/opll.o \
          $(OBJDIR)/ym3438.o $(OBJDIR)/ym2612.o $(OBJDIR)/blip_buf.o $(OBJDIR)/eq.o \
          $(OBJDIR)/sram.o $(OBJDIR)/svp.o $(OBJDIR)/ssp16.o \
          $(OBJDIR)/ggenie.o $(OBJDIR)/areplay.o $(OBJDIR)/eeprom_93c.o \
          $(OBJDIR)/eeprom_i2c.o $(OBJDIR)/eeprom_spi.o $(OBJDIR)/flash_cfi.o \
          $(OBJDIR)/yx5200.o $(OBJDIR)/md_cart.o $(OBJDIR)/sms_cart.o $(OBJDIR)/megasd.o \
          $(OBJDIR)/scd.o $(OBJDIR)/cdd.o $(OBJDIR)/cdc.o $(OBJDIR)/gfx.o \
          $(OBJDIR)/pcm.o $(OBJDIR)/cd_cart.o \
          $(OBJDIR)/sms_ntsc.o $(OBJDIR)/md_ntsc.o \
          $(OBJDIR)/cpuhook.o $(OBJDIR)/hooks.o $(OBJDIR)/validate.o $(OBJDIR)/rc_native.o \
          $(OBJDIR)/lift.o $(OBJDIR)/dc_save.o $(OBJDIR)/dc_render.o $(OBJDIR)/dc_anim.o $(OBJDIR)/dc_overlay.o $(OBJDIR)/dc_game.o $(OBJDIR)/dc_controls.o $(OBJDIR)/dc_math.o $(OBJDIR)/dc_vdp.o $(OBJDIR)/dc_registry.o \
          $(OBJDIR)/gen_insns_0.o $(OBJDIR)/gen_insns_1.o \
          $(OBJDIR)/gen_insns_2.o $(OBJDIR)/gen_insns_3.o $(OBJDIR)/gen_table.o \
          $(OBJDIR)/config.o $(OBJDIR)/error.o $(OBJDIR)/unzip.o $(OBJDIR)/fileio.o \
          $(OBJDIR)/main.o

all: $(NAME)

$(NAME): $(OBJDIR) $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

$(OBJDIR):
	@[ -d $@ ] || mkdir -p $@

$(OBJDIR)/main.o: main.c main.h osd.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SDLDIR)/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/sound/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/input_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/cart_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/cart_hw/svp/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/cd_hw/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/z80/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/m68k/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/ntsc/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/debug/%.c
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

clean:
	rm -rf $(OBJDIR) $(NAME)

$(OBJDIR)/hooks.o: hooks.c hooks.h symbols.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/validate.o: validate.c rcpu.h gen_insns.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/gen_%.o: gen_%.c gen_insns.h rcpu.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/rc_native.o: rc_native.c rcpu.h gen_insns.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/lift.o: lift.c lift.h rcpu.h gen_insns.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@

$(OBJDIR)/dc_%.o: ../decomp/%.c ../harness/lift.h
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) $< -o $@
