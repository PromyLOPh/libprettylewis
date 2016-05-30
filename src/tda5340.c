#include <stdio.h>
#include <assert.h>

#include <xmc_eru.h>
#include <xmc_gpio.h>
#include <xmc_spi.h>

#include "tda5340.h"
#include "util.h"

/* pin config */
/* We cannot use P1.14 or P1.15 here. These are used for the buttons. Yes, I
 * tried that. */
#define TDAPON  P0_11 /* P_ON */
#define TDANINT P0_6 /* PP2 <-> ERU0.3B2, that is ERU0, channel 3, input B, signal 2 */

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
#define ETL_SRCPIN ERU0_ETL3_INPUTB_P0_6
/* select trigger channel */
#define ETL_CHANNEL XMC_ERU_ETL_OUTPUT_TRIGGER_CHANNEL3
/* OGU */
#define OGU ERU0_OGU3

static void spiInit (tda5340Ctx * const ctx) {
	XMC_SPI_CH_CONFIG_t config = {
		.baudrate = 1000000,
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

	puts ("initialized spi");
}

/*	Initialize PON pin
 */
static void ponInit (tda5340Ctx * const ctx) {
	const XMC_GPIO_CONFIG_t config = {
			.mode = XMC_GPIO_MODE_OUTPUT_PUSH_PULL,
			.output_level = XMC_GPIO_OUTPUT_LEVEL_LOW,
			.output_strength = XMC_GPIO_OUTPUT_STRENGTH_MEDIUM
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

	puts ("initialized tda5340");
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

/*	Write a TDA register, no slave select signal changed
 */
static void regWriteNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg,
		const uint8_t val) {
	spiByte (spi, TDA_WR);
	spiByte (spi, reg & 0xff);
	spiByte (spi, val);
}

/*	Set TDA page, no slave select signal changed
 */
static void pageSetNoSS (tda5340Ctx * const ctx, const uint8_t page) {
	assert (page < 4);
	regWriteNoSS (ctx->spi, TDA_SFRPAGE, page);
	ctx->page = page;
}

static uint8_t addressToPage (const tda5340Address address) {
	return (address >> 8) & 0x3;
}

/*	Check whether the current page needs to be changed before accessing the
 *	address; registers above 0xa0 are mirrored on all pages */
static bool pageNeedsChange (tda5340Ctx * const ctx, const tda5340Address address) {
	return (address & 0xff) < 0xa0 && ctx->page != addressToPage (address);
}

static uint8_t regReadNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg) {
	spiByte (spi, TDA_RD);
	spiByte (spi, reg & 0xff);
	return spiByte (spi, 0x00);
}

/*	Read from TDA register. The read is not interruptible, so interrupt handler
 *	callbacks can safely use this function.
 */
uint8_t tda5340RegRead (tda5340Ctx * const ctx, const tda5340Address reg) {
	XMC_USIC_CH_t * const spi = ctx->spi;

	XMC_SPI_CH_EnableSlaveSelect(spi, XMC_SPI_CH_SLAVE_SELECT_0);

	if (pageNeedsChange (ctx, reg)) {
		pageSetNoSS (ctx, addressToPage (reg));
	}
	const uint16_t ret = regReadNoSS (ctx->spi, reg);

	XMC_SPI_CH_DisableSlaveSelect (spi);

	return ret;
}

/*	Verify most recent register write
 */
static bool regWriteVerifyNoSS (XMC_USIC_CH_t * const spi, const tda5340Address reg,
		const uint8_t val) {
	/* check the written value; no page change required here */
	const uint8_t lastAddress = regReadNoSS (spi, TDA_SPIAT);
	const uint8_t lastData = regReadNoSS (spi, TDA_SPIDT);
	return lastAddress == (reg & 0xff) && lastData == val;
}

/*	Set page, write register and verify result
 */
static void regWritePageVerifyNoSS (tda5340Ctx * const ctx,
		const tda5340Address reg, const uint8_t val) {
	if (pageNeedsChange (ctx, reg)) {
		pageSetNoSS (ctx, addressToPage (reg));
	}

	regWriteNoSS (ctx->spi, reg, val);
	assert (regWriteVerifyNoSS (ctx->spi, reg, val));
}

/*	Write to TDA register. See note for tda5340RegRead
 */
void tda5340RegWrite (tda5340Ctx * const ctx, const tda5340Address reg,
		const uint8_t val) {
	XMC_USIC_CH_t * const spi = ctx->spi;

	XMC_SPI_CH_EnableSlaveSelect(spi, XMC_SPI_CH_SLAVE_SELECT_0);

	regWritePageVerifyNoSS (ctx, reg, val);

	XMC_SPI_CH_DisableSlaveSelect(spi);
}

/*	Bulk register write. Can be used to load configurations, but make sure the
 *	TDA is in sleep_mode before.
 */
void tda5340RegWriteBulk (tda5340Ctx * const ctx, const tdaConfigVal * const cfg,
		size_t count) {
	XMC_USIC_CH_t * const spi = ctx->spi;

	XMC_SPI_CH_EnableSlaveSelect(spi, XMC_SPI_CH_SLAVE_SELECT_0);

	for (size_t i = 0; i < count; i++) {
		regWritePageVerifyNoSS (ctx, cfg[i].reg, cfg[i].val);
	}

	XMC_SPI_CH_DisableSlaveSelect(spi);
}

/* start with default value reset, then set all bits in `set` and clear those
 * in `clear`
 * XXX: should be used everywhere!
 */
#define fromReset(reset,set,clear) (((reset) | (set)) & ~(clear))

void tda5340ModeSet (tda5340Ctx * const ctx, const uint8_t mode, const bool sendbit,
		const uint8_t config) {
	/* two bits */
	assert (config < 4);

	ctx->mode = mode;
	ctx->sendbit = sendbit;

	switch (mode) {
		case TDA_TRANSMIT_MODE:
			tda5340RegWrite (ctx, TDA_TXC,
					/* go into tx ready state if fifo runs empty, otherwise the
					 * last bit would be sent over and over */
					1 << TDA_TXC_TXENDFIFO_OFF |
					/* init the fifo (clears all data) */
					1 << TDA_TXC_INITTXFIFO_OFF |
					/* enable start bit transmission mode (sbf) */
					(ctx->sendbit ? 1 : 0) << TDA_TXC_TXMODE_OFF |
					/* enable failsafe mode */
					(1 << TDA_TXC_TXFAILSAFE_OFF) |
					/* not sure if relevant, but enabled by tda explorer */
					(1 << TDA_TXC_TXBDRSYNC_OFF)
					);
			break;

		case TDA_RUN_MODE_SLAVE:
			tda5340RegWrite (ctx, TDA_RXC,
					fromReset (TDA_RXC_RESET,
						/* init rx fifo upon startup */
						(1 << TDA_RXC_INITRXFIFO_OFF),
						/* do not init fifo at frame start */
						(1 << TDA_RXC_FSINITRXFIFO_OFF)
						)); 
			break;

		default:
			/* pass */
			break;
	}

	/* the cmc register is write-only, so we can’t just read the old stuff, add
	 * our new mode and write back again; instead always enable the brown out
	 * detector and hope for the best */
	tda5340RegWrite (ctx, TDA_CMC, (mode << TDA_CMC_MSEL_OFF) |
			(config << TDA_CMC_MCS_OFF) |
			(1 << TDA_CMC_ENBOD_OFF));
}

/*	Start a transmission in SBF mode
 */
void tda5340TransmissionStart (tda5340Ctx * const ctx) {
	tda5340RegWrite (ctx, TDA_TXC,
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

	XMC_SPI_CH_EnableSlaveSelect(spi, XMC_SPI_CH_SLAVE_SELECT_0);

	spiByte (spi, TDA_WRF);
	spiByte (spi, bits-1);
	/* actual data is lsb first */
	XMC_SPI_CH_SetBitOrderLsbFirst (spi);
	for (unsigned char i = 0; i < bytes; i++) {
		spiByte (spi, data[i]);
	}
	/* switch back */
	XMC_SPI_CH_SetBitOrderMsbFirst (spi);

	XMC_SPI_CH_DisableSlaveSelect (spi);
}

/*	Read data from receive fifo. Returns false if fifo overflow occured.
 */
bool tda5340FifoRead (tda5340Ctx * const ctx, uint32_t * const retData,
		uint8_t * const retSize) {
	XMC_USIC_CH_t * const spi = ctx->spi;
	uint32_t data = 0;

	XMC_SPI_CH_EnableSlaveSelect (spi, XMC_SPI_CH_SLAVE_SELECT_0);

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
	XMC_SPI_CH_DisableSlaveSelect (spi);	

	/* bits 5:0 indicate number of valid bits, bit 7 indicates fifo overflow
	 * (i.e. some data was lost), see p. 46 */
	if (bitsValid >> 7) {
		return false;
	}
	*retData = data;
	*retSize = bitsValid & 0x3f;
	return true;
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
					/* something is wrong */
					assert (0);
				}

				/* wait until TDA pulled NINT high after reading the status
				 * register. flag is cleared by hardware on positive edge */
				while (XMC_ERU_ETL_GetStatusFlag (ETL));

				puts ("the interrupt seems to be working");

				if (tda5340RegRead (ctx, TDA_IS2) != 0x00) {
					assert (0);
				}

				puts ("and the register is back to normal");

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
			if (is0 == 0xff/* && is1 == 0xff && is2 == 0xff*/) {
				/* XXX: something looks phishy */
				puts ("phishy status");
				break;
			}
			if (bitIsSet (is0, TDA_IS0_FSYNCA_OFF) && ctx->rxfsync != NULL) {
				/* frame synchronized config A */
				ctx->rxfsync (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_EOMA_OFF) && ctx->rxeom != NULL) {
				/* end of message indicator */
				ctx->rxeom (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_FSYNCB_OFF) && ctx->rxfsync != NULL) {
				/* frame synchronized config B */
				ctx->rxfsync (ctx, ctx->data);
			}
			if (bitIsSet (is0, TDA_IS0_EOMB_OFF) && ctx->rxeom != NULL) {
				/* end of message indicator */
				ctx->rxeom (ctx, ctx->data);
			}
			if (bitIsSet (is2, TDA_IS2_RXAF_OFF) && ctx->rxaf != NULL) {
				/* receive fifo almost full */
				ctx->rxaf (ctx, ctx->data);
			}
			break;
		}

		default:
			assert (0);
			break;
	}
}

