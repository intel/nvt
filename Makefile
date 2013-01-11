OPT = -Wall -m32 -static -g -I.

.PHONY: all clean
all: v4l2n raw2pnm yuv2yuv

v4l2n: v4l2n.c linux/videodev2.h linux/atomisp.h
	gcc $(OPT) $@.c -o $@

raw2pnm: raw2pnm.c
	gcc $(OPT) $@.c -o $@

yuv2yuv: yuv2yuv.c
	gcc $(OPT) $@.c -o $@

clean:
	rm -f v4l2n raw2pnm yuv2yuv


