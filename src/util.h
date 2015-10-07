#pragma once

#define arraysize(a) (sizeof (a)/sizeof (*a))
#define bitIsSet(val, pos) (((val) >> (pos)) & 0x1)
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

__attribute__((unused)) static void delay(uint32_t cycles)
{
  volatile uint32_t i;

  for(i = 0UL; i < cycles ;++i)
  {
    __NOP();
  }
}

