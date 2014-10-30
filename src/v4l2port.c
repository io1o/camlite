#include "v4l2port.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "util.h"




enum V4L2_ERROR_TYPE
{
	V4L2_ERROR_NONE = 0,
	V4L2_ERROR_CAN_NOT_OPEN,
	V4L2_ERROR_NOT_V4L2_DEVICE,
	V4L2_ERROR_VIDIOC_QUERYCAP,
	V4L2_ERROR_NOT_SUPPORT_STREAMER,
	V4L2_ERROR_VIDIOC_S_FMT,
	V4L2_ERROR_VIDIOC_REQBUFS,
	V4L2_ERROR_VIDIOC_REQBUFS_NO_MEMORY,
	V4L2_ERROR_VIDIOC_QUERYBUF,
	V4L2_ERROR_MMAP,
	V4L2_ERROR_VIDIOC_QBUF,
	V4L2_ERROR_VIDIOC_STREAMON,
	V4L2_ERROR_VIDIOC_STREAMOFF,
	V4L2_ERROR_VIDIOC_DQBUF,
	V4L2_ERROR_VIDIOC_DQBUF_EAGAIN,
	V4L2_ERROR_VIDIOC_DQBUF_EIO,
	V4L2_ERROR_VIDIOC_STREAM_IS_ON,
	V4L2_ERROR_VIDIOC_STREAM_IS_OFF,
	V4L2_ERROR_VIDIOC_S_PARM,
	V4L2_ERROR_VIDIOC_G_PARM,
	V4L2_ERROR_NOT_SUPPORT_STREAMING,
	V4L2_ERROR_VIDIOC_END,
};

static const char *v4l2_error_string[V4L2_ERROR_VIDIOC_END] =
{
	"V4L2_ERROR_NONE",
	"V4L2_ERROR_CAN_NOT_OPEN",
	"V4L2_ERROR_NOT_V4L2_DEVICE",
	"V4L2_ERROR_VIDIOC_QUERYCAP",
	"V4L2_ERROR_NOT_SUPPORT_STREAMER",
	"V4L2_ERROR_VIDIOC_S_FMT",
	"V4L2_ERROR_VIDIOC_REQBUFS",
	"V4L2_ERROR_VIDIOC_REQBUFS_NO_MEMORY",
	"V4L2_ERROR_VIDIOC_QUERYBUF",
	"V4L2_ERROR_MMAP",
	"V4L2_ERROR_VIDIOC_QBUF",
	"V4L2_ERROR_VIDIOC_STREAMON",
	"V4L2_ERROR_VIDIOC_STREAMOFF",
	"V4L2_ERROR_VIDIOC_DQBUF",
	"V4L2_ERROR_VIDIOC_DQBUF_EAGAIN",
	"V4L2_ERROR_VIDIOC_DQBUF_EIO",
	"V4L2_ERROR_VIDIOC_STREAM_IS_ON",
	"V4L2_ERROR_VIDIOC_STREAM_IS_OFF",
	"V4L2_ERROR_VIDIOC_S_PARM",
	"V4L2_ERROR_VIDIOC_G_PARM",
	"V4L2_ERROR_NOT_SUPPORT_STREAMING"
};

static int xioctl(int fd, int request, void *arg)
{
	int r;


	do r = ioctl(fd, request, arg);
	while (-1 == r && EINTR == errno);
	
	return r;
}
 
const char *v4l2port_strerror(v4l2port_t *video)
{
	return v4l2_error_string[video->last_error];
}

const char *v4l2port_strerrno(v4l2port_t *video)
{
	return strerror(video->last_errno);
}

int v4l2port_getfd(v4l2port_t *video)
{
	return video->fd;
}

int v4l2port_getstate_init(v4l2port_t *video)
{
	return video->init_flag;
}

int v4l2port_getstate_stream(v4l2port_t *video)
{
	return video->stream_flag;
}

void v4l2port_mmap_release(v4l2port_t *video)
{
	unsigned int i;


	for (i = 0; i < video->reqbufs_count; ++i)
	{
		if (munmap(video->reqbufs[i].start, video->reqbufs[i].length) == -1)
		{
			LOGERROR("munmap errno(%u):%s\n", errno, strerror(errno));
		}
	}

	video->reqbufs_count = 0;
	memset(video->reqbufs, 0, sizeof(video->reqbufs));
	
};

int v4l2port_mmap_init(v4l2port_t *video)
{
	int error_type = 0; 
	errno = 0;
	struct v4l2_requestbuffers req;
	int i;
	struct v4l2_buffer buf;

	if (video->reqbufs_count > 0)
	{
		v4l2port_mmap_release(video);
	}

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = REQ_BUFFER_MAX;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(video->fd, VIDIOC_REQBUFS, &req) == -1)
	{
		error_type = V4L2_ERROR_VIDIOC_REQBUFS;
		goto __error;
	}
	
    if (req.count < REQ_BUFFER_MAX)
	{
		error_type = V4L2_ERROR_VIDIOC_REQBUFS_NO_MEMORY;
		goto __error;
	}
	
	
	video->reqbufs_count = req.count;

	for (i = 0; i < req.count; ++i)
	{
		memset(&buf, 0, sizeof(struct v4l2_buffer));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory  = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(video->fd, VIDIOC_QUERYBUF, &buf))
		{
			error_type = V4L2_ERROR_VIDIOC_QUERYBUF;
			goto __error;
		}

		video->reqbufs[i].length = buf.length;
		video->reqbufs[i].start = 
			mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED , video->fd, buf.m.offset);

		if (MAP_FAILED == video->reqbufs[i].start)
		{
			error_type = V4L2_ERROR_MMAP;
			goto __error;
		}
	}

	for (i = 0; i < video->reqbufs_count; ++i)
	{
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(video->fd, VIDIOC_QBUF, &buf))
		{
			error_type = V4L2_ERROR_VIDIOC_QBUF;
			goto __error;
		}
	}

	return 0;

__error:
	video->last_errno = errno;
	video->last_error = error_type;

	v4l2port_mmap_release(video);
	return -1;
}

v4l2port_t *v4l2port_new(const char *device, int width, int height, int fps)
{
	v4l2port_t *video;
	
	video = fcalloc(1, sizeof(v4l2port_t));
	
	strncpy(video->profile.device, device, sizeof(video->profile.device) - 1);
	video->profile.width = width;
	video->profile.height = height;
	video->profile.fps = fps;

	return video;
}

int v4l2port_init(v4l2port_t *video)
{
	int error_type = 0;
	errno = 0;
	int fd;
	struct v4l2_format fmt;
	struct v4l2_fmtdesc fmtdesc;


	if (video->init_flag)
	{
		v4l2port_uninit(video);
	}

    fd = open(video->profile.device, O_RDWR | O_NONBLOCK);
    if(fd < 0)
	{
        error_type = V4L2_ERROR_CAN_NOT_OPEN;
		goto __error;
    }


    if(xioctl(fd, VIDIOC_QUERYCAP, &video->cap) == -1)
	{
		if(EINVAL == errno)
		{
			error_type = V4L2_ERROR_NOT_V4L2_DEVICE;
			goto __error;
		}
		else
		{
			error_type = V4L2_ERROR_VIDIOC_QUERYCAP;
			goto __error;
		}
		
		return -1;
	}

    if(!(video->cap.capabilities & V4L2_CAP_STREAMING))
	{
        error_type = V4L2_ERROR_NOT_SUPPORT_STREAMING;
		goto __error;
    }
	
	

	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	while(fmtdesc.index < sizeof(video->fmtdesc) / sizeof(struct v4l2_fmtdesc)
		&& ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1)
	{
		memcpy(&video->fmtdesc[fmtdesc.index], &fmtdesc, sizeof(struct v4l2_fmtdesc));
		fmtdesc.index++;
	}
	

	memset(&fmt, 0, sizeof(struct v4l2_format));
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = video->profile.width; 
	fmt.fmt.pix.height      = video->profile.height; 
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
	{
		error_type = V4L2_ERROR_VIDIOC_S_FMT;
		goto __error;
	}

	v4l2port_set_param(video, video->profile.fps);

	video->fd = fd;
	video->init_flag = 1;

	LOGDEBUG("v4l2_init('%s') success\n", video->profile.device);

	return 0;

__error:
	if (fd > 0)
		close(fd);

	video->last_errno = errno;
	video->last_error = error_type;
	return -1;
}

int v4l2port_set_param(v4l2port_t *video, int fps)
{
	struct v4l2_streamparm *streamparm;
	
	
	streamparm = &video->streamparm;
	memset(streamparm, 0, sizeof(struct v4l2_streamparm));

	streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	streamparm->parm.capture.timeperframe.numerator = 1;
	streamparm->parm.capture.timeperframe.denominator = fps;


	if (xioctl(video->fd, VIDIOC_S_PARM, streamparm) == -1)
	{
		video->last_errno = errno;
		video->last_error = V4L2_ERROR_VIDIOC_S_PARM;
		return -1;
	}

	return 0;
}

int v4l2port_get_param(v4l2port_t *video)
{
	struct v4l2_streamparm *streamparm;
	
	streamparm = &video->streamparm;
	memset(streamparm, 0, sizeof(struct v4l2_streamparm));

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(video->fd, VIDIOC_G_PARM, streamparm) == - 1)
	{
		video->last_errno = errno;
		video->last_error = V4L2_ERROR_VIDIOC_G_PARM;
		return -1;
	}

	return 0;
}

int v4l2port_stream(v4l2port_t *video, int flag)
{
	enum v4l2_buf_type type;

	if (flag)
	{
		if (!video->stream_flag)
		{
			if (v4l2port_mmap_init(video) == -1)
			{
				return -1;
			}

			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (xioctl(video->fd, VIDIOC_STREAMON, &type) == -1)
			{
				video->last_errno = errno;
				video->last_error = V4L2_ERROR_VIDIOC_STREAMON;
				return -1;
			}

			video->stream_flag = 1;
		}
		else
		{
			video->last_errno = 0;
			video->last_error = V4L2_ERROR_VIDIOC_STREAM_IS_ON;
			return -1;
		}

		return 0;


	}
	else
	{
		if (video->stream_flag)
		{
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (xioctl(video->fd, VIDIOC_STREAMOFF, &type) == -1)
			{
				video->last_errno = errno;
				video->last_error = V4L2_ERROR_VIDIOC_STREAMOFF;
				return -1;
			}

			v4l2port_mmap_release(video);

			video->stream_flag = 0;
		}
		else
		{
			video->last_errno = 0;
			video->last_error = V4L2_ERROR_VIDIOC_STREAM_IS_OFF;
			return -1;
		}

		return 0;
	}
}

int v4l2port_read(v4l2port_t *video,
	v4l2_read_callback read_callback, void *ptr)
{
	struct v4l2_buffer buf;

	if (!video->stream_flag)
	{
		errno = 0;
		video->last_error = V4L2_ERROR_VIDIOC_STREAM_IS_OFF;
		return -1;
	}

	memset(&buf, 0, sizeof(struct v4l2_buffer));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (xioctl(video->fd, VIDIOC_DQBUF, &buf) == -1)
	{
		switch (errno)
		{
		case EAGAIN:
			video->last_errno = errno;
			video->last_error = V4L2_ERROR_VIDIOC_DQBUF_EAGAIN;
			return -1;
		case EIO:
			video->last_errno = errno; 
			video->last_error = V4L2_ERROR_VIDIOC_DQBUF_EIO;
			return -1;
		default:
			video->last_errno = errno; 
			video->last_error = V4L2_ERROR_VIDIOC_DQBUF;
			return -1;
		}
	}

	if (read_callback != NULL)
		read_callback(video->reqbufs[buf.index].start, buf.bytesused, &buf.timestamp, ptr);

	if (xioctl(video->fd, VIDIOC_QBUF, &buf) == -1)
	{
		video->last_errno = errno;
		video->last_error = V4L2_ERROR_VIDIOC_QBUF;
		return -1;
	}

	return 0;
}


void v4l2port_uninit(v4l2port_t *video)
{
	v4l2port_stream(video, 0);

	if(video->fd >= 0)
	{
		close(video->fd);
		video->fd = 0;
	}

	video->init_flag = 0;
	video->stream_flag = 0;
	memset(&video->cap, 0, sizeof(video->cap));
	memset(video->fmtdesc, 0, sizeof(video->fmtdesc));
	memset(&video->streamparm, 0, sizeof(video->streamparm));
}

void v4l2port_free(v4l2port_t *video)
{
	free(video);
}