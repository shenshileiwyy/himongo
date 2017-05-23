#include "fmacros.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include "async.h"
#include "net.h"
// #include "dict.c"
#include "sds.h"
#include "proto.h"
#include "utils.h"

#define _EL_ADD_READ(ctx) do { \
        if ((ctx)->ev.addRead) (ctx)->ev.addRead((ctx)->ev.data); \
    } while(0)
#define _EL_DEL_READ(ctx) do { \
        if ((ctx)->ev.delRead) (ctx)->ev.delRead((ctx)->ev.data); \
    } while(0)
#define _EL_ADD_WRITE(ctx) do { \
        if ((ctx)->ev.addWrite) (ctx)->ev.addWrite((ctx)->ev.data); \
    } while(0)
#define _EL_DEL_WRITE(ctx) do { \
        if ((ctx)->ev.delWrite) (ctx)->ev.delWrite((ctx)->ev.data); \
    } while(0)
#define _EL_CLEANUP(ctx) do { \
        if ((ctx)->ev.cleanup) (ctx)->ev.cleanup((ctx)->ev.data); \
    } while(0);

/* Forward declaration of function in himongo.c */
int __mongoAppendCommand(mongoContext *c, int32_t opCode, const char *cmd, size_t len);

static mongoAsyncContext *mongoAsyncInitialize(mongoContext *c) {
    mongoAsyncContext *ac;

    ac = realloc(c,sizeof(mongoAsyncContext));
    if (ac == NULL)
        return NULL;

    c = &(ac->c);

    /* The regular connect functions will always set the flag MONGO_CONNECTED.
     * For the async API, we want to wait until the first write event is
     * received up before setting this flag, so reset it here. */
    c->flags &= ~MONGO_CONNECTED;

    ac->err = 0;
    ac->errstr = NULL;
    ac->data = NULL;

    ac->ev.data = NULL;
    ac->ev.addRead = NULL;
    ac->ev.delRead = NULL;
    ac->ev.addWrite = NULL;
    ac->ev.delWrite = NULL;
    ac->ev.cleanup = NULL;

    ac->onConnect = NULL;
    ac->onDisconnect = NULL;

    ac->replies.head = NULL;
    ac->replies.tail = NULL;
    return ac;
}

/* We want the error field to be accessible directly instead of requiring
 * an indirection to the mongoContext struct. */
static void __mongoAsyncCopyError(mongoAsyncContext *ac) {
    if (!ac)
        return;

    mongoContext *c = &(ac->c);
    ac->err = c->err;
    ac->errstr = c->errstr;
}

mongoAsyncContext *mongoAsyncConnect(const char *ip, int port) {
    mongoContext *c;
    mongoAsyncContext *ac;

    c = mongoConnectNonBlock(ip,port);
    if (c == NULL)
        return NULL;

    ac = mongoAsyncInitialize(c);
    if (ac == NULL) {
        mongoFree(c);
        return NULL;
    }

    __mongoAsyncCopyError(ac);
    return ac;
}

mongoAsyncContext *mongoAsyncConnectBind(const char *ip, int port,
                                         const char *source_addr) {
    mongoContext *c = mongoConnectBindNonBlock(ip,port,source_addr);
    mongoAsyncContext *ac = mongoAsyncInitialize(c);
    __mongoAsyncCopyError(ac);
    return ac;
}

mongoAsyncContext *mongoAsyncConnectBindWithReuse(const char *ip, int port,
                                                  const char *source_addr) {
    mongoContext *c = mongoConnectBindNonBlockWithReuse(ip,port,source_addr);
    mongoAsyncContext *ac = mongoAsyncInitialize(c);
    __mongoAsyncCopyError(ac);
    return ac;
}

mongoAsyncContext *mongoAsyncConnectUnix(const char *path) {
    mongoContext *c;
    mongoAsyncContext *ac;

    c = mongoConnectUnixNonBlock(path);
    if (c == NULL)
        return NULL;

    ac = mongoAsyncInitialize(c);
    if (ac == NULL) {
        mongoFree(c);
        return NULL;
    }

    __mongoAsyncCopyError(ac);
    return ac;
}

int mongoAsyncSetConnectCallback(mongoAsyncContext *ac, mongoConnectCallback *fn) {
    if (ac->onConnect == NULL) {
        ac->onConnect = fn;

        /* The common way to detect an established connection is to wait for
         * the first write event to be fired. This assumes the related event
         * library functions are already set. */
        _EL_ADD_WRITE(ac);
        return MONGO_OK;
    }
    return MONGO_ERR;
}

int mongoAsyncSetDisconnectCallback(mongoAsyncContext *ac, mongoDisconnectCallback *fn) {
    if (ac->onDisconnect == NULL) {
        ac->onDisconnect = fn;
        return MONGO_OK;
    }
    return MONGO_ERR;
}

/* Helper functions to push/shift callbacks */
static int __mongoPushCallback(mongoCallbackList *list, mongoCallback *source) {
    mongoCallback *cb;

    /* Copy callback from stack to heap */
    cb = malloc(sizeof(*cb));
    if (cb == NULL)
        return MONGO_ERR_OOM;

    if (source != NULL) {
        memcpy(cb,source,sizeof(*cb));
        cb->next = NULL;
    }

    /* Store callback in list */
    if (list->head == NULL)
        list->head = cb;
    if (list->tail != NULL)
        list->tail->next = cb;
    list->tail = cb;
    return MONGO_OK;
}

static int __mongoShiftCallback(mongoCallbackList *list, mongoCallback *target) {
    mongoCallback *cb = list->head;
    if (cb != NULL) {
        list->head = cb->next;
        if (cb == list->tail)
            list->tail = NULL;

        /* Copy callback from heap to stack */
        if (target != NULL)
            memcpy(target,cb,sizeof(*cb));
        free(cb);
        return MONGO_OK;
    }
    return MONGO_ERR;
}

static void __mongoRunCallback(mongoAsyncContext *ac, mongoCallback *cb, void *reply) {
    mongoContext *c = &(ac->c);
    if (cb->fn != NULL) {
        c->flags |= MONGO_IN_CALLBACK;
        cb->fn(ac,reply,cb->privdata);
        c->flags &= ~MONGO_IN_CALLBACK;
    }
}

/* Helper function to free the context. */
static void __mongoAsyncFree(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    mongoCallback cb;

    /* Execute pending callbacks with NULL reply. */
    while (__mongoShiftCallback(&ac->replies,&cb) == MONGO_OK)
        __mongoRunCallback(ac,&cb,NULL);

    /* Signal event lib to clean up */
    _EL_CLEANUP(ac);

    /* Execute disconnect callback. When mongoAsyncFree() initiated destroying
     * this context, the status will always be MONGO_OK. */
    if (ac->onDisconnect && (c->flags & MONGO_CONNECTED)) {
        if (c->flags & MONGO_FREEING) {
            ac->onDisconnect(ac,MONGO_OK);
        } else {
            ac->onDisconnect(ac,(ac->err == 0) ? MONGO_OK : MONGO_ERR);
        }
    }

    /* Cleanup self */
    mongoFree(c);
}

/* Free the async context. When this function is called from a callback,
 * control needs to be returned to mongoProcessCallbacks() before actual
 * free'ing. To do so, a flag is set on the context which is picked up by
 * mongoProcessCallbacks(). Otherwise, the context is immediately free'd. */
void mongoAsyncFree(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    c->flags |= MONGO_FREEING;
    if (!(c->flags & MONGO_IN_CALLBACK))
        __mongoAsyncFree(ac);
}

/* Helper function to make the disconnect happen and clean up. */
static void __mongoAsyncDisconnect(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);

    /* Make sure error is accessible if there is any */
    __mongoAsyncCopyError(ac);

    if (ac->err == 0) {
        /* For clean disconnects, there should be no pending callbacks. */
        assert(__mongoShiftCallback(&ac->replies,NULL) == MONGO_ERR);
    } else {
        /* Disconnection is caused by an error, make sure that pending
         * callbacks cannot call new commands. */
        c->flags |= MONGO_DISCONNECTING;
    }

    /* For non-clean disconnects, __mongoAsyncFree() will execute pending
     * callbacks with a NULL-reply. */
    __mongoAsyncFree(ac);
}

/* Tries to do a clean disconnect from Mongo, meaning it stops new commands
 * from being issued, but tries to flush the output buffer and execute
 * callbacks for all remaining replies. When this function is called from a
 * callback, there might be more replies and we can safely defer disconnecting
 * to mongoProcessCallbacks(). Otherwise, we can only disconnect immediately
 * when there are no pending callbacks. */
void mongoAsyncDisconnect(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    c->flags |= MONGO_DISCONNECTING;
    if (!(c->flags & MONGO_IN_CALLBACK) && ac->replies.head == NULL)
        __mongoAsyncDisconnect(ac);
}

void mongoProcessCallbacks(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    mongoCallback cb = {NULL, NULL, NULL};
    void *reply = NULL;
    int status;

    while((status = mongoGetReply(c,&reply)) == MONGO_OK) {
        if (reply == NULL) {
            /* When the connection is being disconnected and there are
             * no more replies, this is the cue to really disconnect. */
            if (c->flags & MONGO_DISCONNECTING && sdslen(c->obuf) == 0
                && ac->replies.head == NULL) {
                __mongoAsyncDisconnect(ac);
                return;
            }

            /* When the connection is not being disconnected, simply stop
             * trying to get replies and wait for the next loop tick. */
            break;
        }

        /* Even if the context is subscribed, pending regular callbacks will
         * get a reply before pub/sub messages arrive. */
        if (__mongoShiftCallback(&ac->replies,&cb) != MONGO_OK) {
            /*
             * A spontaneous reply in a not-subscribed context can be the error
             * reply that is sent when a new connection exceeds the maximum
             * number of allowed connections on the server side.
             *
             * This is seen as an error instead of a regular reply because the
             * server closes the connection after sending it.
             *
             * To prevent the error from being overwritten by an EOF error the
             * connection is closed here. See issue #43.
             *
             * Another possibility is that the server is loading its dataset.
             * In this case we also want to close the connection, and have the
             * user wait until the server is ready to take our request.
             */
            // if (((mongoReply*)reply)->type == MONGO_REPLY_ERROR) {
            //     c->err = MONGO_ERR_OTHER;
            //     snprintf(c->errstr,sizeof(c->errstr),"%s",((mongoReply*)reply)->str);
            //     c->reader->fn->freeObject(reply);
            //     __mongoAsyncDisconnect(ac);
            //     return;
            // }
        }

        if (cb.fn != NULL) {
            __mongoRunCallback(ac,&cb,reply);
            c->reader->fn->freeObject(reply);

            /* Proceed with free'ing when mongoAsyncFree() was called. */
            if (c->flags & MONGO_FREEING) {
                __mongoAsyncFree(ac);
                return;
            }
        } else {
            /* No callback for this reply. This can either be a NULL callback,
             * or there were no callbacks to begin with. Either way, don't
             * abort with an error, but simply ignore it because the client
             * doesn't know what the server will spit out over the wire. */
            c->reader->fn->freeObject(reply);
        }
    }

    /* Disconnect when there was an error reading the reply */
    if (status != MONGO_OK)
        __mongoAsyncDisconnect(ac);
}

/* Internal helper function to detect socket status the first time a read or
 * write event fires. When connecting was not successful, the connect callback
 * is called with a MONGO_ERR status and the context is free'd. */
static int __mongoAsyncHandleConnect(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);

    if (mongoCheckSocketError(c) == MONGO_ERR) {
        /* Try again later when connect(2) is still in progress. */
        if (errno == EINPROGRESS)
            return MONGO_OK;

        if (ac->onConnect) ac->onConnect(ac,MONGO_ERR);
        __mongoAsyncDisconnect(ac);
        return MONGO_ERR;
    }

    /* Mark context as connected. */
    c->flags |= MONGO_CONNECTED;
    if (ac->onConnect) ac->onConnect(ac,MONGO_OK);
    return MONGO_OK;
}

/* This function should be called when the socket is readable.
 * It processes all replies that can be read and executes their callbacks.
 */
void mongoAsyncHandleRead(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);

    if (!(c->flags & MONGO_CONNECTED)) {
        /* Abort connect was not successful. */
        if (__mongoAsyncHandleConnect(ac) != MONGO_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & MONGO_CONNECTED))
            return;
    }

    if (mongoBufferRead(c) == MONGO_ERR) {
        __mongoAsyncDisconnect(ac);
    } else {
        /* Always re-schedule reads */
        _EL_ADD_READ(ac);
        mongoProcessCallbacks(ac);
    }
}

void mongoAsyncHandleWrite(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    int done = 0;

    if (!(c->flags & MONGO_CONNECTED)) {
        /* Abort connect was not successful. */
        if (__mongoAsyncHandleConnect(ac) != MONGO_OK)
            return;
        /* Try again later when the context is still not connected. */
        if (!(c->flags & MONGO_CONNECTED))
            return;
    }

    if (mongoBufferWrite(c,&done) == MONGO_ERR) {
        __mongoAsyncDisconnect(ac);
    } else {
        /* Continue writing when not done, stop writing otherwise */
        if (!done)
            _EL_ADD_WRITE(ac);
        else
            _EL_DEL_WRITE(ac);

        /* Always schedule reads after writes */
        _EL_ADD_READ(ac);
    }
}

/* Helper function for the mongoAsyncCommand* family of functions. Writes a
 * formatted command to the output buffer and registers the provided callback
 * function with the context. */
static int __mongoAsyncCommand(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata,
                               int32_t opCode, const char *cmd, size_t len)
{
    mongoContext *c = &(ac->c);
    mongoCallback cb;

    /* Don't accept new commands when the connection is about to be closed. */
    if (c->flags & (MONGO_DISCONNECTING | MONGO_FREEING)) return MONGO_ERR;

    /* Setup callback */
    cb.fn = fn;
    cb.privdata = privdata;

    __mongoPushCallback(&ac->replies,&cb);

    __mongoAppendCommand(c,opCode, cmd,len);

    /* Always schedule a write when the write buffer is non-empty */
    _EL_ADD_WRITE(ac);

    return MONGO_OK;
}

int mongoAsyncQuery(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata,
                    int32_t flags, char *db, char *col, int nrSkip,
                    int nrReturn, bson_t *q, bson_t *rfields)
{
    int status;
    mongoContext *c = &(ac->c);
    mongoCallback cb;

    /* Don't accept new commands when the connection is about to be closed. */
    if (c->flags & (MONGO_DISCONNECTING | MONGO_FREEING)) return MONGO_ERR;
    status = mongoAppendQueryCommand(c, flags, db, col, nrSkip, nrReturn, q, rfields);
    if (status != MONGO_OK) {
        return MONGO_ERR;
    }

    /* Setup callback */
    cb.fn = fn;
    cb.privdata = privdata;

    __mongoPushCallback(&ac->replies,&cb);

    /* Always schedule a write when the write buffer is non-empty */
    _EL_ADD_WRITE(ac);

    return MONGO_OK;
}

int mongoAsyncQueryWithJson(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata,
                            int32_t flags, char *db, char *col, char *q_js,
                            char *rf_js, int nrSkip, int nrReturn)
{
    bson_error_t error;
    bson_t *q, *rf=NULL;
    int status;
    q = bson_new_from_json((uint8_t *)q_js, -1, &error);
    if (!q) {
        return MONGO_ERR;
    }
    if (rf_js) {
        rf = bson_new_from_json((uint8_t *)rf_js, -1, &error);
        if (!rf) {
            return MONGO_ERR;
        }
    }
    status = mongoAsyncQuery(ac, fn, privdata, flags, db, col, nrSkip, nrReturn, q, rf);
    bson_destroy(q);
    if (rf) bson_destroy(rf);
    return status;
}

int mongoAsyncListCollections(mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata, char *db) {
    bson_t *q = bson_new();
    int status = mongoAsyncQuery(ac, fn, privdata, 0, db, (char *)"system.namespaces", 0, -1, q, NULL);
    bson_destroy(q);
    return status;
}
