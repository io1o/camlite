#include "video_manager.h"
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include "util.h"
#include "v4l2port.h"
#include "pevent.h"

#define MAX_VIDEO_COUNT		10	


typedef struct _video_data
{
	v4l2port_t *video;
	pevent_t *pevent;
	unsigned int last_check_time;
} video_data_t;

typedef struct _video_manage
{
	unsigned int timeout;

	int count;
	video_data_t videos[MAX_VIDEO_COUNT];

	pevent_base_t *base;
	v4l2_read_callback read_callback;
} video_manage_t;

static struct _video_manage g_video_manage;


void on_video_event(pevent_t *pevent, int event, video_data_t *data)
{
	if (event == PEVENT_READ)
	{
		v4l2port_read(data->video, g_video_manage.read_callback, data->video);
		
		if (g_video_manage.timeout > 0 
			&& time(NULL) - data->last_check_time > g_video_manage.timeout)
		{
			if (data->pevent)
			{
				v4l2port_stream(data->video, 0);
				pevent_free_no_close(data->pevent);
				data->pevent = NULL;

				LOGINFO("set stream off(device:%s)\n", data->video->profile.device);
			}
		}
	}
	else if (event == PEVENT_ERROR)
	{
		LOGERROR("event == PEVENT_ERROR\n");
	}
}

void video_manager_init(pevent_base_t *base,
	v4l2_read_callback read_callback, unsigned int timeout)
{
	g_video_manage.base = base;
	g_video_manage.read_callback = read_callback;

	g_video_manage.timeout = timeout;
}

unsigned int video_manager_get_timeout()
{
	return g_video_manage.timeout;
}

v4l2port_t * video_manager_get(int index)
{
	if (index >= MAX_VIDEO_COUNT)
		return NULL;

	return g_video_manage.videos[index].video;
}

int video_manager_stream_start(int index)
{
	video_data_t *data;
	pevent_t *pevent;

	if (index >= MAX_VIDEO_COUNT)
		return -1;

	data = &g_video_manage.videos[index];

	if (data->video == NULL)
		return -1;

	data->last_check_time = time(NULL);

	if (data->pevent != NULL)
	{
		return 0;
	}

	if (!data->video->init_flag)
	{
		if (v4l2port_init(data->video) == -1)
		{
			LOGERROR("v4l2port_init failed device:%d\n", index);
			return -1;
		}
	}
	
	if (!data->video->stream_flag)
	{
		if (v4l2port_stream(data->video, 1) == -1)
		{
			LOGERROR("v4l2port_stream failed device:%d\n", index);
			return -1;
		}
	}

	pevent = pevent_new(g_video_manage.base,
		v4l2port_getfd(data->video), (pevent_callback)on_video_event, data);

	if (pevent_set(pevent, PEVENT_READ) == -1)
	{
		LOGERROR("pevent_set failed device:%d\n", index);
		pevent_free_no_close(pevent);
		v4l2port_stream(data->video, 0);
		return -1;
	}

	data->pevent = pevent;
	LOGINFO("set stream on(device:%s)\n", data->video->profile.device);
	return 0;
}

int video_manager_add(const char *device, int width, int height, int fps)
{
	int index;

	if (g_video_manage.count < MAX_VIDEO_COUNT)
	{
		index = g_video_manage.count;

		g_video_manage.videos[index].video = 
			v4l2port_new(device, width, height, fps);
	
		g_video_manage.videos[index].video->profile.value = index;

		++g_video_manage.count;

		LOGINFO("video device add success:%d\n", index);

		return video_manager_init_video(index);
	}

	return -1;
}

int video_manager_init_video(int index)
{
	video_data_t *data;

	if (index >= MAX_VIDEO_COUNT)
		return -1;

	data = &g_video_manage.videos[index];

	if (data->video == NULL)
	{
		LOGERROR("cannot find device:%d\n", index);
		return -1;
	} 

	if (data->pevent)
	{
		pevent_free_no_close(data->pevent);
		data->pevent = NULL;
	}

	if (v4l2port_init(data->video) == -1)
	{
		LOGINFO("video device init failure(%s:%s)\n",
				v4l2port_strerror(data->video),
				v4l2port_strerrno(data->video));
		return -1;
	}

	return 0;
}

void video_manager_set_check_time(v4l2port_t *video)
{
	int i;
	video_data_t *data;

	for (i = 0; i < g_video_manage.count; ++i)
	{
		data = &g_video_manage.videos[i];

		if (data == NULL)
			break;

		if (data->video == video)
		{
			data->last_check_time = time(NULL);
			break;
		}
	}
}

void video_manager_cleanup()
{
	int i;
	video_data_t *data;


	for (i = 0; i < g_video_manage.count; ++i)
	{
		data = &g_video_manage.videos[i];

		if (data == NULL)
			break;

		if (data->pevent)
			pevent_free_no_close(data->pevent);

		v4l2port_uninit(data->video);
		v4l2port_free(data->video);
	}
}