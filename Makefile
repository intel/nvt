.PHONY: all
all: v4l2n

v4l2n: v4l2n.c
	gcc -Wall -static $@.c -o $@

