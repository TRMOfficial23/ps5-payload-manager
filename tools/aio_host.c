/* Linux integration harness for the actual AIO backend, not the PS5 executable. */
#include "../src/aio.c"
static enum MHD_Result handler(void *cls,struct MHD_Connection *c,const char *u,const char *m,const char *v,const char *body,size_t *n,void **state) {
    (void)cls;(void)v;(void)body;
    if(!*state){*state=(void*)1;return MHD_YES;}if(*n){*n=0;return MHD_YES;}return aio_request(c,u,m);
}
int main(void) {
    struct MHD_Daemon *d=MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD|MHD_USE_THREAD_PER_CONNECTION,18084,0,0,handler,0,MHD_OPTION_END);
    if(!d)return 1;puts("ready");fflush(stdout);getchar();aio_shutdown();MHD_stop_daemon(d);return 0;
}
