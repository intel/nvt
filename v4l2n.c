/*
 * Next Video Tool for Linux* OS - Video4Linux2 API tool for developers.
 *
 * Copyright (c) 2011-2017 Intel Corporation. All Rights Reserved.
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

#include "v4l2n.h"
#include "extradefs.h"

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
#include <stdint.h>
#include <setjmp.h>
#include "linux/videodev2.h"
#include "linux/v4l2-subdev.h"

#define LOGPREFIX	"V4L2N> "

#define USE_ATOMISP	1

#if USE_ATOMISP
#define CONFIG_VIDEO_ATOMISP_CSS20
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
static unsigned long int _PAGE_SIZE;
static unsigned long int _PAGE_MASK;
#define PAGE_ALIGN(x)	((typeof(x))(((unsigned long int)(x) + _PAGE_SIZE - 1) & _PAGE_MASK))

#define MAX_RING_BUFFERS	20
#define MAX_CAPTURE_BUFFERS	160
#define MAX_BUFFER_SIZE		(64*1024*1024)
#define MAX_PIPES		6

#define V4L2_TYPE_IS_META(type)			\
	((type) == V4L2_BUF_TYPE_META_CAPTURE	\
	 || (type) == V4L2_BUF_TYPE_META_OUTPUT)

#define V4L2_TYPE_IS_VIDEO(type)				\
	((type) == V4L2_BUF_TYPE_VIDEO_CAPTURE			\
	 || (type) == V4L2_BUF_TYPE_VIDEO_OUTPUT		\
	 || (type) == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE	\
	 || (type) == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)

typedef unsigned char bool;

static char *name = "v4l2n";

static const int FILLER = 0xFE;

/* Holds information returned by QUERYBUF and needed
 * for subsequent QBUF/DQBUF. Buffers are reused for long sequences. */
struct ring_buffer {
	struct v4l2_buffer querybuf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	bool queued;
	void *malloc_p;		/* Points to address returned by malloc() */
	void *mmap_p;		/* Points to address returned by mmap() */
	void *start;		/* Points to beginning of data in the buffer */
};

/* Used for saving each captured frame if saving was requested */
struct capture_buffer {
	struct v4l2_format format;
	void *image;
	int length;		/* Length of data in the buffer in bytes */
};

struct pipe {
	int fd;
	char *output;
	void *bufdata;		/* Data to be stored into buffers for driver */
	unsigned int bufdata_length;
	unsigned int bufdata_pos;	/* Current read position */
	struct v4l2_format format;
	struct v4l2_requestbuffers reqbufs;
	int num_capture_buffers;
	struct capture_buffer capture_buffers[MAX_CAPTURE_BUFFERS];
	struct ring_buffer ring_buffers[MAX_RING_BUFFERS];
	bool streaming;
	bool active;
	bool msg_full_printed;
};

static struct {
	int verbosity;
	FILE *logfile;
	bool save_images;
	bool calculate_stats;
	struct timeval start_time;
	jmp_buf exception;
	unsigned int pipe;
	struct pipe pipes[MAX_PIPES];
} vars;

struct symbol_list {
	int id;
	const char *symbol;
};
#define SYMBOL_END	{ -1, NULL }

struct token_list {
	int id;
	int flags;
	const char *token;
	const struct symbol_list *symbols;
};
#define TOKEN_END	{ 0, 0, NULL, NULL }
#define TOKEN_F_OPTARG	BIT(0)		/* Token may have optional argument */
#define TOKEN_F_ARG	BIT(1)		/* Token must have argument or it is an error */
#define TOKEN_F_ARG2	BIT(2)		/* Token may have 2 integer arguments */
#define TOKEN_F_ARG4	BIT(3)		/* Token may have 4 integer arguments */
#define TOKEN_F_FLAGS	BIT(8)		/* Token contains flags (ie. is a bitmask) */

#define V4L2_CID	"V4L2_CID_"
#define CONTROL(id)	{ V4L2_CID_##id, (#id) }
static const struct symbol_list controls[] = {
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
	CONTROL(MIN_BUFFERS_FOR_CAPTURE),
	CONTROL(MIN_BUFFERS_FOR_OUTPUT),
	CONTROL(ALPHA_COMPONENT),
	CONTROL(COLORFX_CBCR),
	CONTROL(LASTP1),
	CONTROL(USER_MEYE_BASE),
	CONTROL(USER_BTTV_BASE),
	CONTROL(USER_S2255_BASE),
	CONTROL(USER_SI476X_BASE),
	CONTROL(USER_TI_VPE_BASE),
	CONTROL(USER_SAA7134_BASE),
	CONTROL(USER_ADV7180_BASE),
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
	CONTROL(MPEG_AUDIO_DEC_PLAYBACK),
	CONTROL(MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK),
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
	CONTROL(MPEG_VIDEO_DECODER_SLICE_INTERFACE),
	CONTROL(MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER),
	CONTROL(MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB),
	CONTROL(MPEG_VIDEO_FRAME_RC_ENABLE),
	CONTROL(MPEG_VIDEO_HEADER_MODE),
	CONTROL(MPEG_VIDEO_MAX_REF_PIC),
	CONTROL(MPEG_VIDEO_MB_RC_ENABLE),
	CONTROL(MPEG_VIDEO_MULTI_SLICE_MAX_BYTES),
	CONTROL(MPEG_VIDEO_MULTI_SLICE_MAX_MB),
	CONTROL(MPEG_VIDEO_MULTI_SLICE_MODE),
	CONTROL(MPEG_VIDEO_VBV_SIZE),
	CONTROL(MPEG_VIDEO_DEC_PTS),
	CONTROL(MPEG_VIDEO_DEC_FRAME),
	CONTROL(MPEG_VIDEO_VBV_DELAY),
	CONTROL(MPEG_VIDEO_REPEAT_SEQ_HEADER),
	CONTROL(MPEG_VIDEO_MV_H_SEARCH_RANGE),
	CONTROL(MPEG_VIDEO_MV_V_SEARCH_RANGE),
	CONTROL(MPEG_VIDEO_H263_I_FRAME_QP),
	CONTROL(MPEG_VIDEO_H263_P_FRAME_QP),
	CONTROL(MPEG_VIDEO_H263_B_FRAME_QP),
	CONTROL(MPEG_VIDEO_H263_MIN_QP),
	CONTROL(MPEG_VIDEO_H263_MAX_QP),
	CONTROL(MPEG_VIDEO_H264_I_FRAME_QP),
	CONTROL(MPEG_VIDEO_H264_P_FRAME_QP),
	CONTROL(MPEG_VIDEO_H264_B_FRAME_QP),
	CONTROL(MPEG_VIDEO_H264_MIN_QP),
	CONTROL(MPEG_VIDEO_H264_MAX_QP),
	CONTROL(MPEG_VIDEO_H264_8X8_TRANSFORM),
	CONTROL(MPEG_VIDEO_H264_CPB_SIZE),
	CONTROL(MPEG_VIDEO_H264_ENTROPY_MODE),
	CONTROL(MPEG_VIDEO_H264_I_PERIOD),
	CONTROL(MPEG_VIDEO_H264_LEVEL),
	CONTROL(MPEG_VIDEO_H264_LOOP_FILTER_ALPHA),
	CONTROL(MPEG_VIDEO_H264_LOOP_FILTER_BETA),
	CONTROL(MPEG_VIDEO_H264_LOOP_FILTER_MODE),
	CONTROL(MPEG_VIDEO_H264_PROFILE),
	CONTROL(MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT),
	CONTROL(MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH),
	CONTROL(MPEG_VIDEO_H264_VUI_SAR_ENABLE),
	CONTROL(MPEG_VIDEO_H264_VUI_SAR_IDC),
	CONTROL(MPEG_VIDEO_H264_SEI_FRAME_PACKING),
	CONTROL(MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0),
	CONTROL(MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE),
	CONTROL(MPEG_VIDEO_H264_FMO),
	CONTROL(MPEG_VIDEO_H264_FMO_MAP_TYPE),
	CONTROL(MPEG_VIDEO_H264_FMO_SLICE_GROUP),
	CONTROL(MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION),
	CONTROL(MPEG_VIDEO_H264_FMO_CHANGE_RATE),
	CONTROL(MPEG_VIDEO_H264_FMO_RUN_LENGTH),
	CONTROL(MPEG_VIDEO_H264_ASO),
	CONTROL(MPEG_VIDEO_H264_ASO_SLICE_ORDER),
	CONTROL(MPEG_VIDEO_H264_HIERARCHICAL_CODING),
	CONTROL(MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE),
	CONTROL(MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER),
	CONTROL(MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP),
	CONTROL(MPEG_VIDEO_MPEG4_I_FRAME_QP),
	CONTROL(MPEG_VIDEO_MPEG4_P_FRAME_QP),
	CONTROL(MPEG_VIDEO_MPEG4_B_FRAME_QP),
	CONTROL(MPEG_VIDEO_MPEG4_MIN_QP),
	CONTROL(MPEG_VIDEO_MPEG4_MAX_QP),
	CONTROL(MPEG_VIDEO_MPEG4_LEVEL),
	CONTROL(MPEG_VIDEO_MPEG4_PROFILE),
	CONTROL(MPEG_VIDEO_MPEG4_QPEL),
	CONTROL(MPEG_VIDEO_VPX_NUM_PARTITIONS),
	CONTROL(MPEG_VIDEO_VPX_IMD_DISABLE_4X4),
	CONTROL(MPEG_VIDEO_VPX_NUM_REF_FRAMES),
	CONTROL(MPEG_VIDEO_VPX_FILTER_LEVEL),
	CONTROL(MPEG_VIDEO_VPX_FILTER_SHARPNESS),
	CONTROL(MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD),
	CONTROL(MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL),
	CONTROL(MPEG_VIDEO_VPX_MIN_QP),
	CONTROL(MPEG_VIDEO_VPX_MAX_QP),
	CONTROL(MPEG_VIDEO_VPX_I_FRAME_QP),
	CONTROL(MPEG_VIDEO_VPX_P_FRAME_QP),
	CONTROL(MPEG_VIDEO_VPX_PROFILE),
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
	CONTROL(MPEG_MFC51_BASE),
	CONTROL(MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY),
	CONTROL(MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE),
	CONTROL(MPEG_MFC51_VIDEO_FRAME_SKIP_MODE),
	CONTROL(MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE),
	CONTROL(MPEG_MFC51_VIDEO_PADDING),
	CONTROL(MPEG_MFC51_VIDEO_PADDING_YUV),
	CONTROL(MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT),
	CONTROL(MPEG_MFC51_VIDEO_RC_REACTION_COEFF),
	CONTROL(MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY),
	CONTROL(MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK),
	CONTROL(MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH),
	CONTROL(MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC),
	CONTROL(MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P),
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
	CONTROL(AUTO_EXPOSURE_BIAS),
	CONTROL(AUTO_N_PRESET_WHITE_BALANCE),
	CONTROL(WIDE_DYNAMIC_RANGE),
	CONTROL(IMAGE_STABILIZATION),
	CONTROL(ISO_SENSITIVITY),
	CONTROL(ISO_SENSITIVITY_AUTO),
	CONTROL(EXPOSURE_METERING),
	CONTROL(SCENE_MODE),
	CONTROL(3A_LOCK),
	CONTROL(AUTO_FOCUS_START),
	CONTROL(AUTO_FOCUS_STOP),
	CONTROL(AUTO_FOCUS_STATUS),
	CONTROL(AUTO_FOCUS_RANGE),
	CONTROL(PAN_SPEED),
	CONTROL(TILT_SPEED),
	CONTROL(FM_TX_CLASS_BASE),
	CONTROL(FM_TX_CLASS),
	CONTROL(RDS_TX_DEVIATION),
	CONTROL(RDS_TX_PI),
	CONTROL(RDS_TX_PTY),
	CONTROL(RDS_TX_PS_NAME),
	CONTROL(RDS_TX_RADIO_TEXT),
	CONTROL(RDS_TX_MONO_STEREO),
	CONTROL(RDS_TX_ARTIFICIAL_HEAD),
	CONTROL(RDS_TX_COMPRESSED),
	CONTROL(RDS_TX_DYNAMIC_PTY),
	CONTROL(RDS_TX_TRAFFIC_ANNOUNCEMENT),
	CONTROL(RDS_TX_TRAFFIC_PROGRAM),
	CONTROL(RDS_TX_MUSIC_SPEECH),
	CONTROL(RDS_TX_ALT_FREQS_ENABLE),
	CONTROL(RDS_TX_ALT_FREQS),
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
	CONTROL(FLASH_CLASS_BASE),
	CONTROL(FLASH_CLASS),
	CONTROL(FLASH_LED_MODE),
	CONTROL(FLASH_STROBE_SOURCE),
	CONTROL(FLASH_STROBE),
	CONTROL(FLASH_STROBE_STOP),
	CONTROL(FLASH_STROBE_STATUS),
	CONTROL(FLASH_TIMEOUT),
	CONTROL(FLASH_INTENSITY),
	CONTROL(FLASH_TORCH_INTENSITY),
	CONTROL(FLASH_INDICATOR_INTENSITY),
	CONTROL(FLASH_FAULT),
	CONTROL(FLASH_CHARGE),
	CONTROL(FLASH_READY),
	CONTROL(JPEG_CLASS_BASE),
	CONTROL(JPEG_CLASS),
	CONTROL(IMAGE_SOURCE_CLASS_BASE),
	CONTROL(IMAGE_SOURCE_CLASS),
	CONTROL(VBLANK),
	CONTROL(HBLANK),
	CONTROL(ANALOGUE_GAIN),
	CONTROL(TEST_PATTERN_RED),
	CONTROL(TEST_PATTERN_GREENR),
	CONTROL(TEST_PATTERN_BLUE),
	CONTROL(TEST_PATTERN_GREENB),
	CONTROL(MIPI_LANES),
	CONTROL(IMAGE_PROC_CLASS_BASE),
	CONTROL(IMAGE_PROC_CLASS),
	CONTROL(LINK_FREQ),
	CONTROL(PIXEL_RATE),
	CONTROL(TEST_PATTERN),
	CONTROL(DV_CLASS_BASE),
	CONTROL(DV_CLASS),
	CONTROL(DV_TX_RGB_RANGE),
	CONTROL(DV_RX_RGB_RANGE),
	CONTROL(FM_RX_CLASS_BASE),
	CONTROL(FM_RX_CLASS),
	CONTROL(TUNE_DEEMPHASIS),
	CONTROL(RDS_RECEPTION),
	CONTROL(RDS_RX_PTY),
	CONTROL(RDS_RX_PS_NAME),
	CONTROL(RDS_RX_RADIO_TEXT),
	CONTROL(RDS_RX_TRAFFIC_ANNOUNCEMENT),
	CONTROL(RDS_RX_TRAFFIC_PROGRAM),
	CONTROL(RDS_RX_MUSIC_SPEECH),
	CONTROL(RF_TUNER_CLASS_BASE),
	CONTROL(RF_TUNER_CLASS),
	CONTROL(RF_TUNER_BANDWIDTH_AUTO),
	CONTROL(RF_TUNER_BANDWIDTH),
	CONTROL(RF_TUNER_LNA_GAIN_AUTO),
	CONTROL(RF_TUNER_LNA_GAIN),
	CONTROL(RF_TUNER_MIXER_GAIN_AUTO),
	CONTROL(RF_TUNER_MIXER_GAIN),
	CONTROL(RF_TUNER_IF_GAIN_AUTO),
	CONTROL(RF_TUNER_IF_GAIN),
	CONTROL(RF_TUNER_PLL_LOCK),
	CONTROL(DETECT_CLASS_BASE),
	CONTROL(DETECT_CLASS),
	CONTROL(DETECT_MD_MODE),
	CONTROL(DETECT_MD_GLOBAL_THRESHOLD),
	CONTROL(DETECT_MD_THRESHOLD_GRID),
	CONTROL(DETECT_MD_REGION_GRID),
	CONTROL(MAX_CTRLS),
	CONTROL(PRIVATE_BASE),

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
	BUFTYPE(VIDEO_CAPTURE_MPLANE),
	BUFTYPE(VIDEO_OUTPUT),
	BUFTYPE(VIDEO_OUTPUT_MPLANE),
	BUFTYPE(VIDEO_OVERLAY),
	BUFTYPE(VBI_CAPTURE),
	BUFTYPE(VBI_OUTPUT),
	BUFTYPE(SLICED_VBI_CAPTURE),
	BUFTYPE(SLICED_VBI_OUTPUT),
	BUFTYPE(VIDEO_OUTPUT_OVERLAY),
	BUFTYPE(PRIVATE),
	BUFTYPE(META_CAPTURE),
	BUFTYPE(META_OUTPUT),
	SYMBOL_END
};

#define V4L2_PIX_FMT	"V4L2_PIX_FMT_"
#define PIXFMT(id)	{ V4L2_PIX_FMT_##id, (#id) }
#define METAFMT(id)	{ V4L2_META_FMT_##id, (#id) }
static const struct symbol_list pixelformats[] = {
	PIXFMT(RGB332),
	PIXFMT(RGB444),
	PIXFMT(ARGB444),
	PIXFMT(XRGB444),
	PIXFMT(RGB555),
	PIXFMT(ARGB555),
	PIXFMT(XRGB555),
	PIXFMT(RGB565),
	PIXFMT(RGB555X),
	PIXFMT(ARGB555X),
	PIXFMT(XRGB555X),
	PIXFMT(RGB565X),
	PIXFMT(BGR666),
	PIXFMT(BGR24),
	PIXFMT(RGB24),
	PIXFMT(BGR32),
	PIXFMT(ABGR32),
	PIXFMT(XBGR32),
	PIXFMT(RGB32),
	PIXFMT(ARGB32),
	PIXFMT(XRGB32),
	PIXFMT(GREY),
	PIXFMT(Y4),
	PIXFMT(Y6),
	PIXFMT(Y10),
	PIXFMT(Y12),
	PIXFMT(Y16),
	PIXFMT(Y10BPACK),
	PIXFMT(PAL8),
	PIXFMT(UV8),
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
	PIXFMT(M420),
	PIXFMT(NV12),
	PIXFMT(NV21),
	PIXFMT(NV16),
	PIXFMT(NV61),
	PIXFMT(NV24),
	PIXFMT(NV42),
	PIXFMT(NV12M),
	PIXFMT(NV21M),
	PIXFMT(NV16M),
	PIXFMT(NV61M),
	PIXFMT(NV12MT),
	PIXFMT(NV12MT_16X16),
	PIXFMT(YUV420M),
	PIXFMT(YVU420M),
	PIXFMT(SBGGR8),
	PIXFMT(SGBRG8),
	PIXFMT(SGRBG8),
	PIXFMT(SRGGB8),
	PIXFMT(SBGGR10),
	PIXFMT(SGBRG10),
	PIXFMT(SGRBG10),
	PIXFMT(SRGGB10),
	PIXFMT(SBGGR10P),
	PIXFMT(SGBRG10P),
	PIXFMT(SGRBG10P),
	PIXFMT(SRGGB10P),
	PIXFMT(SBGGR10ALAW8),
	PIXFMT(SGBRG10ALAW8),
	PIXFMT(SGRBG10ALAW8),
	PIXFMT(SRGGB10ALAW8),
	PIXFMT(SBGGR10DPCM8),
	PIXFMT(SGBRG10DPCM8),
	PIXFMT(SGRBG10DPCM8),
	PIXFMT(SRGGB10DPCM8),
	PIXFMT(SBGGR12),
	PIXFMT(SGBRG12),
	PIXFMT(SGRBG12),
	PIXFMT(SRGGB12),
	PIXFMT(SBGGR12P),
	PIXFMT(SGBRG12P),
	PIXFMT(SGRBG12P),
	PIXFMT(SRGGB12P),
	PIXFMT(SBGGR14),
	PIXFMT(SGBRG14),
	PIXFMT(SGRBG14),
	PIXFMT(SRGGB14),
	PIXFMT(SBGGR14P),
	PIXFMT(SGBRG14P),
	PIXFMT(SGRBG14P),
	PIXFMT(SRGGB14P),
	PIXFMT(SBGGR16),
	PIXFMT(SBGGR8_16V32),
	PIXFMT(SGBRG8_16V32),
	PIXFMT(SGRBG8_16V32),
	PIXFMT(SRGGB8_16V32),
	PIXFMT(SBGGR10V32),
	PIXFMT(SGBRG10V32),
	PIXFMT(SGRBG10V32),
	PIXFMT(SRGGB10V32),
	PIXFMT(SBGGR12V32),
	PIXFMT(SGBRG12V32),
	PIXFMT(SGRBG12V32),
	PIXFMT(SRGGB12V32),
	PIXFMT(SBGGR8V32),
	PIXFMT(SGBRG8V32),
	PIXFMT(SGRBG8V32),
	PIXFMT(SRGGB8V32),
	PIXFMT(UYVY_V32),
	PIXFMT(YUYV420_V32),
	PIXFMT(MJPEG),
	PIXFMT(JPEG),
	PIXFMT(DV),
	PIXFMT(MPEG),
	PIXFMT(H264),
	PIXFMT(H264_NO_SC),
	PIXFMT(H264_MVC),
	PIXFMT(H263),
	PIXFMT(MPEG1),
	PIXFMT(MPEG2),
	PIXFMT(MPEG4),
	PIXFMT(XVID),
	PIXFMT(VC1_ANNEX_G),
	PIXFMT(VC1_ANNEX_L),
	PIXFMT(VP8),
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
	PIXFMT(JL2005BCD),
	PIXFMT(SN9C2028),
	PIXFMT(SQ905C),
	PIXFMT(PJPG),
	PIXFMT(OV511),
	PIXFMT(OV518),
	PIXFMT(STV0680),
	PIXFMT(TM6000),
	PIXFMT(CIT_YYVYUY),
	PIXFMT(KONICA420),
	PIXFMT(JPGL),
	PIXFMT(SE401),
	PIXFMT(S5C_UYVY_JPG),
	PIXFMT(SMIAPP_META_8),
	PIXFMT(YYUV420_V32),
	PIXFMT(PRIV_MAGIC),
	PIXFMT(FLAG_PREMUL_ALPHA),
	PIXFMT(IPU3_SBGGR10),
	PIXFMT(IPU3_SGBRG10),
	PIXFMT(IPU3_SGRBG10),
	PIXFMT(IPU3_SRGGB10),
	METAFMT(IPU3_PARAMS),
	METAFMT(IPU3_STAT_3A),
	METAFMT(IPU3_STAT_DVS),
	METAFMT(IPU3_STAT_LACE),
	SYMBOL_END
};

static const struct symbol_list v4l2_memory[] = {
	{ V4L2_MEMORY_MMAP, "MMAP" },
	{ V4L2_MEMORY_USERPTR, "USERPTR" },
	SYMBOL_END
};

static const struct symbol_list v4l2_queryctrl_type[] = {
	{ V4L2_CTRL_TYPE_INTEGER,      "V4L2_CTRL_TYPE_INTEGER" },
	{ V4L2_CTRL_TYPE_BOOLEAN,      "V4L2_CTRL_TYPE_BOOLEAN" },
	{ V4L2_CTRL_TYPE_MENU,         "V4L2_CTRL_TYPE_MENU" },
	{ V4L2_CTRL_TYPE_BUTTON,       "V4L2_CTRL_TYPE_BUTTON" },
	{ V4L2_CTRL_TYPE_INTEGER64,    "V4L2_CTRL_TYPE_INTEGER64" },
	{ V4L2_CTRL_TYPE_CTRL_CLASS,   "V4L2_CTRL_TYPE_CTRL_CLASS" },
	{ V4L2_CTRL_TYPE_STRING,       "V4L2_CTRL_TYPE_STRING" },
	{ V4L2_CTRL_TYPE_BITMASK,      "V4L2_CTRL_TYPE_BITMASK" },
	{ V4L2_CTRL_TYPE_INTEGER_MENU, "V4L2_CTRL_TYPE_INTEGER_MENU" },
	/* Compound types are >= 0x0100 */
	{ V4L2_CTRL_TYPE_U8,           "V4L2_CTRL_TYPE_U8" },
	{ V4L2_CTRL_TYPE_U16,          "V4L2_CTRL_TYPE_U16" },
	{ V4L2_CTRL_TYPE_U32,          "V4L2_CTRL_TYPE_U32" },
	{ V4L2_CTRL_TYPE_PRIVATE,      "V4L2_CTRL_TYPE_PRIVATE" },
	SYMBOL_END
};

static const struct symbol_list v4l2_queryctrl_flags[] = {
	{ V4L2_CTRL_FLAG_DISABLED,         "V4L2_CTRL_FLAG_DISABLED" },
	{ V4L2_CTRL_FLAG_GRABBED,          "V4L2_CTRL_FLAG_GRABBED" },
	{ V4L2_CTRL_FLAG_READ_ONLY,        "V4L2_CTRL_FLAG_READ_ONLY" },
	{ V4L2_CTRL_FLAG_UPDATE,           "V4L2_CTRL_FLAG_UPDATE" },
	{ V4L2_CTRL_FLAG_INACTIVE,         "V4L2_CTRL_FLAG_INACTIVE" },
	{ V4L2_CTRL_FLAG_SLIDER,           "V4L2_CTRL_FLAG_SLIDER" },
	{ V4L2_CTRL_FLAG_WRITE_ONLY,       "V4L2_CTRL_FLAG_WRITE_ONLY" },
	{ V4L2_CTRL_FLAG_VOLATILE,         "V4L2_CTRL_FLAG_VOLATILE" },
	{ V4L2_CTRL_FLAG_HAS_PAYLOAD,      "V4L2_CTRL_FLAG_HAS_PAYLOAD" },
	{ V4L2_CTRL_FLAG_EXECUTE_ON_WRITE, "V4L2_CTRL_FLAG_EXECUTE_ON_WRITE" },
	SYMBOL_END
};

static char *get_pipestring(void)
{
	static char buf[5];
	if (vars.pipe < 0 || vars.pipe >= MAX_PIPES)
		return "";
	snprintf(buf, sizeof(buf), "p%u: ", vars.pipe);
	return buf;
}

static void print(int lvl, char *msg, ...)
{
	static bool firstcol = TRUE;
	va_list ap;

	if (vars.verbosity < lvl)
		return;

	va_start(ap, msg);
	if (vars.logfile) {
		if (firstcol) {
			fprintf(vars.logfile, LOGPREFIX);
			fprintf(vars.logfile, "%s", get_pipestring());
		}
		vfprintf(vars.logfile, msg, ap);
		fflush(vars.logfile);
		va_end(ap);
		va_start(ap, msg);
	}
	if (firstcol)
		printf("%s", get_pipestring());
	vprintf(msg, ap);
	if (msg[0] != 0)
		firstcol = msg[strlen(msg) - 1]=='\n';
	va_end(ap);
	fflush(stdout);
}

static void error(char *msg, ...)
{
	FILE *f = stdout;
	va_list ap;
	int e = errno;

	va_start(ap, msg);
	if (vars.logfile) {
		fprintf(vars.logfile, LOGPREFIX);
		fprintf(vars.logfile, "%s%s: ", get_pipestring(), name);
		vfprintf(vars.logfile, msg, ap);
		if (e)
			fprintf(vars.logfile, ": %s (%i)", strerror(e), e);
		fprintf(vars.logfile, "\n");
		fflush(vars.logfile);
	}
	fprintf(f, "%s%s: ", get_pipestring(), name);
	vfprintf(f, msg, ap);
	if (e)
		fprintf(f, ": %s (%i)", strerror(e), e);
	fprintf(f, "\n");
	fflush(f);
	va_end(ap);
	longjmp(vars.exception, 1);
	exit(1);
}

#define itd_xioctl(io, arg) itd_xioctl_(#io, io, arg)

static void itd_xioctl_(char *ios, int ion, void *arg)
{
	int fd = vars.pipes[vars.pipe].fd;
	int r = ioctl(fd, ion, arg);
	if (r)
		error("%s failed on fd %i", ios, fd);
}

static int itd_xioctl_try(int ion, void *arg)
{
	int r = ioctl(vars.pipes[vars.pipe].fd, ion, arg);
	if (r != 0) {
		int e = -errno;
		if (e == 0)
			return INT_MIN;
		return e;
	}
	return 0;
}

static void *ralloc(void *p, int s)
{
	void *r = realloc(p, s);
	if (r)
		return r;
	free(p);
	error("memory reallocation failed");
	return NULL;
}

static void usage(void)
{
	print(1,"v4l2n: Next Video Tool for Linux* OS CLI compiled " __DATE__ " " __TIME__ "\n"
		"Copyright (c) 2011-2017 Intel Corporation. All Rights Reserved.\n"
		"This is free software with ABSOLUTELY NO WARRANTY.\n"
		"License GPLv2: GNU GPL version 2\n"
		" <https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html>.\n"
		"\n"
		"Usage: %s [-h] [-d device]\n"
		"-h		Show this help\n"
		"--help\n"
		"-v\n"
		"--verbose	increase message verbosity\n"
		"-q\n"
		"--quiet	decrease message verbosity\n"
		"--log[=X]	output messages also to log file,\n"
		"-l		default /dev/kmsg\n"
		"-d		open /dev/videoX device node\n"
		"--device\n"
		"--open\n"
		"--close	close device node\n"
		"--querycap	Query device capabilities (VIDIOC_QUERYCAP)\n"
		"-i		Set/get input device (VIDIOC_S/G_INPUT)\n"
		"--input\n"
		"--enuminput	Enumerate available input devices (VIDIOC_ENUMINPUT)\n"
		"--enumfmt	Enumerate formats, frame sizes, and frame intervals\n"
		"		(VIDIOC_ENUM_FMT, VIDIOC_ENUM_FRAMESIZES, VIDIOC_ENUM_FRAMEINTERVALS)\n"
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
		"--subdev_frame_interval=X\n"
		"		Set/get subdev frame interval\n"
		"-x [EC,EF,AG,DG] Set coarse_itg, fine_itg, analog gain, and digital gain [ATOMISP]\n"
		"--exposure	(ATOMISP_IOC_S_EXPOSURE)\n"
		"--sensor_mode_data\n"
		"		Get sensor mode data (ATOMISP_IOC_G_SENSOR_MODE_DATA) [ATOMISP]\n"
		"--priv_data[=file]\n"
		"		Read and optionally save to file sensor OTP/EEPROM data\n"
		"		(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA)\n"
		"--motor_priv_data[=file]\n"
		"		Read and optionally save to file af motor OTP/EEPROM data\n"
		"		(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA)\n"
		"--isp_dump=addr,len\n"
		"		Dump ISP memory to kernel log\n"
		"--cvf_parm	Set continuous vf parameters [num_captures,skip_frames,offset]\n"
		"--parameters	Set ISP image processing parameters\n"
		"--ctrl-list	List supported V4L2 controls\n"
		"--ctrl=$	Request given V4L2 controls\n"
		"--fmt-list	List supported pixel formats\n"
		"--enumctrl	Enumerate device V4L2 controls\n"
		"-c $	(VIDIOC_QUERYCTRL / VIDIOC_S/G_CTRL / VIDIOC_S/G_EXT_CTRLS)\n"
		"-w [x]		Wait of x seconds (may be fractional)\n"
		"--wait\n"
		"--waitkey[=file]\n"
		"		Read a line from given file (default stdin)\n"
		"--shell=CMD	Run shell command CMD\n"
		"--statistics	Calculate statistics from each frame\n"
		"--file	<name>	Read commands (options) from given file\n"
		"--pipe <n,m,..> Select current pipes to operate on\n"
		"--load <name>	Load buffer data from file for driver\n"
		"--crop <args>	Set/get cropping parameters\n"
		"--cropcap <type=x> Get cropping capabilities\n"
		"--selection <args> Set/get selection parameters\n"
		"\n"
		"List of V4L2 controls syntax: <[V4L2_CID_]control_name_or_id>[+][=value|?|#][,...]\n"
		"where control_name_or_id is either symbolic name or numerical id.\n"
		"When + is given, use extended controls, otherwise use old-style control call.\n"
		"\"=\" sets the value, \"?\" gets the current value, and \"#\" shows control info.\n",
			name);
}

static int isident(int c)
{
	return isalnum(c) || c == '_';
}

static int isseparator(int c)
{
	return c ==',' || c == '/' || c == ';';
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

static int values_get(int *val, int val_size, const char **ptr)
{
	const char *s = *ptr;
	bool paren = FALSE;
	int i = 0;

	if (!s)
		error("missing integer values");

	if (*s == '(') {
		s++;
		paren = TRUE;
	}

	while (i < val_size) {
		char *a;
		val[i] = strtol(s, &a, 0);
		if (a == s)
			break;
		s = a;
		i++;
		if (!isseparator(*s))
			break;
		s++;
	}

	if (paren) {
		if (*s != ')')
			error("missing closing parenthesis");
		s++;
	}

	*ptr = s;
	return i;
}

static void value_get(int *val, int val_size, const char **ptr)
{
	int m = values_get(val, val_size, ptr);
	if (m != val_size)
		error("too little arguments, %i given but %i expected", m, val_size);
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
	while (isalpha(*end) || *end == '_' || *end == '.') end++;
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
		if (list[i].flags & TOKEN_F_FLAGS) {
			while (isident(*end)) {
				val[0] |= symbol_get(list[i].symbols, &end);
				if (*end == '|' || *end == '/' || *end == ';') end++;
			}
		} else if (list[i].symbols) {
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
		if (id < 1000)
			sprintf(buffer, "%i", id);
		else
			sprintf(buffer, "0x%08X", id);
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

static void write_file(const char *name, const void *data, int size)
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

static void *read_file(const char *name, int *len)
{
	int length;
	int r;
	void *p;
	FILE *f;

	f = fopen(name, "rb");			if (!f) error("failed to open file `%s'", name);
	r = fseek(f, 0, SEEK_END);		if (r < 0) error("failed to seek file");
	length = ftell(f);			if (length <= 0) error("failed to get file size");
	r = fseek(f, 0, SEEK_SET);		if (r < 0) error("failed to seek file");
	p = ralloc(NULL, length);		if (!p) error("out of memory");

	r = fread(p, length, 1, f);		if (r != 1) error("failed to read file");
	fclose(f);

	*len = length;
	return p;
}

static void itr_iterate(void (*itd)(const char *), const char *arg)
{
	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		if (!vars.pipes[vars.pipe].active) continue;
		itd(arg);
	}
}

static char *get_v4l2_rect(struct v4l2_rect *r)
{
	static char buf[50];

	sprintf(buf, "(%i,%i) %ux%u", r->left, r->top, r->width, r->height);
	return buf;
}

static char *get_v4l2_fract(struct v4l2_fract *f)
{
	static char buf[30];

	sprintf(buf, "%u/%u", f->numerator, f->denominator);
	return buf;
}

static __u32 get_buffer_bytesused(struct v4l2_buffer *buffer)
{
	return V4L2_TYPE_IS_MULTIPLANAR(buffer->type) ? buffer->m.planes[0].bytesused : buffer->bytesused;
}

static void set_buffer_bytesused(struct v4l2_buffer *buffer, __u32 bytesused)
{
	if (V4L2_TYPE_IS_MULTIPLANAR(buffer->type))
		buffer->m.planes[0].bytesused = bytesused;
	else
		buffer->bytesused = bytesused;
}

static __u32 get_buffer_length(struct v4l2_buffer *buffer)
{
	return V4L2_TYPE_IS_MULTIPLANAR(buffer->type) ? buffer->m.planes[0].length : buffer->length;
}

static void set_buffer_length(struct v4l2_buffer *buffer, __u32 length)
{
	if (V4L2_TYPE_IS_MULTIPLANAR(buffer->type))
		buffer->m.planes[0].length = length;
	else
		buffer->length = length;
}

static unsigned long get_buffer_uptr(struct v4l2_buffer *buffer)
{
	return V4L2_TYPE_IS_MULTIPLANAR(buffer->type) ? buffer->m.planes[0].m.userptr : buffer->m.userptr;
}

static void set_buffer_uptr(struct v4l2_buffer *buffer, unsigned long userptr)
{
	if (V4L2_TYPE_IS_MULTIPLANAR(buffer->type))
		buffer->m.planes[0].m.userptr = userptr;
	else
		buffer->m.userptr = userptr;
}

static __u32 get_buffer_offset(struct v4l2_buffer *buffer)
{
	return V4L2_TYPE_IS_MULTIPLANAR(buffer->type) ? buffer->m.planes[0].m.mem_offset : buffer->m.offset;
}

static __u32 get_format_width(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return format->fmt.meta.buffersize;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.width;
	else
		return format->fmt.pix.width;
}

static void set_format_width(struct v4l2_format *format, __u32 width)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.width = width;
	else
		format->fmt.pix.width = width;
}

static __u32 get_format_height(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return 1;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.height;
	else
		return format->fmt.pix.height;
}

static void set_format_height(struct v4l2_format *format, __u32 height)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.height = height;
	else
		format->fmt.pix.height = height;
}

static __u32 get_format_code(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return format->fmt.meta.dataformat;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.pixelformat;
	else
		return format->fmt.pix.pixelformat;
}

static void set_format_code(struct v4l2_format *format, __u32 code)
{
	if (V4L2_TYPE_IS_META(format->type))
		format->fmt.meta.dataformat = code;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.pixelformat = code;
	else
		format->fmt.pix.pixelformat = code;
}

static __u32 get_format_field(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return V4L2_FIELD_NONE;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.field;
	else
		return format->fmt.pix.field;
}

static void set_format_field(struct v4l2_format *format, __u32 field)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.field = field;
	else
		format->fmt.pix.field = field;
}

static __u32 get_format_bytesperline(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return format->fmt.meta.buffersize;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.plane_fmt[0].bytesperline;
	else
		return format->fmt.pix.bytesperline;
}

static void set_format_bytesperline(struct v4l2_format *format, __u32 bytesperline)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.plane_fmt[0].bytesperline = bytesperline;
	else
		format->fmt.pix.bytesperline = bytesperline;
}

static __u32 get_format_size(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return format->fmt.meta.buffersize;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.plane_fmt[0].sizeimage;
	else
		return format->fmt.pix.sizeimage;
}

static void set_format_size(struct v4l2_format *format, __u32 size)
{
	if (V4L2_TYPE_IS_META(format->type))
		format->fmt.meta.buffersize = size;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.plane_fmt[0].sizeimage = size;
	else
		format->fmt.pix.sizeimage = size;
}

static __u32 get_format_colorspace(struct v4l2_format *format)
{
	if (V4L2_TYPE_IS_META(format->type))
		return 0;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return format->fmt.pix_mp.colorspace;
	else
		return format->fmt.pix.colorspace;
}

static void set_format_colorspace(struct v4l2_format *format, __u32 colorspace)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		format->fmt.pix_mp.colorspace = colorspace;
	else
		format->fmt.pix.colorspace = colorspace;
}

static void set_format_priv(struct v4l2_format *format, __u32 priv)
{
	if (V4L2_TYPE_IS_META(format->type))
		return;
	else if (V4L2_TYPE_IS_MULTIPLANAR(format->type))
		return;
	else
		format->fmt.pix.priv = priv;
}

static void itd_vidioc_enuminput(const char *unused)
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
		r = itd_xioctl_try(VIDIOC_ENUMINPUT, &p);
		if (r == 0) {
			print(2, "> index:        %i\n", p.index);
			print(2, "> name:         `%.32s'\n", p.name);
			print(2, "> type:         %s\n", symbol_str(p.type, type));
			print(2, "> audioset:     %i\n", p.audioset);
			print(2, "> tuner:        %i\n", p.tuner);
			print(2, "> std:          %li\n", p.std);
			print(2, "> status:       0x%08X\n", p.status);
			print(2, "> capabilities: 0x%08X\n", p.capabilities);
			print(2, "> reserved[0]:  0x%08X\n", p.reserved[0]);
			print(2, "> reserved[1]:  0x%08X\n", p.reserved[1]);
			print(2, "> reserved[2]:  0x%08X\n", p.reserved[2]);
		}
	} while (r == 0);
	if (r != -EINVAL)
		error("VIDIOC_ENUMINPUT failed");
}

static void itd_vidioc_enum_fmt(const char *arg)
{
#define V4L2_FMT_FLAG	"V4L2_FMT_FLAG_"
#define FMT_FLAG(id)	{ V4L2_FMT_FLAG_##id, (#id) }
	static const struct symbol_list fmt_flags[] = {
		FMT_FLAG(COMPRESSED),
		FMT_FLAG(EMULATED),
	};
#define V4L2_FRMSIZE_TYPE	"V4L2_FRMSIZE_TYPE_"
#define FRMSIZE_TYPE(id)	{ V4L2_FRMSIZE_TYPE_##id, (#id) }
	static const struct symbol_list v4l2_frmsize_types[] = {
		FRMSIZE_TYPE(DISCRETE),
		FRMSIZE_TYPE(CONTINUOUS),
		FRMSIZE_TYPE(STEPWISE),
	};
#define V4L2_FRMIVAL_TYPE	"V4L2_FRMIVAL_TYPE_"
#define FRMIVAL_TYPE(id)	{ V4L2_FRMIVAL_TYPE_##id, (#id) }
	static const struct symbol_list v4l2_frmival_types[] = {
		FRMIVAL_TYPE(DISCRETE),
		FRMIVAL_TYPE(CONTINUOUS),
		FRMIVAL_TYPE(STEPWISE),
	};

	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmsizeenum frmsizeenum;
	struct v4l2_frmivalenum frmivalenum;
	int r, id, is, ii;
	__u32 type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (arg)
		type = symbol_get(v4l2_buf_types, &arg);

	for (id = 0;; id++) {
		CLEAR(fmtdesc);
		fmtdesc.index = id;
		fmtdesc.type  = type;
		r = itd_xioctl_try(VIDIOC_ENUM_FMT, &fmtdesc);
		if (r == -EINVAL) { print(2, "-EINVAL\n"); break; }
		if (r) error("VIDIOC_ENUM_FMT failed");
		print(1, "VIDIOC_ENUM_FMT (type=%s) %i: ",
			symbol_str(fmtdesc.type, v4l2_buf_types), fmtdesc.index);
		print(1, "%s `%.32s' flags %s\n",
			symbol_str(fmtdesc.pixelformat, pixelformats),
			fmtdesc.description, symbol_flag_str(fmtdesc.flags, fmt_flags));

		for (is = 0;; is++) {
			int width = 0, height = 0;
			CLEAR(frmsizeenum);
			frmsizeenum.index = is;
			frmsizeenum.pixel_format = fmtdesc.pixelformat;
			r = itd_xioctl_try(VIDIOC_ENUM_FRAMESIZES, &frmsizeenum);
			if (r == -EINVAL) { print(2, "-EINVAL\n"); break; }
			if (r) error("VIDIOC_ENUM_FRAMESIZES failed");
			print(1, " VIDIOC_ENUM_FRAMESIZES %i: ", frmsizeenum.index);
			print(1, "type %s ", symbol_str(frmsizeenum.type, v4l2_frmsize_types));
			switch (frmsizeenum.type) {
			case V4L2_FRMSIZE_TYPE_DISCRETE:
				print(1, "%ix%i", frmsizeenum.discrete.width, frmsizeenum.discrete.height);
				width  = frmsizeenum.discrete.width;
				height = frmsizeenum.discrete.height;
				break;
			case V4L2_FRMSIZE_TYPE_CONTINUOUS:
			case V4L2_FRMSIZE_TYPE_STEPWISE:
				print(1, "%ix%i ... %ix%i [%i,%i]",
					frmsizeenum.stepwise.min_width, frmsizeenum.stepwise.min_height,
					frmsizeenum.stepwise.max_width, frmsizeenum.stepwise.max_height,
					frmsizeenum.stepwise.step_width, frmsizeenum.stepwise.step_height);
				width  = frmsizeenum.stepwise.min_width;
				height = frmsizeenum.stepwise.min_height;
				break;
			}
			print(1, "\n");

			for (ii = 0;; ii++) {
				CLEAR(frmivalenum);
				frmivalenum.index = ii;
				frmivalenum.pixel_format = fmtdesc.pixelformat;
				frmivalenum.width  = width;
				frmivalenum.height = height;
				r = itd_xioctl_try(VIDIOC_ENUM_FRAMEINTERVALS, &frmivalenum);
				if (r == -EINVAL) { print(2, "-EINVAL\n"); break; }
				if (r) error("VIDIOC_ENUM_FRAMEINTERVALS failed");
				print(1, "  VIDIOC_ENUM_FRAMEINTERVALS %i: ", frmivalenum.index);
				print(1, "type %s ", symbol_str(frmivalenum.type, v4l2_frmival_types));
				switch (frmivalenum.type) {
				case V4L2_FRMIVAL_TYPE_DISCRETE:
					print(1, "%s", get_v4l2_fract(&frmivalenum.discrete));
					break;
				case V4L2_FRMIVAL_TYPE_CONTINUOUS:
				case V4L2_FRMIVAL_TYPE_STEPWISE:
					print(1, "%i/%i ... %i/%i [%i/%i]",
						frmivalenum.stepwise.min.numerator, frmivalenum.stepwise.min.denominator,
						frmivalenum.stepwise.max.numerator, frmivalenum.stepwise.max.denominator,
						frmivalenum.stepwise.step.numerator, frmivalenum.stepwise.step.denominator);
					break;
				}
				print(1, "\n");
			}
		}
	}
}

static void itd_streamon(const char *arg)
{
	bool on = (arg != NULL);
	enum v4l2_buf_type t = vars.pipes[vars.pipe].reqbufs.type;
	print(1, "VIDIOC_STREAM%s (type=%s)\n", on ? "ON" : "OFF", symbol_str(t, v4l2_buf_types));
	if (vars.pipes[vars.pipe].streaming == on)
		print(0, "warning: streaming is already in this state\n");
	if (on)
		itd_xioctl(VIDIOC_STREAMON, &t);
	else
		itd_xioctl(VIDIOC_STREAMOFF, &t);
	vars.pipes[vars.pipe].streaming = on;
}

static void itd_vidioc_parm(const char *s)
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
	p.type = vars.pipes[vars.pipe].reqbufs.type;

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
		itd_xioctl(VIDIOC_G_PARM, &p);
	} else {
		print(1, "VIDIOC_S_PARM\n");
	}

	print(2, ": type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	if (p.type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		print(2, ": capability:    %s\n", symbol_str(p.parm.capture.capability, capability));
		print(2, ": capturemode:   %s\n", symbol_str(p.parm.capture.capturemode, capturemode));
		print(2, ": timeperframe:  %s\n", get_v4l2_fract(&p.parm.capture.timeperframe));
		print(2, ": extendedmode:  %i\n", p.parm.capture.extendedmode);
		print(2, ": readbuffers:   %i\n", p.parm.capture.readbuffers);
	} else if (p.type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		print(2, ": capability:    %s\n", symbol_str(p.parm.output.capability, capability));
		print(2, ": outputmode:    %s\n",  symbol_str(p.parm.capture.capturemode, capturemode));
		print(2, ": timeperframe:  %s\n",  get_v4l2_fract(&p.parm.capture.timeperframe));
	}

	if (*s != '?') {
		itd_xioctl(VIDIOC_S_PARM, &p);
	}
}

static void print_v4l2_format(int v, struct v4l2_format *f, char c)
{
	print(v, "%c type:          %s\n", c, symbol_str(f->type, v4l2_buf_types));
	if (V4L2_TYPE_IS_VIDEO(f->type)) {
		print(v, "%c width:         %i\n", c, get_format_width(f));
		print(v, "%c height:        %i\n", c, get_format_height(f));
		print(v, "%c pixelformat:   %s\n", c, symbol_str(get_format_code(f), pixelformats));
		print(v, "%c field:         %i\n", c, get_format_field(f));
		print(v, "%c bytesperline:  %i\n", c, get_format_bytesperline(f));
		print(v, "%c sizeimage:     %i\n", c, get_format_size(f));
		print(v, "%c colorspace:    %i\n", c, get_format_colorspace(f));
	} else if (V4L2_TYPE_IS_META(f->type)) {
		print(v, "%c format:        %i\n", c, symbol_str(get_format_code(f), pixelformats));
		print(v, "%c size:          %i\n", c, get_format_size(f));
	}
}

static void itd_vidioc_fmt(bool try, const char *s)
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
	struct v4l2_format p = vars.pipes[vars.pipe].format;

	p.type = vars.pipes[vars.pipe].reqbufs.type;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		case 'w': set_format_width(&p, val[0]); break;
		case 'h': set_format_height(&p, val[0]); break;
		case 'p': set_format_code(&p, val[0]); break;
		case 'f': set_format_field(&p, val[0]); break;
		case 'b': set_format_bytesperline(&p, val[0]); break;
		case 's': set_format_size(&p, val[0]); break;
		case 'c': set_format_colorspace(&p, val[0]); break;
		case 'r': set_format_priv(&p, val[0]); break;
		}
	}

	if (try) {
		print(1, "VIDIOC_TRY_FMT\n");
		itd_xioctl(VIDIOC_TRY_FMT, &p);
	} else if (*s == '?') {
		print(1, "VIDIOC_G_FMT\n");
		itd_xioctl(VIDIOC_G_FMT, &p);
	} else {
		print(1, "VIDIOC_S_FMT\n");
		print_v4l2_format(3, &p, '<');
		itd_xioctl(VIDIOC_S_FMT, &p);
	}
	if (!try)
		vars.pipes[vars.pipe].format = p;
	print_v4l2_format(2, &p, '>');
}

static void itd_vidioc_try_fmt(const char *s)
{
	itd_vidioc_fmt(TRUE, s);
}

static void itd_vidioc_sg_fmt(const char *s)
{
	itd_vidioc_fmt(FALSE, s);
}

static void itd_vidioc_sg_crop(const char *s)
{
	static const struct token_list list[] = {
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'c', TOKEN_F_ARG|TOKEN_F_ARG4, "c", NULL },
		TOKEN_END
	};
	struct v4l2_crop p;

	CLEAR(p);
	p.type = vars.pipes[vars.pipe].reqbufs.type;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		case 'c':
			p.c.left   = val[0];
			p.c.top    = val[1];
			p.c.width  = val[2];
			p.c.height = val[3];
			break;
		}
	}

	if (*s == '?') {
		print(1, "VIDIOC_G_CROP\n");
		itd_xioctl(VIDIOC_G_CROP, &p);
	} else {
		print(1, "VIDIOC_S_CROP\n");
		print(3, "< type:          %s\n", symbol_str(p.type, v4l2_buf_types));
		print(3, "< c:             %s\n", get_v4l2_rect(&p.c));
		itd_xioctl(VIDIOC_S_CROP, &p);
	}
	print(2, "> type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	print(2, "> c:             %s\n", get_v4l2_rect(&p.c));
}

static void itd_vidioc_cropcap(const char *s)
{
	static const struct token_list list[] = {
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		TOKEN_END
	};
	struct v4l2_cropcap p;

	CLEAR(p);
	p.type = vars.pipes[vars.pipe].reqbufs.type;

	while (s && *s) {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		}
	}

	print(1, "VIDIOC_CROPCAP\n");
	itd_xioctl(VIDIOC_CROPCAP, &p);

	print(2, ": type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	print(2, ": bounds:        %s\n", get_v4l2_rect(&p.bounds));
	print(2, ": defrect:       %s\n", get_v4l2_rect(&p.defrect));
	print(2, ": pixelaspect:   %s\n", get_v4l2_fract(&p.pixelaspect));
}

static void itd_vidioc_sg_selection(const char *s)
{
	static const struct symbol_list target[] = {
		{ V4L2_SEL_TGT_CROP, "V4L2_SEL_TGT_CROP" },
		{ V4L2_SEL_TGT_CROP_DEFAULT, "V4L2_SEL_TGT_CROP_DEFAULT" },
		{ V4L2_SEL_TGT_CROP_BOUNDS, "V4L2_SEL_TGT_CROP_BOUNDS" },
		{ V4L2_SEL_TGT_NATIVE_SIZE, "V4L2_SEL_TGT_NATIVE_SIZE" },
		{ V4L2_SEL_TGT_COMPOSE, "V4L2_SEL_TGT_COMPOSE" },
		{ V4L2_SEL_TGT_COMPOSE_DEFAULT, "V4L2_SEL_TGT_COMPOSE_DEFAULT" },
		{ V4L2_SEL_TGT_COMPOSE_BOUNDS, "V4L2_SEL_TGT_COMPOSE_BOUNDS" },
		{ V4L2_SEL_TGT_COMPOSE_PADDED, "V4L2_SEL_TGT_COMPOSE_PADDED" },
		SYMBOL_END
	};
	static const struct symbol_list flags[] = {
		{ V4L2_SEL_FLAG_GE, "V4L2_SEL_FLAG_GE" },
		{ V4L2_SEL_FLAG_LE, "V4L2_SEL_FLAG_LE" },
		{ V4L2_SEL_FLAG_KEEP_CONFIG, "V4L2_SEL_FLAG_KEEP_CONFIG" },
		SYMBOL_END
	};
	static const struct token_list list[] = {
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'a', TOKEN_F_ARG, "target", target },
		{ 'f', TOKEN_F_ARG|TOKEN_F_FLAGS, "flags", flags },
		{ 'r', TOKEN_F_ARG|TOKEN_F_ARG4, "r", NULL },
		TOKEN_END
	};
	struct v4l2_selection p;

	CLEAR(p);
	p.type = vars.pipes[vars.pipe].reqbufs.type;

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 't': p.type = val[0]; break;
		case 'a': p.target = val[0]; break;
		case 'f': p.flags = val[0]; break;
		case 'r':
			p.r.left   = val[0];
			p.r.top    = val[1];
			p.r.width  = val[2];
			p.r.height = val[3];
			break;
		}
	}

	if (*s == '?') {
		print(1, "VIDIOC_G_SELECTION\n");
		itd_xioctl(VIDIOC_G_SELECTION, &p);
	} else {
		print(1, "VIDIOC_S_SELECTION\n");
		print(3, "< type:          %s\n", symbol_str(p.type, v4l2_buf_types));
		print(3, "< target:        %s\n", symbol_str(p.target, target));
		print(3, "< flags:         %s\n", symbol_flag_str(p.flags, flags));
		print(3, "< r:             %s\n", get_v4l2_rect(&p.r));
		itd_xioctl(VIDIOC_S_SELECTION, &p);
	}
	print(2, "> type:          %s\n", symbol_str(p.type, v4l2_buf_types));
	print(2, "> target:        %s\n", symbol_str(p.target, target));
	print(2, "> flags:         %s\n", symbol_flag_str(p.flags, flags));
	print(2, "> r:             %s\n", get_v4l2_rect(&p.r));
}

static void itd_vidioc_reqbufs(const char *s)
{
	static const struct token_list list[] = {
		{ 'c', TOKEN_F_ARG, "count", NULL },
		{ 't', TOKEN_F_ARG, "type", v4l2_buf_types },
		{ 'm', TOKEN_F_ARG, "memory", v4l2_memory },
		TOKEN_END
	};
	struct v4l2_requestbuffers *p = &vars.pipes[vars.pipe].reqbufs;

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
	itd_xioctl(VIDIOC_REQBUFS, p);
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
		// BUF_FLAG(INPUT),
		// BUF_FLAG(PREPARED),
		// BUF_FLAG(NO_CACHE_INVALIDATE),
		// BUF_FLAG(NO_CACHE_CLEAN),
		SYMBOL_END
	};

	const int v = 2;
	print(v, "%c index:     %i\n", c, b->index);
	print(v, "%c type:      %s\n", c, symbol_str(b->type, v4l2_buf_types));
	print(v, "%c bytesused: %i\n", c, get_buffer_bytesused(b));
	print(v, "%c flags:     %s\n", c, symbol_flag_str(b->flags, buf_flags));
	print(v, "%c field:     %i\n", c, b->field);
	print(v, "%c timestamp: %i.%06i\n", c, b->timestamp.tv_sec, b->timestamp.tv_usec);
	print(v, "%c timecode:  type:%i flags:0x%X %02i:%02i:%02i.%i\n", c,
		b->timecode.type, b->timecode.flags, b->timecode.hours,
		b->timecode.minutes, b->timecode.seconds, b->timecode.frames);
	print(v, "%c sequence:  %i\n", c, b->sequence);
	print(v, "%c memory:    %s\n", c, symbol_str(b->memory, v4l2_memory));
	if (b->memory == V4L2_MEMORY_MMAP)
		print(v, "%c offset:    0x%08X\n", c, get_buffer_offset(b));
	else if (b->memory == V4L2_MEMORY_USERPTR)
		print(v, "%c userptr:   0x%08lX\n", c, get_buffer_uptr(b));
	print(v, "%c length:    %i\n", c, get_buffer_length(b));
//	print(v, "%c input:     %i\n", c, b->input);
}

static void itd_vidioc_querybuf_cleanup(void)
{
	int i;

	for (i = 0; i < MAX_RING_BUFFERS; i++) {
		struct ring_buffer *rb = &vars.pipes[vars.pipe].ring_buffers[i];
		free(rb->malloc_p);
		if (rb->mmap_p) {
			int r = munmap(rb->mmap_p, get_buffer_length(&rb->querybuf));
			if (r) error("munmap failed");
		}
		CLEAR(*rb);
	}
}

static void itd_vidioc_querybuf(const char *unused)
{
	const enum v4l2_buf_type t = vars.pipes[vars.pipe].reqbufs.type;
	const int bufs = vars.pipes[vars.pipe].reqbufs.count;
	int i;

	itd_vidioc_querybuf_cleanup();

	if (t != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    t != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    t != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    t != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    t != V4L2_BUF_TYPE_META_CAPTURE &&
	    t != V4L2_BUF_TYPE_META_OUTPUT &&
	    t != V4L2_BUF_TYPE_VIDEO_OVERLAY &&
	    t != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		error("unsupported operation type");

	if (vars.pipes[vars.pipe].reqbufs.memory != V4L2_MEMORY_MMAP &&
	    vars.pipes[vars.pipe].reqbufs.memory != V4L2_MEMORY_USERPTR)
		error("unsupported memory type");

	if (bufs > MAX_RING_BUFFERS)
		error("too many ring buffers");

	for (i = 0; i < bufs; i++) {
		struct ring_buffer *rb = &vars.pipes[vars.pipe].ring_buffers[i];

		CLEAR(rb->querybuf);
		rb->querybuf.type = t;
		rb->querybuf.memory = vars.pipes[vars.pipe].reqbufs.memory;
		rb->querybuf.index = i;
		if (V4L2_TYPE_IS_MULTIPLANAR(t)) {
			rb->querybuf.length = 1;
			rb->querybuf.m.planes = rb->planes;
		}
		print(1, "VIDIOC_QUERYBUF index:%i\n", rb->querybuf.index);
		itd_xioctl(VIDIOC_QUERYBUF, &rb->querybuf);
		print_buffer(&rb->querybuf, '>');

		if (rb->querybuf.memory == V4L2_MEMORY_MMAP) {
			void *p = mmap(NULL, get_buffer_length(&rb->querybuf),
				PROT_READ | PROT_WRITE, MAP_SHARED, vars.pipes[vars.pipe].fd, get_buffer_offset(&rb->querybuf));
			if (p == MAP_FAILED)
				error("mmap failed");
			rb->mmap_p = p;
			rb->start = p;
		} else if (rb->querybuf.memory == V4L2_MEMORY_USERPTR) {
			int s = PAGE_ALIGN(get_format_size(&vars.pipes[vars.pipe].format)) + _PAGE_SIZE - 1;
			void *p = malloc(s);
			if (p == NULL)
				error("malloc failed");
			memset(p, FILLER, s);
			rb->malloc_p = p;
			rb->start = PAGE_ALIGN(p);
		}
	}
}

static void capture_buffer_stats(void *image, struct v4l2_format *format)
{
	static const int BPP = 2;
	static const int NUM = 0;
	static const int SUM = 1;
	static const int MIN = 2;
	static const int MAX = 3;
	long long stat[4][4];
	int y, x, p, stride;
	unsigned char *line;

	if (!vars.calculate_stats)
		return;

	if (format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    format->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		error("bad buffer type for statistics");

	switch (get_format_code(format)) {
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		break;
	default:
		error("not supported format for statistics");
	}

	for (p = 0; p < 4; p++) {
		stat[p][NUM] = 0;
		stat[p][SUM] = 0;
		stat[p][MIN] = INT_MAX;
		stat[p][MAX] = 0;
	}

	stride = get_format_bytesperline(format);
	line = image;
	for (y = 0; y < get_format_height(format); y++) {
		unsigned char *ptr = line;
		for (x = 0; x < get_format_width(format); x++) {
			int v = ptr[0] | (ptr[1] << 8);
			p = ((y & 1) << 1) | (x & 1);
			stat[p][NUM]++;
			stat[p][SUM] += v;
			stat[p][MIN] = MIN(stat[p][MIN], v);
			stat[p][MAX] = MAX(stat[p][MAX], v);
			ptr += BPP;
		}
		line += stride;
	}

	for (p = 0; p < 4; p++)
		print(0, "STATISTICS[%i] %.3f %i %i %i\n", p,
			(double)stat[p][SUM] / stat[p][NUM], (int)stat[p][MIN], (int)stat[p][MAX]);
}

static void itd_capture_buffer_save(void *image, struct v4l2_format *format, struct v4l2_buffer *buffer)
{
	struct capture_buffer *cb;

	if (get_buffer_bytesused(buffer) < 0 || get_buffer_bytesused(buffer) >= MAX_BUFFER_SIZE) {
		print(1, "Bad buffer size %i bytes. Not processing.\n", get_buffer_bytesused(buffer));
		return;
	}

	if (!vars.save_images)
		return;

	if (vars.pipes[vars.pipe].num_capture_buffers >= MAX_CAPTURE_BUFFERS) {
		if (!vars.pipes[vars.pipe].msg_full_printed) {
			print(1, "Buffers full. Not saving the rest\n");
			vars.pipes[vars.pipe].msg_full_printed = TRUE;
		}
		return;
	}

	cb = &vars.pipes[vars.pipe].capture_buffers[vars.pipes[vars.pipe].num_capture_buffers++];
	cb->format = *format;
	cb->length = get_buffer_bytesused(buffer);
	cb->image = malloc(cb->length);
	if (!cb->image)
		error("out of memory");
	memcpy(cb->image, image, cb->length);
}

static void itd_vidioc_dqbuf(void)
{
	enum v4l2_buf_type t = vars.pipes[vars.pipe].reqbufs.type;
	enum v4l2_memory m = vars.pipes[vars.pipe].reqbufs.memory;
	struct v4l2_buffer b;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	int i;

	CLEAR(b);
	b.type = t;
	b.memory = m;
	if (V4L2_TYPE_IS_MULTIPLANAR(t)) {
		b.length = 1;
		b.m.planes = planes;
	}
	print(1, "VIDIOC_DQBUF ");
	itd_xioctl(VIDIOC_DQBUF, &b);
	print_time();
	print_buffer(&b, '>');
	i = b.index;
	if (i < 0 || i >= MAX_RING_BUFFERS)
		error("index out of range");

	if (get_buffer_bytesused(&b) > get_format_size(&vars.pipes[vars.pipe].format))
		error("Bad buffer size %i (sizeimage %i)",
		      get_buffer_bytesused(&b), get_format_size(&vars.pipes[vars.pipe].format));
	if (get_buffer_bytesused(&b) > get_buffer_length(&vars.pipes[vars.pipe].ring_buffers[i].querybuf))
		print(1, "warning: Bad buffer size %i (querybuf %i)\n",
		      get_buffer_bytesused(&b), get_buffer_length(&vars.pipes[vars.pipe].ring_buffers[i].querybuf));

	capture_buffer_stats(vars.pipes[vars.pipe].ring_buffers[i].start, &vars.pipes[vars.pipe].format);
	itd_capture_buffer_save(vars.pipes[vars.pipe].ring_buffers[i].start, &vars.pipes[vars.pipe].format, &b);
	vars.pipes[vars.pipe].ring_buffers[i].queued = FALSE;
}

static void itd_vidioc_qbuf(void)
{
	enum v4l2_buf_type t = vars.pipes[vars.pipe].reqbufs.type;
	enum v4l2_memory m = vars.pipes[vars.pipe].reqbufs.memory;
	__u32 sizeimage = get_format_size(&vars.pipes[vars.pipe].format);
	struct v4l2_buffer b;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	int i;

	for (i = 0; i < MAX_RING_BUFFERS; i++)
		if (!vars.pipes[vars.pipe].ring_buffers[i].queued) break;
	if (i >= MAX_RING_BUFFERS)
		error("no free buffers");

	CLEAR(b);
	b.type = t;
	b.index = i;
	b.memory = m;
	if (V4L2_TYPE_IS_MULTIPLANAR(t)) {
		b.length = 1;
		b.m.planes = planes;
	}

	if (m == V4L2_MEMORY_USERPTR) {
		set_buffer_uptr(&b, (unsigned long)vars.pipes[vars.pipe].ring_buffers[i].start);
		set_buffer_length(&b, sizeimage);
	} else if (m == V4L2_MEMORY_MMAP) {
		/* Nothing here */
	} else error("unsupported capture memory");

	if (t == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    t == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
	    t == V4L2_BUF_TYPE_VIDEO_OVERLAY ||
	    t == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		set_buffer_bytesused(&b, sizeimage);

	memset(vars.pipes[vars.pipe].ring_buffers[i].start, FILLER, sizeimage);
	if (vars.pipes[vars.pipe].bufdata) {
		memcpy(vars.pipes[vars.pipe].ring_buffers[i].start,
		       vars.pipes[vars.pipe].bufdata + vars.pipes[vars.pipe].bufdata_pos,
		       MIN(vars.pipes[vars.pipe].bufdata_length - vars.pipes[vars.pipe].bufdata_pos,
		           sizeimage));
		vars.pipes[vars.pipe].bufdata_pos += sizeimage;
		if (vars.pipes[vars.pipe].bufdata_pos >= vars.pipes[vars.pipe].bufdata_length)
			vars.pipes[vars.pipe].bufdata_pos = 0;
	}

	print(1, "VIDIOC_QBUF index:%i\n", i);
	print_buffer(&b, '>');
	itd_xioctl(VIDIOC_QBUF, &b);
	vars.pipes[vars.pipe].ring_buffers[i].queued = TRUE;
}

/* - Initialize capture,
 * - start streaming,
 * - capture (queue and dequeue) given number of frames,
 * - and disable streaming.
 * This keeps always queue as full as possible.
 */
static void itr_capture(int frames)
{
	itr_iterate(itd_vidioc_querybuf, NULL);

	if (frames <= 0)
		return;

	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		int i;
		const int bufs = vars.pipes[vars.pipe].reqbufs.count;
		const int tail = MIN(bufs, frames);
		if (!vars.pipes[vars.pipe].active) continue;
		for (i = 0; i < tail; i++)
			itd_vidioc_qbuf();
	}

	itr_iterate(itd_streamon, (char*)TRUE);

	do {
		for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
			if (!vars.pipes[vars.pipe].active) continue;
			itd_vidioc_dqbuf();
			if (frames > vars.pipes[vars.pipe].reqbufs.count)
				itd_vidioc_qbuf();
		}
	} while (--frames);

	itr_iterate(itd_streamon, (char*)FALSE);
}

/* - Initialize capture unless it already has been done,
 * - Start streaming unless it already has been done,
 * - Capture (queue and dequeue) given number of frames,
 * Compared to capture(), this allows precisely mixing control
 * settings with capture because streaming is left enabled.
 * The argument `frames' may be zero, in which case streaming
 * is enabled and buffers are queued but none are captured.
 */
static void itr_stream(int frames)
{
	int i;

	/* Queue buffers (only for pipes which are not yet streaming) */
	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		if (!vars.pipes[vars.pipe].active ||
		    vars.pipes[vars.pipe].streaming) continue;
		itd_vidioc_querybuf(NULL);
		/* Initialize streaming and queue all buffers */
		for (i = 0; i < vars.pipes[vars.pipe].reqbufs.count; i++)
			itd_vidioc_qbuf();
	}

	/* Start streaming (only for pipes which are not yet streaming) */
	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		if (!vars.pipes[vars.pipe].active ||
		    vars.pipes[vars.pipe].streaming) continue;
		itd_streamon((char*)TRUE);
	}

	/* Stream frames (all active pipes) */
	for (i = 0; i < frames; i++) {
		for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
			if (!vars.pipes[vars.pipe].active) continue;
			itd_vidioc_dqbuf();
			itd_vidioc_qbuf();
		}
	}
}

static __u32 get_control_id(const char *name)
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

static void itd_close_device(const char *unused)
{
	if (vars.pipes[vars.pipe].fd == -1)
		return;

	close(vars.pipes[vars.pipe].fd);
	vars.pipes[vars.pipe].fd = -1;
	print(1, "CLOSED video device\n");
}

static void itd_open_device(const char *device)
{
	static const char DEFAULT_DEV[] = "/dev/video0";
	if (device == NULL && vars.pipes[vars.pipe].fd != -1)
		return;

	itd_close_device(NULL);
	if (!device || device[0] == 0)
		device = DEFAULT_DEV;
	print(1, "OPEN video device `%s'\n", device);
	vars.pipes[vars.pipe].fd = open(device, 0);
	if (vars.pipes[vars.pipe].fd == -1)
		error("failed to open `%s'", device);
}

static void itd_vidioc_querycap(const char *unused)
{
#define V4L2_CAP_FLAG	"V4L2_CAP_"
#define CAP_FLAG(id)	{ V4L2_CAP_##id, (#id) }

	static const struct symbol_list cap_flags[] = {
		CAP_FLAG(VIDEO_CAPTURE),
		CAP_FLAG(VIDEO_OUTPUT),
		CAP_FLAG(VIDEO_OVERLAY),
		CAP_FLAG(VBI_CAPTURE),
		CAP_FLAG(VBI_OUTPUT),
		CAP_FLAG(SLICED_VBI_CAPTURE),
		CAP_FLAG(SLICED_VBI_OUTPUT),
		CAP_FLAG(RDS_CAPTURE),
		CAP_FLAG(VIDEO_OUTPUT_OVERLAY),
		CAP_FLAG(HW_FREQ_SEEK),
		CAP_FLAG(RDS_OUTPUT),
		CAP_FLAG(VIDEO_CAPTURE_MPLANE),
		CAP_FLAG(VIDEO_OUTPUT_MPLANE),
		CAP_FLAG(VIDEO_M2M_MPLANE),
		CAP_FLAG(VIDEO_M2M),
		CAP_FLAG(TUNER),
		CAP_FLAG(AUDIO),
		CAP_FLAG(RADIO),
		CAP_FLAG(MODULATOR),
		CAP_FLAG(READWRITE),
		CAP_FLAG(ASYNCIO),
		CAP_FLAG(STREAMING),
		CAP_FLAG(DEVICE_CAPS),
		CAP_FLAG(META_CAPTURE),
		CAP_FLAG(META_OUTPUT),
		SYMBOL_END
	};

	struct v4l2_capability c;

	CLEAR(c);
	itd_xioctl(VIDIOC_QUERYCAP, &c);
	print(1, "VIDIOC_QUERYCAP\n");
	print(2, "> driver:       `%.16s'\n", c.driver);
	print(2, "> card:         `%.32s'\n", c.card);
	print(2, "> bus_info:     `%.32s'\n", c.bus_info);
	print(2, "> version:      %i.%u.%u\n", c.version >> 16, (c.version >> 8) & 0xFF, c.version & 0xFF);
	print(2, "> capabilities: %s\n", symbol_flag_str(c.capabilities, cap_flags));
	print(2, "> device_caps:  %s\n", symbol_flag_str(c.device_caps, cap_flags));
}

static void itd_vidioc_sg_input(const char *arg)
{
	int i;

	if (strchr(arg, '?')) {
		/* G_INPUT */
		itd_xioctl(VIDIOC_G_INPUT, &i);
		print(1, "VIDIOC_G_INPUT -> %i\n", i);
	} else {
		/* S_INPUT */
		i = atoi(arg);
		print(1, "VIDIOC_S_INPUT <- %i\n", i);
		itd_xioctl(VIDIOC_S_INPUT, &i);
	}
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

static void itd_v4l2_s_ctrl(__u32 id, __s32 val)
{
	struct v4l2_control c;

	CLEAR(c);
	c.id = id;
	c.value = val;
	print(1, "VIDIOC_S_CTRL[%s] = %i\n", get_control_name(id), c.value);
	itd_xioctl(VIDIOC_S_CTRL, &c);
}

static __s32 itd_v4l2_g_ctrl(__u32 id)
{
	struct v4l2_control c;

	CLEAR(c);
	c.id = id;
	itd_xioctl(VIDIOC_G_CTRL, &c);
	print(1, "VIDIOC_G_CTRL[%s] = %i\n", get_control_name(id), c.value);
	return c.value;
}

static void itd_v4l2_s_ext_ctrl(__u32 id, const char *opts)
{
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c;

	char *type = NULL;
	char *valuescan = NULL;
	char *valuebuf = NULL;
	const char *valuespec;
	long value;
	int size = 0;

	CLEAR(cs);
	cs.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	cs.count = 1;
	cs.controls = &c;

	CLEAR(c);
	c.id = id;

	sscanf(opts, "%m[^:]:%ms", &type, &valuescan);

	if (type && !valuescan) error("bad compound type extended control specification");
	valuespec = valuescan ? valuescan : opts;

	if (valuespec[0]==':') {
		const char *filename = &valuespec[1];
		valuebuf = read_file(filename, &size);
		if (size <= 0 || !valuebuf)
			error("can not read control value from file `%s'", filename);
		print(1, "Reading control %s (%i bytes) from `%s'\n", get_control_name(id), size, filename);
	}

	if (!type || strcmp(type, "value") == 0) {
		if (sscanf(valuespec, "%li", &value) != 1)
			error("bad extended control value");
		c.value = value;
		print(1, "VIDIOC_S_EXT_CTRLS[%s] = %i\n", get_control_name(id), (int)c.value);
	} else if (strcmp(type, "value64") == 0) {
		if (sscanf(valuespec, "%li", &value) != 1)
			error("bad extended control value64");
		c.value64 = value;
		print(1, "VIDIOC_S_EXT_CTRLS[%s] = %li\n", get_control_name(id), (long)c.value64);
	} else if (strcmp(type, "string") == 0) {
		if (!valuespec) error("get extended control string: buffer size missing");
		c.size = strlen(valuespec);
		c.string = (char *)valuespec;	/* OK to cast from const, since not modified */
		print(1, "VIDIOC_S_EXT_CTRLS[%s] = `%s'\n", get_control_name(id), c.string);
	} else if (strcmp(type, "p_u8") == 0 ||
		   strcmp(type, "p_u16") == 0 ||
		   strcmp(type, "p_u32") == 0 ||
		   strcmp(type, "ptr") == 0) {
		int max_size = 0;
		if (!valuebuf) {
			while (isxdigit(valuespec[size*2]) && isxdigit(valuespec[size*2+1])) {
				while (max_size <= size) {
					max_size += 16;
					valuebuf = ralloc(valuebuf, max_size);
				}
				sscanf(&valuespec[size*2], "%02lx", &value);
				valuebuf[size] = value;
				size++;
			}
		}
		c.ptr = valuebuf;
		c.size = size;
		print(1, "VIDIOC_S_EXT_CTRLS[%s] = %p\n", get_control_name(id), c.ptr);
	} else {
		error("bad extended control type `%s'", type);
	}

	print(2, "< ctrl_class: 0x%08X\n", cs.ctrl_class);
	print(2, "< count:      %i\n", cs.count);
	print(2, "< controls:   %p\n", cs.controls);
	print(2, "<< id:        0x%08X\n", c.id);
	print(2, "<< size:      %i\n", c.size);
	print(2, "<< value:     %i\n", c.value);
	print(2, "<< value64:   %li\n", (long)c.value64);
	print(2, "<< ptr:       %p\n", c.ptr);
	itd_xioctl(VIDIOC_S_EXT_CTRLS, &cs);

	free(type);
	free(valuescan);
	free(valuebuf);
}

static int itd_v4l2_query_ctrl(__u32 id, int errout, int *next_id)
{
	struct v4l2_queryctrl q;
	int r = 0;

	CLEAR(q);
	q.id = id;
	if (next_id)
		q.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
	if (errout)
		itd_xioctl(VIDIOC_QUERYCTRL, &q);
	else
		r = itd_xioctl_try(VIDIOC_QUERYCTRL, &q);
	if (r)
		return r;
	if (next_id)
		*next_id = q.id;

	print(1, "VIDIOC_QUERYCTRL[%s] =\n", get_control_name(q.id));
	print(2, "> type:    %s\n", symbol_str(q.type, v4l2_queryctrl_type));
	print(2, "> name:    `%.32s'\n", q.name);
	print(2, "> limits:  %i..%i / %i\n", q.minimum, q.maximum, q.step);
	print(2, "> default: %i\n", q.default_value);
	print(2, "> flags:   %s\n", symbol_flag_str(q.flags, v4l2_queryctrl_flags));
	return 0;
}

static int itd_v4l2_query_ext_ctrl(__u32 id, int errout, int *next_id)
{
	char dims[V4L2_CTRL_MAX_DIMS * 11 + 11] = "<none>";
	struct v4l2_query_ext_ctrl q;
	int n, i, r = 0;

	CLEAR(q);
	q.id = id;
	if (next_id)
		q.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
	if (errout)
		itd_xioctl(VIDIOC_QUERY_EXT_CTRL, &q);
	else
		r = itd_xioctl_try(VIDIOC_QUERY_EXT_CTRL, &q);
	if (r)
		return r;
	if (next_id)
		*next_id = q.id;

	for (n = 0, i = 0; i < MIN(q.nr_of_dims, V4L2_CTRL_MAX_DIMS); i++)
		n += sprintf(&dims[n], "%u,", q.dims[i]);
	if (n > 1)
		dims[n - 1] = 0;

	print(1, "VIDIOC_QUERY_EXT_CTRL[%s] =\n", get_control_name(q.id));
	print(2, "> type:       %s\n", symbol_str(q.type, v4l2_queryctrl_type));
	print(2, "> name:       `%.32s'\n", q.name);
	print(2, "> limits:     %li..%li / %lu\n", (long)q.minimum, (long)q.maximum, (long)q.step);
	print(2, "> default:    %li\n", (long)q.default_value);
	print(2, "> flags:      %s\n", symbol_flag_str(q.flags, v4l2_queryctrl_flags));
	print(2, "> elem_size:  %u\n", q.elem_size);
	print(2, "> elems:      %u\n", q.elems);
	print(2, "> nr_of_dims: %u\n", q.nr_of_dims);
	print(2, "> dims:       %s\n", dims);
	return 0;
}

static void itd_v4l2_g_ext_ctrl(__u32 id, char *opts)
{
	char buf[256] = "<truncated>";
	struct v4l2_ext_controls cs;
	struct v4l2_ext_control c;
	unsigned char *ptr = NULL;
	int i;

	char *type = NULL;
	unsigned int size = 0;
	char *filename = NULL;

	CLEAR(cs);
	cs.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	cs.count = 1;
	cs.controls = &c;

	CLEAR(c);
	c.id = id;

	if (opts)
		sscanf(opts, "%m[^:]:%u:%ms", &type, &size, &filename);
	if (size > 0) {
		ptr = ralloc(NULL, size);
		c.size = size;
		c.ptr = ptr;
	}

	itd_xioctl(VIDIOC_G_EXT_CTRLS, &cs);

	if (ptr && size*2+1 < SIZE(buf))
		for (i = 0; i < size; i++) sprintf(&buf[2*i], "%02X", ptr[i]);

	if (!type || strcmp(type, "value") == 0) {
		print(1, "VIDIOC_G_EXT_CTRLS[%s] = %i\n", get_control_name(id), c.value);
	} else if (strcmp(type, "value64") == 0) {
		print(1, "VIDIOC_G_EXT_CTRLS[%s] = %li\n", get_control_name(id), (long)c.value64);
	} else if (strcmp(type, "string") == 0) {
		if (!ptr) error("get extended control string: buffer size missing");
		ptr[size - 1] = 0;
		print(1, "VIDIOC_G_EXT_CTRLS[%s] = %s\n", get_control_name(id), ptr);
	} else if (strcmp(type, "p_u8") == 0 ||
		   strcmp(type, "p_u16") == 0 ||
		   strcmp(type, "p_u32") == 0 ||
		   strcmp(type, "ptr") == 0) {
		if (!ptr) error("get extended control: buffer size missing");
		print(1, "VIDIOC_G_EXT_CTRLS[%s] = %s\n", get_control_name(id), buf);
	} else {
		error("bad extended control type `%s'", type);
	}

	if (ptr && filename) {
		print(1, "Writing control %s (%i bytes) to `%s'\n", get_control_name(id), size, filename);
		write_file(filename, ptr, size);
	}

	free(filename);
	free(type);
	free(ptr);
}

static void itd_request_controls(const char *start)
{
	char *end, *value, *type;
	bool ext, next;
	char op;
	__u32 id;
	int val;

	do {
		for (end = (char*)start; isident(*end); end++);
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
			for (end = value; *end && *end != ','; end++);
			if (*end == ',')
				next = TRUE;
			if (*end) *end++ = 0;
			if (ext) {
				itd_v4l2_s_ext_ctrl(id, value);
			} else {
				if (sscanf(value, "%i", &val) != 1)
					error("bad control value");
				itd_v4l2_s_ctrl(id, val);
			}
		} else if (op == '?') {
			/* Get value */
			if (isident(*value)) {
				type = value;
				while (*value && *value != ',') value++;
			} else
				type = NULL;
			if (*value == ',')
				next = TRUE;
			*value = 0;
			if (ext) {
				itd_v4l2_g_ext_ctrl(id, type);
			} else
				itd_v4l2_g_ctrl(id);
			end = value + 1;
		} else if (op == '#') {
			/* Query control */
			if (*value == ',')
				next = TRUE;
			end = value + 1;
			if (ext)
				itd_v4l2_query_ext_ctrl(id, 1, NULL);
			else
				itd_v4l2_query_ctrl(id, 1, NULL);
		} else error("bad request for control");
		start = end;
	} while (next);
}

static void itd_enumerate_controls(const char *s)
{
	bool ext = s ? (strcmp(s, "+") == 0) : 0;
	int id = 0, r;

	do {
		if (ext)
			r = itd_v4l2_query_ext_ctrl(id, 0, &id);
		else
			r = itd_v4l2_query_ctrl(id, 0, &id);
	} while (r == 0);

	if (r != -EINVAL)
		error("%s failed", ext ? "VIDIOC_QUERY_EXT_CTRL" : "VIDIOC_QUERY_CTRL");
}

static void itd_subdev_frame_interval(const char *s)
{
	static const struct token_list list[] = {
		{ 'p', TOKEN_F_ARG, "pad", NULL },
		{ 'i', TOKEN_F_ARG|TOKEN_F_ARG2, "interval", NULL },
		TOKEN_END
	};
	struct v4l2_subdev_frame_interval p;

	CLEAR(p);

	while (*s && *s!='?') {
		int val[4];
		switch (token_get(list, &s, val)) {
		case 'p': p.pad = val[0]; break;
		case 'i':
			p.interval.numerator = val[0];
			p.interval.denominator = val[1];
			break;
		}
	}

	if (*s == '?') {
		print(1, "VIDIOC_SUBDEV_G_FRAME_INTERVAL\n");
		itd_xioctl(VIDIOC_SUBDEV_G_FRAME_INTERVAL, &p);
	} else {
		print(1, "VIDIOC_SUBDEV_S_FRAME_INTERVAL\n");
	}

	print(2, ": pad:       %i\n", p.pad);
	print(2, ": interval:  %s\n", get_v4l2_fract(&p.interval));

	if (*s != '?') {
		itd_xioctl(VIDIOC_SUBDEV_S_FRAME_INTERVAL, &p);
	}
}

#if USE_ATOMISP
static void itd_atomisp_ioc_s_exposure(const char *arg)
{
	struct atomisp_exposure exposure;
	int val[4];

	CLEAR(exposure);
	value_get(val, SIZE(val), &arg);
	exposure.integration_time[0] = val[0];
	exposure.integration_time[1] = val[1];
	exposure.gain[0] = val[2];
	exposure.gain[1] = val[3];

	print(1, "ATOMISP_IOC_S_EXPOSURE integration_time={%i,%i} gain={%i,%i}\n",
		exposure.integration_time[0],
		exposure.integration_time[1],
		exposure.gain[0],
		exposure.gain[1]);
	itd_xioctl(ATOMISP_IOC_S_EXPOSURE, &exposure);
}

static void itd_atomisp_ioc_g_priv_int_data(int request, const char *filename)
{
	char buffer[1024*32];
	struct v4l2_private_int_data data;

	CLEAR(data);
	data.size = sizeof(buffer);
	data.data = buffer;
	itd_xioctl(request, &data);
	print(2, "> size: %i\n", data.size);
	if (filename) {
		write_file(filename, buffer, data.size);
		print(2, "Wrote to file `%s'\n", filename);
	}
}

static void itd_atomisp_ioc_g_sensor_priv_int_data(const char *filename)
{
	print(1, "ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA\n");
	itd_atomisp_ioc_g_priv_int_data(ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA, filename);
}

static void itd_atomisp_ioc_g_motor_priv_int_data(const char *filename)
{
	print(1, "ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA\n");
	itd_atomisp_ioc_g_priv_int_data(ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA, filename);
}

static void itd_atomisp_ioc_g_sensor_mode_data(const char *unused)
{
	/* In principle this structure is sensor-specific.
	 * In practice most (all?) drivers use the same structure.
	 */
	typedef unsigned int sensor_register;
	struct sensor_mode_data {
		sensor_register coarse_integration_time_min;
		sensor_register coarse_integration_time_max_margin;
		sensor_register fine_integration_time_min;
		sensor_register fine_integration_time_max_margin;
		sensor_register fine_integration_time_def;
		sensor_register frame_length_lines;
		sensor_register line_length_pck;
		sensor_register read_mode;
		int vt_pix_clk_freq_mhz;
	};
	struct atomisp_sensor_mode_data data;
	struct sensor_mode_data *ov8830 = (struct sensor_mode_data *)&data;

	CLEAR(data);
	print(1, "ATOMISP_IOC_G_SENSOR_MODE_DATA\n");
	itd_xioctl(ATOMISP_IOC_G_SENSOR_MODE_DATA, &data);
	print(2, "> coarse_integration_time_min:        %i\n", ov8830->coarse_integration_time_min);
	print(2, "> coarse_integration_time_max_margin: %i\n", ov8830->coarse_integration_time_max_margin);
	print(2, "> fine_integration_time_min:          %i\n", ov8830->fine_integration_time_min);
	print(2, "> fine_integration_time_max_margin:   %i\n", ov8830->fine_integration_time_max_margin);
	print(2, "> fine_integration_time_def:          %i\n", ov8830->fine_integration_time_def);
	print(2, "> frame_length_lines:                 %i\n", ov8830->frame_length_lines);
	print(2, "> line_length_pck:                    %i\n", ov8830->line_length_pck);
	print(2, "> read_mode:                          0x%08X\n", ov8830->read_mode);
	print(2, "> vt_pix_clk_freq_mhz:                %i\n", ov8830->vt_pix_clk_freq_mhz);
}

static void itd_atomisp_memory_dump(const char *arg)
{
	struct atomisp_de_config config;
	int addr, len, r;

	if (sscanf(arg, "%i,%i", &addr, &len) != 2)
		error("missing address or length");

	CLEAR(config);
	config.pixelnoise = 0xDB60D83B;
	config.c1_coring_threshold = addr;
	config.c2_coring_threshold = len;
	print(1, "ATOMISP_IOC_S_ISP_FALSE_COLOR_CORRECTION: addr 0x%08X len %i\n", addr, len);
	r = itd_xioctl_try(ATOMISP_IOC_S_ISP_FALSE_COLOR_CORRECTION, &config);
	print(2, "> dumped ISP memory to kernel log\n");
	if (r != -EDOTDOT) print(1, "> incorrect response %i from kernel\n", r);
}

static void itd_atomisp_ioc_s_cvf_params(const char *arg)
{
	struct atomisp_cont_capture_conf cvf_parm;
	int val[3];

	CLEAR(cvf_parm);
	value_get(val, SIZE(val), &arg);

	cvf_parm.num_captures = val[0];
	cvf_parm.skip_frames = val[1];
	cvf_parm.offset = val[2];

	print(1, "ATOMISP_IOC_S_CONT_CAPTURE_CONFIG: num_captures %d skip_frames %d offset %d\n",
		cvf_parm.num_captures,
		cvf_parm.skip_frames,
		cvf_parm.offset);
	itd_xioctl(ATOMISP_IOC_S_CONT_CAPTURE_CONFIG, &cvf_parm);
}

static void itd_atomisp_ioc_s_parameters(const char *s)
{
	static const struct token_list list[] = {
		{ 'W' << 8, 0, "wb_config", NULL },
		 { 'W'<<8 | 'i', TOKEN_F_ARG, "wb_config.integer_bits", NULL },
		 { 'W'<<8 | 'R', TOKEN_F_ARG, "wb_config.gr", NULL },
		 { 'W'<<8 | 'r', TOKEN_F_ARG, "wb_config.r", NULL },
		 { 'W'<<8 | 'b', TOKEN_F_ARG, "wb_config.b", NULL },
		 { 'W'<<8 | 'B', TOKEN_F_ARG, "wb_config.gb", NULL },
		{ 'C' << 8, 0, "cc_config", NULL },
		{ 'T' << 8, 0, "tnr_config", NULL },
		 { 'T'<<8 | 'g', TOKEN_F_ARG, "tnr_config.gain", NULL },
		 { 'T'<<8 | 'y', TOKEN_F_ARG, "tnr_config.threshold_y", NULL },
		 { 'T'<<8 | 'u', TOKEN_F_ARG, "tnr_config.threshold_uv", NULL },
//		{ 'E' << 8, 0, "ecd_config", NULL },
//		{ 'Y' << 8, 0, "ynr_config", NULL },
//		{ 'F' << 8, 0, "fc_config", NULL },
//		{ 'N' << 8, 0, "cnr_config", NULL },
		{ 'M' << 8, 0, "macc_config", NULL },
//		{ 'O' << 8, 0, "ctc_config", NULL },
//		{ 'A' << 8, 0, "aa_config", NULL },
//		{ 'B' << 8, 0, "baa_config", NULL },
		{ 'Q' << 8, 0, "ce_config", NULL },
		 { 'Q'<<8 | 'i', TOKEN_F_ARG, "ce_config.uv_level_min", NULL },
		 { 'Q'<<8 | 'a', TOKEN_F_ARG, "ce_config.uv_level_max", NULL },
//		{ 'D' << 8, 0, "dvs_6axis_config", NULL },
		{ 'J' << 8, 0, "ob_config", NULL },
		{ 'P' << 8, 0, "dp_config", NULL },
		 { 'P'<<8 | 't', TOKEN_F_ARG, "dp_config.threshold", NULL },
		 { 'P'<<8 | 'g', TOKEN_F_ARG, "dp_config.gain", NULL },
		{ 'R' << 8, 0, "nr_config", NULL },
		 { 'R'<<8 | 'b', TOKEN_F_ARG, "nr_config.bnr_gain", NULL },
		 { 'R'<<8 | 'y', TOKEN_F_ARG, "nr_config.ynr_gain", NULL },
		 { 'R'<<8 | 'd', TOKEN_F_ARG, "nr_config.direction", NULL },
		 { 'R'<<8 | 't', TOKEN_F_ARG, "nr_config.threshold_cb", NULL },
		 { 'R'<<8 | 'g', TOKEN_F_ARG, "nr_config.threshold_cr", NULL },
		{ 'G' << 8, 0, "ee_config", NULL },
		 { 'G'<<8 | 'g', TOKEN_F_ARG, "ee_config.gain", NULL },
		 { 'G'<<8 | 't', TOKEN_F_ARG, "ee_config.threshold", NULL },
		 { 'G'<<8 | 'd', TOKEN_F_ARG, "ee_config.detail_gain", NULL },
		{ 'S' << 8, 0, "de_config", NULL },
		 { 'S'<<8 | 'p', TOKEN_F_ARG, "de_config.pixelnoise", NULL },
		 { 'S'<<8 | '1', TOKEN_F_ARG, "de_config.c1_coring_threshold", NULL },
		 { 'S'<<8 | '2', TOKEN_F_ARG, "de_config.c2_coring_threshold", NULL },
		{ 'I' << 8, 0, "gc_config", NULL },
//		{ 'V' << 8, 0, "anr_config", NULL },
		{ 'K' << 8, 0, "a3a_config", NULL },
		{ 'X' << 8, 0, "xnr_config", NULL },
		 { 'X'<<8 | 't', TOKEN_F_ARG, "xnr_config.threshold", NULL },
//		{ 'Z' << 8, 0, "dz_config", NULL },
//		{ 'U' << 8, 0, "yuv2rgb_cc_config", NULL },
//		{ 'H' << 8, 0, "rgb2yuv_cc_config", NULL },
//		{ 'm' << 8, 0, "macc_table", NULL },
		{ 'l' << 8, 0, "gamma_table", NULL },
		{ 'c' << 8, 0, "ctc_table", NULL },
//		{ 'x' << 8, 0, "xnr_table", NULL },
//		{ 'r' << 8, 0, "r_gamma_table", NULL },
//		{ 'g' << 8, 0, "g_gamma_table", NULL },
//		{ 'b' << 8, 0, "b_gamma_table", NULL },
//		{ 'o' << 8, 0, "motion_vector", NULL },
		{ 's' << 8, 0, "shading_table", NULL },
		{ 'p' << 8, 0, "morph_table", NULL },
//		{ 'd' << 8, 0, "dvs_coefs", NULL },
//		{ 'v' << 8, 0, "dvs2_coefs", NULL },
//		{ 't' << 8, 0, "capture_config", NULL },
//		{ 'a' << 8, 0, "anr_thres", NULL },
		TOKEN_END
	};
	struct atomisp_parameters p = { };
	struct atomisp_wb_config	wb_config = {
		.integer_bits	= 1,
		.gr		= 32768,
		.r		= 32768,
		.b		= 32768,
		.gb		= 32768,
	};
	struct atomisp_cc_config	cc_config = { };
	struct atomisp_tnr_config	tnr_config = {
		.gain		= 65535,
		.threshold_y	= 0,
		.threshold_uv	= 0,
	};
//	struct atomisp_ecd_config	ecd_config = { };
//	struct atomisp_ynr_config	ynr_config = { };
//	struct atomisp_fc_config	fc_config = { };
//	struct atomisp_cnr_config	cnr_config = { };
	struct atomisp_macc_config	macc_config = { };
//	struct atomisp_ctc_config	ctc_config = { };
//	struct atomisp_aa_config	aa_config = { };
//	struct atomisp_aa_config	baa_config = { };
	struct atomisp_ce_config	ce_config = {
		.uv_level_min	= 0,
		.uv_level_max	= 255,
	};
//	struct atomisp_dvs_6axis_config	dvs_6axis_config = { };
	struct atomisp_ob_config	ob_config = { };
	struct atomisp_dp_config	dp_config = {
		.threshold	= 0xFFFF,
		.gain		= 0xFFFF,
	};
	struct atomisp_nr_config	nr_config = { };
	struct atomisp_ee_config	ee_config = {
		.gain		= 0,
		.threshold	= 65535,
		.detail_gain	= 0,
	};
	struct atomisp_de_config	de_config = { };
	struct atomisp_gc_config	gc_config = { };
//	struct atomisp_anr_config	anr_config = { };
	struct atomisp_3a_config	a3a_config = { };
	struct atomisp_xnr_config	xnr_config = { };
//	struct atomisp_dz_config	dz_config = { };
//	struct atomisp_cc_config	yuv2rgb_cc_config = { };
//	struct atomisp_cc_config	rgb2yuv_cc_config = { };
//	struct atomisp_macc_table	macc_table = { };
	struct atomisp_gamma_table	gamma_table = { };
	struct atomisp_ctc_table	ctc_table = { };
//	struct atomisp_xnr_table	xnr_table = { };
//	struct atomisp_rgb_gamma_table	r_gamma_table = { };
//	struct atomisp_rgb_gamma_table	g_gamma_table = { };
//	struct atomisp_rgb_gamma_table	b_gamma_table = { };
//	struct atomisp_vector		motion_vector = { };
	struct atomisp_shading_table	shading_table = { };
	struct atomisp_morph_table	morph_table = { };
//	struct atomisp_dvs_coefficients	dvs_coefs = { };
//	struct atomisp_dvs2_coefficients dvs2_coefs = { };
//	struct atomisp_capture_config	capture_config = { };
//	struct atomisp_anr_thres	anr_thres = { };

	while (*s && *s!='?') {
		int val[4];
		int t = token_get(list, &s, val);
		switch (t >> 8) {	/* Set default structure */
		case 'W': p.wb_config = &wb_config; break;
		case 'C': p.cc_config = &cc_config; break;
		case 'T': p.tnr_config = &tnr_config; break;
		case 'M': p.macc_config = &macc_config; break;
		case 'Q': p.ce_config = &ce_config; break;
//		case 'D': p.dvs_6axis_config = &dvs_6axis_config; break;
		case 'J': p.ob_config = &ob_config; break;
		case 'P': p.dp_config = &dp_config; break;
		case 'R': p.nr_config = &nr_config; break;
		case 'G': p.ee_config = &ee_config; break;
		case 'S': p.de_config = &de_config; break;
		case 'I': p.gc_config = &gc_config; break;
		case 'K': p.a3a_config = &a3a_config; break;
		case 'X': p.xnr_config = &xnr_config; break;
//		case 'U': p.yuv2rgb_cc_config = &yuv2rgb_cc_config; break;
//		case 'H': p.rgb2yuv_cc_config = &rgb2yuv_cc_config; break;
//		case 'm': p.macc_table = &macc_table; break;
		case 'l': p.gamma_table = &gamma_table; break;
		case 'c': p.ctc_table = &ctc_table; break;
		case 's': p.shading_table = &shading_table; break;
		case 'p': p.morph_table = &morph_table; break;
		}
		switch (t) {		/* Fill structure */
		case 'W'<<8 | 'i': wb_config.integer_bits = val[0]; break;
		case 'W'<<8 | 'R': wb_config.gr = val[0]; break;
		case 'W'<<8 | 'r': wb_config.r = val[0]; break;
		case 'W'<<8 | 'b': wb_config.b = val[0]; break;
		case 'W'<<8 | 'B': wb_config.gb = val[0]; break;
		case 'T'<<8 | 'g': tnr_config.gain = val[0]; break;
		case 'T'<<8 | 'y': tnr_config.threshold_y = val[0]; break;
		case 'T'<<8 | 'u': tnr_config.threshold_uv = val[0]; break;
		case 'Q'<<8 | 'i': ce_config.uv_level_min = val[0]; break;
		case 'Q'<<8 | 'a': ce_config.uv_level_max = val[0]; break;
		case 'P'<<8 | 't': dp_config.threshold = val[0]; break;
		case 'P'<<8 | 'g': dp_config.gain = val[0]; break;
		case 'R'<<8 | 'b': nr_config.bnr_gain = val[0]; break;
		case 'R'<<8 | 'y': nr_config.ynr_gain = val[0]; break;
		case 'R'<<8 | 'd': nr_config.direction = val[0]; break;
		case 'R'<<8 | 't': nr_config.threshold_cb = val[0]; break;
		case 'R'<<8 | 'g': nr_config.threshold_cr = val[0]; break;
		case 'G'<<8 | 'g': ee_config.gain = val[0]; break;
		case 'G'<<8 | 't': ee_config.threshold = val[0]; break;
		case 'G'<<8 | 'd': ee_config.detail_gain = val[0]; break;
		case 'S'<<8 | 'p': de_config.pixelnoise = val[0]; break;
		case 'S'<<8 | '1': de_config.c1_coring_threshold = val[0]; break;
		case 'S'<<8 | '2': de_config.c2_coring_threshold = val[0]; break;
		case 'X'<<8 | 't': xnr_config.threshold = val[0]; break;
		}
	}

	print(1, "ATOMISP_IOC_S_PARAMETERS\n");
	if (p.wb_config) {
		print(2, "< wb_config->integer_bits:    %i\n", p.wb_config->integer_bits);
		print(2, "< wb_config->gr:              %i\n", p.wb_config->gr);
		print(2, "< wb_config->r:               %i\n", p.wb_config->r);
		print(2, "< wb_config->b:               %i\n", p.wb_config->b);
		print(2, "< wb_config->gb:              %i\n", p.wb_config->gb);
	} else  print(2, "< wb_config: NULL\n");
	if (p.tnr_config) {
		print(2, "< tnr_config->gain:           %i\n", p.tnr_config->gain);
		print(2, "< tnr_config->threshold_y:    %i\n", p.tnr_config->threshold_y);
		print(2, "< tnr_config->threshold_uv:   %i\n", p.tnr_config->threshold_uv);
	} else  print(2, "< tnr_config: NULL\n");
	if (p.ce_config) {
		print(2, "< ce_config->uv_level_min:    %i\n", p.ce_config->uv_level_min);
		print(2, "< ce_config->uv_level_max:    %i\n", p.ce_config->uv_level_max);
	} else  print(2, "< ce_config: NULL\n");
	if (p.dp_config) {
		print(2, "< dp_config->threshold:       %i\n", p.dp_config->threshold);
		print(2, "< dp_config->gain:            %i\n", p.dp_config->gain);
	} else  print(2, "< dp_config: NULL\n");
	if (p.nr_config) {
		print(2, "< nr_config->bnr_gain:        %i\n", p.nr_config->bnr_gain);
		print(2, "< nr_config->ynr_gain:        %i\n", p.nr_config->ynr_gain);
		print(2, "< nr_config->direction:       %i\n", p.nr_config->direction);
		print(2, "< nr_config->threshold_cb:    %i\n", p.nr_config->threshold_cb);
		print(2, "< nr_config->threshold_cr:    %i\n", p.nr_config->threshold_cr);
	} else  print(2, "< nr_config: NULL\n");
	if (p.ee_config) {
		print(2, "< ee_config->gain:            %i\n", p.ee_config->gain);
		print(2, "< ee_config->threshold:       %i\n", p.ee_config->threshold);
		print(2, "< ee_config->detail_gain:     %i\n", p.ee_config->detail_gain);
	} else  print(2, "< ee_config: NULL\n");
	if (p.de_config) {
		print(2, "< de_config->pixelnoise:      %i\n", p.de_config->pixelnoise);
		print(2, "< de_config->c1_coring_threshold: %i\n", p.de_config->c1_coring_threshold);
		print(2, "< de_config->c2_coring_threshold: %i\n", p.de_config->c1_coring_threshold);
	} else  print(2, "< de_config: NULL\n");
	if (p.xnr_config) {
		print(2, "< xnr_config->threshold:      %i\n", p.xnr_config->threshold);
	} else  print(2, "< xnr_config: NULL\n");
	itd_xioctl(ATOMISP_IOC_S_PARAMETERS, &p);
}

#endif

static void itd_output_name(const char *arg)
{
	free(vars.pipes[vars.pipe].output);
	vars.pipes[vars.pipe].output = strdup(arg);
	if (!vars.pipes[vars.pipe].output) error("out of memory");
	vars.save_images = TRUE;
}

static void itd_load_bufdata(const char *arg)
{
	int length;
	int r;
	void *p;
	FILE *f;

	free(vars.pipes[vars.pipe].bufdata);
	vars.pipes[vars.pipe].bufdata = NULL;

	f = fopen(arg, "rb");			if (!f) error("failed to open file `%s'", arg);
	r = fseek(f, 0, SEEK_END);		if (r < 0) error("failed to seek file");
	length = ftell(f);			if (length <= 0) error("failed to get file size");
	r = fseek(f, 0, SEEK_SET);		if (r < 0) error("failed to seek file");
	p = ralloc(NULL, length);		if (!p) error("out of memory");

	print(1, "Reading buffer (%i bytes) from `%s'\n", length, arg);
	vars.pipes[vars.pipe].bufdata = p;
	vars.pipes[vars.pipe].bufdata_length = length;
	vars.pipes[vars.pipe].bufdata_pos = 0;

	r = fread(p, length, 1, f);		if (r != 1) error("failed to read file");
	fclose(f);
}

static void select_pipes(const char *p)
{
	int pipes[MAX_PIPES];
	int n, i;

	for (i = 0; i < MAX_PIPES; i++)
		vars.pipes[i].active = FALSE;

	n = values_get(pipes, SIZE(pipes), &p);
	for (i = 0; i < n; i++) {
		if (pipes[i] < 0 || pipes[i] >= MAX_PIPES)
			error("Maximum of %i pipes available", MAX_PIPES - 1);
		vars.pipes[pipes[i]].active = TRUE;
	}

	print(1, "Selected pipes:");
	for (i = 0; i < MAX_PIPES; i++) {
		if (vars.pipes[i].active)
			print(1, " %i", i);
	}
	print(1, "\n");
	vars.pipe = pipes[0];	/* Set the default pipe. FIXME: temporary */
}

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
		r = setjmp(vars.exception);	/* Clear exception handling */
		if (!r) {
			execvp(cmd, argv);
			error("execvp failed");
		}
		while (1) exit(1);
	}
	r = waitpid(pid, &status, 0);
	if (r < 0)
		error("waitpid failed");
	print(1, "Executed `%s', status: %i\n", cmd, status);
}

static void process_commands(int argc, char *argv[]);

static void process_file(char *name)
{
	FILE *f;
	int argv_max = 16;
	int argv_size = 1;
	char **argv;
	int arg_max;
	int arg_size;
	char *arg;
	int c;

	f = fopen(name, "r");
	if (!f)
		error("failed to open `%s'", name);
	argv = ralloc(NULL, sizeof(*argv) * argv_max);
	argv[0] = "<none>";
	do {
		do {
			c = getc(f);
		} while (isspace(c));
		if (c == EOF)
			break;
		arg_max = 16;
		arg_size = 0;
		arg = ralloc(NULL, arg_max);
		do {
			if (arg_size + 1 >= arg_max) {
				arg_max *= 2;
				arg = ralloc(arg, arg_max);
			}
			arg[arg_size++] = c;
			c = getc(f);
		} while (!isspace(c) && c != EOF);
		arg[arg_size] = 0;
		if (argv_size + 1 >= argv_max) {
			argv_max *= 2;
			argv = ralloc(argv, sizeof(*argv) * argv_max);
		}
		argv[argv_size++] = arg;
	} while (c != EOF);
	argv[argv_size] = NULL;
	fclose(f);

	process_commands(argv_size, argv);

	while (--argv_size > 0) free(argv[argv_size]);
	free(argv);
}

static void process_commands(int argc, char *argv[])
{
	int saved_optind = optind;
	optind = 1;

	while (1) {
		static const struct option long_options[] = {
			{ "help", 0, NULL, 'h' },
			{ "verbose", 2, NULL, 'v' },
			{ "quiet", 0, NULL, 'q' },
			{ "log", 2, NULL, 'l' },
			{ "device", 1, NULL, 'd' },	/* Synonym for --open for backwards compatibility */
			{ "open", 1, NULL, 'd' },
			{ "close", 0, NULL, 1017 },
			{ "querycap", 0, NULL, 1001 },
			{ "input", 1, NULL, 'i' },
			{ "enuminput", 0, NULL, 1005 },
			{ "enumfmt", 2, NULL, 1020 },
			{ "output", 1, NULL, 'o' },
			{ "parm", 1, NULL, 'p' },
			{ "try_fmt", 1, NULL, 't' },
			{ "fmt", 1, NULL, 'f' },
			{ "reqbufs", 1, NULL, 'r' },
			{ "streamon", 0, NULL, 's' },
			{ "streamoff", 0, NULL, 'e' },
			{ "capture", 2, NULL, 'a' },
			{ "stream", 2, NULL, 1006 },
			{ "subdev_frame_interval", 1, NULL, 1019 },
			{ "exposure", 1, NULL, 'x' },
			{ "sensor_mode_data", 0, NULL, 1011 },
			{ "priv_data", 2, NULL, 1008 },
			{ "motor_priv_data", 2, NULL, 1010 },
			{ "isp_dump", 1, NULL, 9999 },
			{ "cvf_parm", 1, NULL, 1012 },
			{ "parameters", 1, NULL, 1014 },
			{ "ctrl-list", 0, NULL, 1003 },
			{ "fmt-list", 0, NULL, 1004 },
			{ "enumctrl", 2, NULL, 1018 },
			{ "ctrl", 1, NULL, 'c' },
			{ "wait", 2, NULL, 'w' },
			{ "waitkey", 2, NULL, 1009 },
			{ "shell", 1, NULL, 1007 },
			{ "statistics", 0, NULL, 1013 },
			{ "file", 1, NULL, 1015 },
			{ "pipe", 1, NULL, 1016 },
			{ "load", 1, NULL, 1021 },
			{ "crop", 1, NULL, 1022 },
			{ "cropcap", 2, NULL, 1023 },
			{ "selection", 1, NULL, 1024 },
			{ NULL, 0, NULL, 0 }
		};

		int c = getopt_long(argc, argv, "hv::ql::d:i:o:p:t:f:r:sea::x:c:w::", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':	/* --help, -h */
			usage();
			break;

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

		case 'l':	/* --log, -l */
			if (vars.logfile)
				fclose(vars.logfile);
			vars.logfile = fopen(optarg ? optarg : "/dev/kmsg", "w");
			if (!vars.logfile)
				error("opening log file failed");
			break;

		case 'd':	/* --device, --open, -d */
			itr_iterate(itd_open_device, optarg);
			break;

		case 1017:	/* --close */
			itr_iterate(itd_close_device, NULL);
			break;

		case 1001:	/* --querycap */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_querycap, NULL);
			break;

		case 'i':	/* --input, -i, VIDIOC_S/G_INPUT */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_sg_input, optarg);
			break;

		case 1005:	/* --enuminput */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_enuminput, NULL);
			break;

		case 1020:	/* --enumfmt, VIDIOC_ENUM_FMT/FRAMESIZES/FRAMEINTERVALS */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_enum_fmt, optarg);
			break;

		case 'o':
			itr_iterate(itd_output_name, optarg);
			break;

		case 'p':	/* --parm, -p, VIDIOC_S/G_PARM */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_parm, optarg);
			break;

		case 't':	/* --try-fmt, -t, VIDIOC_TRY_FMT */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_try_fmt, optarg);
			break;

		case 'f':	/* --fmt, -f, VIDIOC_S/G_FMT */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_sg_fmt, optarg);
			break;

		case 'r':	/* --reqbufs, -r, VIDIOC_REQBUFS */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_reqbufs, optarg);
			break;

		case 's':	/* --streamon, -s, VIDIOC_STREAMON: start streaming */
			itr_iterate(itd_streamon, (char*)TRUE);
			break;

		case 'e':	/* --streamoff, -e, VIDIOC_STREAMOFF: stop streaming */
			itr_iterate(itd_streamon, (char*)FALSE);
			break;

		case 'a':	/* --capture=N, -a: capture N buffers using QBUF/DQBUF */
			itr_capture(optarg ? atoi(optarg) : 1);
			break;

		case 1006:	/* --stream=N: capture N buffers and leave streaming on */
			itr_stream(optarg ? atoi(optarg) : 1);
			break;

		case 1019:	/* --subdev_frame_interval=X */
			itr_iterate(itd_subdev_frame_interval, optarg);
			break;

#if USE_ATOMISP
		case 'x':	/* --exposure=S, -x: ATOMISP_IOC_S_EXPOSURE */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_atomisp_ioc_s_exposure, optarg);
			break;

		case 1011:	/* --sensor_mode_data, ATOMISP_IOC_G_SENSOR_MODE_DATA */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_atomisp_ioc_g_sensor_mode_data, NULL);
			break;

		case 1008:	/* --priv_data=F, ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_atomisp_ioc_g_sensor_priv_int_data, optarg);
			break;

		case 1010:	/* --motor_priv_data=F, ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_atomisp_ioc_g_motor_priv_int_data, optarg);
			break;

		case 9999:	/* --isp_dump */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_atomisp_memory_dump, optarg);
			break;

		case 1012:	/* --cvf_parm */
			itr_iterate(itd_atomisp_ioc_s_cvf_params, optarg);
			break;

		case 1014:	/* --parameters, ATOMISP_IOC_S_PARAMETERS */
			itr_iterate(itd_atomisp_ioc_s_parameters, optarg);
			break;
#endif

		case 1003:	/* --ctrl-list */
			symbol_dump(V4L2_CID, controls);
			break;

		case 1004:	/* --fmt-list */
			symbol_dump(V4L2_PIX_FMT, pixelformats);
			break;

		case 1018:	/* --enumctrl */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_enumerate_controls, optarg);
			break;

		case 'c':	/* --ctrl, -c, VIDIOC_QUERYCTRL / VIDIOC_S/G_CTRL / VIDIOC_S/G_EXT_CTRLS */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_request_controls, optarg);
			break;

		case 'w':	/* -w, --wait */
			delay(optarg ? atof(optarg) : 0.0);
			break;

		case 1009: {	/* --waitkey */
			char b[256] = { 0 };
			FILE *f = optarg ? fopen(optarg, "r") : stdin;
			int l;
			if (!f) error("could not open file for reading a line");
			print(1, "WAITKEY from %s...", optarg ? optarg : "stdin");
			fgets(b, sizeof(b), f);
			if (optarg) fclose(f);
			l = strlen(b);
			if (l>0 && b[l-1]=='\n') b[l-1] = 0;
			print(1, "got `%s'\n", b);
			break;
		}

		case 1007:	/* --shell */
			shell(optarg);
			break;

		case 1013:	/* --statistics */
			vars.calculate_stats = TRUE;
			break;

		case 1015:	/* --file */
			process_file(optarg);
			break;

		case 1016:	/* --pipe */
			select_pipes(optarg);
			break;

		case 1021:	/* --load */
			itr_iterate(itd_load_bufdata, optarg);
			break;

		case 1022:	/* --crop */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_sg_crop, optarg);
			break;

		case 1023:	/* --cropcap */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_cropcap, optarg);
			break;

		case 1024:	/* --selection */
			itr_iterate(itd_open_device, NULL);
			itr_iterate(itd_vidioc_sg_selection, optarg);
			break;

		default:
			error("unknown option");
		}
	}
	optind = saved_optind;
}

int v4l2n_process_commands(int argc, char *argv[])
{
	int saved_optind = optind;
	int ret = setjmp(vars.exception);
	if (!ret)
		process_commands(argc, argv);
	optind = saved_optind;
	return ret;
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
		symbol_str(get_format_code(&cb->format), pixelformats), b);
	write_file(b, cb->image, cb->length);
}

int v4l2n_init(void)
{
	int i;
	int ret = setjmp(vars.exception);
	if (ret) return ret;

	_PAGE_SIZE = getpagesize();
	_PAGE_MASK = ~(_PAGE_SIZE - 1);

	memset(&vars, 0, sizeof(vars));
	if (gettimeofday(&vars.start_time, NULL) < 0) error("getting start time failed");
	vars.verbosity = 2;

	for (i = 0; i < MAX_PIPES; i++) {
		vars.pipes[i].fd = -1;
		vars.pipes[i].reqbufs.count = 2;
		vars.pipes[i].reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vars.pipes[i].reqbufs.memory = V4L2_MEMORY_USERPTR;
	}
	vars.pipes[0].active = TRUE;

	return 0;
}

int v4l2n_cleanup(void)
{
	int i;
	int ret = setjmp(vars.exception);
	if (ret) return ret;

	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		/* Save images */
		for (i = 0; i < vars.pipes[vars.pipe].num_capture_buffers; i++)
			capture_buffer_write(&vars.pipes[vars.pipe].capture_buffers[i], vars.pipes[vars.pipe].output, i);
	}

	for (vars.pipe = 0; vars.pipe < MAX_PIPES; vars.pipe++) {
		/* Stop streaming */
		if (vars.pipes[vars.pipe].streaming)
			itd_streamon((char*)FALSE);

		/* Free memory */
		itd_vidioc_querybuf_cleanup();
		for (i = 0; i < vars.pipes[vars.pipe].num_capture_buffers; i++) {
			free(vars.pipes[vars.pipe].capture_buffers[i].image);
		}
		itd_close_device(NULL);
		free(vars.pipes[vars.pipe].output);
		free(vars.pipes[vars.pipe].bufdata);
	}

	if (vars.logfile)
		fclose(vars.logfile);
	vars.logfile = NULL;

	return 0;
}

__attribute__((weak)) int main(int argc, char *argv[])
{
	int ret;

	print(1, "Starting %s\n", name);
	name = argv[0];
	if (v4l2n_init() < 0)
		return 1;
	ret = v4l2n_process_commands(argc, argv);
	if (v4l2n_cleanup() < 0)
		return 1;

	return ret;
}

/* EOF */
