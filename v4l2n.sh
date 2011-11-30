#!/bin/sh

P="$@"
AUTO=""

if [ "$P" = "" ]; then
	P="-h"
fi

if [ "$P" = "-a" ]; then
	echo "Enabling AUTO mode"
	AUTO=1
	rm -f /tmp/testimage_000.raw /tmp/testimage_000.pnm
	P="--input 1 --parm type=1 --fmt type=1,width=800,height=600,pixelformat=NV12 --reqbufs count=1,memory=USERPTR --capture=1 -o /sdcard/testimage_@.raw"
fi

adb shell 'mount -o remount -w /dev/block/mmcblk0p6 /system' && \
adb push v4l2n /system/ && \
adb shell "/system/v4l2n $P"

if [ "$AUTO" = "1" ]; then
	adb pull /sdcard/testimage_000.raw /tmp/
	if [ -s /tmp/testimage_000.raw ]; then
		./raw2pnm -x 800 -y 600 -f NV12 /tmp/testimage_000.raw /tmp/testimage_000.pnm && \
		gqview /tmp/testimage_000.pnm
	fi
fi
