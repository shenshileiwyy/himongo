#include "fmacros.h"
#include <string.h>
#include <stdlib.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "endianconv.h"
#include "read.h"
#include "sds.h"
#include "proto.h"

/* Default set of functions to build the reply. Keep in mind that such a
 * function returning NULL is interpreted as error. */
static mongoReplyObjectFunctions defaultFunctions = {
        mongoReplyCreateFromBytes,
        mongoReplyFree
};

static void __mongoReaderSetError(mongoReader *r, int type, const char *str) {
    size_t len;

    if (r->reply != NULL && r->fn && r->fn->freeObject) {
        r->fn->freeObject(r->reply);
        r->reply = NULL;
    }

    /* Clear input buffer on errors. */
    if (r->buf != NULL) {
        sdsfree(r->buf);
        r->buf = NULL;
        r->pos = r->len = 0;
    }

    /* Set error. */
    r->err = type;
    len = strlen(str);
    len = len < (sizeof(r->errstr)-1) ? len : (sizeof(r->errstr)-1);
    memcpy(r->errstr,str,len);
    r->errstr[len] = '\0';
}

static void __mongoReaderSetErrorOOM(mongoReader *r) {
    __mongoReaderSetError(r,MONGO_ERR_OOM,"Out of memory");
}

mongoReader *mongoReaderCreate(void) {
    return mongoReaderCreateWithFunctions(&defaultFunctions);
}

mongoReader *mongoReaderCreateWithFunctions(mongoReplyObjectFunctions *fn) {
    mongoReader *r;

    r = calloc(sizeof(mongoReader),1);
    if (r == NULL)
        return NULL;

    r->err = 0;
    r->errstr[0] = '\0';
    r->fn = fn;
    r->buf = sdsempty();
    r->maxbuf = MONGO_READER_MAX_BUF;
    if (r->buf == NULL) {
        free(r);
        return NULL;
    }

    return r;
}

void mongoReaderFree(mongoReader *r) {
    if (r->reply != NULL && r->fn && r->fn->freeObject)
        r->fn->freeObject(r->reply);
    if (r->buf != NULL)
        sdsfree(r->buf);
    free(r);
}

int mongoReaderFeed(mongoReader *r, const char *buf, size_t len) {
    sds newbuf;

    /* Return early when this reader is in an erroneous state. */
    if (r->err)
        return MONGO_ERR;

    /* Copy the provided buffer. */
    if (buf != NULL && len >= 1) {
        /* Destroy internal buffer when it is empty and is quite large. */
        if (r->len == 0 && r->maxbuf != 0 && sdsavail(r->buf) > r->maxbuf) {
            sdsfree(r->buf);
            r->buf = sdsempty();
            r->pos = 0;

            /* r->buf should not be NULL since we just free'd a larger one. */
            assert(r->buf != NULL);
        }

        newbuf = sdscatlen(r->buf,buf,len);
        if (newbuf == NULL) {
            __mongoReaderSetErrorOOM(r);
            return MONGO_ERR;
        }

        r->buf = newbuf;
        r->len = sdslen(r->buf);
    }

    return MONGO_OK;
}

int mongoReaderGetReply(mongoReader *r, void **reply) {
    /* Default target pointer to NULL. */
    if (reply != NULL)
        *reply = NULL;

    /* Return early when this reader is in an erroneous state. */
    if (r->err)
        return MONGO_ERR;

    /* When the buffer is empty, there will never be a reply. */
    if (r->len == 0)
        return MONGO_OK;
    if (r->len - r->pos < 4)
        return MONGO_OK;
    if (r->pktlen == 0) r->pktlen = load32le(r->buf+r->pos);
    if (r->len - r->pos < r->pktlen) return MONGO_OK;
    /* create a reply object */
    r->reply = r->fn->createReply(r->buf+r->pos, r->pktlen);

    r->pos += r->pktlen;
    r->pktlen = 0;
    /* Return ASAP when an error occurred. */
    if (r->err)
        return MONGO_ERR;

    /* Discard part of the buffer when we've consumed at least 1k, to avoid
     * doing unnecessary calls to memmove() in sds.c. */
    if (r->pos >= 1024) {
        sdsrange(r->buf,r->pos,-1);
        r->pos = 0;
        r->len = sdslen(r->buf);
    }

    /* Emit a reply when there is one. */
    if (reply != NULL)
        *reply = r->reply;
    r->reply = NULL;
    return MONGO_OK;
}
