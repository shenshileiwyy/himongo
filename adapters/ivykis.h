#ifndef __HIMONGO_IVYKIS_H__
#define __HIMONGO_IVYKIS_H__
#include <iv.h>
#include "../himongo.h"
#include "../async.h"

typedef struct mongoIvykisEvents {
    mongoAsyncContext *context;
    struct iv_fd fd;
} mongoIvykisEvents;

static void mongoIvykisReadEvent(void *arg) {
    mongoAsyncContext *context = (mongoAsyncContext *)arg;
    mongoAsyncHandleRead(context);
}

static void mongoIvykisWriteEvent(void *arg) {
    mongoAsyncContext *context = (mongoAsyncContext *)arg;
    mongoAsyncHandleWrite(context);
}

static void mongoIvykisAddRead(void *privdata) {
    mongoIvykisEvents *e = (mongoIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, mongoIvykisReadEvent);
}

static void mongoIvykisDelRead(void *privdata) {
    mongoIvykisEvents *e = (mongoIvykisEvents*)privdata;
    iv_fd_set_handler_in(&e->fd, NULL);
}

static void mongoIvykisAddWrite(void *privdata) {
    mongoIvykisEvents *e = (mongoIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, mongoIvykisWriteEvent);
}

static void mongoIvykisDelWrite(void *privdata) {
    mongoIvykisEvents *e = (mongoIvykisEvents*)privdata;
    iv_fd_set_handler_out(&e->fd, NULL);
}

static void mongoIvykisCleanup(void *privdata) {
    mongoIvykisEvents *e = (mongoIvykisEvents*)privdata;

    iv_fd_unregister(&e->fd);
    free(e);
}

static int mongoIvykisAttach(mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    mongoIvykisEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return MONGO_ERR;

    /* Create container for context and r/w events */
    e = (mongoIvykisEvents*)malloc(sizeof(*e));
    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = mongoIvykisAddRead;
    ac->ev.delRead = mongoIvykisDelRead;
    ac->ev.addWrite = mongoIvykisAddWrite;
    ac->ev.delWrite = mongoIvykisDelWrite;
    ac->ev.cleanup = mongoIvykisCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    IV_FD_INIT(&e->fd);
    e->fd.fd = c->fd;
    e->fd.handler_in = mongoIvykisReadEvent;
    e->fd.handler_out = mongoIvykisWriteEvent;
    e->fd.handler_err = NULL;
    e->fd.cookie = e->context;

    iv_fd_register(&e->fd);

    return MONGO_OK;
}
#endif
