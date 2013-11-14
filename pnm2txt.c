#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

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
 * Read PNM image file and return the image buffer containing size[0]*size[1] pixels.
 * Each pixel is R, G, B triplet, each component containing 16 bits, MSB first.
 * Therefore there will be size[0]*size[1]*2*3 bytes in the returned buffer.
 * Stores the returned image size into size.
 * Supports both 8-bit and 16-bit PNM files. For 8-bit PNM files, scale pixel
 * values from 0..255 to 0..65535.
 */
static unsigned char *read_pnm(char *input, int size[2])
{
	FILE *f;
	unsigned char *buf, *p;
	int r, maxval, byp;
	int y, x, c, b;

	f = fopen(input, "rb");
	if (!f) error("failed to open input file");
	r = fscanf(f, "P6 %u %u %u", &size[0], &size[1], &maxval);
	if (r != 3) error("bad ppm header");
	if (!isspace(fgetc(f))) error("bad ppm header blank");
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

/*
 * Input: raw bayer format size[0]*size[1] pixels. Each pixel is stored as
 *        16-bit value, LSB first, and has a value from 0..1023.
 * Output: From each four sequential input pixels p1 p2 p3 p4, five bytes are
 *         stored into output buffer as in MIPI standard:
 *         Byte#0  Byte#1  Byte#2  Byte#3  Byte#4
 *         p1[9:2] p2[9:2] p3[9:2] p4[9:2] p4[1:0]p3[1:0]p2[1:0]p1[1:0]
 *         Output buffer size is size[0] * size[1] * 5 / 4 bytes.
 *         outsize[0] = size[0] * 5 / 4 (with rounding) and
 *         outsize[1] = size[1]
 */
static unsigned char *raw2mipi(unsigned char *raw10, int size[2], int outsize[2])
{
	unsigned char *buf, *src, *dst, *srcp, *dstp;
	int y, x, x0;

	outsize[0] = (size[0] * 5 + 3) / 4;
	outsize[1] = size[1];
	src = raw10;
	dst = buf = calloc(outsize[0] * outsize[1], 1);

	for (y = 0; y < size[1]; y++) {
		srcp = src;
		dstp = dst;
		for (x = 0; x < size[0] - 3; x += 4) {
			unsigned char lsbs = 0;		/* Store all least significant 2-bits to 5th byte */
			for (x0 = 0; x0 < 4; x0++) {
				unsigned int v = srcp[0] | (srcp[1] << 8);
				*dstp++ = v >> 2;
				lsbs |= (v & 3) << (x0 * 2);
				srcp += 2;
			}
			*dstp++ = lsbs;
		}
		src += size[0] * 2;
		dst += outsize[0];
	}

	return buf;
}

static void write_txt(char *output, unsigned char *mipi, int size[2])
{
	FILE *f;
	int x, y;

	f = fopen(output, "wb");
	if (!f) error("failed to open output file");
	for (y = 0; y < size[1]; y++) {
		for (x = 0; x < size[0]; x++) {
			fprintf(f, "%02x", *mipi++);
			if (x < size[0] - 1) fputc(' ', f);
			else fputc('\n', f);
		}
	}
	fclose(f);
}

int main(int argc, char *argv[])
{
	char *input, *output;
	int size[2];
	int datasize[2];
	unsigned char *rgb16, *raw10, *mipi;

	if (argc != 3) error("give exactly too arguments: input and output filename\n");
	input = argv[1];
	output = argv[2];

	rgb16 = read_pnm(input, size);
	raw10 = rgb2raw10(rgb16, size, SGRBG10);
	mipi = raw2mipi(raw10, size, datasize);
	write_txt(output, mipi, datasize);

	printf("Converted %ix%i pixels of data from %s to %s\n", size[0], size[1], input, output);

	free(rgb16);
	free(raw10);
	free(mipi);

	return 0;
}
