/*
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __HIMONGO_LIBEVENT_H__
#define __HIMONGO_LIBEVENT_H__
#include <event2/event.h>
#include "../himongo.h"
#include "../async.h"

typedef struct mongoLibeventEvents {
    mongoAsyncContext *context;
    struct event *rev, *wev;
} mongoLibeventEvents;

static void mongoLibeventReadEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    mongoLibeventEvents *e = (mongoLibeventEvents*)arg;
    mongoAsyncHandleRead(e->context);
}

static void mongoLibeventWriteEvent(int fd, short event, void *arg) {
    ((void)fd); ((void)event);
    mongoLibeventEvents *e = (mongoLibeventEvents*)arg;
    mongoAsyncHandleWrite(e->context);
}

static void mongoLibeventAddRead(void *privdata) {
    mongoLibeventEvents *e = (mongoLibeventEvents*)privdata;
    event_add(e->rev,NULL);
}

static void mongoLibeventDelRead(void *privdata) {
    mongoLibeventEvents *e = (mongoLibeventEvents*)privdata;
    event_del(e->rev);
}

static void mongoLibeventAddWrite(void *privdata) {
    mongoLibeventEvents *e = (mongoLibeventEvents*)privdata;
    event_add(e->wev,NULL);
}

static void mongoLibeventDelWrite(void *privdata) {
    mongoLibeventEvents *e = (mongoLibeventEvents*)privdata;
    event_del(e->wev);
}

static void mongoLibeventCleanup(void *privdata) {
    mongoLibeventEvents *e = (mongoLibeventEvents*)privdata;
    event_free(e->rev);
    event_free(e->wev);
    free(e);
}

static int mongoLibeventAttach(mongoAsyncContext *ac, struct event_base *base) {
    mongoContext *c = &(ac->c);
    mongoLibeventEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return MONGO_ERR;

    /* Create container for context and r/w events */
    e = (mongoLibeventEvents*)malloc(sizeof(*e));
    e->context = ac;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = mongoLibeventAddRead;
    ac->ev.delRead = mongoLibeventDelRead;
    ac->ev.addWrite = mongoLibeventAddWrite;
    ac->ev.delWrite = mongoLibeventDelWrite;
    ac->ev.cleanup = mongoLibeventCleanup;
    ac->ev.data = e;

    /* Initialize and install read/write events */
    e->rev = event_new(base, c->fd, EV_READ, mongoLibeventReadEvent, e);
    e->wev = event_new(base, c->fd, EV_WRITE, mongoLibeventWriteEvent, e);
    event_add(e->rev, NULL);
    event_add(e->wev, NULL);
    return MONGO_OK;
}
#endif
