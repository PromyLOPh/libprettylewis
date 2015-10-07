PREFIX=install
LIBDIR=$(PREFIX)/lib
INCDIR=$(PREFIX)/include

#### Setup ####
XMCLIB = /home/lbraun/projekte/treufunk/software/xmclib
SRC = $(wildcard src/*.c)
UC = XMC4500
UC_TYPE = F100
UC_MEM = 1024
TOOLCHAIN = arm-none-eabi
CPU = cortex-m4
FPU = fpv4-sp-d16
FABI = hard

CMSIS = $(XMCLIB)/CMSIS/
CMSIS_SRC = $(CMSIS)/Infineon/$(UC)_series/Source/
LINKER_FILE = $(CMSIS_SRC)/GCC/$(UC)x$(UC_MEM).ld

CC   = $(TOOLCHAIN)-gcc
AR   = $(TOOLCHAIN)-ar

CFLAGS = -mthumb -mcpu=$(CPU) -mfpu=$(FPU) -mfloat-abi=$(FABI)
CFLAGS+= -O0 -ffunction-sections -fdata-sections
CFLAGS+= -MD -std=c11 -Wall -Werror -fstrict-aliasing -fms-extensions
CFLAGS+= -DXMC_ASSERT_ENABLE -DXMC_USER_ASSERT_FUNCTION
CFLAGS+= -g3 -fmessage-length=0 -I$(CMSIS)/Include
CFLAGS+= -I$(CMSIS)/Infineon/Include
CFLAGS+= -I$(CMSIS)/Infineon/$(UC)_series/Include
CFLAGS+= -I$(XMCLIB)/XMClib/inc/
#CFLAGS+= -nostdlib -D__SKIP_LIBC_INIT_ARRAY
# define uc type for xmclib
CFLAGS+= -D$(UC)_$(UC_TYPE)x$(UC_MEM)
LFLAGS = -L$(CMSIS)/Lib/GCC -Wl,--gc-sections
CPFLAGS = -Obinary
ODFLAGS = -S

OBJS  = $(SRC:.c=.o)
ARCHIVE = src/libprettylewis.a

#### Rules ####
all: $(ARCHIVE)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

src/libprettylewis.a: $(OBJS)
	$(AR) rcs $(ARCHIVE) $(OBJS)

.PHONY: clean install

install:
	install -d $(LIBDIR)
	install -m 644 $(ARCHIVE) $(LIBDIR)
	install -d $(INCDIR)
	install -m 644 src/tda5340.h src/tda5340_reg.h $(INCDIR)

clean:
	rm -f $(OBJS) $(ARCHIVE)

