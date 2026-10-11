/* Linux integration harness for the actual AIO backend, not the PS5 executable. */
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
/* Emulate a small console default stack. Explicit worker attributes must
   override it; Linux's usual multi-megabyte default hid the original bug. */
static int host_thread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void*),void *arg) {
    if(attr)return pthread_create(thread,attr,fn,arg);
    pthread_attr_t small;int rc=pthread_attr_init(&small);if(rc)return rc;
    rc=pthread_attr_setstacksize(&small,65536);
    if(!rc)rc=pthread_create(thread,&small,fn,arg);
    pthread_attr_destroy(&small);return rc;
}
#define pthread_create host_thread_create
#include "../src/aio.c"
#undef pthread_create
void pldmgr_log(const char *fmt,...) {va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);}
static enum MHD_Result handler(void *cls,struct MHD_Connection *c,const char *u,const char *m,const char *v,const char *body,size_t *n,void **state) {
    (void)cls;(void)v;(void)body;
    if(!*state){*state=(void*)1;return MHD_YES;}if(*n){*n=0;return MHD_YES;}return aio_request(c,u,m);
}
int main(void) {
    struct MHD_Daemon *d=MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD|MHD_USE_THREAD_PER_CONNECTION,18084,0,0,handler,0,MHD_OPTION_END);
    if(!d)return 1;puts("ready");fflush(stdout);getchar();aio_shutdown();MHD_stop_daemon(d);return 0;
}
