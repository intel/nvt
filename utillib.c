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
#include <ctype.h>
#include <string.h>

static void error(char *s)
{
	fprintf(stderr, "%s\n", s);
	exit(1);
}

/*
 * Read PNM image file and return the image buffer containing size[0]*size[1] pixels.
 * Each pixel is R, G, B triplet, each component containing 16 bits, MSB first.
 * Therefore there will be size[0]*size[1]*2*3 bytes in the returned buffer.
 * Stores the returned image size into size.
 * Supports both 8-bit and 16-bit PNM files. For 8-bit PNM files, scale pixel
 * values from 0..255 to 0..65535.
 */
unsigned char *read_pnm(char *input, int size[2])
{
	FILE *f;
	unsigned char *buf, *p;
	int r, tokens, maxval, byp;
	int y, x, c, b;
	char token[16];

	f = fopen(input, "rb");
	if (!f) error("failed to open input file");

	/* Read PNM header */
	tokens = 0;
	b = 0;
	c = fgetc(f);
	do {
		if ((isspace(c) || c == '#') && b > 0) {
			/* New token found */
			token[b] = 0;
			if (tokens == 0) {
				if (strcmp("P6", token) != 0) error("bad ppm header");
			} else if (tokens >= 1 && tokens <= 2) {
				r = sscanf(token, "%u", &size[tokens - 1]);
				if (r != 1) error("bad resolution");
			} else {
				r = sscanf(token, "%u", &maxval);
				if (r != 1) error("bad maxval");
				break;		/* Done */
			}
			tokens++;
			b = 0;
		}
		while (isspace(c) || c == '#') {
			if (c == '#') while (c != '\n' && c != EOF) c = fgetc(f);
			c = fgetc(f);
		}
		if (c == EOF) error("bad ppm file");
		token[b++] = c;
		if (sizeof(token) < b) error("bad ppm header");
		c = fgetc(f);
	} while (1);

	/* Read actual image data */
	byp = maxval < 256 ? 1 : 2;
	p = buf = malloc(size[0] * size[1] * 3 * 2);
	for (y = 0; y < size[1]; y++)
		for (x = 0; x < size[0]; x++)
			for (c = 0; c < 3; c++) {
				unsigned int v = 0;
				for (b = 0; b < byp; b++) {
					v <<= 8;
					v |= fgetc(f) & 0xff;
					if (feof(f)) error("ppm read error");
				}
				if (byp == 1) v = (v << 8) | v;		/* Scale 0..255 to 0..65535 */
				*p++ = (v >> 8) & 0xff;
				*p++ = v & 0xff;
			}
	fclose(f);
	return buf;
}

void write_file(const char *name, const unsigned char *data, int size)
{
	FILE *f;
	int r;

	f = fopen(name, "wb");
	if (!f)
		error("can not open file");
	r = fwrite(data, size, 1, f);
	if (r != 1)
		error("failed to write data to file");
	r = fclose(f);
	if (r != 0)
		error("failed to close file");
}

