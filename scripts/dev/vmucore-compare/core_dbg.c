/* vmu_core with its insides visible to the comparison harness */
#include "vmu_core.c"
unsigned dbg_pc(const VmuCore *c) { return c->pc; }
const uint8_t *dbg_ram(const VmuCore *c) { return &c->ram[0][0]; }
const uint8_t *dbg_sfr(const VmuCore *c) { return c->sfr; }
const uint8_t *dbg_xram(const VmuCore *c) { return &c->xram[0][0]; }
int dbg_ext_rom(const VmuCore *c) { return c->ext_rom; }
void dbg_timers(const VmuCore *c, int *v) { v[0] = c->t0l; v[1] = c->t0h; v[2] = c->t1l; v[3] = c->t1h; v[4] = c->bt_counter; v[5] = c->int_req; }
