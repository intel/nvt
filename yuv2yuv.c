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
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static char *name = "yuv2yuv";

static int verbosity = 2;

static void print(int lvl, char *msg, ...)
{
	va_list ap;

	if (verbosity < lvl)
		return;

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
}

static void error(char *msg, ...)
{
	FILE *f = stdout;
	va_list ap;
	int e = errno;

	va_start(ap, msg);
	fprintf(f, "%s: ", name);
	vfprintf(f, msg, ap);
	if (e)
		fprintf(f, ": %s (%i)", strerror(e), e);
	fprintf(f, "\n");
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	print(1, "Convert planar YUV 4:2:0 to interleaved NV12 or vice versa\n");
	print(1, "Usage: %s [-r] [-x width] [-y height] [inputfile] [outputfile]\n", name);
	print(1, "-r: Convert from interleaved NV12 to planar YUV 4:2:0\n");
	print(1, "    Default is to convert planar YUV 4:2:0 to interleaved NV12\n");
	print(1, "-x, -y: Image width and height\n");
}

static long fsize(FILE *f)
{
	int i;
	long p, s, r = 0;

	p = ftell(f);
	if (p == -1)
		return -1;
	i = fseek(f, 0, SEEK_END);
	if (i == -1)
		r = -1;
	s = ftell(f);
	if (s == -1)
		r = -1;
	i = fseek(f, p, SEEK_SET);
	if (i != p)
		r = -1;

	return r == 0 ? s : r;
}

/* Conversion routines from Aleksandar Sutic */
static int yuv420_to_nv12(void *image, int width, int height)
{
	unsigned char *u, *v, *uv, *uv_head;
	int w, h, len = width * height / 2;

	uv_head = uv = calloc(1, len);			if (!uv) error("out of memory");
	u = (unsigned char *) image + 4 * width * height / 4;
	v = (unsigned char *) image + 5 * width * height / 4;

	for (h=0; h<height/2; h++) {
		for (w=0; w<width; w+=2) {
			*(uv + w) = *(u++);
			*(uv + w + 1) = *(v++);
		}
		uv += width;
	}

	memcpy(((unsigned char *) image) + width * height, uv_head, len);
	free(uv_head);

	return 0;
}

static int nv12_to_yuv420(void *image, int width, int height)
{
	unsigned char *u, *v, *uv, *u_head, *v_head;
	int w, h, len = width * height / 2;

	u_head = u = calloc(1, len / 2);		if (!u) error("out of memory");
	v_head = v = calloc(1, len / 2);		if (!v) error("out of memory");
	uv = (unsigned char *) image + width * height;

	for (h=0; h<height/2; h++) {
		for (w=0; w<width; w+=2) {
			*(u++) = *(uv + w);
			*(v++) = *(uv + w + 1);
		}
		uv += width;
	}

	memcpy(((unsigned char *) image) + 4 * width * height / 4, u_head, len / 2);
	memcpy(((unsigned char *) image) + 5 * width * height / 4, v_head, len / 2);

	free(u_head);
	free(v_head);

	return 0;
}

int main(int argc, char *argv[])
{
	int to_planar = 0;
	char *in_name = NULL;
	char *out_name = NULL;
	void *buffer;
	int size;
	FILE *f;
	int i;

	int opt;
	int width = -1;
	int height = -1;

	while ((opt = getopt(argc, argv, "hrx:y:")) != -1) {
		switch (opt) {
		case 'r':
			to_planar = 1;
			break;
		case 'x':
			width = atoi(optarg);
			break;
		case 'y':
			height = atoi(optarg);
			break;
		default:
			usage();
			return -1;
	        }
	}

	if (optind+2 > argc)
		error("input or output filename missing");

	in_name = argv[optind++];
	out_name = argv[optind++];

	print(1, "Reading file `%s', %ix%i\n", in_name, width, height);
	f = fopen(in_name, "rb");
	if (!f) error("failed opening file");
	size = fsize(f);
	if (size == -1) error("error checking file size");
	if (size < width*height + width*height/2) error("file too small");
	if (size > width*height + width*height/2) print(1, "warning: file has extra bytes");
	size = width*height + width*height/2;
	print(2, "File size %i bytes\n", size);
	buffer = malloc(size);
	if (!buffer) error("can not allocate input buffer");
	i = fread(buffer, size, 1, f);
	if (i != 1) error("failed reading file");
	fclose(f);

	if (to_planar) {
		i = nv12_to_yuv420(buffer, width, height);
	} else {
		i = yuv420_to_nv12(buffer, width, height);
	}
	if (i < 0) error("failed to convert image");

	print(1, "Writing file `%s', %i bytes\n", out_name, size);
	f = fopen(out_name, "wb");
	if (!f) error("failed opening file");
	i = fwrite(buffer, size, 1, f);
	if (i != 1) error("failed writing file");
	fclose(f);

	free(buffer);
	return 0;
}
