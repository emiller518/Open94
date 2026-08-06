#ifndef _NHL94_HOOKS_H_
#define _NHL94_HOOKS_H_

#include "shared.h"
#include "symbols.h"

typedef void (*nhl_hook_fn)(unsigned int pc);

/* Install the CPU hook dispatcher (call once, after system_init). */
void hooks_init(void);

/* Run fn every time execution reaches addr. */
void hooks_add_exec(unsigned int addr, nhl_hook_fn fn, const char *name);

/* Log to stdout whenever execution reaches addr (first `limit` times). */
void hooks_add_log(unsigned int addr, const char *name, int limit);

/* Queue a ROM byte patch; applied when execution first reaches RunGame
 * (i.e. after the game's own checksum has passed). */
void hooks_rom_patch(unsigned int addr, const unsigned char *data, int len);

#endif
