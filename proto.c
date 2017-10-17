//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-18
//
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "proto.h"
#include "utils.h"
#include "read.h"

void * mongoReplyCreateFromBytes(char *buf, size_t size) {
    int offset;
    int num = 0;
    bson_t *bs;
    bson_reader_t *reader;
    bool eof;
    mongoReply *m = calloc(1, sizeof(*m));
    offset = mongoSnunpack(buf, 0, size, "<iiiiiqii",
                           &(m->messageLength), &(m->requestID), &(m->responseTo),
                           &(m->opCode), &(m->responseFlags), &(m->cursorID),
                           &(m->startingFrom), &(m->numberReturned));
    if (offset < 0) {
        free(m);
        return NULL;
    }
    if (m->numberReturned > 0) {
        m->docs = calloc(1, m->numberReturned * sizeof(bson_t *));
        reader = bson_reader_new_from_data((uint8_t *)(buf+offset), size-offset);
        while((bs = (bson_t *)bson_reader_read(reader, &eof))) {
            if (num >= m->numberReturned) {
                goto invalid;
            }
            /*
             * since the bson object returned by bson_reader_read is a
             * static field in bson_reader_t struct,
             * so we must copy this bson object.
             */
            m->docs[num++] = bson_copy(bs);
        }
        if (!eof || num != m->numberReturned) {
            goto invalid;
        }
        bson_reader_destroy(reader);
    }
    return m;
invalid:
    mongoReplyFree(m);
    return NULL;
}

void mongoReplyFree(void *p) {
    mongoReply *m = p;
    if (!m) return;

    for (int i = 0; i < m->numberReturned; ++i) {
        if (m->docs[i]) bson_destroy(m->docs[i]);
    }
    free(m->docs);
    free(m);
}

bson_t *mongoReplyGetBson(mongoReply *m, int idx) {
    if (idx >= m->numberReturned) return NULL;
    return m->docs[idx];
}

int mongoReplyToStr(mongoReply *m, char *buf, size_t len) {
    int n;
    int offset = 0;
    n = snprintf(buf+offset, len,
                 "rid: %i \nrespTO: %d \nOpCode: %d \n"
                 "respFlags: %d \ncursorID: %ld \nstartFrom: %d \nnrReturn: %d\n",
                 m->requestID, m->responseTo, m->opCode,
                 m->responseFlags, m->cursorID, m->startingFrom, m->numberReturned);
    offset += n;
    for (int32_t i = 0; i < m->numberReturned; ++i) {
        bson_t *doc = m->docs[i];
        size_t ll;
        char *ss = bson_as_json(doc, &ll);
        n = snprintf(buf+offset, len-offset, "DOC %d\n%s\n\n",i, ss);
        offset += n;
        bson_free(ss);
        if (offset >= len-1) return MONGO_ERR;
    }
    return MONGO_OK;
}
