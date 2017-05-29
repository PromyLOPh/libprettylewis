#pragma once

#include <xmc_scu.h>

#define arraysize(a) (sizeof (a)/sizeof (*a))
#define bitIsSet(val, pos) (((val) >> (pos)) & 0x1)
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

/* busy-loop delay CPU by delay μs.
 * loops = delay*10^-6*freq = delay*(freq/10^6)
 */
inline static void delayus (const uint32_t delay) {
	const uint32_t freq = XMC_SCU_CLOCK_GetCpuClockFrequency ()/1000000;
	const uint32_t loops = delay*freq;
	for (volatile uint32_t i = 0; i < loops; i++);
}
