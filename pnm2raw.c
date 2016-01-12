#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "utillib.h"

enum format {
	SBGGR10,
	SGBRG10,
	SRGGB10,
	SGRBG10,
};

static void error(char *s)
{
	fprintf(stderr, "%s\n", s);
	exit(1);
}

/*
 * Read 16-bit word at the given memory location, MSB first.
 * Return 10 most significant bits of the value.
 */
static inline unsigned int get_word(unsigned char *src)
{
	unsigned int hi = src[0];
	unsigned int lo = src[1];
	unsigned int w = (hi << 8) | lo;
	w >>= 6;		/* 16 bits per pixel to 10 bits per pixel */
	return w;
}

/*
 * Store the given 16-bit pixel value into given memory location as two bytes.
 */
static inline void write_pixel(unsigned char *p, unsigned int v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

/*
 * Convert RGB-image into RAW 10 bits-per-pixel format.
 * Image size is size[0]*size[1] pixels.
 * Input: size[0]*size[1] pixels, each pixel contains R, G, and B components,
 *        in this order, and each component contains 16 bits from 0..65535,
 *        MSB first. Input buffer size must be size[0]*size[1]*3*2 bytes.
 * Output: raw bayer format size[0]*size[1] pixels, each pixel containing either
 *        R, G, or B according to the given format. Each pixel is stored as
 *        16-bit value, LSB first, and has a value from 0..1023
 *        (6 most significant bits are zero). The output buffer takes
 *        size[0]*size[1]*2 bytes of memory.
 */
static unsigned char *rgb2raw10(unsigned char *rgb16, int size[2], enum format format)
{
	int oddrow, oddpix, initrow, initpix;
	unsigned char *buf, *p;
	int y, x;

	p = buf = malloc(size[0] * size[1] * 2);

	if (format == SBGGR10) { initrow = 1; initpix = 0; } else
	if (format == SGBRG10) { initrow = 1; initpix = 1; } else
	if (format == SRGGB10) { initrow = 0; initpix = 1; } else
	if (format == SGRBG10) { initrow = 0; initpix = 0; } else error("bad format");

	oddrow = initrow;
	for (y = 0; y < size[1]; y++) {
		oddpix = initpix;
		for (x = 0; x < size[0]; x++) {
			unsigned int r = get_word(&rgb16[0]);
			unsigned int g = get_word(&rgb16[2]);
			unsigned int b = get_word(&rgb16[4]);
			unsigned int v;
			if (!oddrow && !oddpix) v = g;
			if (!oddrow &&  oddpix) v = r;
			if ( oddrow && !oddpix) v = b;
			if ( oddrow &&  oddpix) v = g;
			write_pixel(p, v);
			rgb16 += 3 * 2;
			p += 2;
			oddpix ^= 1;
		}
		oddrow ^= 1;
	}

	return buf;
}

int main(int argc, char *argv[])
{
	char *input, *output;
	int size[2];
	unsigned char *rgb16, *raw10;

	if (argc != 3) error("give exactly two arguments: input and output filename\n");
	input = argv[1];
	output = argv[2];

	rgb16 = read_pnm(input, size);
	raw10 = rgb2raw10(rgb16, size, SGRBG10);
	write_file(output, raw10, size[0] * size[1] * 2);

	printf("Converted %ix%i pixels of data from %s to %s\n", size[0], size[1], input, output);

	free(rgb16);
	free(raw10);

	return 0;
}
