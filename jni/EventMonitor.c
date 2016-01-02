#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/limits.h>
#include <sys/poll.h>

#include "devinfo.h"
#include <jni.h>

#include <android/log.h>

#include <linux/fb.h>
#include <linux/kd.h>
#include "pixelflinger.h"

#define TAG "EventInjector::JNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG  , TAG, __VA_ARGS__)

struct uinput_event {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

char outMessage[4096];
char tmpmsg[4096];
int g_debug = 1;
int g_iTouchEventFileID;
int g_iHasTouchABS;
struct orng_device_info devinfo;
struct pollfd ufds;
struct input_event event;

typedef struct {
	long filesize;
	char reserved[2];
	long headersize;
	long infoSize;
	long width;
	long depth;
	short biPlanes;
	short bits;
	long biCompression;
	long biSizeImage;
	long biXPelsPerMeter;
	long biYPelsPerMeter;
	long biClrUsed;
	long biClrImportant;
} BMPHEAD;
static GGLSurface gr_framebuffer[2];
//handler
static int gr_fb_fd = -1;
//v screen info
static struct fb_var_screeninfo vi;
//f screen info
static struct fb_fix_screeninfo fi;
int g_iGraphicsFileID;

void debug(char *szFormat, ...) {
	if (g_debug == 0)
		return;
	//if (strlen(szDbgfile) == 0) return;

	char szBuffer[4096]; //in this buffer we form the message
	const size_t NUMCHARS = sizeof(szBuffer) / sizeof(szBuffer[0]);
	const int LASTCHAR = NUMCHARS - 1;
	//format the input string
	va_list pArgs;
	va_start(pArgs, szFormat);
	// use a bounded buffer size to prevent buffer overruns.  Limit count to
	// character size minus one to allow for a NULL terminating character.
	vsnprintf(szBuffer, NUMCHARS - 1, szFormat, pArgs);
	va_end(pArgs);
	//ensure that the formatted string is NULL-terminated
	szBuffer[LASTCHAR] = '\0';

	LOGD(szBuffer);
	//TextCallback(szBuffer);
}

static struct orng_device_info *
read_devinfo(struct orng_device_info *devinfo, int with_scancodes, int fd) {
	int i;
	char buf[1024];
	__u32 sc;
	__u16 j;
	int res = 0;
	__u32 nsc;

	memset(devinfo, 0, sizeof(*devinfo));

	/* device identifier */

	if (ioctl(fd, EVIOCGID, &devinfo->id) < 0) {
		sprintf(tmpmsg, "ioctl(EVIOCGID): %s\n", strerror(errno));
		strcat(outMessage, tmpmsg);
		return NULL;
	}

	/* event bits */

	if (ioctl(fd, EVIOCGBIT(0, sizeof(devinfo->evbit)), devinfo->evbit) < 0) {
		sprintf(tmpmsg, "ioctl(EVIOCGBIT(0)): %s\n", strerror(errno));
		strcat(outMessage, tmpmsg);
		return NULL;
	}

	/* keys */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_KEY)) {
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(devinfo->keybit)),
				devinfo->keybit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_KEY)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}

		/* key state */

		if (TEST_ARRAY_BIT(devinfo->evbit, EV_KEY)) {
			if (ioctl(fd, EVIOCGKEY(sizeof(devinfo->key)), devinfo->key) < 0) {
				sprintf(tmpmsg, "ioctl(EVIOCGKEY(%zu)): %s\n", sizeof(buf),
						strerror(errno));
				strcat(outMessage, tmpmsg);
				return NULL;
			}
		}

		/* read mapping between scan codes and key codes */

		if (with_scancodes) {
			nsc = 1ul << ((CHAR_BIT * sizeof(devinfo->keymap[0][0])) - 1);

			for (sc = 0, j = 0; sc < nsc; ++sc) {

				int map[2] = { sc, 0 };

				int res = ioctl(fd, EVIOCGKEYCODE, map);

				if (res < 0) {
					if (errno != EINVAL) {
						sprintf(tmpmsg, "ioctl: %s\n", strerror(errno));
						strcat(outMessage, tmpmsg);
						return NULL;
					}
				} else {
					/* save mapping */

					devinfo->keymap[j][0] = map[0]; /* scan code */
					devinfo->keymap[j][1] = map[1]; /* key code */
					++j;

					if (j
							>= sizeof(devinfo->keymap)
									/ sizeof(devinfo->keymap[0])) {
						break;
					}
				}
			}
		}
	}

	/* relative positioning */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_REL)) {
		if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(devinfo->relbit)),
				devinfo->relbit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_REL)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;

		}
	}

	/* absolute positioning */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_ABS)) {
		if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(devinfo->absbit)),
				devinfo->absbit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_ABS)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}

		/* limits */

		for (i = 0; i <= ABS_MAX; ++i) {
			if (TEST_ARRAY_BIT(devinfo->absbit, i)) {
				if (ioctl(fd, EVIOCGABS(i), devinfo->absinfo+i) < 0) {
					sprintf(tmpmsg, "ioctl(EVIOCGABS(%d)): %s\n", i,
							strerror(errno));
					strcat(outMessage, tmpmsg);
					return NULL;
				}

			}
		}
	}

	/* misc */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_MSC)) {
		if (ioctl(fd, EVIOCGBIT(EV_MSC, sizeof(devinfo->mscbit)),
				devinfo->mscbit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_MSC)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	}

	/* LEDs */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_LED)) {
		if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(devinfo->ledbit)),
				devinfo->ledbit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_LED)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}

		/* LED state */

		if (TEST_ARRAY_BIT(devinfo->evbit, EV_LED)) {
			if (ioctl(fd, EVIOCGLED(sizeof(devinfo->led)), devinfo->led) < 0) {
				sprintf(tmpmsg, "ioctl(EVIOCGLED(%zu)): %s\n", sizeof(buf),
						strerror(errno));
				strcat(outMessage, tmpmsg);
				return NULL;
			}
		}
	}

	/* sound */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_SND)) {
		if (ioctl(fd, EVIOCGBIT(EV_SND, sizeof(devinfo->sndbit)),
				devinfo->sndbit) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_SND)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}

		/* sound state */

		if (TEST_ARRAY_BIT(devinfo->evbit, EV_SW)) {
			if (ioctl(fd, EVIOCGSND(sizeof(devinfo->snd)), devinfo->snd) < 0) {
				sprintf(tmpmsg, "ioctl(EVIOCGSND(%zu)): %s\n", sizeof(buf),
						strerror(errno));
				strcat(outMessage, tmpmsg);
				return NULL;
			}
		}
	}

	/* force feedback */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_FF)) {
		if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(devinfo->ffbit)), devinfo->ffbit)
				< 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_FF)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	}

	/* switches */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_SW)) {
		if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(devinfo->swbit)), devinfo->swbit)
				< 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGBIT(EV_SW)): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}

		/* switch state */

		if (TEST_ARRAY_BIT(devinfo->evbit, EV_SW)) {
			if (ioctl(fd, EVIOCGSW(sizeof(devinfo->sw)), devinfo->sw) < 0) {
				sprintf(tmpmsg, "ioctl(EVIOCGSW(%zu)): %s\n", sizeof(buf),
						strerror(errno));
				strcat(outMessage, tmpmsg);
				return NULL;
			}
		}
	}

	/* auto repeat */

	if (TEST_ARRAY_BIT(devinfo->evbit, EV_REP)) {
		if (ioctl(fd, EVIOCGREP, devinfo->rep) < 0) {
			sprintf(tmpmsg, "ioctl(EVIOCGREP): %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	}

	/* name */

	memset(buf, 0, sizeof(buf));

	do {
		res = ioctl(fd, EVIOCGNAME(sizeof(buf)), buf);
	} while ((res < 0) && (errno == EINTR));

	if (res >= 0) {
		devinfo->name = strndup(buf, sizeof(buf) - 1);

		if (!devinfo->name) {
			sprintf(tmpmsg, "strdup: %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	} else if (errno != ENOENT) {
		sprintf(tmpmsg, "ioctl(EVIOCGPHYS(%lu)): %s\n",
				(unsigned long) sizeof(buf), strerror(errno));
		strcat(outMessage, tmpmsg);
		return NULL;
	}

	/* physical location */

	memset(buf, 0, sizeof(buf));

	do {
		res = ioctl(fd, EVIOCGPHYS(sizeof(buf)), buf);
	} while ((res < 0) && (errno == EINTR));

	if (res >= 0) {
		devinfo->phys = strndup(buf, sizeof(buf) - 1);

		if (!devinfo->phys) {
			sprintf(tmpmsg, "strdup: %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	} else if (errno != ENOENT) {
		sprintf(tmpmsg, "ioctl(EVIOCGPHYS(%lu)): %s\n",
				(unsigned long) sizeof(buf), strerror(errno));
		strcat(outMessage, tmpmsg);
		return NULL;
	}

	/* unique identifier */

	memset(buf, 0, sizeof(buf));

	do {
		res = ioctl(fd, EVIOCGUNIQ(sizeof(buf)), buf);
	} while ((res < 0) && (errno == EINTR));

	if (res >= 0) {
		devinfo->uniq = strndup(buf, sizeof(buf) - 1);

		if (!devinfo->uniq) {
			sprintf(tmpmsg, "strdup: %s\n", strerror(errno));
			strcat(outMessage, tmpmsg);
			return NULL;
		}
	} else if (errno != ENOENT) {
		sprintf(tmpmsg, "ioctl(EVIOCGUNIQ(%lu)): %s\n",
				(unsigned long) sizeof(buf), strerror(errno));
		strcat(outMessage, tmpmsg);
		return NULL;
	}

	return devinfo;

}

jstring Java_com_is_eventdebugger_EventMonitorThread_GetGraphicsInfo(
		JNIEnv* env, jobject jobj) {

	char tmpmsg[4096];

	tmpmsg[0] = 0;

	if (ioctl(g_iGraphicsFileID, FBIOGET_FSCREENINFO, &fi) < 0) {
		perror("failed to get fb0 info");
		return -1;
	}
	debug("ioctl ");

	if (ioctl(g_iGraphicsFileID, FBIOGET_VSCREENINFO, &vi) < 0) {
		perror("failed to get fb0 info");
		return -1;
	}

	sprintf(tmpmsg, "X Res = %d\n"
			"Y Res = %d\n"
			"X Virtual = %d\n"
			"Y Virtual = %d\n"
			"Xoffset = %d\n"
			"YoffSet = %d\n"
			"Bits Per Pixel = %d\n"
			"Grayscale = %d\n"
			"Nonstd = %d\n"
			"Activate = %d\n"
			"Height = %d\n"
			"Width = %d\n"
			"accel flag = %d\n"
			"Pixel clock = %d\n"
			"left Margin = %d\n"
			"right margin = %d\n"
			"upper margin = %d\n"
			"lower margin = %d\n"
			"hsync len = %d\n"
			"vsync len = %d\n"
			"sync = %d\n"
			"vmode = %d\n"
			"rotate = %d\n"
			"reserverd = %d\n"
			"line lenght = %d\n", vi.xres, vi.yres, vi.xres_virtual,
			vi.yres_virtual, vi.xoffset, vi.yoffset, vi.bits_per_pixel,
			vi.grayscale, vi.nonstd, vi.activate, vi.height, vi.width,
			vi.accel_flags, vi.pixclock, vi.left_margin, vi.right_margin,
			vi.upper_margin, vi.lower_margin, vi.hsync_len, vi.vsync_len,
			vi.sync, vi.vmode, vi.rotate,vi.reserved,
			fi.line_length);
	return (*env)->NewStringUTF(env, tmpmsg);

}

jint Java_com_concertochoir_service_InputDevice_intSendEvent(JNIEnv* env,
		jobject thiz, jint index, uint16_t type, uint16_t code, int32_t value) {
	int fd = index;
	//debug("SendEvent call (%d,%d,%d,%d)", fd, type, code, value);
	struct uinput_event event;
	int len;

	if (fd <= fileno(stderr))
		return;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;

	len = write(fd, &event, sizeof(event));
	debug("SendEvent done:%d", len);
}

jint Java_com_is_eventdebugger_EventMonitorThread_PollDev(JNIEnv* env,
		jobject thiz) {

	int pollres = poll(&ufds, 1, -1);
	if (ufds.revents) {
		if (ufds.revents & POLLIN) {
			int res = read(ufds.fd, &event, sizeof(event));
			if (res < (int) sizeof(event)) {
				return 1;
			} else
				return 0;
		}
	}
	return -1;
}
jint Java_com_is_eventdebugger_EventMonitorThread_getType(JNIEnv* env,
		jobject thiz) {
	return event.type;
}

jint Java_com_is_eventdebugger_EventMonitorThread_getCode(JNIEnv* env,
		jobject thiz) {
	return event.code;
}

jint Java_com_is_eventdebugger_EventMonitorThread_getValue(JNIEnv* env,
		jobject thiz) {
	return event.value;
}

jint Java_com_is_eventdebugger_EventMonitorThread_getSU(JNIEnv* env,
		jobject jobj) {

	char devname[PATH_MAX];
	char *filename;
	DIR *dir;
	struct dirent *de;

	outMessage[0] = 0;

	dir = opendir("/dev/input/");
	if (dir == NULL)
		return -1;
	//filename = devname + strlen(devname);
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.'
				&& (de->d_name[1] == '\0'
						|| (de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;
		if (de->d_name[0] != 'e' || de->d_name[1] != 'v' || de->d_name[2] != 'e'
				|| de->d_name[3] != 'n' || de->d_name[4] != 't')
			continue;

		filename = strdup(de->d_name);
		debug("scan_dir:prepare to open:%s", filename);
		// add new filename to our structure: devname
		strcpy(devname, "/dev/input/");
		strcat(devname, filename);
		system("su -c \"chmod 666 /dev/input/*\"");
		int iTempHandle = open(devname, O_RDONLY);
		debug("Handler value is %d :", iTempHandle);
		if (iTempHandle < 0) {
			debug("system");
			system("su -c \"chmod 666 /dev/input/*\"");

			iTempHandle = open(devname, O_RDONLY);
			if (iTempHandle < 0)
				return -1;

		}
		close(iTempHandle);
		iTempHandle = open("/dev/graphics/fb0", O_RDWR);
		debug("after open");
		if (iTempHandle < 0) {
			debug("system");
			system("su -c \"chmod 666 /dev/graphics/fb0\"");

			iTempHandle = open("/dev/graphics/fb0", O_RDONLY);
			if (iTempHandle < 0)
				return -1;
		}
		close(iTempHandle);
	}
	closedir(dir);

	return 0;

}

jint Java_com_is_eventdebugger_EventMonitorThread_OpenTouchDevice(JNIEnv* env,
		jobject jobj) {

	char devname[PATH_MAX];
	char *filename;
	DIR *dir;
	struct dirent *de;

	outMessage[0] = 0;

	dir = opendir("/dev/input/");
	if (dir == NULL)
		return -1;
	//filename = devname + strlen(devname);
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.'
				&& (de->d_name[1] == '\0'
						|| (de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;
		if (de->d_name[0] != 'e' || de->d_name[1] != 'v' || de->d_name[2] != 'e'
				|| de->d_name[3] != 'n' || de->d_name[4] != 't')
			continue;

		filename = strdup(de->d_name);
		debug("scan_dir:prepare to open:%s", filename);
		// add new filename to our structure: devname
		strcpy(devname, "/dev/input/");
		strcat(devname, "event");
		system("su -c \"chmod 666 /dev/input/*\"");
		//int iTempHandle = open(devname, O_RDONLY);


		g_iTouchEventFileID = open(devname, O_RDONLY);
		debug("Handler value is %d :", g_iTouchEventFileID);
		if (g_iTouchEventFileID < 0) {
			debug("system");
			system("su -c \"chmod 666 /dev/input/*\"");

			g_iTouchEventFileID = open(devname, O_RDONLY);
			if (g_iTouchEventFileID < 0)
				return g_iTouchEventFileID;

		}

		g_iGraphicsFileID = open("/dev/graphics/fb0", O_RDWR);
		debug("after open");
		/*if (g_iGraphicsFileID < 0) {
			debug("system");
			system("su -c \"chmod 666 /dev/graphics/fb0\"");

			g_iGraphicsFileID = open("/dev/graphics/fb0", O_RDONLY);
			if (g_iGraphicsFileID < 0)
				return -1;
		}*/
		ufds.fd = g_iTouchEventFileID;
		ufds.events = POLLIN;

		g_iHasTouchABS = 0;
		if (!read_devinfo(&devinfo, 0, g_iTouchEventFileID)) {
			return -1;
		}
		break;
		/*debug("after read_devinfo");
		int i;
		if (TEST_ARRAY_BIT(devinfo.evbit, EV_ABS)) {
			for (i = 0;
					i < sizeof(devinfo.absinfo) / sizeof(devinfo.absinfo[0]);
					++i) {
				if (TEST_ARRAY_BIT(devinfo.absbit, i)) {
					if (i == 53)
						g_iHasTouchABS++;
					if (i == 54)
						g_iHasTouchABS++;
				}
			}
		}

		debug(devinfo.name);
		if (g_iHasTouchABS == 2)
			break;
		close(g_iTouchEventFileID);
*/
	}
	closedir(dir);

	//if (g_iHasTouchABS)
		return g_iTouchEventFileID;
	//else
	//	return -1;

}
jstring Java_com_is_eventdebugger_EventMonitorThread_GetDeviceInfo(JNIEnv* env,
		jobject jobj) {

	//if (!write_devinfo(&devinfo, 0)) {
	//}
	outMessage[0] = 0;

	int i;
	if (TEST_ARRAY_BIT(devinfo.evbit, EV_ABS)) {
		for (i = 0; i < sizeof(devinfo.absinfo) / sizeof(devinfo.absinfo[0]);
				++i) {
			if (TEST_ARRAY_BIT(devinfo.absbit, i)) {
				sprintf(tmpmsg, "%lu,%d,%d,", (unsigned int) i,
						devinfo.absinfo[i].minimum, devinfo.absinfo[i].maximum);
				strcat(outMessage, tmpmsg);
			}
		}
	}

	debug("After write_devinfo");
	debug(outMessage);

	return (*env)->NewStringUTF(env, outMessage);

}

static int get_framebuffer(GGLSurface *fb) {
	//int fd;
	void *bits;

	debug("before open - %d", g_iGraphicsFileID);

	if (ioctl(g_iGraphicsFileID, FBIOGET_FSCREENINFO, &fi) < 0) {
		perror("failed to get fb0 info");
		return -1;
	}
	debug("ioctl ");

	if (ioctl(g_iGraphicsFileID, FBIOGET_VSCREENINFO, &vi) < 0) {
		perror("failed to get fb0 info");
		return -1;
	}
	debug("ioctl ");

	//dumpinfo();

	bits = mmap(0, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
			g_iGraphicsFileID, 0);
	debug("mmap ");
	if (bits == MAP_FAILED) {
		perror("failed to mmap framebuffer");
		return -1;
	}

	fb->version = sizeof(*fb);
	fb->width = vi.xres;
	fb->height = vi.yres;
	fb->stride = fi.line_length / (vi.bits_per_pixel >> 3);
	fb->data = bits;
	fb->format = GGL_PIXEL_FORMAT_RGB_565;

	fb++;

	fb->version = sizeof(*fb);
	fb->width = vi.xres;
	fb->height = vi.yres;
	fb->stride = fi.line_length / (vi.bits_per_pixel >> 3);
	fb->data = (void*) (((unsigned) bits) + vi.yres * vi.xres * 2);
	fb->format = GGL_PIXEL_FORMAT_RGB_565;

	return g_iGraphicsFileID;
}

jint Java_com_is_eventdebugger_EventMonitorThread_SayCheese(JNIEnv* env,
		jobject jobj, jstring filename) {
	//get screen capture

	char* path;

	path = (*env)->GetStringUTFChars(env, filename, NULL);
	gr_fb_fd = get_framebuffer(gr_framebuffer);
	debug("after framebuffer ");

	if (gr_fb_fd <= 0)
		exit(1);

	int w = vi.xres, h = vi.yres, depth = vi.bits_per_pixel;
	debug("depth = %d", depth);
	//convert pixel data
	uint8_t *rgb24;
	if (depth == 16) {
		rgb24 = (uint8_t *) malloc(w * h * 3);
		int i = 0;
		for (; i < w * h; i++) {
			uint16_t pixel16 = ((uint16_t *) gr_framebuffer[0].data)[i];
			// RRRRRGGGGGGBBBBBB -> RRRRRRRRGGGGGGGGBBBBBBBB
			// in rgb24 color max is 2^8 per channel (*255/32 *255/64 *255/32)
			rgb24[3 * i + 2] = (255 * (pixel16 & 0x001F)) / 32; //Blue
			rgb24[3 * i + 1] = (255 * ((pixel16 & 0x07E0) >> 5)) / 64; //Green
			rgb24[3 * i] = (255 * ((pixel16 & 0xF800) >> 11)) / 32; //Red
		}
	} else if (depth == 24) //exactly what we need
			{
		rgb24 = (uint8_t *) gr_framebuffer[0].data;
	} else if (depth == 32) //skip transparency channel
			{
		rgb24 = (uint8_t *) malloc(w * h * 3);
		int i = 0;
		for (; i < w * h; i++) {
			uint32_t pixel32 = ((uint32_t *) gr_framebuffer[0].data)[i];
			// in rgb24 color max is 2^8 per channel
			rgb24[3 * i + 2] = pixel32 & 0x000000FF; //Blue
			rgb24[3 * i + 1] = (pixel32 & 0x0000FF00) >> 8; //Green
			rgb24[3 * i + 0] = (pixel32 & 0x00FF0000) >> 16; //Red
		}
	} else {
		//free
		close(gr_fb_fd);
		exit(2);
	};
	//save RGB 24 Bitmap
	int bytes_per_pixel = 3;
	BMPHEAD bh;
	memset((char *) &bh, 0, sizeof(BMPHEAD)); // sets everything to 0
	//bh.filesize  =   calculated size of your file (see below)
	//bh.reserved  = two zero bytes
	bh.headersize = 54L; // for 24 bit images
	bh.infoSize = 0x28L; // for 24 bit images
	bh.width = w; // width of image in pixels
	bh.depth = h; // height of image in pixels
	bh.biPlanes = 1; // for 24 bit images
	bh.bits = 8 * bytes_per_pixel; // for 24 bit images
	bh.biCompression = 0L; // no compression
	int bytesPerLine;
	bytesPerLine = w * bytes_per_pixel; // for 24 bit images
	//round up to a dword boundary
	if (bytesPerLine & 0x0003) {
		bytesPerLine |= 0x0003;
		++bytesPerLine;
	}
	bh.filesize = bh.headersize + (long) bytesPerLine * bh.depth;
	FILE * bmpfile;
	//printf("Bytes per line : %d\n", bytesPerLine);
	debug("before bmp file open ");
	char fullpath[1024];
	fullpath[0] = 0;
	strcpy(fullpath, "/mnt/sdcard/mConcerto/snapshots/");
	strcat(fullpath, path);
	debug("File path %s", fullpath);

	bmpfile = fopen(fullpath, "wb");
	if (bmpfile == NULL) {
		close(gr_fb_fd);
		exit(3);
	}
	fwrite("BM", 1, 2, bmpfile);
	fwrite((char *) &bh, 1, sizeof(bh), bmpfile);
	//fwrite(rgb24,1,w*h*3,bmpfile);
	char *linebuf;
	linebuf = (char *) calloc(1, bytesPerLine);
	if (linebuf == NULL) {
		fclose(bmpfile);
		close(gr_fb_fd);
		exit(4);
	}
	int line, x;
	for (line = h - 1; line >= 0; line--) {
		// fill line linebuf with the image data for that line
		for (x = 0; x < w; x++) {
			*(linebuf + x * bytes_per_pixel) = *(rgb24
					+ (x + line * w) * bytes_per_pixel + 2);
			*(linebuf + x * bytes_per_pixel + 1) = *(rgb24
					+ (x + line * w) * bytes_per_pixel + 1);
			*(linebuf + x * bytes_per_pixel + 2) = *(rgb24
					+ (x + line * w) * bytes_per_pixel + 0);
		}
		// remember that the order is BGR and if width is not a multiple
		// of 4 then the last few bytes may be unused
		//debug("before write bmp");
		fwrite(linebuf, 1, bytesPerLine, bmpfile);

	}
	fclose(bmpfile);
	//close(gr_fb_fd);
	return 0;
}

