#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

void
sys_yield(void) {
	yield();
}
