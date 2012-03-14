#!/bin/sh

# Some default settings. Override with environment variables
if [ "$V4L2N_WIDTH"  = "" ]; then V4L2N_WIDTH=800;     fi
if [ "$V4L2N_HEIGHT" = "" ]; then V4L2N_HEIGHT=600;    fi
if [ "$V4L2N_FORMAT" = "" ]; then V4L2N_FORMAT=NV12;   fi
if [ "$V4L2N_INPUT"  = "" ]; then V4L2N_INPUT=1;       fi
if [ "$V4L2N_VIEWER" = "" ]; then V4L2N_VIEWER=gqview; fi
if [ "$V4L2N_DIR"    = "" ]; then V4L2N_DIR=/cache;    fi

P="$@"
AUTO=""

if [ "$P" = "" ]; then
	P="-h"
fi

if [ "$P" = "-a" ]; then
	echo "Enabling AUTO mode"
	AUTO=1
	rm -f /tmp/testimage_*.raw /tmp/testimage_*.pnm
	P=""
	P="$P --input $V4L2N_INPUT"
	P="$P --parm type=1"
	P="$P --fmt type=1,width=$V4L2N_WIDTH,height=$V4L2N_HEIGHT,pixelformat=$V4L2N_FORMAT --fmt '?'"
	P="$P --reqbufs count=2,memory=USERPTR"
	P="$P --capture=2"
	P="$P -o $V4L2N_DIR/testimage_@.raw"
fi

adb shell 'mount -o remount -w /dev/block/mmcblk0p6 /system'
adb shell rm "$V4L2N_DIR/testimage_*.raw"
adb push v4l2n /system/ && \
adb shell "/system/v4l2n --enuminput" && \
echo "/system/v4l2n $P" && \
adb shell "/system/v4l2n $P"

if [ "$AUTO" = "1" ]; then
	for i in 0 1; do
		adb pull $V4L2N_DIR/testimage_00$i.raw /tmp/
		if [ -s /tmp/testimage_00$i.raw ]; then
			./raw2pnm -x $V4L2N_WIDTH -y $V4L2N_HEIGHT -f $V4L2N_FORMAT /tmp/testimage_00$i.raw /tmp/testimage_00$i.pnm
		fi
	done
	if [ -s /tmp/testimage_001.pnm ]; then
		$V4L2N_VIEWER /tmp/testimage_001.pnm
	else
		if [ -s /tmp/testimage_000.pnm ]; then
			$V4L2N_VIEWER /tmp/testimage_000.pnm
		fi
	fi
fi
