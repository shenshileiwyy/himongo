//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-18
//
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "proto.h"
#include "utils.h"

struct replyMSg* replyMsgCreateFromBytes(char *buf, size_t size) {
    int offset;
    int num = 0;
    bson_t *bs;
    bson_reader_t *reader;
    bool eof;
    struct replyMsg *m = calloc(1, sizeof(*m));
    offset = snunpack(buf, 0, size, "<iiiiiqii",
                      &(m->messageLength), &(m->requestID), &(m->responseTo),
                      &(m->opCode), &(m->responseFlags), &(m->curserID),
                      &(m->startingFrom), &(m->numberReturned));
    if (offset < 0) {
        free(m);
        return NULL;
    }
    if (m->numberReturned > 0) {
        m->docs = malloc(m->numberReturened * sizeof(bson_t *));
        reader = bson_reader_new_from_data(buf+offset, size-offset);
        while((bs = bson_reader_read(reader, &eof))) {
            m->docs[num++] = bs;
        }
        if (!eof) {
            free(m);
            return NULL;
        }
        bson_reader_destroy(reader);
    }
    return m;
}

void replyMsgFree(struct replyMsg *m) {
    if (!m) return;

    for (int i = 0; i < m->numberReturned; ++i) {
        bson_destroy(m->docs[i]);
    }
    free(m->docs);
    free(m);
}
