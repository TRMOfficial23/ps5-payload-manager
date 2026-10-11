#ifndef TRM_AIO_H
#define TRM_AIO_H
#include <microhttpd.h>
enum MHD_Result aio_request(struct MHD_Connection *, const char *, const char *);
void aio_shutdown(void);
#endif
