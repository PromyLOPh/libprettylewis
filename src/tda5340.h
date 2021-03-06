/*
Copyright (c) 2015–2018 Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#include <xmc_usic.h>

#include "tda5340_reg.h"

typedef struct {
	uint16_t reg;
	uint8_t val;
} tdaConfigVal;

struct tda5340;
typedef void (*tda5340Callback) (struct tda5340 * const, void * const);

typedef struct tda5340 {
	/* configuration, fill before calling init */
	/* init fifo at frame start, see FSINITRXFIFO */
	bool fsInitFifo;
	/* spi baudrate */
	uint32_t baudrate;
	/* max retries for SPI register write */
	uint8_t retries;

	/* spi channel */
	XMC_USIC_CH_t *spi;

	/* callbacks */
	/* transmission error */
	tda5340Callback txerror,
	/* transmitter ready */
			txready,
	/* tx fifo almost empty */
			txae,
	/* tx fifo empty */
			txempty,
	/* receiver synchronized */
			rxfsync,
	/* receiver got end of message */
			rxeom,
	/* receive fifo almost full */
			rxaf;
	/* callback data */
	void *data;

	/* private data, do not touch */
	/* current mode, see TDA_CMC, written by isr */
	volatile uint8_t mode;
	/* transmission mode: with/without start bit */
	bool sendbit;
	/* current page, avoids setting it every time */
	uint8_t page;
	/* locking flag ensuring atomic SPI transactions, for debugging only */
	uint8_t lock;
} tda5340Ctx;

typedef uint16_t tda5340Address;

/* SPI commands */
#define TDA_WR 0x2 /* write to chip */
#define TDA_RD 0x3 /* read from chip */
#define TDA_RDF 0x4 /* read fifo */
#define TDA_WRF 0x6 /* write fifo */

/* CMC */
#define TDA_CMC_MSEL_OFF 0
#define TDA_CMC_MSEL_MSK 0x3
#define TDA_SLEEP_MODE 0
#define TDA_SELF_POLLING_MODE 1
#define TDA_RUN_MODE_SLAVE 2
#define TDA_TRANSMIT_MODE 3
#define TDA_RESET_MODE 4 /* not an actual mode */
#define TDA_CMC_MCS_OFF (2)
#define TDA_CONFIG_A (0)
#define TDA_CONFIG_B (1)
#define TDA_CONFIG_C (2)
#define TDA_CONFIG_D (3)
#define TDA_CMC_ENBOD_OFF 4

/* TXC */
#define TDA_TXC_TXFAILSAFE_OFF 0
#define TDA_TXC_TXENDFIFO_OFF 1
#define TDA_TXC_TXMODE_OFF 3
#define TDA_TXC_TXBDRSYNC_OFF (4)
#define TDA_TXC_INITTXFIFO_OFF 5
#define TDA_TXC_TXSTART_OFF 7

/* RXC */
#define TDA_RXC_RESET (0x84)
#define TDA_RXC_INITRXFIFO_OFF (3)
#define TDA_RXC_FSINITRXFIFO_OFF (2)

/* IS2 */
#define TDA_IS2_RXAF_OFF 0
#define TDA_IS2_TXEMPTY_OFF 2
#define TDA_IS2_TXAE_OFF 3
#define TDA_IS2_TXR_OFF 6
#define TDA_IS2_TXE_OFF 7

/* IS0 */
#define TDA_IS0_WUA_OFF (0)
#define TDA_IS0_FSYNCA_OFF (1)
#define TDA_IS0_EOMA_OFF (3)
#define TDA_IS0_FSYNCB_OFF (5)
#define TDA_IS0_EOMB_OFF (7)

/* IM0, bit positions */
enum {
	TDA_IM0_WUA_OFF = 0,
	TDA_IM0_FSYNCA_OFF = 1,
	TDA_IM0_MIDFA_OFF = 2,
	TDA_IM0_EOMA_OFF = 3,
	TDA_IM0_WUB_OFF = 4,
	TDA_IM0_FSYNCB_OFF = 5,
	TDA_IM0_MIDFB_OFF = 6,
	TDA_IM0_EOMB_OFF = 7,
};

/* IM2, bit positions */
enum {
	TDA_IM2_RXAF_OFF = 0,
	TDA_IM2_SYSRDY_OFF = 1,
	TDA_IM2_TXEMPTY_OFF = 2,
	TDA_IM2_TXAE_OFF = 3,
	TDA_IM2_TXAF_OFF = 4,
	TDA_IM2_TXDS_OFF = 5,
	TDA_IM2_TXREADY_OFF = 6,
	TDA_IM2_TXERROR_OFF = 7,
};

/* receive fifo size, in bits */
#define TDA_RXFIFO_SIZE 288

typedef enum {
	TDA_FIFO_OK = 0x0,
	/* TDA’s fifo overrun */
	TDA_FIFO_OVERFLOW,
	/* supplied buffer is too small to hold entire response */
	TDA_FIFO_BUFFER_TOO_SMALL,
} tda5340FifoReadStatus;

void tda5340Init (tda5340Ctx * const ctx, const uint32_t priority);
void tda5340Reset (tda5340Ctx * const ctx);
bool tda5340RegWriteBulk (tda5340Ctx * const ctx, const tdaConfigVal * const cfg, size_t count);
bool tda5340RegWrite (tda5340Ctx * const ctx, const tda5340Address, const uint8_t);
uint8_t tda5340RegRead (tda5340Ctx * const ctx, const tda5340Address);
bool tda5340ModeSet (tda5340Ctx * const ctx, const uint8_t mode, const bool, const uint8_t);
uint8_t tda5340Receive (tda5340Ctx * const ctx, uint8_t * const data);
void tda5340IrqHandle (tda5340Ctx * const ctx);
void tda5340FifoWrite (tda5340Ctx * const ctx, const uint8_t *data, const size_t bits);
bool tda5340TransmissionStart (tda5340Ctx * const ctx);
bool tda5340FifoRead (tda5340Ctx * const ctx, uint32_t * const retData,
		uint8_t * const retSize);
tda5340FifoReadStatus tda5340FifoReadAll (tda5340Ctx * const ctx, uint8_t * const data,
		size_t * const dataLen);

/* IRQ handler name */
#define TDA5350IRQHANDLER ERU0_3_IRQHandler

#include "tda5340_reg.h"
#include "tda5340_presets.h"

