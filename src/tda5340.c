#include <assert.h>

#include <xmc_eru.h>
#include <xmc_gpio.h>
#include <xmc_spi.h>

#include "tda5340.h"
#include "util.h"
#include <bitbuffer.h>

/* pin config */
/* We cannot use P1.14 or P1.15 here. These are used for the buttons. Yes, I
 * tried that. */
#define TDAPON  P0_3 /* P_ON */
#define TDANINT P0_2 /* PP2 <-> ERU0.3B3, that is ERU0, channel 3, input B, signal 3 */

/* spi pins, usic 1, channel 1 */
#define SPI_MISO P0_0 /* SDO, DX0D */
#define SPI_INPUTSRC USIC1_C1_DX0_P0_0
#define SPI_MOSI P0_1 /* SDI, DOUT0 */
#define SPI_SS   P0_9 /* NCS, SELO0 */
#define SPI_SCLK P0_10 /* SCLK, SCLKOUT */

/* interrupt, depends on ETL used, make sure you change TDA5350IRQHANDLER in .h
 * too */
#define INTERRUPT ERU0_3_IRQn
/* ETL config */
#define ETL ERU0_ETL3
#define ETL_SRCAB XMC_ERU_ETL_SOURCE_B
#define ETL_SRCPIN ERU0_ETL3_INPUTB_P0_2
/* select trigger channel */
#define ETL_CHANNEL XMC_ERU_ETL_OUTPUT_TRIGGER_CHANNEL3
/* OGU */
#define OGU ERU0_OGU3

#include <SEGGER_RTT.h>
//#define debug(...)
#define debug(f, ...) SEGGER_RTT_printf(0, "tda: " f, ##__VA_ARGS__)

static void spiInit (tda5340Ctx * const ctx) {
	assert (ctx != NULL);
	/* 5m works fine, 10m does not */
	assert (ctx->baudrate > 0 && ctx->baudrate < 10000000);
	XMC_SPI_CH_CONFIG_t config = {
		.baudrate = ctx->baudrate,
		.bus_mode = XMC_SPI_CH_BUS_MODE_MASTER,
		.selo_inversion = XMC_SPI_CH_SLAVE_SEL_INV_TO_MSLS, /* low-active */
		.parity_mode = XMC_USIC_CH_PARITY_MODE_NONE
		};
	XMC_USIC_CH_t * const spi = ctx->spi;

	/* init spi */
	XMC_SPI_CH_Init(spi, &config);

	XMC_SPI_CH_SetInputSource(spi, XMC_USIC_CH_INPUT_DX0, SPI_INPUTSRC);
	XMC_SPI_CH_SetBitOrderMsbFirst (spi);
	/* the clock must be shifted by half a period, so data on MOSI is set on
	 * falling edge. The TDA samples its signal on the rising edge */
	XMC_USIC_CH_ConfigureShiftClockOutput (spi,
			XMC_USIC_CH_BRG_SHIFT_CLOCK_PASSIVE_LEVEL_0_DELAY_ENABLED,
			XMC_USIC_CH_BRG_SHIFT_CLOCK_OUTPUT_SCLK);
#if 0
	XMC_USIC_CH_DisableInputInversion (XMC_SPI1_CH0, XMC_USIC_CH_INPUT_DX0);
#endif

	XMC_SPI_CH_Start(spi);

	XMC_GPIO_SetMode(SPI_MOSI, XMC_GPIO_MODE_OUTPUT_PUSH_PULL_ALT2);
	XMC_GPIO_SetMode(SPI_SS, XMC_GPIO_MODE_OUTPUT_PUSH_PULL_ALT2);
	XMC_GPIO_SetMode(SPI_SCLK, XMC_GPIO_MODE_OUTPUT_PUSH_PULL_ALT2);
	XMC_GPIO_SetMode(SPI_MISO, XMC_GPIO_MODE_INPUT_TRISTATE);

	debug ("initialized spi\n");
}

/*	Initialize PON pin
 */
static void ponInit (tda5340Ctx * const ctx) {
	const XMC_GPIO_CONFIG_t config = {
			.mode = XMC_GPIO_MODE_OUTPUT_PUSH_PULL,
			.output_level = XMC_GPIO_OUTPUT_LEVEL_LOW,
			};
	XMC_GPIO_Init (TDAPON, &config);
}

/*	Initialize NINT pin and interrupt handler
 */
static void nintInit (tda5340Ctx * const ctx, const uint32_t priority) {
	XMC_GPIO_SetMode(TDANINT, XMC_GPIO_MODE_INPUT_TRISTATE);

	static const XMC_ERU_ETL_CONFIG_t etlCfg = {
		/* XXX: is is _very_ important that you use .input_a OR .input_b here
		 * and NOT (BY ALL FUCKING MEANS NOT!) .input */
		.input_b = ETL_SRCPIN,
		.source = ETL_SRCAB,
		.edge_detection = XMC_ERU_ETL_EDGE_DETECTION_FALLING,
		.status_flag_mode = XMC_ERU_ETL_STATUS_FLAG_MODE_HWCTRL,
		.enable_output_trigger = true,
		/* trigger ogu x */
		.output_trigger_channel = ETL_CHANNEL,
		};
	static const XMC_ERU_OGU_CONFIG_t oguCfg = {
		.service_request = XMC_ERU_OGU_SERVICE_REQUEST_ON_TRIGGER
		};
	XMC_ERU_ETL_Init(ETL, &etlCfg);
	XMC_ERU_OGU_Init(OGU, &oguCfg);

	NVIC_SetPriority(INTERRUPT, priority);
	NVIC_EnableIRQ(INTERRUPT);
}

static void txerror (tda5340Ctx * const ctx, void * const data) {
	assert (0);
}

/*	Initialize TDA handle
 *
 *	:param priority: priority for NINT interrupt, encoded with NVIC_EncodePriority()
 */
void tda5340Init (tda5340Ctx * const ctx, const uint32_t priority) {
	spiInit (ctx);
	ponInit (ctx);
	nintInit (ctx, priority);

	ctx->mode = TDA_RESET_MODE;
	ctx->page = 0;
	/* defaults to standard handler */
	ctx->txerror = txerror;
	ctx->lock = 0;

	debug ("init complete\n");
}

#define POR_MAGIC_STATUS 0xff

/*	Reset TDA by pulling P_ON low for at least 100μs, see datasheet p. 24.
 *	Startup sanity checks are managed by interrupt handler.
 */
void tda5340Reset (tda5340Ctx * const ctx) {
	XMC_GPIO_SetOutputLow (TDAPON);
	delay (10000000); /* XXX: use timer */
	XMC_GPIO_SetOutputHigh (TDAPON);
}

/*	Write one byte to the SPI bus, blocking.
 */
static uint16_t spiByte (XMC_USIC_CH_t * const spi, const uint8_t write) {
	const uint32_t recvFlag = XMC_SPI_CH_STATUS_FLAG_RECEIVE_INDICATION |
				XMC_SPI_CH_STATUS_FLAG_ALTERNATIVE_RECEIVE_INDICATION;
	XMC_SPI_CH_ClearStatusFlag (spi, recvFlag);

	XMC_SPI_CH_Transmit(spi, write, XMC_SPI_CH_MODE_STANDARD);

	while ((XMC_SPI_CH_GetStatusFlag(spi) & (recvFlag)) == 0U);

	/* the value must be read otherwise the buffer is going to overrun and new
	 * data is not stored anymore */
	return XMC_SPI_CH_GetReceivedData (spi);
}

/*	Basic register read, SS signal unchanged
 */
static uint8_t regReadNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg) {
	spiByte (spi, TDA_RD);
	spiByte (spi, reg & 0xff);
	return spiByte (spi, 0x00);
}

/*	Write a TDA register, no slave select signal changed
 */
static void regWriteNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg,
		const uint8_t val) {
	spiByte (spi, TDA_WR);
	spiByte (spi, reg & 0xff);
	spiByte (spi, val);
}

/*	Write and verify most recent register write
 */
static bool regWriteVerifyNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg,
		const uint8_t val) {
	regWriteNoSS (spi, reg, val);
	/* page change never required here */
	const uint8_t lastAddress = regReadNoSS (spi, TDA_SPIAT);
	const uint8_t lastData = regReadNoSS (spi, TDA_SPIDT);
	return lastAddress == (reg & 0xff) && lastData == val;
}

/*	Translate register address to page number
 */
static uint8_t addressToPage (const tda5340Address address) {
	return (address >> 8) & 0x3;
}

/*	Change page (if required), slave select signal unchanged
 */
static bool pageChangeNoSS (tda5340Ctx * const ctx, const tda5340Address reg) {
	/*	Check whether the current page needs to be changed before accessing the
	 *	address; registers above 0xa0 are mirrored on all pages */
	const uint8_t page = addressToPage (reg);
	bool ret = true;
	if ((reg & 0xff) < 0xa0 && ctx->page != page) {
		ret = regWriteVerifyNoSS (ctx->spi, TDA_SFRPAGE, page);
		ctx->page = page;
	}
	return ret;
}

/* 	Atomic SPI transaction start/end primitives
 */
static void spiStart (tda5340Ctx * const ctx) {
	/* isr uses spi as well and should not interrupt this */
	NVIC_DisableIRQ(INTERRUPT);
	assert (__sync_bool_compare_and_swap (&ctx->lock, 0, 1) && "busy");
	XMC_SPI_CH_EnableSlaveSelect(ctx->spi, XMC_SPI_CH_SLAVE_SELECT_0);
}

static void spiEnd (tda5340Ctx * const ctx) {
	XMC_SPI_CH_DisableSlaveSelect (ctx->spi);
	ctx->lock = 0;
	NVIC_EnableIRQ(INTERRUPT);
}

/*	Read from TDA register. The read is not interruptible, so interrupt handler
 *	callbacks can safely use this function.
 */
uint8_t tda5340RegRead (tda5340Ctx * const ctx, const tda5340Address reg) {
	spiStart (ctx);
	pageChangeNoSS (ctx, reg);
	const uint16_t ret = regReadNoSS (ctx->spi, reg);
	spiEnd (ctx);

	return ret;
}

/*	Set page, write register, verify result and retry (if necessary)
 */
static bool regWritePageVerifyNoSS (tda5340Ctx * const ctx,
		const tda5340Address reg, const uint8_t val) {
	pageChangeNoSS (ctx, reg);

	bool success = false;
	uint8_t retries = ctx->retries;
	while (!(success = regWriteVerifyNoSS (ctx->spi, reg, val)) && retries-- > 0);
	return success;
}

/*	Write to TDA register. See note for tda5340RegRead
 */
bool tda5340RegWrite (tda5340Ctx * const ctx, const tda5340Address reg,
		const uint8_t val) {
	spiStart (ctx);
	const bool ret = regWritePageVerifyNoSS (ctx, reg, val);
	spiEnd (ctx);

	return ret;
}

/*	Bulk register write. Can be used to load configurations, but make sure the
 *	TDA is in sleep_mode before.
 */
bool tda5340RegWriteBulk (tda5340Ctx * const ctx, const tdaConfigVal * const cfg,
		size_t count) {
	bool ret = true;

	spiStart (ctx);
	for (size_t i = 0; i < count; i++) {
		ret = regWritePageVerifyNoSS (ctx, cfg[i].reg, cfg[i].val);
		if (!ret) {
			break;
		}
	}
	spiEnd (ctx);

	return ret;
}

/* start with default value reset, then set all bits in `set` and clear those
 * in `clear`
 * XXX: should be used everywhere!
 */
#define fromReset(reset,set,clear) (((reset) | (set)) & ~(clear))

bool tda5340ModeSet (tda5340Ctx * const ctx, const uint8_t mode, const bool sendbit,
		const uint8_t config) {
	/* two bits */
	assert (config < 4);

	switch (mode) {
		case TDA_TRANSMIT_MODE:
			if (!tda5340RegWrite (ctx, TDA_TXC,
					/* go into tx ready state if fifo runs empty, otherwise the
					 * last bit would be sent over and over */
					1 << TDA_TXC_TXENDFIFO_OFF |
					/* init the fifo (clears all data) */
					1 << TDA_TXC_INITTXFIFO_OFF |
					/* enable start bit transmission mode (sbf) */
					(sendbit ? 1 : 0) << TDA_TXC_TXMODE_OFF |
					/* enable failsafe mode */
					(1 << TDA_TXC_TXFAILSAFE_OFF) |
					/* not sure if relevant, but enabled by tda explorer */
					(1 << TDA_TXC_TXBDRSYNC_OFF)
					)) {
				return false;
			}
			ctx->sendbit = sendbit;
			break;

		case TDA_RUN_MODE_SLAVE: {
			/* do not init fifo at frame start */
			const uint8_t set = ctx->fsInitFifo ? (1 << TDA_RXC_FSINITRXFIFO_OFF) : 0;
			const uint8_t reset = ctx->fsInitFifo ? 0 : (1 << TDA_RXC_FSINITRXFIFO_OFF);
			if (!tda5340RegWrite (ctx, TDA_RXC,
					fromReset (TDA_RXC_RESET,
						/* init rx fifo upon startup */
						(1 << TDA_RXC_INITRXFIFO_OFF) | set, reset))) {
				return false;
			}
			break;
		}

		default:
			/* pass */
			break;
	}

	/* the cmc register is write-only, so we can’t just read the old stuff, add
	 * our new mode and write back again; instead always enable the brown out
	 * detector and hope for the best */
	if (!tda5340RegWrite (ctx, TDA_CMC, (mode << TDA_CMC_MSEL_OFF) |
			(config << TDA_CMC_MCS_OFF) |
			(1 << TDA_CMC_ENBOD_OFF))) {
		return false;
	}
	ctx->mode = mode;

	return true;
}

/*	Start a transmission in SBF mode
 */
bool tda5340TransmissionStart (tda5340Ctx * const ctx) {
	return tda5340RegWrite (ctx, TDA_TXC,
			(1 << TDA_TXC_TXENDFIFO_OFF) |
			(ctx->sendbit ? 1 : 0) << TDA_TXC_TXMODE_OFF |
			/* enable failsafe mode */
			(1 << TDA_TXC_TXFAILSAFE_OFF) |
			/* not sure if relevant, but enabled by tda explorer */
			(1 << TDA_TXC_TXBDRSYNC_OFF) |
			/* actually start transmission */
			(1 << TDA_TXC_TXSTART_OFF)
			);
}

/*	Write packet to transmission fifo
 */
void tda5340FifoWrite (tda5340Ctx * const ctx, const uint8_t * const data, const size_t bits) {
	assert (ctx->mode == TDA_TRANSMIT_MODE);
	assert (data != NULL);
	assert (bits > 0 && bits <= 256);

	const size_t bytes = (bits-1)/8 + 1;
	XMC_USIC_CH_t * const spi = ctx->spi;

	spiStart (ctx);

	spiByte (spi, TDA_WRF);
	spiByte (spi, bits-1);
	/* actual data is lsb first */
	XMC_SPI_CH_SetBitOrderLsbFirst (spi);
	for (unsigned char i = 0; i < bytes; i++) {
		spiByte (spi, data[i]);
	}
	/* switch back */
	XMC_SPI_CH_SetBitOrderMsbFirst (spi);

	spiEnd (ctx);
}

/*	Read data from receive fifo. Returns false if fifo overflow occured.
 */
bool tda5340FifoRead (tda5340Ctx * const ctx, uint32_t * const retData,
		uint8_t * const retSize) {
	XMC_USIC_CH_t * const spi = ctx->spi;
	uint32_t data = 0;

	spiStart (ctx);

	spiByte (spi, TDA_RDF);
	/* the actual data is lsb first */
	XMC_SPI_CH_SetBitOrderLsbFirst (spi);
	for (unsigned char i = 0; i < 4*8; i += 8) {
		data |= spiByte (spi, 0x00) << i;
	}
	/* … and the valid bits switches back */
	XMC_SPI_CH_SetBitOrderMsbFirst (spi);
	const uint8_t bitsValid = spiByte (spi, 0x00);

	/*Disable Slave Select line */
	spiEnd (ctx);

	/* bits 5:0 indicate number of valid bits, bit 7 indicates fifo overflow
	 * (i.e. some data was lost), see p. 46 */
	if (bitsValid >> 7) {
		return false;
	}
	*retData = data;
	*retSize = bitsValid & 0x3f;
	return true;
}

/*	Retrieve fifo contents, put it into data, which can hold up to len _bytes_.
 *	len is set to received _bits_.
 */
tda5340FifoReadStatus tda5340FifoReadAll (tda5340Ctx * const ctx, uint8_t * const data,
		size_t * const len) {
	assert (ctx != NULL);
	assert (data != NULL);

	bitbuffer bb;
	bitbufferInit (&bb, (uint32_t *) data, (*len)*8);

	while (true) {
		uint32_t block;
		uint8_t bitsReceived;
		if (!tda5340FifoRead (ctx, &block, &bitsReceived)) {
			/* overflow */
			return TDA_FIFO_OVERFLOW;
		}
		/* truncated packet or done receiving */
		if (bitsReceived == 0) {
			break;
		}

		if (!bitbufferPush32 (&bb, block, bitsReceived)) {
			return TDA_FIFO_BUFFER_TOO_SMALL;
		}
	}
	*len = bitbufferLength (&bb);

	return TDA_FIFO_OK;
}

/*	Interrupt handler, calls the appropriate callbacks */
void tda5340IrqHandle (tda5340Ctx * const ctx) {
	assert (ctx != NULL);

	switch (ctx->mode) {
		case TDA_RESET_MODE:
			/* wait until NINT has been pulled low. triggering on falling edge,
			 * thus check if flag is set */
			if (XMC_ERU_ETL_GetStatusFlag (ETL)) {
				if (tda5340RegRead (ctx, TDA_IS0) != POR_MAGIC_STATUS ||
						tda5340RegRead (ctx, TDA_IS1) != POR_MAGIC_STATUS ||
						tda5340RegRead (ctx, TDA_IS2) != POR_MAGIC_STATUS) {
					/* something is wrong, try again */
					debug ("reset failed, trying again\n");
					tda5340Reset (ctx);
					break;
				}

				/* wait until TDA pulled NINT high after reading the status
				 * register. flag is cleared by hardware on positive edge */
				while (XMC_ERU_ETL_GetStatusFlag (ETL));

				debug ("the interrupt seems to be working\n");

				if (tda5340RegRead (ctx, TDA_IS2) != 0x00) {
					debug ("reset failed, trying again\n");
					tda5340Reset (ctx);
					break;
				}

				debug ("and the register is back to normal\n");

				ctx->mode = TDA_SLEEP_MODE;
			}
			break;

		case TDA_SLEEP_MODE:
			/* ? */
			break;

		case TDA_TRANSMIT_MODE: {
			/* only IS2 is relevant in transmit mode */
			const uint8_t is2 = tda5340RegRead (ctx, TDA_IS2);
			if (is2 == 0xff) {
				/* XXX: check the others, it might be a reset interrupt? */
				break;
			}
			if (bitIsSet (is2, TDA_IS2_TXE_OFF) && ctx->txerror != NULL) {
				/* transmission error */
				ctx->txerror (ctx, ctx->data);
			}
			if (bitIsSet (is2, TDA_IS2_TXAE_OFF) && ctx->txae != NULL) {
				/* transmission fifo almost empty */
				ctx->txae (ctx, ctx->data);
			}
			if (bitIsSet (is2, TDA_IS2_TXEMPTY_OFF) && ctx->txempty != NULL) {
				/* transmission fifo empty */
				ctx->txempty (ctx, ctx->data);
			}
			if (bitIsSet (is2, TDA_IS2_TXR_OFF) && ctx->txready != NULL) {
				/* tx ready */
				ctx->txready (ctx, ctx->data);
			}
			break;
		}

		/* receive modes */
		case TDA_RUN_MODE_SLAVE:
		case TDA_SELF_POLLING_MODE: {
			/* XXX: config b/c/d? */
			const uint8_t is0 = tda5340RegRead (ctx, TDA_IS0);
			//const uint8_t is1 = tda5340RegRead (ctx, TDA_IS1);
			const uint8_t is2 = tda5340RegRead (ctx, TDA_IS2);
			if (is0 == 0xff/* && is1 == 0xff*/ && is2 == 0xff) {
				/* XXX: something looks phishy */
				debug ("phishy status\n");
				break;
			}
			/* order matters, if all events are received at the same time, the
			 * “natural” order (frame start, rx full, end of message) should be
			 * chosen */
			if (bitIsSet (is0, TDA_IS0_FSYNCA_OFF) && ctx->rxfsync != NULL) {
				/* frame synchronized config A */
				ctx->rxfsync (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_FSYNCB_OFF) && ctx->rxfsync != NULL) {
				/* frame synchronized config B */
				ctx->rxfsync (ctx, ctx->data);
			}
			if (bitIsSet (is2, TDA_IS2_RXAF_OFF) && ctx->rxaf != NULL) {
				/* receive fifo almost full */
				ctx->rxaf (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_EOMA_OFF) && ctx->rxeom != NULL) {
				/* end of message indicator */
				ctx->rxeom (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_EOMB_OFF) && ctx->rxeom != NULL) {
				/* end of message indicator */
				ctx->rxeom (ctx, ctx->data);
			}
			break;
		}

		default:
			assert (0);
			break;
	}
}

