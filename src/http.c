#include "http.h"
#include "pevent.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h> 
#include <sys/socket.h>
#include <sys/errno.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include "util.h"
#include "uthash.h"

#define HTTP_MAX_STRING_SIZE	2048
#define HTTP_MAX_HEADER			10
#define HTTP_MAX_SEND_BUFFER	(200 * 1024)



typedef struct _io_buffer
{
	char *buf;
	int size;
	int max;
	int cur;
} io_buffer_t;

struct _http_server
{
	pevent_t *pevent;
	pevent_base_t *base;
	struct sockaddr_in addr_in;
	http_request_callback request_callback;

	http_client_t *clients;
};

typedef struct _http_client
{
	int fd;
	
	pevent_t *pevent;
	http_server_t *service;

	char ip[32];
	unsigned short port;

	void *delay_ptr;

	int reading;
	int writing;
	io_buffer_t write_buf;

	char request_buf[HTTP_MAX_STRING_SIZE + 1];
	int request_buf_cur;
	int request_header_compile;
	http_request_t request;

	UT_hash_handle hh;

} http_client_t;

typedef struct _http_response
{
	int code;
	int keeplive;
	char *headers[HTTP_MAX_HEADER];

	char body[HTTP_MAX_STRING_SIZE + 1];

	char *extra_buf;
	int extra_size;
} http_response_t;

void on_event(pevent_t *poll_event, int events, http_client_t *client);

const char *get_http_code_string(int code) {
	switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return "Internal Server Error";
    }
}

void http_client_clear_request(http_client_t *client)
{
	memset(&client->request, 0, sizeof(client->request));
	client->request.client = client;
}

http_client_t * http_client_new(http_server_t *service, int fd)
{
	http_client_t *client;
	pevent_t *pevent;

	client = fcalloc(1, sizeof(http_client_t));

	pevent = pevent_new(service->base, fd, (pevent_callback)on_event, client);
	if (pevent_set(pevent, PEVENT_READ) == -1)
	{
		free(client);
		pevent_free(pevent);
		return NULL;
	}
	client->fd = fd;
	client->pevent = pevent;
	client->service = service;

	http_client_clear_request(client);


	HASH_ADD_INT(service->clients, fd, client);

	return client;
}

void http_client_free(http_client_t *client)
{
	LOGDEBUG("disconnect(%s:%u) fd:%d\n", client->ip,
		client->port, client->fd);

	HASH_DEL(client->service->clients, client);

	pevent_free(client->pevent);

	if (client->write_buf.buf != NULL)
	{
		free(client->write_buf.buf);
		client->write_buf.buf = NULL;
	}
	
	free(client);
}

int http_client_send(http_client_t *client, char *buf, int size)
{
	int write_bytes;
	int need_size;
	int alloc_size;

	write_bytes = 0;
	if (!client->writing)
	{
		write_bytes = pevent_write(client->pevent, buf, size);
		if (write_bytes == -1)
		{
			LOGWARN("http send error fd:%d\n", client->fd);
			http_client_free(client);
			return -1;
		}
	}

	
	buf += write_bytes;
	size -= write_bytes;

	if (size > 0)
	{ 
		need_size = client->write_buf.size + size;

		if (need_size > HTTP_MAX_SEND_BUFFER)
		{
			LOGWARN("http write buf overflow fd:%d\n", client->fd);
			http_client_free(client);
			return -1;
		}

		alloc_size = ((need_size / 4096) + 1) * 4096;

		if (client->write_buf.buf == NULL)
		{
			client->write_buf.buf = fmalloc(alloc_size);
			if (client->write_buf.buf == NULL)
			{
				LOGERROR("not enough memory\n");
				exit(EXIT_FAILURE);
			}

			client->write_buf.max = alloc_size;
		}
		else if (client->write_buf.max < need_size)
		{
			char *new_buf = frealloc(client->write_buf.buf, alloc_size);
			client->write_buf.buf = new_buf;
			client->write_buf.max = alloc_size;
		}


		memcpy(client->write_buf.buf + client->write_buf.size,
			buf, size);

		client->write_buf.size += size;
		
		if (!client->writing)
		{
			client->writing = 1;

			if (pevent_set(client->pevent, PEVENT_WRITE) == -1)
			{
				LOGERROR("pevent_set(client->pevent, PEVENT_WRITE) == -1 fd:%d\n", client->fd);
				http_client_free(client);
				return -1;
			}
		}
	}

	return 0;
}

void http_client_set_delay(http_client_t *client, void *ptr)
{
	client->delay_ptr = ptr;
}

const char * http_client_getip(http_client_t *client)
{
	return client->ip;
}

unsigned short http_client_getport(http_client_t *client)
{
	return client->port;
}

int http_request_parse_line(http_client_t *client, char *line)
{
	if (sscanf(line, "GET %255[^?^ ]?%255sHTTP",
		client->request.path, client->request.param) > 0)
	{
		client->request.method = HTTP_METHOD_GET;
		return 0;
	}
	else if (sscanf(line, "POST %255[^?^ ]?%255sHTTP",
		client->request.path, client->request.param) > 0)
	{
		client->request.method = HTTP_METHOD_POST;
		return 0;
	}
	else if (sscanf(line, "Host: %255s", client->request.host) > 0)
	{
		return 0;
	}
	else if (strstr(line, "Authorization: Digest") == line)
	{
		strncpy(client->request.digest, line + sizeof("Authorization: Digest") - 1, 255);
		return 0;
	}

	return 0;
}

http_response_t * http_response_new(int code, const char *format, ...)
{
	http_response_t *response;
	va_list ap ;
	
	response = fcalloc(1, sizeof(http_response_t));
	response->code = code;

	if (format != NULL)
	{
		va_start(ap, format);
		vsnprintf(response->body, HTTP_MAX_STRING_SIZE, format, ap);
		va_end(ap);
	}

	return response;
}

void http_response_free(http_response_t *response)
{
	int i;

	for (i = 0; i < HTTP_MAX_HEADER && response->headers[i]; ++i)
	{
		if (response->headers[i] != NULL)
		{
			free(response->headers[i]);
		}
	}

	free(response);
}

int http_response_addheader(http_response_t *response, const char *format, ...)
{
	int i;
	va_list ap;
	int ret = -1;
	
	
	va_start(ap, format);

	for (i = 0; i < HTTP_MAX_HEADER; ++i)
	{
		if (response->headers[i] == NULL)
		{
			char *header = fmalloc(256);
			vsnprintf(header, 256, format, ap);

			response->headers[i] = header;
			ret = 0;
			break;
		}
	}

	va_end(ap);

	return ret;
}

void http_response_append(http_response_t *response, const char *format, ...)
{
	int len;
	va_list ap;


	va_start(ap, format);

	len = strlen(response->body);
	vsnprintf(response->body + len, HTTP_MAX_STRING_SIZE - len, format, ap);

	va_end(ap);
}

void http_response_set_data(http_response_t *response, char *buf, int size)
{
	response->extra_buf = buf;
	response->extra_size = size;
}

int http_response_compile(http_response_t *response, http_client_t *client)
{
	int i;
	int has_type = 0;
	int has_connection = 0;
	char *send_string;
	int ret;

	send_string = fmalloc(HTTP_MAX_STRING_SIZE + 1);

	if (response->code > 0)
	{
		snprintf(send_string, HTTP_MAX_STRING_SIZE, "HTTP/1.1 %d %s\r\n",
			response->code, get_http_code_string(response->code));
	}
	else
	{
		send_string[0] = '\0';
	}

	for (i = 0; i < HTTP_MAX_HEADER; ++i)
	{
		if (response->headers[i] != NULL)
		{
			if (strstr(response->headers[i], "Content-Type") > 0)
				has_type = 1;

			if (strstr(response->headers[i], "Connection:") > 0)
				has_connection = 1;

			snprintf(send_string, HTTP_MAX_STRING_SIZE,
				"%s%s\r\n", send_string, response->headers[i]);
		}
	}

	if (!has_type)
		snprintf(send_string, HTTP_MAX_STRING_SIZE, "%sContent-type: text/html\r\n", send_string);

	if (!has_connection)
	{
		snprintf(send_string, HTTP_MAX_STRING_SIZE, "%sConnection: close\r\n", send_string);
	}

	snprintf(send_string, HTTP_MAX_STRING_SIZE, "%s\r\n", send_string);

	if (response->code
		&& response->code != 200
		&& response->body[0] == '\0')
	{
		snprintf(send_string, HTTP_MAX_STRING_SIZE, "%s%d:%s",
			send_string,
			response->code,
			get_http_code_string(response->code));
	}
	else
	{
		snprintf(send_string, HTTP_MAX_STRING_SIZE, "%s%s", send_string, response->body);
	}



	ret = http_client_send(client, send_string, strlen(send_string));
	if (ret != -1 && response->extra_size)
	{
		ret = http_client_send(client, response->extra_buf, response->extra_size);
	}

	//LOGINFO("send_string:%s\n", send_string);

	free(send_string);
	http_response_free(response);

	return ret;
}

static int set_nonblocking(int fd)
{
	int opts;


	opts = fcntl(fd, F_GETFL);
	if (opts < 0) {
		LOGERROR("set_nonblocking fcntl failed\n");
		return -1;
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(fd, F_SETFL, opts) < 0) {
		LOGERROR("set_nonblocking fcntl failed\n");
		return -1;
	}
	return 0;
}

void on_read(http_client_t *client)
{
	int i, read_bytes, wrap_pos, ret;
	http_response_t *response;
	
	while (1)
	{
		if (client->request_buf_cur >= HTTP_MAX_STRING_SIZE)
		{
			LOGWARN("http request buffer overflow fd:%d\n", client->fd);
			http_client_free(client);
			return;
		}

		read_bytes = pevent_read(client->pevent,
			client->request_buf + client->request_buf_cur,
			HTTP_MAX_STRING_SIZE - client->request_buf_cur);

		if (read_bytes < 0)
		{
			LOGDEBUG("close(fd:%d)\n", client->fd);
			http_client_free(client);
			return;
		}

		if (read_bytes == 0)
			break; //read end

		client->request_buf_cur += read_bytes;
	}

	wrap_pos = 0;
	for (i = 1; i < client->request_buf_cur; ++i)
	{
		if (client->request_buf[i] == '\n' 
			&& client->request_buf[i - 1] == '\r')
		{
			client->request_buf[i - 1] = '\0';
			
			if (wrap_pos == i - 1)
			{
				client->request_buf_cur = 0;
				client->request_header_compile = 1;
				break;
			}


			ret = http_request_parse_line(client, client->request_buf + wrap_pos);

			if (ret == -1)
			{
				LOGWARN("http request parse line error fd:%d \n", client->fd);
				http_client_free(client);
				return;
			}

			wrap_pos = i + 1;
		}
	}

	if (!client->request_header_compile)
	{
		client->request_buf_cur -= wrap_pos;

		if (client->request_buf_cur)
			memcpy(client->request_buf,
			client->request_buf + wrap_pos,
			client->request_buf_cur);

		client->reading = 1;
		return;//continue read
	}
	
	//header compile
	if (client->request.method != HTTP_METHOD_GET)
	{
		http_client_free(client);
		return;
	}

	response = client->service->request_callback(&client->request);
	if (response != NULL)
	{
		if (http_response_compile(response, client) == -1)
		{
			return; //already free
		}
	}
	
	client->request_header_compile = 0;
	client->reading = 0;
	http_client_clear_request(client);

	if (!client->writing && !client->delay_ptr)
	{
		http_client_free(client);
		return;
	}
}

void on_write(http_client_t *client)
{
	int write_bytes;
	int write_size;

	if (!client->writing)
	{
		LOGERROR("!client->writing fd:%d\n", client->fd);
		http_client_free(client);
		return;
	}


	while (1)
	{
		write_size = client->write_buf.size - client->write_buf.cur;
		if (write_size <= 0)
		{
			client->write_buf.cur = 0;
			client->write_buf.size = 0;
			client->writing = 0;

			if (!client->delay_ptr)
			{
				http_client_free(client);
				return;
			}

			if (pevent_set(client->pevent, PEVENT_READ) == -1)
			{
				LOGERROR("pevent_set(client->pevent, PEVENT_WRITE) == -1 fd:%d\n", client->fd);
				http_client_free(client);
				return;
			}

			return;
		}

		//LOGDEBUG("client->write_buf.size:%d client->write_buf.cur:%d\n", client->write_buf.size, client->write_buf.cur);

		write_bytes = pevent_write(client->pevent,
			client->write_buf.buf + client->write_buf.cur, write_size);
		if (write_bytes == -1)
		{
			LOGERROR("send error fd:%d\n", client->fd);
			http_client_free(client);
			return;
		}
		else if (write_bytes == 0)
		{
			return; //EAGAIN
		}
		else
		{
			client->write_buf.cur += write_bytes;
		}
	}
}

void on_event(pevent_t *pevent, int event, http_client_t *client)
{
	if (client == NULL)
	{
		LOGERROR("client == NULL fd:%d\n", pevent_get_fd(pevent));
		pevent_free(pevent);
		return;
	}

	switch (event)
	{
	case PEVENT_ERROR:
		http_client_free(client);
		break;
	case PEVENT_READ:
		on_read(client);
		break;
	case PEVENT_WRITE:
		on_write(client);
		break;
	}
}

void on_accept(pevent_t *pevent, int event, http_server_t *service)
{
	struct sockaddr in_addr;
	socklen_t in_len;
	http_client_t *client;
	int fd;

	if (service == NULL || event != PEVENT_READ)
	{
		LOGERROR("service == NULL || event != PEVENT_READ fd:%d\n", pevent_get_fd(pevent));
		pevent_free(pevent);
		return;
	}

	in_len = sizeof(struct sockaddr);

	while (1) {
		fd = accept(pevent_get_fd(pevent), &in_addr, &in_len);
		if (fd == -1)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				break;
			}
			else
			{
				LOGERROR("accept error fd:%u\n", pevent_get_fd(pevent));
				break;
			}
		}

		if (set_nonblocking(fd) == -1)
		{
			LOGWARN("set_nonblocking(%s:%u) error\n",
				inet_ntoa(service->addr_in.sin_addr), service->addr_in.sin_port);
			close(fd);
			continue;
		}

		client = http_client_new(service, fd);
		if (client == NULL)
		{
			close(fd);
			continue;
		}

		strcpy(client->ip,
			inet_ntoa(((struct sockaddr_in *)&in_addr)->sin_addr));

		client->port = ((struct sockaddr_in *)&in_addr)->sin_port;

		LOGDEBUG("connect(%s:%u) fd:%d\n", client->ip, client->port, client->fd);
	}
}

http_server_t * http_server_create(pevent_base_t *base,
	const char *ip, unsigned short port,
	http_request_callback request_callback)
{
	http_server_t *service;
	

	service = fcalloc(1, sizeof(http_server_t));

	service->base = base;

	service->addr_in.sin_family = AF_INET;
	service->addr_in.sin_port = htons(port);
	service->addr_in.sin_addr.s_addr = inet_addr(ip);
	service->request_callback = request_callback;

	return service;
}

int http_server_start(http_server_t *service)
{
	int fd;
	int opt;
	pevent_t *pevent;

	fd = -1;
	opt = 1;
	pevent = NULL;

	if (service->pevent)
		return -1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
	{
		LOGERROR("create socket error");
		goto __error;
	}

	
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (bind(fd, (struct sockaddr *)&service->addr_in, sizeof(service->addr_in)) == -1)
	{
		LOGWARN("bind socket(%s:%u) error\n",
			inet_ntoa(service->addr_in.sin_addr), service->addr_in.sin_port);
		goto __error;
	}


	if (listen(fd, 32) == -1)
	{
		LOGWARN("bind socket(%s:%u) error\n",
			inet_ntoa(service->addr_in.sin_addr), service->addr_in.sin_port);
		goto __error;
	}

	if (set_nonblocking(fd) == -1)
	{
		LOGWARN("set_nonblocking(%s:%u) error\n",
			inet_ntoa(service->addr_in.sin_addr), service->addr_in.sin_port);
		goto __error;
	}

	pevent = pevent_new(service->base, fd, (pevent_callback)on_accept, service);

	if (pevent_set(pevent, PEVENT_READ) == -1)
	{
		LOGERROR("pevent_set(fd:%d) error\n", fd);
		goto __error;
	}

	service->pevent = pevent;

	return 0;

__error:
	if (fd > 0)
		close(fd);

	if (!pevent)
		pevent_free(pevent);

	return -1;
}

void http_server_stop(http_server_t *service)
{
	if (service->pevent)
	{
		pevent_free(service->pevent);
		service->pevent = NULL;
	}
}

void http_server_cleanup(http_server_t *service)
{
	free(service);
}

int http_server_keeplive_delay_iter(http_server_t *service,
	http_delay_callback callback, void *ptr)
{
	int count;
	http_client_t *client;
	http_client_t *tmp_client;
	http_response_t *response;
	
	count = 0;
	client = NULL;
	tmp_client = NULL;

	HASH_ITER(hh, service->clients, client, tmp_client)
	{
		if (client->delay_ptr && !client->writing && !client->reading)
		{
			response = callback(client, ptr, client->delay_ptr);
			if (response != NULL)
			{
				if (http_response_compile(response, client) == -1)
				{
					continue; //already free
				}

			}

			if (!client->writing && !client->delay_ptr)
			{
				http_client_free(client);
			}

			++count;
		}
	}

	return count;
}

