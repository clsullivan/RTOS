/*
 * context switch header file
 * @author Andrew Morton, 2018
 */
#ifndef __context_h
#define __context_h

#include <stdint.h>

uint32_t storeContext(void);
void restoreContext(uint32_t sp);

#endif
