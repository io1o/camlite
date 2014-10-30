#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include "util.h"
#include "camhttp.h"
#include "pevent_base.h"
#include "video_manager.h"

#define CAMLITE_VERSION		"0.1"

static pevent_base_t *g_base;

void signal_handler(int sig)
{
	camhttp_stop();
	LOGINFO("http cleanup\n");

	video_manager_cleanup();
	LOGINFO("video cleanup\n");
}

int main(int argc, char *argv[])
{	
	char *device, *username, *password;
	int width, height, fps, timeout, port;


	LOGINFO("camlite version:%s\n\n", CAMLITE_VERSION);

	if (argc != 9)
	{
		LOGINFO("usage: camlite DEVICE WIDTH HEIGHT FPS TIMEOUT PORT USERNAME PASSWORD\n\n");
		return 0;
	}

	device = argv[1];
	width = atoi(argv[2]);
	height = atoi(argv[3]);
	fps = atoi(argv[4]);
	timeout = atoi(argv[5]);
	port = atoi(argv[6]);
	username = argv[7];
	password = argv[8];

	LOGINFO("device:%s\n", device);
	LOGINFO("width:%d\n", width);
	LOGINFO("height:%d\n", height);
	LOGINFO("fps:%d\n", fps);
	LOGINFO("timeout:%d\n", timeout);
	LOGINFO("port:%d\n", port);
	LOGINFO("username:%s\n", username);
	LOGINFO("password:%s\n", password);
	LOGINFO("\n");

	signal(SIGPIPE, SIG_IGN);


	if(signal(SIGINT, signal_handler) == SIG_ERR)
	{
        LOGERROR("could not register signal handler\n");
        exit(EXIT_FAILURE);
    }

	g_base = pevent_base_create();
	if (g_base == NULL)
	{
		LOGERROR("pevent_base_create error\n");
		exit(EXIT_FAILURE);
	}

	if (camhttp_start(g_base, port, username, password) == -1)
	{
		LOGERROR("http start error\n");
		exit(EXIT_FAILURE);
	}

	video_manager_init(g_base, (v4l2_read_callback)camhttp_on_video_read, timeout);

	video_manager_add(device, width, height, fps);


	while (pevent_base_loop(g_base, -1) != -1);

	pevent_base_cleanup(g_base);
	LOGINFO("event loop cleanup\n");

	return 0;
}
