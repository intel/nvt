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

