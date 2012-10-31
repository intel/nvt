#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <sys/wait.h>
#include "linux/videodev2.h"

#define USE_ATOMISP	1

#if USE_ATOMISP
#include "linux/atomisp.h"
#endif

#define FALSE		0
#define TRUE		(!FALSE)

#define STRINGIFY_(x)	#x
#define STRINGIFY(x)	STRINGIFY_(x)

#define MIN(a,b)	((a) <= (b) ? (a) : (b))
#define MAX(a,b)	((a) >= (b) ? (a) : (b))
#define CLEAR(x)	memset(&(x), 0, sizeof(x));
#define SIZE(x)		(sizeof(x)/sizeof((x)[0]))
#define BIT(x)		(1<<(x))
static unsigned long int PAGE_SIZE;
static unsigned long int PAGE_MASK;
#define PAGE_ALIGN(x)	((typeof(x))(((unsigned long int)(x) + PAGE_SIZE - 1) & PAGE_MASK))

#define MAX_RING_BUFFERS	20
#define MAX_CAPTURE_BUFFERS	100
#define MAX_BUFFER_SIZE		(64*1024*1024)

typedef unsigned char bool;

static char *name = "v4l2n";

/* Holds information returned by QUERYBUF and needed
 * for subsequent QBUF/DQBUF. Buffers are reused for long sequences. */
struct ring_buffer {
	struct v4l2_buffer querybuf;
	bool queued;
	void *malloc_p;		/* Points to address returned by malloc() */
	void *mmap_p;		/* Points to address returned by mmap() */
	void *start;		/* Points to beginning of data in the buffer */
};

/* Used for saving each captured frame if saving was requested */
struct capture_buffer {
	struct v4l2_pix_format pix_format;
	void *image;
	int length;		/* Length of data in the buffer in bytes */
};

static struct {
	char *output;
	int verbosity;
	int fd;
	bool save_images;
	struct v4l2_format format;
	struct v4l2_requestbuffers reqbufs;
	struct ring_buffer ring_buffers[MAX_RING_BUFFERS];
	int num_capture_buffers;
	struct capture_buffer capture_buffers[MAX_CAPTURE_BUFFERS];
	bool streaming;
	struct timeval start_time;
} vars = {
	.verbosity = 2,
	.fd = -1,
	.save_images = TRUE,
	.reqbufs.count = 2,
	.reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	.reqbufs.memory = V4L2_MEMORY_USERPTR,
};

struct symbol_list {
	int id;
	const char *symbol;
};
#define SYMBOL_END	{ -1, NULL }

struct token_list {
	char id;
	int flags;
	const char *token;
	const struct symbol_list *symbols;
};
#define TOKEN_END	{ 0, 0, NULL, NULL }
#define TOKEN_F_OPTARG	BIT(0)		/* Token may have optional argument */
#define TOKEN_F_ARG	BIT(1)		/* Token must have argument or it is an error */
#define TOKEN_F_ARG2	BIT(2)		/* Token may have 2 integer arguments */
#define TOKEN_F_ARG4	BIT(3)		/* Token may have 4 integer arguments */

#define V4L2_CID	"V4L2_CID_"
#define CONTROL(id)	{ V4L2_CID_##id, (#id) }
static const struct symbol_list controls[] = {
	CONTROL(BASE),
	CONTROL(USER_BASE),
	CONTROL(PRIVATE_BASE),
	CONTROL(USER_CLASS),
	CONTROL(BRIGHTNESS),
	CONTROL(CONTRAST),
	CONTROL(SATURATION),
	CONTROL(HUE),
	CONTROL(AUDIO_VOLUME),
	CONTROL(AUDIO_BALANCE),
	CONTROL(AUDIO_BASS),
	CONTROL(AUDIO_TREBLE),
	CONTROL(AUDIO_MUTE),
	CONTROL(AUDIO_LOUDNESS),
	CONTROL(BLACK_LEVEL),
	CONTROL(AUTO_WHITE_BALANCE),
	CONTROL(DO_WHITE_BALANCE),
	CONTROL(RED_BALANCE),
	CONTROL(BLUE_BALANCE),
	CONTROL(GAMMA),
	CONTROL(WHITENESS),
	CONTROL(EXPOSURE),
	CONTROL(AUTOGAIN),
	CONTROL(GAIN),
	CONTROL(HFLIP),
	CONTROL(VFLIP),
	CONTROL(HCENTER),
	CONTROL(VCENTER),
	CONTROL(POWER_LINE_FREQUENCY),
	CONTROL(HUE_AUTO),
	CONTROL(WHITE_BALANCE_TEMPERATURE),
	CONTROL(SHARPNESS),
	CONTROL(BACKLIGHT_COMPENSATION),
	CONTROL(CHROMA_AGC),
	CONTROL(COLOR_KILLER),
	CONTROL(COLORFX),
	CONTROL(AUTOBRIGHTNESS),
	CONTROL(BAND_STOP_FILTER),
	CONTROL(ROTATE),
	CONTROL(BG_COLOR),
	CONTROL(CHROMA_GAIN),
	CONTROL(ILLUMINATORS_1),
	CONTROL(ILLUMINATORS_2),
	CONTROL(LASTP1),
	CONTROL(MPEG_BASE),
	CONTROL(MPEG_CLASS),
	CONTROL(MPEG_STREAM_TYPE),
	CONTROL(MPEG_STREAM_PID_PMT),
	CONTROL(MPEG_STREAM_PID_AUDIO),
	CONTROL(MPEG_STREAM_PID_VIDEO),
	CONTROL(MPEG_STREAM_PID_PCR),
	CONTROL(MPEG_STREAM_PES_ID_AUDIO),
	CONTROL(MPEG_STREAM_PES_ID_VIDEO),
	CONTROL(MPEG_STREAM_VBI_FMT),
	CONTROL(MPEG_AUDIO_SAMPLING_FREQ),
	CONTROL(MPEG_AUDIO_ENCODING),
	CONTROL(MPEG_AUDIO_L1_BITRATE),
	CONTROL(MPEG_AUDIO_L2_BITRATE),
	CONTROL(MPEG_AUDIO_L3_BITRATE),
	CONTROL(MPEG_AUDIO_MODE),
	CONTROL(MPEG_AUDIO_MODE_EXTENSION),
	CONTROL(MPEG_AUDIO_EMPHASIS),
	CONTROL(MPEG_AUDIO_CRC),
	CONTROL(MPEG_AUDIO_MUTE),
	CONTROL(MPEG_AUDIO_AAC_BITRATE),
	CONTROL(MPEG_AUDIO_AC3_BITRATE),
	CONTROL(MPEG_VIDEO_ENCODING),
	CONTROL(MPEG_VIDEO_ASPECT),
	CONTROL(MPEG_VIDEO_B_FRAMES),
	CONTROL(MPEG_VIDEO_GOP_SIZE),
	CONTROL(MPEG_VIDEO_GOP_CLOSURE),
	CONTROL(MPEG_VIDEO_PULLDOWN),
	CONTROL(MPEG_VIDEO_BITRATE_MODE),
	CONTROL(MPEG_VIDEO_BITRATE),
	CONTROL(MPEG_VIDEO_BITRATE_PEAK),
	CONTROL(MPEG_VIDEO_TEMPORAL_DECIMATION),
	CONTROL(MPEG_VIDEO_MUTE),
	CONTROL(MPEG_VIDEO_MUTE_YUV),
	CONTROL(MPEG_CX2341X_BASE),
	CONTROL(MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE),
	CONTROL(MPEG_CX2341X_VIDEO_SPATIAL_FILTER),
	CONTROL(MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE),
	CONTROL(MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE),
	CONTROL(MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE),
	CONTROL(MPEG_CX2341X_VIDEO_TEMPORAL_FILTER),
	CONTROL(MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE),
	CONTROL(MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM),
	CONTROL(MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP),
	CONTROL(MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM),
	CONTROL(MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP),
	CONTROL(MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS),
	CONTROL(CAMERA_CLASS_BASE),
	CONTROL(CAMERA_CLASS),
	CONTROL(EXPOSURE_AUTO),
	CONTROL(EXPOSURE_ABSOLUTE),
	CONTROL(EXPOSURE_AUTO_PRIORITY),
	CONTROL(PAN_RELATIVE),
	CONTROL(TILT_RELATIVE),
	CONTROL(PAN_RESET),
	CONTROL(TILT_RESET),
	CONTROL(PAN_ABSOLUTE),
	CONTROL(TILT_ABSOLUTE),
	CONTROL(FOCUS_ABSOLUTE),
	CONTROL(FOCUS_RELATIVE),
	CONTROL(FOCUS_AUTO),
	CONTROL(ZOOM_ABSOLUTE),
	CONTROL(ZOOM_RELATIVE),
	CONTROL(ZOOM_CONTINUOUS),
	CONTROL(PRIVACY),
	CONTROL(IRIS_ABSOLUTE),
	CONTROL(IRIS_RELATIVE),
	CONTROL(FM_TX_CLASS_BASE),
	CONTROL(FM_TX_CLASS),
	CONTROL(RDS_TX_DEVIATION),
	CONTROL(RDS_TX_PI),
	CONTROL(RDS_TX_PTY),
	CONTROL(RDS_TX_PS_NAME),
	CONTROL(RDS_TX_RADIO_TEXT),
	CONTROL(AUDIO_LIMITER_ENABLED),
	CONTROL(AUDIO_LIMITER_RELEASE_TIME),
	CONTROL(AUDIO_LIMITER_DEVIATION),
	CONTROL(AUDIO_COMPRESSION_ENABLED),
	CONTROL(AUDIO_COMPRESSION_GAIN),
	CONTROL(AUDIO_COMPRESSION_THRESHOLD),
	CONTROL(AUDIO_COMPRESSION_ATTACK_TIME),
	CONTROL(AUDIO_COMPRESSION_RELEASE_TIME),
	CONTROL(PILOT_TONE_ENABLED),
	CONTROL(PILOT_TONE_DEVIATION),
	CONTROL(PILOT_TONE_FREQUENCY),
	CONTROL(TUNE_PREEMPHASIS),
	CONTROL(TUNE_POWER_LEVEL),
	CONTROL(TUNE_ANTENNA_CAPACITOR),

#if USE_ATOMISP
	CONTROL(ATOMISP_BAD_PIXEL_DETECTION),
	CONTROL(ATOMISP_POSTPROCESS_GDC_CAC),
	CONTROL(ATOMISP_VIDEO_STABLIZATION),
	CONTROL(ATOMISP_FIXED_PATTERN_NR),
	CONTROL(ATOMISP_FALSE_COLOR_CORRECTION),
	CONTROL(ATOMISP_LOW_LIGHT),
	CONTROL(FOCAL_ABSOLUTE),
	CONTROL(FNUMBER_ABSOLUTE),
	CONTROL(FNUMBER_RANGE),
	CONTROL(REQUEST_FLASH),
	CONTROL(FLASH_INTENSITY),
	CONTROL(FLASH_STATUS),
	CONTROL(FLASH_TORCH_INTENSITY),
	CONTROL(FLASH_INDICATOR_INTENSITY),
	CONTROL(FLASH_TIMEOUT),
	CONTROL(FLASH_STROBE),
	CONTROL(FLASH_MODE),
	CONTROL(VCM_SLEW),
	CONTROL(VCM_TIMEING),
	CONTROL(TEST_PATTERN),
	CONTROL(FOCUS_STATUS),
	CONTROL(BIN_FACTOR_HORZ),
	CONTROL(BIN_FACTOR_VERT),
	CONTROL(G_SKIP_FRAMES),
#endif
	SYMBOL_END
};

#define V4L2_BUF_TYPE	"V4L2_BUF_TYPE_"
#define BUFTYPE(id)	{ V4L2_BUF_TYPE_##id, (#id) }
static const struct symbol_list v4l2_buf_types[] = {
	BUFTYPE(VIDEO_CAPTURE),
	// BUFTYPE(VIDEO_CAPTURE_MPLANE),
	BUFTYPE(VIDEO_OUTPUT),
	// BUFTYPE(VIDEO_OUTPUT_MPLANE),
	BUFTYPE(VIDEO_OVERLAY),
	BUFTYPE(VBI_CAPTURE),
	BUFTYPE(VBI_OUTPUT),
	BUFTYPE(SLICED_VBI_CAPTURE),
	BUFTYPE(SLICED_VBI_OUTPUT),
	BUFTYPE(VIDEO_OUTPUT_OVERLAY),
	BUFTYPE(PRIVATE),
	SYMBOL_END
};

#define V4L2_PIX_FMT	"V4L2_PIX_FMT_"
#define PIXFMT(id)	{ V4L2_PIX_FMT_##id, (#id) }
static const struct symbol_list pixelformats[] = {
	PIXFMT(RGB332),
	PIXFMT(RGB444),
	PIXFMT(RGB555),
	PIXFMT(RGB565),
	PIXFMT(RGB555X),
	PIXFMT(RGB565X),
	PIXFMT(BGR24),
	PIXFMT(RGB24),
	PIXFMT(BGR32),
	PIXFMT(RGB32),
	PIXFMT(GREY),
	PIXFMT(Y4),
	PIXFMT(Y6),
	PIXFMT(Y10),
	PIXFMT(Y16),
	PIXFMT(PAL8),
	PIXFMT(YVU410),
	PIXFMT(YVU420),
	PIXFMT(YUYV),
	PIXFMT(YYUV),
	PIXFMT(YVYU),
	PIXFMT(UYVY),
	PIXFMT(VYUY),
	PIXFMT(YUV422P),
	PIXFMT(YUV411P),
	PIXFMT(Y41P),
	PIXFMT(YUV444),
	PIXFMT(YUV555),
	PIXFMT(YUV565),
	PIXFMT(YUV32),
	PIXFMT(YUV410),
	PIXFMT(YUV420),
	PIXFMT(HI240),
	PIXFMT(HM12),
	PIXFMT(NV12),
	PIXFMT(NV21),
	PIXFMT(NV16),
	PIXFMT(NV61),
	PIXFMT(SBGGR8),
	PIXFMT(SGBRG8),
	PIXFMT(SGRBG8),
	PIXFMT(SRGGB8),
	PIXFMT(SBGGR10),
	PIXFMT(SGBRG10),
	PIXFMT(SGRBG10),
	PIXFMT(SRGGB10),
	PIXFMT(SBGGR12),
	PIXFMT(SGBRG12),
	PIXFMT(SGRBG12),
	PIXFMT(SRGGB12),
	PIXFMT(SGRBG10DPCM8),
	PIXFMT(SBGGR16),
	PIXFMT(MJPEG),
	PIXFMT(JPEG),
	PIXFMT(DV),
	PIXFMT(MPEG),
	PIXFMT(CPIA1),
	PIXFMT(WNVA),
	PIXFMT(SN9C10X),
	PIXFMT(SN9C20X_I420),
	PIXFMT(PWC1),
	PIXFMT(PWC2),
	PIXFMT(ET61X251),
	PIXFMT(SPCA501),
	PIXFMT(SPCA505),
	PIXFMT(SPCA508),
	PIXFMT(SPCA561),
	PIXFMT(PAC207),
	PIXFMT(MR97310A),
	PIXFMT(SN9C2028),
	PIXFMT(SQ905C),
	PIXFMT(PJPG),
	PIXFMT(OV511),
	PIXFMT(OV518),
	PIXFMT(STV0680),
	PIXFMT(TM6000),
	SYMBOL_END
};

static const struct symbol_list v4l2_memory[] = {
	{ V4L2_MEMORY_MMAP, "MMAP" },
	{ V4L2_MEMORY_USERPTR, "USERPTR" },
	SYMBOL_END
};

static void print(int lvl, char *msg, ...)
{
	va_list ap;

	if (vars.verbosity < lvl)
		return;

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
}

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
	int fd = vars.fd;
	int r = ioctl(fd, ion, arg);
	if (r)
		error("%s failed on fd %i", ios, fd);
}

static int xioctl_try(int ion, void *arg)
{
	int r = ioctl(vars.fd, ion, arg);
	if (r != 0) {
		int e = -errno;
		if (e == 0)
			return INT_MIN;
		return e;
	}
	return 0;
}

static void usage(void)
{
	print(1,"Usage: %s [-h] [-d device]\n", name);
	print(1,"-h		Show this help\n"
		"--help\n"
		"-d		/dev/videoX device node\n"
		"--device\n"
		"--querycap	Query device capabilities (VIDIOC_QUERYCAP)\n"
		"-i		Set/get input device (VIDIOC_S/G_INPUT)\n"
		"--input\n"
		"--enuminput	Enumerate available input devices (VIDIOC_ENUMINPUT)\n"
		"-o FILENAME	Set output filename for captured images\n"
		"--output\n"
		"--parm		Set/get parameters (VIDIOC_S/G_PARM)\n"
		"-t		Try format (VIDIOC_TRY_FORMAT)\n"
		"--try_fmt\n"
		"-f		Set/get format (VIDIOC_S/G_FMT)\n"
		"--fmt\n"
		"-r		Request buffers (VIDIOC_REQBUFS)\n"
		"--reqbufs\n"
		"-s		VIDIOC_STREAMON: start streaming\n"
		"--streamon\n"
		"-e		VIDIOC_STREAMOFF: stop streaming\n"
		"--streamoff\n"
		"-a [N]		Capture N [1] buffers with QBUF/DQBUF\n"
		"--capture=[N]\n"
		"--stream=[N]	Capture N [1] buffers with QBUF/DQBUF and leave streaming on\n"
		"-x [EC,EF,G]	Set coarse_itg, fine_itg, and gain [ATOMISP]\n"
		"--exposure	(ATOMISP_IOC_S_EXPOSURE)\n"
		"--priv_data[=file]\n"
		"		Read and optionally save to file sensor OTP/EEPROM data\n"
		"		(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA)\n"
		"--motor_priv_data[=file]\n"
		"		Read and optionally save to file af motor OTP/EEPROM data\n"
		"		(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA)\n"
		"--ctrl-list	List supported V4L2 controls\n"
		"--ctrl=$	Request given V4L2 controls\n"
		"--fmt-list	List supported pixel formats\n"
		"-c $	(VIDIOC_QUERYCTRL / VIDIOC_S/G_CTRL / VIDIOC_S/G_EXT_CTRLS)\n"
		"-w [x]		Wait of x seconds (may be fractional)\n"
		"--wait\n"
		"--waitkey	Wait for character input from stdin\n"
		"--shell=CMD	Run shell command CMD\n"
		"\n"
		"List of V4L2 controls syntax: <[V4L2_CID_]control_name_or_id>[+][=value|?|#][,...]\n"
		"where control_name_or_id is either symbolic name or numerical id.\n"
		"When + is given, use extended controls, otherwise use old-style control call.\n"
		"\"=\" sets the value, \"?\" gets the current value, and \"#\" shows control info.\n");
}

static void symbol_dump(const char *prefix, const struct symbol_list *list)
{
	int i;
	for (i = 0; list[i].symbol != NULL; i++)
		print(0, "%s%s [0x%08X]\n", prefix, list[i].symbol, list[i].id);
}

static int symbol_get(const struct symbol_list *list, const char **symbol)
{
	const char *start;
	const char *end;
	int r, i;

	if (!symbol || !symbol[0])
		error("symbol missing");

	start = *symbol;

	if (isdigit(start[0])) {
		r = strtol(start, (char **)&end, 0);
		if (end == start)
			error("zero-length symbol value");
	} else {
		end = start;
		while (isalnum(*end) || *end == '_') end++;
		if (start == end)
			error("zero-length symbol");
		if (!list)
			error("only numeric value allowed");
		for (i=0; list[i].symbol!=NULL; i++) {
			if (strncasecmp(start, list[i].symbol, end-start) == 0)
				break;
		}
		if (list[i].symbol == NULL)
			error("symbol `%s' not found", start);
		r = list[i].id;
	}

	*symbol = end;
	return r;
}

static void value_get(int *val, int n, const char **ptr)
{
	const char *s = *ptr;
	bool paren = FALSE;
	int i;

	if (!s)
		error("missing integer values");

	if (*s == '(') {
		s++;
		paren = TRUE;
	}

	for (i=0; i<n; i++) {
		char *a;
		val[i] = strtol(s, &a, 0);
		if (a == s)
			error("failure parsing %ith integer argument", i);
		s = a;
		if (i+1 < n) {
			if (*s!=',' && *s!='/' && *s!=';')
				error("missing %ith integer argument", i + 1);
			s++;
		}
	}

	if (paren) {
		if (*s != ')')
			error("missing closing parenthesis");
		s++;
	}

	*ptr = s;
}

static int token_get(const struct token_list *list, const char **token, int val[4])
{
	const char *start;
	const char *end;
	int r, i;

	if (!token || !token[0])
		error("token missing");

	CLEAR(*val);
	end = start = *token;
	while (isalpha(*end) || *end == '.') end++;
	if (start == end)
		error("zero-length token");
	for (i=0; list[i].token!=NULL; i++) {
		if (strncasecmp(start, list[i].token, end-start) == 0)
			break;
	}
	if (list[i].token == NULL)
		error("unrecognized token `%s'", start);
	r = list[i].id;

	if (*end == '=' || *end == ':') {
		if (!(list[i].flags & (TOKEN_F_ARG | TOKEN_F_OPTARG)))
			error("token has argument but should not have");
		end++;
		if (list[i].symbols) {
			val[0] = symbol_get(list[i].symbols, &end);
		} else if (list[i].flags & TOKEN_F_ARG4) {
			value_get(val, 4, &end);
		} else if (list[i].flags & TOKEN_F_ARG2) {
			value_get(val, 2, &end);
		} else {
			value_get(val, 1, &end);
		}
	} else {
		if (list[i].flags & TOKEN_F_ARG)
			error("token does not have an argument but should have");
	}

	/* Go to the beginning of next token in the list */
	if (*end && *end==',') end++;
	*token = end;
	return r;
}

static const char *symbol_str(int id, const struct symbol_list list[])
{
	static char buffer[200];
	int i;

	for (i=0; list[i].symbol; i++)
		if (list[i].id == id)
			break;

	if (list[i].symbol) {
		if (id < 1000)
			sprintf(buffer, "%s [%i]", list[i].symbol, id);
		else
			sprintf(buffer, "%s [0x%08X]", list[i].symbol, id);
	} else {
		sprintf(buffer, "%i", id);
	}
	return buffer;
}

static const char *symbol_flag_str(int id, const struct symbol_list list[])
{
	const int MARGIN = 32;
	static char buffer[512];
	int len = 0;
	int i;

	for (i=0; list[i].symbol; i++) {
		if ((id & list[i].id) == list[i].id) {
			if (len == 0) {
				strcpy(buffer, list[i].symbol);
			} else {
				strcat(buffer, " ");
				strcat(buffer, list[i].symbol);
				len++;
			}
			len += strlen(list[i].symbol);
			if (len > sizeof(buffer)-MARGIN)
				error("buffer outrun while printing flags");
		}
	}

	if (len == 0) {
		len += sprintf(buffer, "0x%08X", id);
	} else {
		len += sprintf(&buffer[len], " [0x%08X]", id);
	}
	return buffer;
}

static void print_time(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0) error("gettimeofday failed");
	tv.tv_sec -= vars.start_time.tv_sec;
	tv.tv_usec -= vars.start_time.tv_usec;
	while (tv.tv_usec < 0) {
		tv.tv_usec += 1000000;
		tv.tv_sec -= 1;
	}
	print(1, "[%3i.%06i]\n", tv.tv_sec, tv.tv_usec);
}

static void write_file(const char *name, const char *data, int size)
{
	FILE *f;
	int r;

	f = fopen(name, "wb");
	if (!f)
		error("can not open file `%s'", name);
	r = fwrite(data, size, 1, f);
	if (r != 1)
		error("failed to write data to file");
	r = fclose(f);
	if (r != 0)
		error("failed to close file");
}

static void vidioc_enuminput(void)
{
	static const struct symbol_list type[] = {
		{ V4L2_INPUT_TYPE_TUNER, "V4L2_INPUT_TYPE_TUNER" },
		{ V4L2_INPUT_TYPE_CAMERA, "V4L2_INPUT_TYPE_CAMERA" },
		SYMBOL_END
	};

	struct v4l2_input p;
	int r, i = 0;

	do {
		CLEAR(p);
		p.index = i++;
		print(1, "VIDIOC_ENUMINPUT (index=%i)\n", p.index);
		r = xioctl_try(VIDIOC_ENUMINPUT, &p);
		if (r == 0) {
			print(2, "> index:        %i\n", p.index);
			print(2, "> name:         `%.32s'\n", p.name);
			print(2, "> type:         %s\n", symbol_str(p.type, type));
			print(2, "> audioset:     %i\n", p.audioset);
			print(2, "> tuner:        %i\n", p.tuner);
			print(2, "> std:          %li\n", p.std);
			print(2, "> status:       0x%08X\n", p.status);
			print(2, "> capabilities: 0x%08X\n", p.capabilities);
		}
	} while (r == 0);
	if (r != -EINVAL)
		error("VIDIOC_ENUMINPUT failed");
}

static void streamon(bool on)
{
	enum v4l2_buf_type t = vars.reqbufs.type;
	print(1, "VIDIOC_STREAM%s (type=%s)\n", on ? "ON" : "OFF", symbol_str(t, v4l2_buf_types));
	if (vars.streaming == on)
		print(0, "warning: streaming is already in this state\n");
	if (on)
		xioctl(VIDIOC_STREAMON, &t);
	else
		xioctl(VIDIOC_STREAMOFF, &t);
	vars.streaming = on;
}

static void vidioc_parm(const char *s)
{
	static const struct symbol_list capturemode[] = {
		{ V4L2_MODE_HIGHQUALITY, "V4L2_MODE_HIGHQUALITY" },
#if USE_ATOMISP
		{ CI_MODE_PREVIEW, "CI_MODE_PREVIEW" },
		{ CI_MODE_VIDEO, "CI_MODE_VIDEO" },
		{ CI_MODE_STILL_CAPTURE, "CI_MODE_STILL_CAPTURE" },
		{ CI_MODE_NONE, "CI_MODE_NONE" },
#endif
		SYMBOL_END
	};
	static const struct symbol_list capability[] = {
		{ V4L2_CAP_TIMEPERFRAME, "V4L2_CAP_TIMEPERFRAME" },
		SYMBOL_END
	};
	static const struct token_list list[] = {
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'b', TOKEN_F_ARG, "capability", capability },
		{ 'c', TOKEN_F_ARG, "capturemode", capturemode },
		{ 'f', TOKEN_F_ARG|TOKEN_F_ARG2, "timeperframe", NULL },
		{ 'e', TOKEN_F_ARG, "extendedmode", NULL },
		{ 'r', TOKEN_F_ARG, "readbuffers", NULL },
		{ 'o', TOKEN_F_ARG, "outputmode", capturemode },
		TOKEN_END
	};
	struct v4l2_streamparm p;

	CLEAR(p);
	p.type = vars.reqbufs.type;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		case 'b': if (p.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) p.parm.capture.capability = val[0];
			  else  p.parm.output.capability = val[0];
			break;
		case 'c': p.parm.capture.capturemode = val[0]; break;
		case 'f': if (p.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
				p.parm.capture.timeperframe.numerator = val[0];
				p.parm.capture.timeperframe.denominator = val[1];
			} else {
				p.parm.output.timeperframe.numerator = val[0];
				p.parm.output.timeperframe.denominator = val[1];
			}
			break;
		case 'e': p.parm.capture.extendedmode = val[0]; break;
		case 'r': p.parm.capture.readbuffers = val[0]; break;
		case 'o': p.parm.output.outputmode = val[0]; break;
		}
	}

	if (*s == '?') {
		print(1, "VIDIOC_G_PARM\n");
		xioctl(VIDIOC_G_PARM, &p);
	} else {
		print(1, "VIDIOC_S_PARM\n");
	}

	print(2, ": type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	if (p.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		print(2, ": capability:    %s\n", symbol_str(p.parm.capture.capability, capability));
		print(2, ": capturemode:   %s\n", symbol_str(p.parm.capture.capturemode, capturemode));
		print(2, ": timeperframe:  %i/%i\n", p.parm.capture.timeperframe.numerator, p.parm.capture.timeperframe.denominator);
		print(2, ": extendedmode:  %i\n", p.parm.capture.extendedmode);
		print(2, ": readbuffers:   %i\n", p.parm.capture.readbuffers);
	} else if (p.type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		print(2, ": capability:    %s\n", symbol_str(p.parm.output.capability, capability));
		print(2, ": outputmode:    %s\n",  symbol_str(p.parm.capture.capturemode, capturemode));
		print(2, ": timeperframe:  %i/%i\n",  p.parm.capture.timeperframe.numerator, p.parm.capture.timeperframe.denominator);
	}

	if (*s != '?') {
		xioctl(VIDIOC_S_PARM, &p);
	}
}

static void vidioc_fmt(bool try, const char *s)
{
	static const struct token_list list[] = {
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'w', TOKEN_F_ARG, "width", NULL },
		{ 'h', TOKEN_F_ARG, "height", NULL },
		{ 'p', TOKEN_F_ARG, "pixelformat", pixelformats },
		{ 'f', TOKEN_F_ARG, "field", NULL },
		{ 'b', TOKEN_F_ARG, "bytesperline", NULL },
		{ 's', TOKEN_F_ARG, "sizeimage", NULL },
		{ 'c', TOKEN_F_ARG, "colorspace", NULL },
		{ 'r', TOKEN_F_ARG, "priv", NULL },
		TOKEN_END
	};
	struct v4l2_format p;

	CLEAR(p);
	p.type = vars.reqbufs.type;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		case 'w': p.fmt.pix.width = val[0]; break;
		case 'h': p.fmt.pix.height = val[0]; break;
		case 'p': p.fmt.pix.pixelformat = val[0]; break;
		case 'f': p.fmt.pix.field = val[0]; break;
		case 'b': p.fmt.pix.bytesperline = val[0]; break;
		case 's': p.fmt.pix.sizeimage = val[0]; break;
		case 'c': p.fmt.pix.colorspace = val[0]; break;
		case 'r': p.fmt.pix.priv = val[0]; break;
		}
	}

	if (try) {
		print(1, "VIDIOC_TRY_FMT\n");
		xioctl(VIDIOC_TRY_FMT, &p);
	} else if (*s == '?') {
		print(1, "VIDIOC_G_FMT\n");
		xioctl(VIDIOC_G_FMT, &p);
	} else {
		print(1, "VIDIOC_S_FMT\n");
	}

	print(2, ": type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	if (p.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		print(2, ": width:         %i\n", p.fmt.pix.width);
		print(2, ": height:        %i\n", p.fmt.pix.height);
		print(2, ": pixelformat:   %s\n", symbol_str(p.fmt.pix.pixelformat, pixelformats));
		print(2, ": field:         %i\n", p.fmt.pix.field);
		print(2, ": bytesperline:  %i\n", p.fmt.pix.bytesperline);
		print(2, ": sizeimage:     %i\n", p.fmt.pix.sizeimage);
		print(2, ": colorspace:    %i\n", p.fmt.pix.colorspace);
		print(2, ": priv:          %i\n", p.fmt.pix.priv);
	}

	if (!try && *s != '?') {
		xioctl(VIDIOC_S_FMT, &p);
		vars.format = p;
	}
}

static void vidioc_reqbufs(const char *s)
{
	static const struct token_list list[] = {
		{ 'c', TOKEN_F_ARG, "count", NULL },
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'm', TOKEN_F_ARG, "memory", v4l2_memory },
		TOKEN_END
	};
	struct v4l2_requestbuffers *p = &vars.reqbufs;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 'c': p->count = val[0]; break;
		case 't': p->type = val[0]; break;
		case 'm': p->memory = val[0]; break;
		}
	}

	print(1, "VIDIOC_REQBUFS\n");
	print(2, ": count:   %i\n", p->count);
	print(2, ": type:    %s\n", symbol_str(p->type, v4l2_buf_types));
	print(2, ": memory:  %s\n", symbol_str(p->memory, v4l2_memory));
	xioctl(VIDIOC_REQBUFS, p);
	print(2, "> count:   %i\n", p->count);
}

static void print_buffer(struct v4l2_buffer *b, char c)
{
#define V4L2_BUF_FLAG	"V4L2_BUF_FLAG_"
#define BUF_FLAG(id)	{ V4L2_BUF_FLAG_##id, (#id) }

	static const struct symbol_list buf_flags[] = {
		BUF_FLAG(MAPPED),
		BUF_FLAG(QUEUED),
		BUF_FLAG(DONE),
		BUF_FLAG(ERROR),
		BUF_FLAG(KEYFRAME),
		BUF_FLAG(PFRAME),
		BUF_FLAG(BFRAME),
		BUF_FLAG(TIMECODE),
		BUF_FLAG(INPUT),
		// BUF_FLAG(PREPARED),
		// BUF_FLAG(NO_CACHE_INVALIDATE),
		// BUF_FLAG(NO_CACHE_CLEAN),
		SYMBOL_END
	};

	const int v = 2;
	print(v, "%c index:     %i\n", c, b->index);
	print(v, "%c type:      %s\n", c, symbol_str(b->type, v4l2_buf_types));
	print(v, "%c bytesused: %i\n", c, b->bytesused);
	print(v, "%c flags:     %s\n", c, symbol_flag_str(b->flags, buf_flags));
	print(v, "%c field:     %i\n", c, b->field);
	print(v, "%c timestamp: %i.%06i\n", c, b->timestamp.tv_sec, b->timestamp.tv_usec);
	print(v, "%c timecode:  type:%i flags:0x%X %02i:%02i:%02i.%i\n", c,
		b->timecode.type, b->timecode.flags, b->timecode.hours,
		b->timecode.minutes, b->timecode.seconds, b->timecode.frames);
	print(v, "%c sequence:  %i\n", c, b->sequence);
	print(v, "%c memory:    %s\n", c, symbol_str(b->memory, v4l2_memory));
	if (b->memory == V4L2_MEMORY_MMAP)
	print(v, "%c offset:    0x%08X\n", c, b->m.offset);
	else if (b->memory == V4L2_MEMORY_USERPTR)
	print(v, "%c userptr:   0x%08X\n", c, b->m.userptr);
	print(v, "%c length:    %i\n", c, b->length);
	print(v, "%c input:     %i\n", c, b->input);
}

static void vidioc_querybuf_cleanup(void)
{
	int i;

	for (i = 0; i < MAX_RING_BUFFERS; i++) {
		struct ring_buffer *rb = &vars.ring_buffers[i];
		free(rb->malloc_p);
		if (rb->mmap_p) {
			int r = munmap(rb->mmap_p, rb->querybuf.length);
			if (r) error("munmap failed");
		}
		CLEAR(*rb);
	}
}

static void vidioc_querybuf(void)
{
	const enum v4l2_buf_type t = vars.reqbufs.type;
	const int bufs = vars.reqbufs.count;
	int i;

	vidioc_querybuf_cleanup();

	if (t != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		error("unsupported operation type");

	if (vars.reqbufs.memory != V4L2_MEMORY_MMAP &&
	    vars.reqbufs.memory != V4L2_MEMORY_USERPTR)
		error("unsupported memory type");

	if (bufs > MAX_RING_BUFFERS)
		error("too many ring buffers");

	for (i = 0; i < bufs; i++) {
		struct ring_buffer *rb = &vars.ring_buffers[i];

		CLEAR(rb->querybuf);
		rb->querybuf.type = t;
		rb->querybuf.memory = vars.reqbufs.memory;
		rb->querybuf.index = i;
		print(1, "VIDIOC_QUERYBUF index:%i\n", rb->querybuf.index);
		xioctl(VIDIOC_QUERYBUF, &rb->querybuf);
		print_buffer(&rb->querybuf, '>');

		if (rb->querybuf.memory == V4L2_MEMORY_MMAP) {
			void *p = mmap(NULL, rb->querybuf.length,
				PROT_READ | PROT_WRITE, MAP_SHARED, vars.fd, rb->querybuf.m.offset);
			if (p == MAP_FAILED)
				error("mmap failed");
			rb->mmap_p = p;
			rb->start = p;
		} else if (rb->querybuf.memory == V4L2_MEMORY_USERPTR) {
			void *p = malloc(PAGE_ALIGN(vars.format.fmt.pix.sizeimage) + PAGE_SIZE - 1);
			if (p == NULL)
				error("malloc failed");
			rb->malloc_p = p;
			rb->start = PAGE_ALIGN(p);
		}
	}
}

static void capture_buffer_save(void *image, struct v4l2_format *format, struct v4l2_buffer *buffer)
{
	struct capture_buffer *cb;

	if (buffer->bytesused < 0 || buffer->bytesused >= MAX_BUFFER_SIZE) {
		print(1, "Bad buffer size %i bytes. Not processing.\n", buffer->bytesused);
		return;
	}

	if (!vars.save_images)
		return;

	if (vars.num_capture_buffers >= MAX_CAPTURE_BUFFERS) {
		static bool printed = FALSE;
		if (!printed) {
			print(1, "Buffers full. Not saving the rest\n");
			printed = TRUE;
		}
		return;
	}

	cb = &vars.capture_buffers[vars.num_capture_buffers++];
	cb->pix_format = format->fmt.pix;
	cb->length = buffer->bytesused;
	cb->image = malloc(cb->length);
	if (!cb->image)
		error("out of memory");
	memcpy(cb->image, image, cb->length);
}

static void vidioc_dqbuf(void)
{
	enum v4l2_buf_type t = vars.reqbufs.type;
	enum v4l2_memory m = vars.reqbufs.memory;
	struct v4l2_buffer b;
	int i;

	CLEAR(b);
	b.type = t;
	b.memory = m;
	print(1, "VIDIOC_DQBUF ");
	xioctl(VIDIOC_DQBUF, &b);
	print_time();
	print_buffer(&b, '>');
	i = b.index;
	if (i < 0 || i >= MAX_RING_BUFFERS)
		error("index out of range");

	if (b.bytesused > vars.format.fmt.pix.sizeimage)
		error("Bad buffer size %i (sizeimage %i)",
		      b.bytesused, vars.format.fmt.pix.sizeimage);
	if (b.bytesused > vars.ring_buffers[i].querybuf.length)
		print(1, "warning: Bad buffer size %i (querybuf %i)\n",
		      b.bytesused, vars.ring_buffers[i].querybuf.length);

	capture_buffer_save(vars.ring_buffers[i].start, &vars.format, &b);
	vars.ring_buffers[i].queued = FALSE;
}

static void vidioc_qbuf(void)
{
	enum v4l2_buf_type t = vars.reqbufs.type;
	enum v4l2_memory m = vars.reqbufs.memory;
	struct v4l2_buffer b;
	int i;

	for (i = 0; i < MAX_RING_BUFFERS; i++)
		if (!vars.ring_buffers[i].queued) break;
	if (i >= MAX_RING_BUFFERS)
		error("no free buffers");

	CLEAR(b);
	b.type = t;
	b.index = i;
	b.memory = m;

	if (m == V4L2_MEMORY_USERPTR) {
		b.m.userptr = (unsigned long)vars.ring_buffers[i].start;
		b.length = vars.format.fmt.pix.sizeimage;
	} else if (m == V4L2_MEMORY_MMAP) {
		/* Nothing here */
	} else error("unsupported capture memory");

	print(1, "VIDIOC_QBUF index:%i\n", i);
	print_buffer(&b, '>');
	xioctl(VIDIOC_QBUF, &b);
	vars.ring_buffers[i].queued = TRUE;
}

/* - Initialize capture,
 * - start streaming,
 * - capture (queue and dequeue) given number of frames,
 * - and disable streaming.
 * This keeps always queue as full as possible.
 */
static void capture(int frames)
{
	const int bufs = vars.reqbufs.count;
	const int tail = MIN(bufs, frames);
	int i;

	vidioc_querybuf();

	for (i = 0; i < tail; i++) {
		vidioc_qbuf();
	}

	streamon(TRUE);

	if (frames > tail) {
		for (i = 0; i < frames - tail; i++) {
			vidioc_dqbuf();
			vidioc_qbuf();
		}
	}

	for (i = 0; i < tail; i++) {
		vidioc_dqbuf();
	}

	streamon(FALSE);
}

/* - Initialize capture unless it already has been done,
 * - Start streaming unless it already has been done,
 * - Capture (queue and dequeue) given number of frames,
 * Compared to capture(), this allows precisely mixing control
 * settings with capture because streaming is left enabled.
 * The argument `frames' may be zero, in which case streaming
 * is enabled and buffers are queued but none are captured.
 */
static void stream(int frames)
{
	int i;

	if (!vars.streaming) {
		/* Initialize streaming and queue all buffers */
		vidioc_querybuf();
		for (i = 0; i < vars.reqbufs.count; i++) {
			vidioc_qbuf();
		}
		streamon(TRUE);
	}

	for (i = 0; i < frames; i++) {
		vidioc_dqbuf();
		vidioc_qbuf();
	}
}

static __u32 get_control_id(char *name)
{
	__u32 id;

	if (isdigit(*name)) {
		int v;
		if (sscanf(name, "%i", &v) != 1)
			error("bad numeric id");
		id = v;
	} else {
		int i;
		for (i = 0; controls[i].symbol != NULL; i++) {
			if (strcmp(name, controls[i].symbol) == 0)
				break;
			if ((strlen(name) >= sizeof(V4L2_CID)) &&
			    (memcmp(name, V4L2_CID, sizeof(V4L2_CID) - 1) == 0) &&
			    (strcmp(name + sizeof(V4L2_CID) - 1, controls[i].symbol) == 0))
				break;
		}
		if (controls[i].symbol == NULL)
			error("unknown control");
		id = controls[i].id;
	}

	return id;
}

static void close_device()
{
	if (vars.fd == -1)
		return;

	close(vars.fd);
	vars.fd = -1;
	print(1, "CLOSED video device\n");
}

static void open_device(const char *device)
{
	static const char DEFAULT_DEV[] = "/dev/video0";
	if (device == NULL && vars.fd != -1)
		return;

	close_device();	
	if (!device || device[0] == 0)
		device = DEFAULT_DEV;
	print(1, "OPEN video device `%s'\n", device);
	vars.fd = open(device, 0);
	if (vars.fd == -1)
		error("failed to open `%s'", device);
}

static void vidioc_querycap()
{
	struct v4l2_capability c;

	CLEAR(c);
	xioctl(VIDIOC_QUERYCAP, &c);
	print(1, "VIDIOC_QUERYCAP\n");
	print(2, "> driver:       `%.16s'\n", c.driver);
	print(2, "> card:         `%.32s'\n", c.card);
	print(2, "> bus_info:     `%.32s'\n", c.bus_info);
	print(2, "> version:      %i.%u.%u\n", c.version >> 16, (c.version >> 8) & 0xFF, c.version & 0xFF);
}

static const char *get_control_name(__u32 id)
{
	static char buf[11];
	int i;

	for (i = 0; controls[i].symbol != NULL; i++)
		if (controls[i].id == id)
			return controls[i].symbol;

	sprintf(buf, "0x%08X", id);
	return buf;
}

static void v4l2_s_ctrl(__u32 id, __s32 val)
{
	struct v4l2_control c;

	CLEAR(c);
	c.id = id;
	c.value = val;
	print(1, "VIDIOC_S_CTRL[%s] = %i\n", get_control_name(id), c.value);
	xioctl(VIDIOC_S_CTRL, &c);
}

static __s32 v4l2_g_ctrl(__u32 id)
{
	struct v4l2_control c;

	CLEAR(c);
	c.id = id;
	xioctl(VIDIOC_G_CTRL, &c);
	print(1, "VIDIOC_G_CTRL[%s] = %i\n", get_control_name(id), c.value);
	return c.value;
}

static void v4l2_s_ext_ctrl(__u32 id, __s32 val)
{
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c;

	CLEAR(cs);
	cs.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	cs.count = 1;
	cs.controls = &c;

	CLEAR(c);
	c.id = id;
	c.value = val;

	print(1, "VIDIOC_S_EXT_CTRLS[%s] = %i\n", get_control_name(id), c.value);
	print(2, "< ctrl_class: 0x%08X\n", cs.ctrl_class);
	print(2, "< count:      %i\n", cs.count);
	print(2, "< controls:   %p\n", cs.controls);
	print(2, "<< id:        0x%08X\n", c.id);
	print(2, "<< value:     %i\n", c.value);
	xioctl(VIDIOC_S_EXT_CTRLS, &cs);
}

static void v4l2_query_ctrl(__u32 id)
{
	struct v4l2_queryctrl q;

	CLEAR(q);
	q.id = id;
	xioctl(VIDIOC_QUERYCTRL, &q);
	print(1, "VIDIOC_QUERYCTRL[%s] =\n", get_control_name(id));
	print(2, "> type:    %i\n", q.type);
	print(2, "> name:    `%.32s'\n", q.name);
	print(2, "> limits:  %i..%i / %i\n", q.minimum, q.maximum, q.step);
	print(2, "> default: %i\n", q.default_value);
	print(2, "> flags:   %i\n", q.flags);
}

static __s32 v4l2_g_ext_ctrl(__u32 id)
{
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c;

	CLEAR(cs);
	cs.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	cs.count = 1;
	cs.controls = &c;

	CLEAR(c);
	c.id = id;

	xioctl(VIDIOC_G_EXT_CTRLS, &cs);
	print(1, "VIDIOC_G_EXT_CTRLS[%s] = %i\n", get_control_name(id), c.value);
	return c.value;
}

static int isident(int c)
{
	return isalnum(c) || c == '_';
}

static void request_controls(char *start)
{
	char *end, *value;
	bool ext, next;
	char op;
	__u32 id;
	int val;

	do {
		for (end = start; isident(*end); end++);
		value = end;
		ext = FALSE;
		if (*value == '+') {
			value++;
			ext = TRUE;
		}
		op = *value++;
		*end = 0;
		next = FALSE;
		id = get_control_id(start);
		if (op == '=') {
			/* Set value */
			for (end = value; isident(*end); end++);
			if (*end == ',')
				next = TRUE;
			if (*end) *end++ = 0;
			if (sscanf(value, "%i", &val) != 1)
				error("bad control value");
			if (ext)
				v4l2_s_ext_ctrl(id, val);
			else
				v4l2_s_ctrl(id, val);
		} else if (op == '?') {
			/* Get value */
			if (*value == ',')
				next = TRUE;
			end = value + 1;
			if (ext)
				v4l2_g_ext_ctrl(id);
			else
				v4l2_g_ctrl(id);
		} else if (op == '#') {
			/* Query control */
			if (*value == ',')
				next = TRUE;
			end = value + 1;
			v4l2_query_ctrl(id);
		} else error("bad request for control");
		start = end;
	} while (next);
}

#if USE_ATOMISP
static void atomisp_ioc_s_exposure(const char *arg)
{
	struct atomisp_exposure exposure;
	int val[3];

	CLEAR(exposure);
	value_get(val, SIZE(val), &arg);
	exposure.integration_time[0] = val[0];
	exposure.integration_time[1] = val[1];
	exposure.gain[0] = val[2];

	print(1, "ATOMISP_IOC_S_EXPOSURE integration_time={%i,%i} gain={%i}\n",
		exposure.integration_time[0],
		exposure.integration_time[1],
		exposure.gain[0]);
	xioctl(ATOMISP_IOC_S_EXPOSURE, &exposure);
}

static void atomisp_ioc_g_priv_int_data(int request, const char *filename)
{
	char buffer[1024*32];
	struct v4l2_private_int_data data;

	CLEAR(data);
	data.size = sizeof(buffer);
	data.data = buffer;
	print(1, request == ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA ? "ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA\n" :
		 request == ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA ? "ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA\n" : "????");
	xioctl(request, &data);
	print(2, "> size: %i\n", data.size);
	if (filename) {
		write_file(filename, buffer, data.size);
		print(2, "Wrote to file `%s'\n", filename);
	}
}
#endif

static void delay(double t)
{
	struct timespec ts;
	int r;

	if (t > 0.0) {
		ts.tv_sec = (long)t;
		ts.tv_nsec = (long)((t - ts.tv_sec) * 1000000000.0 + 0.5);
	} else {
		/* Zero means some small delay, here 1/100 second */
		ts.tv_sec = 0;
		ts.tv_nsec = 10*1000*1000;
	}
	print(1, "SLEEP %li.%08li s\n", (long)ts.tv_sec, ts.tv_nsec);
	r = nanosleep(&ts, NULL);
	if (r != 0)
		error("nanosleep failed");
}

static void shell(char *cmd)
{
	int status, r;
	pid_t pid;
	char *argv[2];

	print(1, "Executing `%s'\n", cmd);
	argv[0] = cmd;
	argv[1] = NULL;
	pid = fork();
	if (pid < 0)
		error("fork failed");
	if (!pid) {
		execvp(cmd, argv);
		error("execvp failed");
	}
	r = waitpid(pid, &status, 0);
	if (r < 0)
		error("waitpid failed");
	print(1, "Executed `%s', status: %i\n", cmd, status);
}

static void process_options(int argc, char *argv[])
{
	while (1) {
		static const struct option long_options[] = {
			{ "help", 0, NULL, 'h' },
			{ "verbose", 2, NULL, 'v' },
			{ "quiet", 0, NULL, 'q' },
			{ "device", 1, NULL, 'd' },
			{ "querycap", 0, NULL, 1001 },
			{ "input", 1, NULL, 'i' },
			{ "enuminput", 0, NULL, 1005 },
			{ "output", 1, NULL, 'o' },
			{ "parm", 1, NULL, 'p' },
			{ "try_fmt", 1, NULL, 't' },
			{ "fmt", 1, NULL, 'f' },
			{ "reqbufs", 1, NULL, 'r' },
			{ "streamon", 0, NULL, 's' },
			{ "streamoff", 0, NULL, 'e' },
			{ "capture", 2, NULL, 'a' },
			{ "stream", 2, NULL, 1006 },
			{ "exposure", 1, NULL, 'x' },
			{ "priv_data", 2, NULL, 1008 },
			{ "motor_priv_data", 2, NULL, 1010 },
			{ "ctrl-list", 0, NULL, 1003 },
			{ "fmt-list", 0, NULL, 1004 },
			{ "ctrl", 1, NULL, 'c' },
			{ "wait", 2, NULL, 'w' },
			{ "waitkey", 0, NULL, 1009 },
			{ "shell", 1, NULL, 1007 },
			{ 0, 0, 0, 0 }
		};

		int c = getopt_long(argc, argv, "hv::qd:i:o:p:t:f:r:soa::x:c:w::", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':	/* --help, -h */
			usage();
			return;

		case 'v':	/* --verbose, -v */
			if (optarg) {
				vars.verbosity = atoi(optarg);
			} else {
				vars.verbosity++;
			}
			break;
		
		case 'q':	/* --quiet, -q */
			vars.verbosity--;
			break;

		case 'd':	/* --device, -d */
			open_device(optarg);
			break;

		case 1001:	/* --querycap */
			open_device(NULL);
			vidioc_querycap();
			break;

		case 'i': {	/* --input, -i, VIDIOC_S/G_INPUT */
			int i;
			open_device(NULL);
			if (strchr(optarg, '?')) {
				/* G_INPUT */
				xioctl(VIDIOC_G_INPUT, &i);
				print(1, "VIDIOC_G_INPUT -> %i\n", i);
			} else {
				/* S_INPUT */
				i = atoi(optarg);
				print(1, "VIDIOC_S_INPUT <- %i\n", i);
				xioctl(VIDIOC_S_INPUT, &i);
			}
			break;
		}

		case 1005:	/* --enuminput */
			open_device(NULL);
			vidioc_enuminput();
			break;

		case 'o':
			vars.output = strdup(optarg);
			if (!vars.output) error("out of memory");
			break;

		case 'p':	/* --parm, -p, VIDIOC_S/G_PARM */
			open_device(NULL);
			vidioc_parm(optarg);
			break;

		case 't':	/* --try-fmt, -t, VIDIOC_TRY_FMT */
			open_device(NULL);
			vidioc_fmt(TRUE, optarg);
			break;

		case 'f':	/* --fmt, -f, VIDIOC_S/G_FMT */
			open_device(NULL);
			vidioc_fmt(FALSE, optarg);
			break;

		case 'r':	/* --reqbufs, -r, VIDIOC_REQBUFS */
			open_device(NULL);
			vidioc_reqbufs(optarg);
			break;

		case 's':	/* --streamoff, -o, VIDIOC_STREAMOFF: stop streaming */
			streamon(TRUE);
			break;

		case 'e':	/* --streamon, -s, VIDIOC_STREAMON: start streaming */
			streamon(FALSE);
			break;

		case 'a':	/* --capture=N, -a: capture N buffers using QBUF/DQBUF */
			capture(optarg ? atoi(optarg) : 1);
			break;

		case 1006:	/* --stream=N: capture N buffers and leave streaming on */
			stream(optarg ? atoi(optarg) : 1);
			break;

#if USE_ATOMISP
		case 'x':	/* --exposure=S, -x: ATOMISP_IOC_S_EXPOSURE */
			open_device(NULL);
			atomisp_ioc_s_exposure(optarg);
			break;

		case 1008:	/* --priv_data=F, ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA */
			open_device(NULL);
			atomisp_ioc_g_priv_int_data(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, optarg);
			break;

		case 1010:	/* --motor_priv_data=F, ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA */
			open_device(NULL);
			atomisp_ioc_g_priv_int_data(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, optarg);
			break;
#endif

		case 1003:	/* --ctrl-list */
			symbol_dump(V4L2_CID, controls);
			return;

		case 1004:	/* --fmt-list */
			symbol_dump(V4L2_PIX_FMT, pixelformats);
			return;

		case 'c':	/* --ctrl, -c, VIDIOC_QUERYCTRL / VIDIOC_S/G_CTRL / VIDIOC_S/G_EXT_CTRLS */
			open_device(NULL);
			request_controls(optarg);
			break;

		case 'w':	/* -w, --wait */
			delay(optarg ? atof(optarg) : 0.0);
			break;

		case 1009: {	/* --waitkey */
			char b[256];
			fgets(b, sizeof(b), stdin);
			break;
		}

		case 1007:	/* --shell */
			shell(optarg);
			break;

		default:
			error("unknown option");
		}
	}
}

static void capture_buffer_write(struct capture_buffer *cb, char *name, int i)
{
	static const char number_mark = '@';
	char b[256];
	char n[5];
	char *c;

	if (!name || !cb->image)
		return;

	if (strlen(name)+sizeof(n) >= sizeof(b))
		error("too long filename");

	sprintf(n, "%03i", i);
	if ((c = strrchr(name, number_mark))) {
		int l = c - name;
		memcpy(b, name, l);
		strcpy(&b[l], n);
		strcat(b, &name[l+1]);
	} else {
		sprintf(b, "%s_%s", name, n);
	}

	print(1, "Writing buffer #%03i (%i bytes) format %s to `%s'\n", i, cb->length,
		symbol_str(cb->pix_format.pixelformat, pixelformats), b);
	write_file(b, cb->image, cb->length);
}

static void cleanup(void)
{
	int i;

	/* Save images */
	for (i = 0; i < vars.num_capture_buffers; i++)
		capture_buffer_write(&vars.capture_buffers[i], vars.output, i);

	/* Stop streaming */
	if (vars.streaming)
		streamon(FALSE);

	/* Free memory */
	vidioc_querybuf_cleanup();
	for (i = 0; i < vars.num_capture_buffers; i++) {
		free(vars.capture_buffers[i].image);
	}
	close_device();
	free(vars.output);
}

int main(int argc, char *argv[])
{
	print(1, "Starting %s\n", name);
	name = argv[0];
	PAGE_SIZE = getpagesize();
	PAGE_MASK = ~(PAGE_SIZE - 1);
	if (gettimeofday(&vars.start_time, NULL) < 0) error("getting start time failed");
	process_options(argc, argv);
	cleanup();
	return 0;
}
