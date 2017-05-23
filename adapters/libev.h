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

#ifndef __HIMONGO_LIBEV_H__
#define __HIMONGO_LIBEV_H__
#include <stdlib.h>
#include <sys/types.h>
#include <ev.h>
#include "../himongo.h"
#include "../async.h"

typedef struct mongoLibevEvents {
    mongoAsyncContext *context;
    struct ev_loop *loop;
    int reading, writing;
    ev_io rev, wev;
} mongoLibevEvents;

static void mongoLibevReadEvent(EV_P_ ev_io *watcher, int revents) {
#if EV_MULTIPLICITY
    ((void)loop);
#endif
    ((void)revents);

    mongoLibevEvents *e = (mongoLibevEvents*)watcher->data;
    mongoAsyncHandleRead(e->context);
}

static void mongoLibevWriteEvent(EV_P_ ev_io *watcher, int revents) {
#if EV_MULTIPLICITY
    ((void)loop);
#endif
    ((void)revents);

    mongoLibevEvents *e = (mongoLibevEvents*)watcher->data;
    mongoAsyncHandleWrite(e->context);
}

static void mongoLibevAddRead(void *privdata) {
    mongoLibevEvents *e = (mongoLibevEvents*)privdata;
    struct ev_loop *loop = e->loop;
    ((void)loop);
    if (!e->reading) {
        e->reading = 1;
        ev_io_start(EV_A_ &e->rev);
    }
}

static void mongoLibevDelRead(void *privdata) {
    mongoLibevEvents *e = (mongoLibevEvents*)privdata;
    struct ev_loop *loop = e->loop;
    ((void)loop);
    if (e->reading) {
        e->reading = 0;
        ev_io_stop(EV_A_ &e->rev);
    }
}

static void mongoLibevAddWrite(void *privdata) {
    mongoLibevEvents *e = (mongoLibevEvents*)privdata;
    struct ev_loop *loop = e->loop;
    ((void)loop);
    if (!e->writing) {
        e->writing = 1;
        ev_io_start(EV_A_ &e->wev);
    }
}

static void mongoLibevDelWrite(void *privdata) {
    mongoLibevEvents *e = (mongoLibevEvents*)privdata;
    struct ev_loop *loop = e->loop;
    ((void)loop);
    if (e->writing) {
        e->writing = 0;
        ev_io_stop(EV_A_ &e->wev);
    }
}

static void mongoLibevCleanup(void *privdata) {
    mongoLibevEvents *e = (mongoLibevEvents*)privdata;
    mongoLibevDelRead(privdata);
    mongoLibevDelWrite(privdata);
    free(e);
}

static int mongoLibevAttach(EV_P_ mongoAsyncContext *ac) {
    mongoContext *c = &(ac->c);
    mongoLibevEvents *e;

    /* Nothing should be attached when something is already attached */
    if (ac->ev.data != NULL)
        return MONGO_ERR;

    /* Create container for context and r/w events */
    e = (mongoLibevEvents*)malloc(sizeof(*e));
    e->context = ac;
#if EV_MULTIPLICITY
    e->loop = loop;
#else
    e->loop = NULL;
#endif
    e->reading = e->writing = 0;
    e->rev.data = e;
    e->wev.data = e;

    /* Register functions to start/stop listening for events */
    ac->ev.addRead = mongoLibevAddRead;
    ac->ev.delRead = mongoLibevDelRead;
    ac->ev.addWrite = mongoLibevAddWrite;
    ac->ev.delWrite = mongoLibevDelWrite;
    ac->ev.cleanup = mongoLibevCleanup;
    ac->ev.data = e;

    /* Initialize read/write events */
    ev_io_init(&e->rev,mongoLibevReadEvent,c->fd,EV_READ);
    ev_io_init(&e->wev,mongoLibevWriteEvent,c->fd,EV_WRITE);
    return MONGO_OK;
}

#endif
