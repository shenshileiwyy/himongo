//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-06-06
//

#include <stdbool.h>

#include "ae.h"
#include "../adapters/ae.h"
#include "../himongo.h"

static aeEventLoop *el;

static void reloadAllCallback(mongoAsyncContext *c, void *r, void *privdata) {
    ((void) c); ((void) privdata);
    struct replyMsg *reply = r;
    char **namev;
    char **p;

    if (reply == NULL) return;
    namev = bson_extract_collection_names(reply->docs[0]);
    for (p = namev; *p != NULL; ++p) {
        printf("collection name: %s\n", *p);
    }
    mongoFreev((void **)namev);
    return;
}

static void RRSetGetCallback(mongoAsyncContext *c, void *r, void *privdata) {
    ((void) c);
    char *name, *type, *rdata;
    uint32_t ttl;

    struct replyMsg *reply = r;

    for (int i = 0; i < reply->numberReturned; ++i) {
        bson_t *b = reply->docs[i];
        name = bson_extract_string(b, "name");
        ttl = (uint32_t)bson_extract_int32(b, "ttl");
        type = bson_extract_string(b, "type");
        rdata = bson_extract_string(b, "rdata");
        printf("RR%d %s %d %s %s\n", i, name, ttl, type, rdata);
    }
    return;
}

void connectCallback(const mongoAsyncContext *c, int status) {
    if (status != MONGO_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(el);
        return;
    }

    printf("Connected...\n");
}

void disconnectCallback(const mongoAsyncContext *c, int status) {
    if (status != MONGO_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(el);
        return;
    }

    printf("Disconnected...\n");
    aeStop(el);
}

int main(int argc, char *argv[]) {
    int status;

    el = aeCreateEventLoop(1024, true);
    mongoAsyncContext *ac = mongoAsyncConnect("127.0.0.1", 27017);
    if (ac->err) {
        printf("Failed to init mongo: %s", ac->errstr);
        return -1;
    }

    mongoAeAttach(el, ac);
    mongoAsyncSetConnectCallback(ac,connectCallback);
    mongoAsyncSetDisconnectCallback(ac,disconnectCallback);
    // perform some operations
    status = mongoAsyncGetCollectionNames(ac, reloadAllCallback, NULL, "zone");
    status = mongoAsyncFindAll(ac, RRSetGetCallback, NULL, "zone", "example.com", NULL, NULL, 10);
    aeMain(el);
}
