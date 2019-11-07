/*
 * context switch implementation.
 * @author Andrew Morton, 2018
 */
#include "context.h"

__asm uint32_t storeContext(void) {
	MRS		R0,PSP
	STMFD	R0!,{R4-R11}
	BX		LR
}

__asm void restoreContext(uint32_t sp) {
	LDMFD	R0!,{R4-R11}
	MSR		PSP,R0
	BX		LR
}
