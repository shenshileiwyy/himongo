#ifndef __HIMONGO_ASYNC_H
#define __HIMONGO_ASYNC_H
#include "himongo.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mongoAsyncContext; /* need forward declaration of mongoAsyncContext */
struct dict; /* dictionary header is included in async.c */

/* Reply callback prototype and container */
typedef void (mongoCallbackFn)(struct mongoAsyncContext*, void*, void*);
typedef struct mongoCallback {
    struct mongoCallback *next; /* simple singly linked list */
    mongoCallbackFn *fn;
    void *privdata;
} mongoCallback;

/* List of callbacks for either regular replies or pub/sub */
typedef struct mongoCallbackList {
    mongoCallback *head, *tail;
} mongoCallbackList;

/* Connection callback prototypes */
typedef void (mongoDisconnectCallback)(const struct mongoAsyncContext*, int status);
typedef void (mongoConnectCallback)(const struct mongoAsyncContext*, int status);

/* Context for an async connection to Mongo */
typedef struct mongoAsyncContext {
    /* Hold the regular context, so it can be realloc'ed. */
    mongoContext c;

    /* Setup error flags so they can be used directly. */
    int err;
    char *errstr;

    /* Not used by himongo */
    void *data;

    /* Event library data and hooks */
    struct {
        void *data;

        /* Hooks that are called when the library expects to start
         * reading/writing. These functions should be idempotent. */
        void (*addRead)(void *privdata);
        void (*delRead)(void *privdata);
        void (*addWrite)(void *privdata);
        void (*delWrite)(void *privdata);
        void (*cleanup)(void *privdata);
    } ev;

    /* Called when either the connection is terminated due to an error or per
     * user request. The status is set accordingly (MONGO_OK, MONGO_ERR). */
    mongoDisconnectCallback *onDisconnect;

    /* Called when the first write event was received. */
    mongoConnectCallback *onConnect;

    /* Regular command callbacks */
    mongoCallbackList replies;

    /* Subscription callbacks */
    struct {
        mongoCallbackList invalid;
        struct dict *channels;
        struct dict *patterns;
    } sub;
} mongoAsyncContext;

/* Functions that proxy to himongo */
mongoAsyncContext *mongoAsyncConnect(const char *ip, int port);
mongoAsyncContext *mongoAsyncConnectBind(const char *ip, int port, const char *source_addr);
mongoAsyncContext *mongoAsyncConnectBindWithReuse(const char *ip, int port,
                                                  const char *source_addr);
mongoAsyncContext *mongoAsyncConnectUnix(const char *path);
int mongoAsyncSetConnectCallback(mongoAsyncContext *ac, mongoConnectCallback *fn);
int mongoAsyncSetDisconnectCallback(mongoAsyncContext *ac, mongoDisconnectCallback *fn);
void mongoAsyncDisconnect(mongoAsyncContext *ac);
void mongoAsyncFree(mongoAsyncContext *ac);

/* Handle read/write events */
void mongoAsyncHandleRead(mongoAsyncContext *ac);
void mongoAsyncHandleWrite(mongoAsyncContext *ac);

/* Command functions for an async context. Write the command to the
 * output buffer and register the provided callback. */
int mongovAsyncCommand(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata, const char *format, va_list ap);
int mongoAsyncCommand(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata, const char *format, ...);
int mongoAsyncCommandArgv(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata, int argc, const char **argv, const size_t *argvlen);
int mongoAsyncFormattedCommand(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata, const char *cmd, size_t len);

#ifdef __cplusplus
}
#endif

#endif
