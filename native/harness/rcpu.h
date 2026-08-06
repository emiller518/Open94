/* Recompiled-CPU runtime: state struct + memory access for generated
 * semantic functions (gen_insns_*.c). Reads come from live emulator memory;
 * writes go to a log so the validator can compare against the interpreter. */
#ifndef _RCPU_H_
#define _RCPU_H_

#include <stdint.h>

#define RC_WLOG_MAX 40

typedef struct {
  uint32_t d[8], a[8];
  uint32_t pc;                    /* predicted next PC */
  uint32_t sr_high;               /* SR bits 15..5 from interpreter (T/S/int mask) */
  uint32_t usp;                   /* user stack pointer from interpreter */
  uint8_t xf, nf, zf, vf, cf;
  struct { uint32_t addr; uint32_t val; uint8_t sz; } wlog[RC_WLOG_MAX];
  int nw;
  int unpred;                     /* touched hw / unmodeled: skip validation */
} rcpu_t;

#define MASK(v, sz) ((sz) == 4 ? (uint32_t)(v) : ((uint32_t)(v) & ((1u << ((sz) * 8)) - 1u)))
#define GETR(r, sz) MASK((r), (sz))
#define SETR(r, sz, v) ((r) = (sz) == 4 ? (uint32_t)(v) : (((r) & ~((1u << ((sz) * 8)) - 1u)) | MASK((v), (sz))))
#define SEXT8(v)  ((int32_t)(int8_t)(v))
#define SEXT16(v) ((int32_t)(int16_t)(v))
#define SEXT32(v) ((int32_t)(v))
#define SETNZ(c, v, sz) do { (c)->nf = (MASK((v), (sz)) >> ((sz) * 8 - 1)) & 1; \
                             (c)->zf = (MASK((v), (sz)) == 0); } while (0)

uint32_t rc_read8(rcpu_t *c, uint32_t addr);
uint32_t rc_read16(rcpu_t *c, uint32_t addr);
uint32_t rc_read32(rcpu_t *c, uint32_t addr);
void rc_write8(rcpu_t *c, uint32_t addr, uint32_t v);
void rc_write16(rcpu_t *c, uint32_t addr, uint32_t v);
void rc_write32(rcpu_t *c, uint32_t addr, uint32_t v);

/* validator entry, called from the hook dispatcher on every executed PC */
void rc_validate_step(unsigned int pc);
void rc_validate_enable(int on);
void rc_validate_report(void);
void rc_validate_covout(const char *path);
void rc_validate_profout(const char *path);

/* phase-2 native dispatcher (rc_native.c) */
void rc_native_enable(int on);
void rc_native_report(void);

/* raw memory read (ROM/RAM/SRAM), shared with lift.c */
uint32_t rc_real_read8(uint32_t addr);

#endif
