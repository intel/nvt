#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

char *name = "v4l2n";

struct {
	char *device;
} args = {
	.device = "/dev/video0",
};

struct {
	int device;
} vars = {
	.device = -1,
};

static void error(char *msg, ...)
{
	FILE *f = stdout;
	va_list ap;
	int e = errno;
 
	va_start(ap, msg);
	fprintf(f, "%s: ", name);
	vfprintf(f, msg, ap);
	if (e)
		fprintf(f, ": %s (%i)", strerror(e), e);
	fprintf(f, "\n");
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	printf("Usage: %s [-h] [-d device]\n", name);
	printf(	"-h	Show this help\n"
		"-d	/dev/videoX device node\n");
}

static void get_options(int argc, char *argv[])
{
	while (1) {
		static struct option long_options[] = {
			{ "help", 0, NULL, 'h' },
			{ "device", 1, NULL, 'd' },
			{ 0, 0, 0, 0 }
		};

		int c = getopt_long(argc, argv, "hd:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			exit(0);

		case 'd':
			args.device = optarg;
			break;

		default:
			error("unknown option");
		}
	}
}

static void open_device()
{
	vars.device = open(args.device, 0);
	if (vars.device == -1)
		error("failed to open %s", args.device);
}

static void close_device()
{
	close(vars.device);
	vars.device = -1;
}

int main(int argc, char *argv[])
{
	printf("Starting %s\n", name);
	name = argv[0];
	get_options(argc, argv);
	open_device();
	close_device();
	return 0;
}
