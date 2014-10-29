#ifndef HTTP_H_
#define HTTP_H_


#define HTTP_METHOD_GET		1
#define HTTP_METHOD_POST	2

typedef struct _pevent_base pevent_base_t;
typedef struct _http_server http_server_t;
typedef struct _http_client http_client_t;
typedef struct _http_response http_response_t;



typedef union _http_request_data
{
  void *ptr;
  int value;
  unsigned int u32;
  unsigned long long int u64;
} http_request_data_t;


typedef struct _http_request
{
	int method;
	char path[256];
	char param[256];
	char host[256];
	char digest[256];

	http_client_t *client;
} http_request_t;

typedef http_response_t * (*http_request_callback)(http_request_t *);

typedef http_response_t * (*http_delay_callback)(http_client_t *, void *ptr, void *delay_ptr);

void http_client_set_delay(http_client_t *client, void *ptr);

const char * http_client_getip(http_client_t *client);

unsigned short http_client_getport(http_client_t *client);

http_response_t * http_response_new(int code, const char *format, ...);

int http_response_addheader(http_response_t *response, const char *format, ...);

void http_response_append(http_response_t *response, const char *string, ...);

void http_response_set_data(http_response_t *response, char *buf, int size);

http_server_t * http_server_create(pevent_base_t *base,
	const char *ip, unsigned short port,
	http_request_callback request_callback);

int http_server_start(http_server_t *service);

void http_server_stop(http_server_t *service);

void http_server_cleanup(http_server_t *service);

int http_server_keeplive_delay_iter(http_server_t *service,
	http_delay_callback callback, void *ptr);

#endif