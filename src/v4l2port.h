#ifndef V4L2PORT_H_
#define V4L2PORT_H_

#include <ctype.h>
#include <linux/videodev2.h>

struct timeval;


#define REQ_BUFFER_MAX	1


struct reqbuffer {
	void * start;
	int length;
};

struct v4l2profile
{
	char device[256];
	int width;
	int height;
	int fps;

	union
	{
		void *ptr;
		int value;
	};
};


typedef struct _v4l2port
{
	struct v4l2profile profile;
	int fd;
	int init_flag;
	int stream_flag;
	int last_error;
	int last_errno;
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmtdesc[3];
	struct v4l2_streamparm streamparm;

	unsigned int reqbufs_count;
	struct reqbuffer reqbufs[REQ_BUFFER_MAX];
} v4l2port_t;


typedef void (*v4l2_read_callback)(const char *, int, struct timeval *, void *);

const char *v4l2port_strerror(v4l2port_t *video);

const char *v4l2port_strerrno(v4l2port_t *video);

int v4l2port_getfd(v4l2port_t *video);

int v4l2port_getstate_init(v4l2port_t *video);

int v4l2port_getstate_stream(v4l2port_t *video);

v4l2port_t *v4l2port_new(const char *device, int width, int height, int fps);

int v4l2port_init(v4l2port_t *video);

int v4l2port_set_param(v4l2port_t *video, int fps);

int v4l2port_get_param(v4l2port_t *video);

int v4l2port_stream(v4l2port_t *video, int flag);

int v4l2port_read(v4l2port_t *video,
	v4l2_read_callback read_callback, void *ptr);

void v4l2port_uninit(v4l2port_t *video);

void v4l2port_free(v4l2port_t *video);

#endif