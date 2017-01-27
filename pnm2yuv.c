/*
 * Next Video Tool for Linux* OS - Video4Linux2 API tool for developers.
 *
 * Copyright (c) 2017 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "utillib.h"

/* From https://msdn.microsoft.com/en-us/library/aa917087.aspx */
/* Modified so that R, G, B are 16-bit pixel values 0..65535 */
#define RGB2Y(R,G,B)	(((  66 * (R) + 129 * (G) +  25 * (B) + 32768) >> 16) +  16)
#define RGB2U(R,G,B)	((( -38 * (R) -  74 * (G) + 112 * (B) + 32768) >> 16) + 128)
#define RGB2V(R,G,B)	((( 112 * (R) -  94 * (G) -  18 * (B) + 32768) >> 16) + 128)

static void *rgb2nv12(uint16_t *rgb, int size[2])
{
	static const int bpp = 3;
	unsigned char *yuv, *uv, *r;
	int x, y;

	r = yuv = malloc(size[0] * size[1] * 3/2);
	uv = yuv + size[0] * size[1];

	for (y = 0; y < size[1]; y += 2) {
		/* y is even, convert chrominance */
		for (x = 0; x < size[0]; x += 2) {
			/* x is even, convert chrominance */
			*yuv++ = RGB2Y(rgb[0], rgb[1], rgb[2]);
			*uv++ = RGB2U(rgb[0], rgb[1], rgb[2]);
			*uv++ = RGB2V(rgb[0], rgb[1], rgb[2]);
			rgb += bpp;
			/* x is odd, don't convert chrominance */
			*yuv++ = RGB2Y(rgb[0], rgb[1], rgb[2]);
			rgb += bpp;
		}
		/* y is odd, don't convert chrominance */
		for (x = 0; x < size[0]; x++) {
			*yuv++ = RGB2Y(rgb[0], rgb[1], rgb[2]);
			rgb += bpp;
		}
	}

	return r;
}

int main(int argc, char *argv[])
{
	char *in_name;
	char *out_name;
	void *rgb, *yuv;
	int size[2];

	if (argc < 3) {
		printf("Usage: %s [input.pnm] [output.yuv]\n", argv[0]);
		exit(1);
	}
	in_name = argv[1];
	out_name = argv[2];

	rgb = read_pnm(in_name, size);
	printf("Read file %ix%i pixels\n", size[0], size[1]);
	yuv = rgb2nv12(rgb, size);
	write_file(out_name, yuv, size[0]*size[1]*3/2);

	free(rgb);
	free(yuv);

	return 0;
}
