#include "camhttp.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include "util.h"
#include "video_manager.h"
#include "v4l2port.h"
#include "md5.h"
#include "uthash.h"

#define REQUEST_TYPE_SNAPSHORT	 1
#define REQUEST_TYPE_STREAM		 2



#define MD5_UPDATE_STRING(c, s) md5_update(c, (const unsigned char *)s, strlen(s))

#define HTTP_DIGEST_REALM			"camlite"
#define HTTP_DIGEST_NONCE			"camlite"
#define HTTP_DIGEST_MAX				1000
#define HTTP_DIGEST_EXPIRETIME		3600

struct comond_patten
{
	char path[32];
	http_response_t * (*command_callback)(http_request_t *);
};

typedef struct _video_read_data
{
	char *buf;
	int size;
	struct timeval *timestamp;
	unsigned int index;
} video_read_data_t;

typedef struct _http_parameter
{
	char *key;
	char *value;
} http_parameter_t;

typedef struct _http_digest
{
	char nonce[33];
	time_t check_time;
	unsigned int ip;
	int count;

	UT_hash_handle hh;
} http_digest_t;



static http_server_t *g_service;

static char g_digest_ha1[64];

static http_digest_t *g_digest_list;

static char HEX_DICT[0x10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

static void hex2string(unsigned char *hex, int hex_len, char *buff, int buf_len)
{
	int i;
	unsigned char *pc;

	i = 0;
    pc = hex + hex_len;
    while(i < buf_len - 1 && hex < pc)
	{
        unsigned char c1 = (*hex) >> 4;
        unsigned char c2 = (*hex) & 0x0f;
        buff[i++] = HEX_DICT[c1];
        buff[i++] = HEX_DICT[c2];
        hex++;
    }

    buff[i] = 0;
}

int get_http_parameter(char *src, http_parameter_t param[], int count)
{
	int len, nel, i;
	char *q, *name, *value, *temp;

	q = src;
	len = strlen(src);
	nel = 1;


	while (strsep(&q, ","))
		nel++;


	for (q = src; q < (src + len);)
	{
		value = name = q;
		for (q += strlen(q); q < (src + len) && !*q; q++);

		name = strsep(&value, "=");
		if (name == NULL || value == NULL)
			break;

		//trim name
		while (*name != '\0' && (*name == '\t' || *name == ' '))
			++name;

		temp = name;
		while (*temp != '\0')
			++temp;

		while (temp > name)
		{
			if (*temp == '\0' || *temp == '\t' || *temp == ' ')
				*temp = '\0';
			else
				break;

			--temp;
		}

		//trim value
		while (*value != '\0' && (*value == '\t' || *value == ' ' || *value == '"'))
		{
			if (*value == '"')
			{
				++value;
				break;
			}

			++value;
		}

		temp = value;
		while (*temp != '\0')
			++temp;

		while (temp >= value)
		{
			if (*temp == '\0' || *temp == '\t' || *temp == ' ')
				*temp = '\0';
			else if (*temp == '"')
			{
				*temp = '\0';
				break;
			}
			else
			{
				break;
			}

			--temp;
		}
		
		for (i = 0; i < count; ++i)
		{
			if (strcmp(name, param[i].key) == 0)
			{
				param[i].value = value;
			}
		}
	}

	for (i = 0; i < count; ++i)
	{
		if (!param[i].value)
			return 0;
	}

	return 1;
}

void check_digest_expire()
{
	http_digest_t *dt, *temp_dt;
	unsigned int timenow;

	timenow = time(NULL);

	HASH_ITER(hh, g_digest_list, dt, temp_dt)
	{
		if (dt->check_time + HTTP_DIGEST_EXPIRETIME < timenow)
		{
			LOGDEBUG("digest expire('%s')\n", dt->nonce);
			HASH_DEL(g_digest_list, dt);

			free(dt);
		}
	}
}

http_digest_t * new_digest(http_request_t *request)
{
	md5ctx ctx;
	unsigned char digest[16];
	http_digest_t *dt, dt_temp;
	static unsigned int digest_count = 0;
	

	if (HASH_COUNT(g_digest_list) >= HTTP_DIGEST_MAX)
	{
		LOGERROR("http digest data count >= HTTP_DIGEST_MAX\n");
		return NULL;
	}

	dt_temp.check_time = time(NULL);
	dt_temp.ip = inet_addr(http_client_getip(request->client));
	dt_temp.count = 1;

	md5_init(&ctx);
	md5_update(&ctx, (uint8_t *)&digest_count, sizeof(digest_count));
	md5_update(&ctx, (uint8_t *)&dt_temp.check_time, sizeof(dt_temp.check_time));
	md5_update(&ctx, (uint8_t *)&dt_temp.ip, sizeof(dt_temp.ip));
	md5_final(digest, &ctx);
	hex2string(digest, 16, dt_temp.nonce, 33);

	dt = NULL;
	HASH_FIND(hh, g_digest_list, dt_temp.nonce, 32, dt);
	if (dt != NULL)
	{
		LOGERROR("http digest data exist:%s\n", dt_temp.nonce);
		return NULL;
	}

	dt = fmalloc(sizeof(http_digest_t));
	memcpy(dt, &dt_temp, sizeof(http_digest_t));
	
	HASH_ADD(hh, g_digest_list, nonce, 32, dt);

	++digest_count;
	return dt;
}

http_response_t * on_check_digest(http_request_t *request)
{
	//HA1=MD5(A1)=MD5(username:realm:password)
	//HA2=MD5(A2)=MD5(method:digestURI)
	//response=MD5(HA1:nonce:nonceCount:clientNonce:qop:HA2)
	char temp[64];
	md5ctx ctx;
	unsigned char digest[16];
	http_parameter_t parameter[4];
	http_response_t *response;
	http_digest_t *dt;

	check_digest_expire();

	if (!g_digest_ha1[0])
		return NULL;

	if (request->digest[0] == '\0')
	{
		goto __proc_failure;
	}


	memset(parameter, 0, sizeof(parameter));
	parameter[0].key = "nonce";
	parameter[1].key = "nc";
	parameter[2].key = "cnonce";
	parameter[3].key = "response";

	if (!get_http_parameter(request->digest, parameter, sizeof(parameter) / sizeof(http_parameter_t)))
	{
		LOGWARN("get_http_parameter failed\n");
		goto __proc_failure;
	}


	//ha2
	md5_init(&ctx);
	MD5_UPDATE_STRING(&ctx, "GET:");
	MD5_UPDATE_STRING(&ctx, request->path);

	if (request->param[0])
	{
		MD5_UPDATE_STRING(&ctx, "?");
		MD5_UPDATE_STRING(&ctx, request->param);
	}

	md5_final(digest, &ctx);
	hex2string(digest, 16, temp, 64);


	//response
	md5_init(&ctx);
	MD5_UPDATE_STRING(&ctx, g_digest_ha1);
	MD5_UPDATE_STRING(&ctx, ":");
	MD5_UPDATE_STRING(&ctx, parameter[0].value);
	MD5_UPDATE_STRING(&ctx, ":");
	MD5_UPDATE_STRING(&ctx, parameter[1].value);
	MD5_UPDATE_STRING(&ctx, ":");
	MD5_UPDATE_STRING(&ctx, parameter[2].value);
	MD5_UPDATE_STRING(&ctx, ":");
	MD5_UPDATE_STRING(&ctx, "auth");
	MD5_UPDATE_STRING(&ctx, ":");
	MD5_UPDATE_STRING(&ctx, temp);
	md5_final(digest, &ctx);
	hex2string(digest, 16, temp, 64);

	LOGDEBUG("response:%s %s\n", temp, parameter[3].value);

	if (memcmp(temp, parameter[3].value, 16) == 0)
	{
		if (strcmp(parameter[0].value, HTTP_DIGEST_NONCE) == 0)
		{
			dt = new_digest(request);
			if (dt == NULL)
				return http_response_new(500, NULL, "can not create digest data");

			response = http_response_new(401, NULL);
			http_response_addheader(response, "WWW-Authenticate: Digest "\
				"realm=\"" HTTP_DIGEST_REALM "\","
				"qop=\"auth\","
				"nonce=\"%s\","
				"stale=TRUE",
				dt->nonce);

			LOGINFO("login success(%s:%u new digest:%s)\n",
				http_client_getip(request->client),
				http_client_getport(request->client),
				dt->nonce);

			return response;
		}
		else
		{
			if (strlen(parameter[0].value) != 32)
				goto __proc_failure;

			HASH_FIND(hh, g_digest_list, parameter[0].value, 32, dt);
			if (dt == NULL)
				goto __proc_failure;

			if (dt->count != strtol(parameter[1].value, NULL, 16))
				goto __proc_failure;

			++dt->count;
			dt->check_time = time(NULL);
			return NULL;
		}
	}


__proc_failure:
	response = http_response_new(401, NULL);
	http_response_addheader(response, "WWW-Authenticate: Digest "\
		"realm=\"" HTTP_DIGEST_REALM "\","
		"qop=\"auth\","
		"nonce=\"" HTTP_DIGEST_NONCE "\"");

	return response;
}

http_response_t * camhttp_on_send_jpeg(http_client_t *client, video_read_data_t *data, int flag)
{
	http_response_t *response;


	if (data == NULL || data->index != flag << 16 >> 16)
		return NULL;

	
	if (flag >> 16 == REQUEST_TYPE_SNAPSHORT)
	{
		response = http_response_new(200, NULL);

		http_response_addheader(response, "Pragma: no-cache");
		http_response_addheader(response,
			"Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
		http_response_addheader(response, "Content-Type: image/jpeg");

		http_response_set_data(response, data->buf, data->size);

		http_client_set_delay(client, NULL);

		return response;
	}
	else if (flag >> 16 == REQUEST_TYPE_STREAM)
	{
		response = http_response_new(0, NULL);

		http_response_addheader(response, "--[data-boundary-data]");
		http_response_addheader(response, "Content-Type: image/jpeg");
		http_response_addheader(response, "Content-Length: %d", data->size);
		http_response_addheader(response, "X-Timestamp  /: %d.%06d",
			(int)data->timestamp->tv_sec,
			(int)data->timestamp->tv_usec);

		http_response_set_data(response, data->buf, data->size);

		return response;
	}

	http_client_set_delay(client, NULL);
	return NULL;
}

void camhttp_on_video_read(const char *buf, int size, struct timeval *timestamp, v4l2port_t *video)
{
	video_read_data_t data;


	data.buf = (char *)buf;
	data.size = size;
	data.timestamp = timestamp;
	data.index = video->profile.value;

	if (http_server_keeplive_delay_iter(g_service, (http_delay_callback)camhttp_on_send_jpeg, &data) > 0)
	{
		video_manager_set_check_time(video);
	}
}

http_response_t * on_get_root(http_request_t *request)
{
	return http_response_new(200, "<html><body>"\
		"<a href='/snapshot'>snapshot</a><br>"\
		"<a href='/stream'>stream</a><br>"\
		"<a href='/status'>status</a><br>"\
		"</body></html>");
}

http_response_t * on_get_snapshot(http_request_t *request)
{
	int n;
	v4l2port_t *video;
	

	n = atoi(request->param);

	video = video_manager_get(n);
	if (video == NULL)
	{
		return http_response_new(200, "<html><body>can not find device:%d</body></html>", n);
	}

	if (video_manager_stream_start(n) == -1)
	{
		return http_response_new(200, "<html><body>start stream failure:%d</body></html>", n);
	}

	http_client_set_delay(request->client, (void *)(REQUEST_TYPE_SNAPSHORT << 16 | n));

	return NULL;
}

http_response_t * on_get_stream(http_request_t *request)
{
	int n;
	http_response_t *response;


	if (strlen(request->param) == 0)
	{
		return http_response_new(200, "<html><body><img src='/stream?0'/></body></html>");
	}
	else
	{
		n = atoi(request->param);

		if (video_manager_get(n) == NULL)
		{
			return http_response_new(200, "<html><body>can not find device:%d</body></html>", n);
		}

		if (video_manager_stream_start(n) == -1)
		{
			return http_response_new(200, "<html><body>start stream failure:%d</body></html>", n);
		}

		response = http_response_new(200, NULL);
		http_response_addheader(response, "Pragma: no-cache");
		http_response_addheader(response,
			"Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");

		http_response_addheader(response,
			"Content-Type: multipart/x-mixed-replace;boundary=[data-boundary-data]");

		http_client_set_delay(request->client, (void *)(REQUEST_TYPE_STREAM << 16 | n));

		return response;
	}
}

http_response_t * on_get_status(http_request_t *request)
{
	int n;
	v4l2port_t *video;
	
	n = atoi(request->param);

	video = video_manager_get(n);
	if (video == NULL)
	{
		return http_response_new(200, "<html><body>can not find device:%d</body></html>", n);
	}

	return http_response_new(200, "<html><body>"\
		"<p>init:<a href='/control?n=%d&c=reinit'>%s</a></p>"\
		"<p>stream:%s</p>"\
		"<p>card:%s</p>"\
		"<p>path:%s</p>"\
		"<p>width:%u</p>"\
		"<p>height:%u</p>"\
		"<p>timeperframe:%d/%d</p>"\
		"<p>support:%s&nbsp;%s&nbsp;%s</p>"\
		"<p>timeout:%d</p>"\
		"<p>error:%s</p>"\
		"<p>errno:%s</p>"\
		"<p>cache:%u</p>"\
		"<p>memory:%uk</p>"\
		"<p>jiffies:%u</p>"\
		"<p>digest:%u</p>"\
		"<a href='/'>back</a><br/></body></html>",
		n, video->init_flag ? "true" : "false",
		video->stream_flag ? "on" : "off",
		video->cap.card,
		video->profile.device,
		video->profile.width,
		video->profile.height,
		video->streamparm.parm.capture.timeperframe.numerator,
		video->streamparm.parm.capture.timeperframe.denominator,
		video->fmtdesc[0].description,
		video->fmtdesc[1].description,
		video->fmtdesc[2].description,
		video_manager_get_timeout(),
		v4l2port_strerror(video),
		v4l2port_strerrno(video),
		REQ_BUFFER_MAX,
		read_memory_status(),
		read_cpu_jiffies(),
		HASH_COUNT(g_digest_list));
}

http_response_t * on_get_control(http_request_t *request)
{
	int n;
	char control[25];
	v4l2port_t *video;
	

	if (sscanf(request->param, "n=%d&c=%24s", &n, control) != 2)
	{
		return http_response_new(200, "<html><body>param erro(%s)</body></html>", request->param);
	}

	video = video_manager_get(n);
	if (video == NULL)
	{
		return http_response_new(200, "<html><body>can not find device:%d</body></html>", n);
	}

	if (strcmp(control, "reinit") == 0)
	{
		if (video_manager_init_video(n) == -1)
		{
			return http_response_new(200, "<html><body>reinit failure(%d)<br/><a href='/'>back</a></body></html>", n);
		}
		else
		{
			return http_response_new(200, "<html><body>reinit success(%d)<br/><a href='/'>back</a></body></html>", n);
		}
	}
	else
	{
		return http_response_new(200, "<html><body>unknow control:%s</body></html>", control);
	}

	return NULL;
}

http_response_t * on_reuqest(http_request_t *request)
{
	int i;
	http_response_t *response;

	static struct comond_patten comond_list[] = {
			{ "/", on_get_root },
			{ "/snapshot", on_get_snapshot },
			{ "/stream", on_get_stream },
			{ "/status", on_get_status },
			{ "/control", on_get_control },
	};

	LOGDEBUG("http request:%s%s%s(%s:%u)\n",
		request->path,
		strlen(request->param) > 0 ? "?" : "",
		request->param,
		http_client_getip(request->client),
		http_client_getport(request->client));


	response = on_check_digest(request);
	if (response != NULL)
		return response;

	for (i = 0; i < sizeof(comond_list) / sizeof(struct comond_patten); ++i)
	{
		if (strcmp(request->path, comond_list[i].path) == 0)
		{
			return comond_list[i].command_callback(request);
		}
	}

	return http_response_new(404, NULL);
}

int camhttp_start(pevent_base_t *base,
	unsigned short port,
	const char *username, const char *password)
{
	md5ctx ctx;
	unsigned char digest[16];

	g_service = http_server_create(base, "0.0.0.0", port, on_reuqest);
	if (!g_service)
		return -1;

	if (http_server_start(g_service) == -1)
		return -1;

	

	if (username && password)
	{
		md5_init(&ctx);
		MD5_UPDATE_STRING(&ctx, username);
		MD5_UPDATE_STRING(&ctx, ":" HTTP_DIGEST_REALM ":");
		MD5_UPDATE_STRING(&ctx, password);
		md5_final(digest, &ctx);
		hex2string(digest, 16, g_digest_ha1, 64);
	}
	
	LOGINFO("http server start(%s:%u)\n", "0.0.0.0", port);

	return 0;
}

void camhttp_stop()
{
	http_server_stop(g_service);
	http_server_cleanup(g_service);
}

void camhttp_stop();
