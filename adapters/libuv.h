#ifndef __HIMONGO_LIBUV_H__
#define __HIMONGO_LIBUV_H__
#include <stdlib.h>
#include <uv.h>
#include "../himongo.h"
#include "../async.h"
#include <string.h>

typedef struct mongoLibuvEvents {
  mongoAsyncContext* context;
  uv_poll_t          handle;
  int                events;
} mongoLibuvEvents;


static void mongoLibuvPoll(uv_poll_t* handle, int status, int events) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)handle->data;

  if (status != 0) {
    return;
  }

  if (p->context != NULL && (events & UV_READABLE)) {
    mongoAsyncHandleRead(p->context);
  }
  if (p->context != NULL && (events & UV_WRITABLE)) {
    mongoAsyncHandleWrite(p->context);
  }
}


static void mongoLibuvAddRead(void *privdata) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)privdata;

  p->events |= UV_READABLE;

  uv_poll_start(&p->handle, p->events, mongoLibuvPoll);
}


static void mongoLibuvDelRead(void *privdata) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)privdata;

  p->events &= ~UV_READABLE;

  if (p->events) {
    uv_poll_start(&p->handle, p->events, mongoLibuvPoll);
  } else {
    uv_poll_stop(&p->handle);
  }
}


static void mongoLibuvAddWrite(void *privdata) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)privdata;

  p->events |= UV_WRITABLE;

  uv_poll_start(&p->handle, p->events, mongoLibuvPoll);
}


static void mongoLibuvDelWrite(void *privdata) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)privdata;

  p->events &= ~UV_WRITABLE;

  if (p->events) {
    uv_poll_start(&p->handle, p->events, mongoLibuvPoll);
  } else {
    uv_poll_stop(&p->handle);
  }
}


static void on_close(uv_handle_t* handle) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)handle->data;

  free(p);
}


static void mongoLibuvCleanup(void *privdata) {
  mongoLibuvEvents* p = (mongoLibuvEvents*)privdata;

  p->context = NULL; // indicate that context might no longer exist
  uv_close((uv_handle_t*)&p->handle, on_close);
}


static int mongoLibuvAttach(mongoAsyncContext* ac, uv_loop_t* loop) {
  mongoContext *c = &(ac->c);

  if (ac->ev.data != NULL) {
    return MONGO_ERR;
  }

  ac->ev.addRead  = mongoLibuvAddRead;
  ac->ev.delRead  = mongoLibuvDelRead;
  ac->ev.addWrite = mongoLibuvAddWrite;
  ac->ev.delWrite = mongoLibuvDelWrite;
  ac->ev.cleanup  = mongoLibuvCleanup;

  mongoLibuvEvents* p = (mongoLibuvEvents*)malloc(sizeof(*p));

  if (!p) {
    return MONGO_ERR;
  }

  memset(p, 0, sizeof(*p));

  if (uv_poll_init(loop, &p->handle, c->fd) != 0) {
    return MONGO_ERR;
  }

  ac->ev.data    = p;
  p->handle.data = p;
  p->context     = ac;

  return MONGO_OK;
}
#endif
