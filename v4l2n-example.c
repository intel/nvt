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
#include "v4l2n.h"

void error(void)
{
	printf("libv4l2n execution failed");
	exit(1);
}

int main(int argc, char *argv[])
{
	printf("libv4l2n example\n");

	if (v4l2n_init() < 0) error();
	if (v4l2n_process_commands(argc, argv) < 0) error();
	if (v4l2n_cleanup() < 0) error();

	return 0;
}

