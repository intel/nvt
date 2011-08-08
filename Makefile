.PHONY: all clean
all: v4l2n

v4l2n: v4l2n.c
	gcc -Wall -m32 -static $@.c -o $@

clean:
	rm -f v4l2n

