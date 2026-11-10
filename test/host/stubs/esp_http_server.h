#pragma once
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct {
    int content_len;
    const unsigned char *body;
    const char *id;
    unsigned char response[64];
    int response_len;
    char status[64];
} httpd_req_t;
typedef struct {const char *uri; int method; esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_400_BAD_REQUEST 400
#define ESP_FAIL -1
static inline size_t strlcpy(char *out,const char *in,size_t size) { size_t len=strlen(in);if(size){size_t copy=len<size-1?len:size-1;memcpy(out,in,copy);out[copy]=0;}return len; }
static inline int httpd_req_get_hdr_value_len(httpd_req_t *r,const char *key) { (void)key;return r->id?strlen(r->id):0; }
static inline int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size) { (void)key;strlcpy(out,r->id,size);return 0; }
static inline int httpd_resp_send(httpd_req_t *r,const char *bytes,int size) {r->response_len=size;if(size>0)memcpy(r->response,bytes,size);return 0;}
static inline int httpd_resp_sendstr(httpd_req_t *r,const char *text) { (void)r;(void)text;return 0; }
static inline int httpd_resp_send_err(httpd_req_t *r,int status,const char *text) {(void)text;snprintf(r->status,sizeof(r->status),"%d",status);return 0;}
static inline int httpd_resp_set_status(httpd_req_t *r,const char *status) {strlcpy(r->status,status,sizeof(r->status));return 0;}
static inline int httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *v) {(void)r;(void)key;(void)v;return 0;}
static inline int httpd_resp_set_type(httpd_req_t *r,const char *v) {(void)r;(void)v;return 0;}
static inline int httpd_req_recv(httpd_req_t *r,char *out,int size) {memcpy(out,r->body,size);r->body+=size;return size;}
static inline int httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *route) {(void)s;(void)route;return 0;}
