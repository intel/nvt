#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>

char *name = "v4l2n";

static void error(char *msg, ...)
{
	FILE *f = stdout;
	va_list ap;
 
	va_start(ap, msg);
	fprintf(f, "%s: ", name);
	vfprintf(f, msg, ap);
	fprintf(f, "\n");
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	printf("Usage: %s [-h]\n", name);
	printf("-h	Show this help\n");
}

static void get_options(int argc, char *argv[])
{
	while (1) {
		static struct option long_options[] = {
			{"help", 0, NULL, 'h'},
			{ 0, 0, 0, 0 }
		};

		int c = getopt_long(argc, argv, "h", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			exit(0);
		default:
			error("unknown option");
		}
	}
}

int main(int argc, char *argv[])
{
	printf("Starting %s\n", name);
	name = argv[0];
	get_options(argc, argv);
	return 0;
}
