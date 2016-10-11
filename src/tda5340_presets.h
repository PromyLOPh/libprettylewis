/* predefined configs */
#define TDA_CFG_TXFREQ(config, frequency) _TDA_CFG_TXFREQ(config, frequency)
#define TDA_CFG_TXBAUDRATE(config, rate) _TDA_CFG_TXBAUDRATE(config, rate)
#define TDA_CFG_RXFREQ(config, frequency) _TDA_CFG_RXFREQ(config, frequency)
#define TDA_CFG_RXBAUDRATE(config, rate) _TDA_CFG_RXBAUDRATE(config, rate)

/* idiom to expand macro’s arguments once, i.e. for TDA_CFG_TXFREQ(A, MACRO)
 * MACRO is expanded once */
#define _TDA_CFG_TXFREQ(config, frequency) _TDA_CFG_TXFREQ_ ## frequency(config)
#define _TDA_CFG_TXBAUDRATE(config, rate) _TDA_CFG_TXBAUDRATE_ ## rate(config)
#define _TDA_CFG_RXFREQ(config, frequency) _TDA_CFG_RXFREQ_ ## frequency(config)
#define _TDA_CFG_RXBAUDRATE(config, rate) _TDA_CFG_RXBAUDRATE_ ## rate(config)

/* common parameters */
#define _TDA_CFG_TXFREQ_COMMON(config) \
	{TDA_ ## config ## _PLLINTC1, 0x67}, \
	/* +/- 50 kHz frequency deviation */ \
	{TDA_ ## config ## _TXFDEV, 0xD9}, \
	{TDA_ ## config ## _TXPLLBW, 0x1A}

/* 868.0 MHz */
#define _TDA_CFG_TXFREQ_8680(config) \
	{TDA_ ## config ## _PLLFRAC0C1, 0xCF}, \
	{TDA_ ## config ## _PLLFRAC1C1, 0x7E}, \
	{TDA_ ## config ## _PLLFRAC2C1, 0x11}, \
	_TDA_CFG_TXFREQ_COMMON(config)

/* 869.8 MHz center frequency (TDA Explorer will show upper FSK freq 869.85
 * MHz) */
#define _TDA_CFG_TXFREQ_8698(config) \
	{TDA_ ## config ## _PLLFRAC0C1, 0x4B}, \
	{TDA_ ## config ## _PLLFRAC1C1, 0x31}, \
	{TDA_ ## config ## _PLLFRAC2C1, 0x14}, \
	_TDA_CFG_TXFREQ_COMMON(config)

/* 100 kchip/s */
#define _TDA_CFG_TXBAUDRATE_100(config) \
	{TDA_ ## config ## _TXBDRDIV0, 0x6D}, \
	{TDA_ ## config ## _TXBDRDIV1, 0x00}, \
	{TDA_ ## config ## _TXDSHCFG0, 0x6B}, \
	{TDA_ ## config ## _TXDSHCFG1, 0x12}, \
	{TDA_ ## config ## _TXDSHCFG2, 0x00}

/* 50 kchip/s */
#define _TDA_CFG_TXBAUDRATE_50(config) \
	{TDA_ ## config ## _TXBDRDIV0, 0xDA}, \
	{TDA_ ## config ## _TXBDRDIV1, 0x00}, \
	{TDA_ ## config ## _TXDSHCFG0, 0xD6}, \
	{TDA_ ## config ## _TXDSHCFG1, 0x25}, \
	{TDA_ ## config ## _TXDSHCFG2, 0x00}

/* 5 kchip/s */
#define _TDA_CFG_TXBAUDRATE_5(config) \
	{TDA_ ## config ## _TXBDRDIV0, 0x92}, \
	{TDA_ ## config ## _TXBDRDIV1, 0x08}, \
	{TDA_ ## config ## _TXDSHCFG0, 0x6A}, \
	{TDA_ ## config ## _TXDSHCFG1, 0x7C}, \
	{TDA_ ## config ## _TXDSHCFG2, 0x18}

#define _TDA_CFG_RXFREQ_COMMON(config) \
	{TDA_ ## config ## _PLLINTC1, 0x67}

/* 868.0 MHz */
#define _TDA_CFG_RXFREQ_8680(config) \
	{TDA_ ## config ## _PLLFRAC0C1, 0x36}, \
	{TDA_ ## config ## _PLLFRAC1C1, 0xE5}, \
	{TDA_ ## config ## _PLLFRAC2C1, 0x01}, \
	_TDA_CFG_RXFREQ_COMMON(config)

/* 869.8 MHz */
#define _TDA_CFG_RXFREQ_8698(config) \
	{TDA_ ## config ## _PLLFRAC0C1, 0xB2}, \
	{TDA_ ## config ## _PLLFRAC1C1, 0x97}, \
	{TDA_ ## config ## _PLLFRAC2C1, 0x04}, \
	_TDA_CFG_RXFREQ_COMMON(config)

/* 100 kchip/s */
#define _TDA_CFG_RXBAUDRATE_100(config) \
	{TDA_ ## config ## _AFCKCFG0, 0xFF}, \
	{TDA_ ## config ## _AFCKCFG1, 0xCF}, \
	{TDA_ ## config ## _EXTSLC0, 0x04}, \
	{TDA_ ## config ## _PDECSCFSK, 0x2F}, \
	{TDA_ ## config ## _PDFMFC, 0xA6}, \
	{TDA_ ## config ## _PMFUDSF, 0x01}, \
	{TDA_ ## config ## _SRC, 0x5F}

/* 50 kchip/s */
#define _TDA_CFG_RXBAUDRATE_50(config) \
	{TDA_ ## config ## _AFCKCFG0, 0xA0}, \
	{TDA_ ## config ## _AFCKCFG1, 0xCF}, \
	{TDA_ ## config ## _EXTSLC0, 0x00}, \
	{TDA_ ## config ## _PDECSCFSK, 0x12}, \
	{TDA_ ## config ## _PDFMFC, 0xA6}, \
	{TDA_ ## config ## _PMFUDSF, 0x11}, \
	{TDA_ ## config ## _SRC, 0x5F}

/* 5 kchip/s */
#define _TDA_CFG_RXBAUDRATE_5(config) \
	{TDA_ ## config ## _AFCKCFG0, 0x90}, \
	{TDA_ ## config ## _AFCKCFG1, 0xC1}, \
	{TDA_ ## config ## _PDECF, 0x0C}, \
	{TDA_ ## config ## _PDECSCFSK, 0x0A}, \
	{TDA_ ## config ## _PDFMFC, 0x76}, \
	{TDA_ ## config ## _PMFUDSF, 0x41}, \
	{TDA_ ## config ## _SRC, 0x0E}, \
	{TDA_ ## config ## _EXTSLC0, 0x00}

