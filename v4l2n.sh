#!/bin/sh

P="$@"
if [ "$P" = "" ]; then
	P="-h"
fi


adb shell 'mount -o remount -w /dev/block/mmcblk0p6 /system' && \
adb push v4l2n /system/ && \
adb shell "/system/v4l2n $P"
