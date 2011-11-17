OPT = -Wall -m32 -static -g

.PHONY: all clean
all: v4l2n

v4l2n: v4l2n.c
	gcc $(OPT) $@.c -o $@

clean:
	rm -f v4l2n

