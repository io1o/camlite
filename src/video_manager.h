#ifndef VIDEO_MANAGER_H_
#define VIDEO_MANAGER_H_


#include <sys/time.h>

typedef struct _pevent_base pevent_base_t;

typedef struct _v4l2port v4l2port_t;

typedef void (*v4l2_read_callback)(const char *, int, struct timeval *, void *);


void video_manager_init(pevent_base_t *base,
	v4l2_read_callback read_callback, unsigned int timeout);

unsigned int video_manager_get_timeout();

v4l2port_t * video_manager_get(int index);

int video_manager_stream_start(int index);

int video_manager_init_video(int index);

int video_manager_add(const char *device, int width, int height, int fps);

void video_manager_set_check_time(v4l2port_t *video);

void video_manager_cleanup();

#endif