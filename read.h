#ifndef __HIMONGO_READ_H
#define __HIMONGO_READ_H
#include <stdio.h> /* for size_t */

#define MONGO_ERR -1
#define MONGO_OK 0

/* When an error occurs, the err flag in a context is set to hold the type of
 * error that occurred. MONGO_ERR_IO means there was an I/O error and you
 * should use the "errno" variable to find out what is wrong.
 * For other values, the "errstr" field will hold a description. */
#define MONGO_ERR_IO 1 /* Error in read or write */
#define MONGO_ERR_EOF 3 /* End of file */
#define MONGO_ERR_PROTOCOL 4 /* Protocol error */
#define MONGO_ERR_OOM 5 /* Out of memory */
#define MONGO_ERR_OTHER 2 /* Everything else... */

#define MONGO_READER_MAX_BUF (1024*16)  /* Default max unused reader buffer. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mongoReplyObjectFunctions {
    void *(*createReply)(char*, size_t);
    void (*freeObject)(void*);
} mongoReplyObjectFunctions;

typedef struct mongoReader {
    int err; /* Error flags, 0 when there is no error */
    char errstr[128]; /* String representation of error when applicable */

    char *buf; /* Read buffer */
    size_t pos; /* Buffer cursor */
    size_t len; /* Buffer length */
    size_t maxbuf; /* Max length of unused buffer */
    size_t pktlen; /* length of current packet. */
    void *reply; /* Temporary reply pointer */

    mongoReplyObjectFunctions *fn;
    void *privdata;
} mongoReader;

/* Public API for the protocol parser. */

mongoReader *mongoReaderCreate(void);
mongoReader *mongoReaderCreateWithFunctions(mongoReplyObjectFunctions *fn);

void mongoReaderFree(mongoReader *r);
int mongoReaderFeed(mongoReader *r, const char *buf, size_t len);
int mongoReaderGetReply(mongoReader *r, void **reply);

#define mongoReaderSetPrivdata(_r, _p) (int)(((mongoReader*)(_r))->privdata = (_p))
#define mongoReaderGetObject(_r) (((mongoReader*)(_r))->reply)
#define mongoReaderGetError(_r) (((mongoReader*)(_r))->errstr)

#ifdef __cplusplus
}
#endif

#endif
