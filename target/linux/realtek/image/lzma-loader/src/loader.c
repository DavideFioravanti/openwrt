/*
 * LZMA compressed kernel loader for Realtek SoCs based boards
 *
 * Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2017 Weijie Gao <hackpascal@gmail.com>
 *
 * Some parts of this code was based on the OpenWrt specific lzma-loader
 * for the BCM47xx and ADM5120 based boards:
 *	Copyright (C) 2004 Manuel Novoa III (mjn3@codepoet.org)
 *	Copyright (C) 2005 Mineharu Takahara <mtakahar@yahoo.com>
 *	Copyright (C) 2005 by Oleg I. Vdovikin <oleg@cs.msu.su>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <stddef.h>
#include <stdint.h>

#include "cache.h"
#include "printf.h"
#include "LzmaDecode.h"

#undef LZMA_DEBUG

#ifdef LZMA_DEBUG
#  define DBG(f, a...)	printf(f, ## a)
#else
#  define DBG(f, a...)	do {} while (0)
#endif

/* beyond the image end, size not known in advance */
extern unsigned char workspace[];
extern void board_init(void);

static CLzmaDecoderState lzma_state;
static unsigned char *lzma_data;
static unsigned long lzma_datasize;
static unsigned long lzma_outsize;
static unsigned long kernel_la;

#ifdef CONFIG_KERNEL_CMDLINE
#define kernel_argc	2
static const char kernel_cmdline[] = CONFIG_KERNEL_CMDLINE;
static const char *kernel_argv[] = {
	NULL,
	kernel_cmdline,
	NULL,
};
#endif /* CONFIG_KERNEL_CMDLINE */

static void halt(void)
{
	printf("\nSystem halted!\n");
	for(;;);
}

static __inline__ unsigned long get_be32(void *buf)
{
	unsigned char *p = buf;

	return (((unsigned long) p[0] << 24) +
	        ((unsigned long) p[1] << 16) +
	        ((unsigned long) p[2] << 8) +
	        (unsigned long) p[3]);
}

static __inline__ unsigned char lzma_get_byte(void)
{
	unsigned char c;

	lzma_datasize--;
	c = *lzma_data++;

	return c;
}

static int lzma_init_props(void)
{
	unsigned char props[LZMA_PROPERTIES_SIZE];
	int res;
	int i;

	/* read lzma properties */
	for (i = 0; i < LZMA_PROPERTIES_SIZE; i++)
		props[i] = lzma_get_byte();

	/* read the lower half of uncompressed size in the header */
	lzma_outsize = ((SizeT) lzma_get_byte()) +
		       ((SizeT) lzma_get_byte() << 8) +
		       ((SizeT) lzma_get_byte() << 16) +
		       ((SizeT) lzma_get_byte() << 24);

	/* skip rest of the header (upper half of uncompressed size) */
	for (i = 0; i < 4; i++)
		lzma_get_byte();

	res = LzmaDecodeProperties(&lzma_state.Properties, props,
					LZMA_PROPERTIES_SIZE);
	return res;
}

static int lzma_decompress(unsigned char *outStream)
{
	SizeT ip, op;
	int ret;

	lzma_state.Probs = (CProb *) workspace;

	ret = LzmaDecode(&lzma_state, lzma_data, lzma_datasize, &ip, outStream,
			 lzma_outsize, &op);

	if (ret != LZMA_RESULT_OK) {
		int i;

		DBG("LzmaDecode error %d at %08x, osize:%d ip:%d op:%d\n",
		    ret, lzma_data + ip, lzma_outsize, ip, op);

		for (i = 0; i < 16; i++)
			DBG("%02x ", lzma_data[ip + i]);

		DBG("\n");
	}

	return ret;
}

static void lzma_init_data(void)
{
	extern unsigned char _lzma_data_start[];
	extern unsigned char _lzma_data_end[];

	kernel_la = LOADADDR;
	lzma_data = _lzma_data_start;
	lzma_datasize = _lzma_data_end - _lzma_data_start;
}

void loader_main(unsigned long reg_a0, unsigned long reg_a1,
		 unsigned long reg_a2, unsigned long reg_a3)
{
	void (*kernel_entry) (unsigned long, unsigned long, unsigned long,
			      unsigned long);
	int res;

	board_init();

	printf("\n\nOpenWrt kernel loader for Realtek RTL819X\n");
	printf("Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>\n");
	printf("Copyright (C) 2017 Weijie Gao <hackpascal@gmail.com>\n");
	printf("Copyright (C) 2019 Gaspare Bruno <gaspare@anlix.io>\n");

	lzma_init_data();

	res = lzma_init_props();
	if (res != LZMA_RESULT_OK) {
		printf("Incorrect LZMA stream properties!\n");
		halt();
	}

	printf("Decompressing kernel... ");

	res = lzma_decompress((unsigned char *) kernel_la);
	if (res != LZMA_RESULT_OK) {
		printf("failed, ");
		switch (res) {
		case LZMA_RESULT_DATA_ERROR:
			printf("data error!\n");
			break;
		default:
			printf("unknown error %d!\n", res);
		}
		halt();
	} else {
		printf("done!\n");
	}

	flush_cache(kernel_la, lzma_outsize);

	printf("Starting kernel at %08x...\n\n", kernel_la);

#ifdef CONFIG_KERNEL_CMDLINE
	reg_a0 = kernel_argc;
	reg_a1 = (unsigned long) kernel_argv;
	reg_a2 = 0;
	reg_a3 = 0;
#endif

	kernel_entry = (void *) kernel_la;
	kernel_entry(reg_a0, reg_a1, reg_a2, reg_a3);
}

