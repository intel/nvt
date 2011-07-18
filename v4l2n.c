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
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "linux/atomisp.h"

char *name = "v4l2n";

#define FALSE		0
#define TRUE		(!FALSE)

#define STRINGIFY_(x)	#x
#define STRINGIFY(x)	STRINGIFY_(x)

typedef unsigned char bool;

struct {
	char *device;
	bool idsensor;
	bool idflash;
} args = {
	.device = "/dev/video0",
};

struct {
	int fd;
} vars = {
	.fd = -1,
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

#define xioctl(io, arg) xioctl_(#io, io, arg)

static void xioctl_(char *ios, int ion, void *arg)
{
	int r = ioctl(vars.fd, ion, arg);
	if (r)
		error("%s failed", ios);
}

static void usage(void)
{
	printf("Usage: %s [-h] [-d device] [--idsensor] [--idflash]\n", name);
	printf(	"-h	Show this help\n"
		"-d	/dev/videoX device node\n"
		"--idsensor	Get sensor identification\n"
		"--idflash	Get flash identification\n");
}

static void get_options(int argc, char *argv[])
{
	while (1) {
		static struct option long_options[] = {
			{ "help", 0, NULL, 'h' },
			{ "device", 1, NULL, 'd' },
			{ "idsensor", 0, NULL, 1001 },
			{ "idflash", 0, NULL, 1002 },
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

		case 1001:
			args.idsensor = TRUE;
			break;

		case 1002:
			args.idflash = TRUE;
			break;

		default:
			error("unknown option");
		}
	}
}

static void open_device()
{
	vars.fd = open(args.device, 0);
	if (vars.fd == -1)
		error("failed to open %s", args.device);
}

static void close_device()
{
	close(vars.fd);
	vars.fd = -1;
}

int main(int argc, char *argv[])
{
	printf("Starting %s\n", name);
	name = argv[0];
	get_options(argc, argv);
	open_device();

	if (args.idsensor) {
		struct atomisp_model_id id;
		xioctl(ATOMISP_IOC_G_SENSOR_MODEL_ID, &id);
		printf("ATOMISP_IOC_G_SENSOR_MODEL_ID: [%s]\n", id.model);
	}

	if (args.idflash) {
		struct atomisp_model_id id;
		xioctl(ATOMISP_IOC_G_FLASH_MODEL_ID, &id);
		printf("ATOMISP_IOC_G_FLASH_MODEL_ID: [%s]\n", id.model);
	}

	close_device();
	return 0;
}
