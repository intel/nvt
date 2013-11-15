OPT = -Wall -m32 -static -g -I.
PROGS = v4l2n raw2pnm yuv2yuv txt2raw pnm2txt

.PHONY: all clean
all: $(PROGS)

v4l2n: v4l2n.c linux/videodev2.h linux/v4l2-controls.h linux/v4l2-common.h linux/compiler.h linux/atomisp.h
	gcc $(OPT) $@.c -o $@

raw2pnm: raw2pnm.c
	gcc $(OPT) $@.c -o $@

yuv2yuv: yuv2yuv.c
	gcc $(OPT) $@.c -o $@

txt2raw: txt2raw.c
	gcc $(OPT) $@.c -o $@

pnm2txt: pnm2txt.c
	gcc $(OPT) $@.c -o $@

clean:
	rm -f $(PROGS)


