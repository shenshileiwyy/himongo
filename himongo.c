#include "fmacros.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "proto.h"
#include "himongo.h"
#include "net.h"
#include "sds.h"
#include "utils.h"

void __mongoSetError(mongoContext *c, int type, const char *str) {
    size_t len;

    c->err = type;
    if (str != NULL) {
        len = strlen(str);
        len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
        memcpy(c->errstr,str,len);
        c->errstr[len] = '\0';
    } else {
        /* Only MONGO_ERR_IO may lack a description! */
        assert(type == MONGO_ERR_IO);
        __mongo_strerror_r(errno, c->errstr, sizeof(c->errstr));
    }
}

char *bson_extract_string(bson_t *b, char *k) {
    bson_iter_t it1, it2;
    char *ss = NULL;
    if (bson_iter_init(&it1, b) &&
        bson_iter_find_descendant(&it1, k, &it2) &&
        BSON_ITER_HOLDS_UTF8(&it2)) {
        ss = (char *)bson_iter_utf8(&it2, NULL);
    }
    return ss;
}

int64_t bson_extract_int64(bson_t *b, char *k) {
    bson_iter_t it1, it2;
    int64_t i = 0;
    if (bson_iter_init(&it1, b) &&
        bson_iter_find_descendant(&it1, k, &it2) &&
        BSON_ITER_HOLDS_INT64(&it2)) {
        i = bson_iter_int64(&it2);
    }
    return i;
}

int32_t bson_extract_int32(bson_t *b, char *k) {
    bson_iter_t it1, it2;
    int64_t i = 0;
    if (bson_iter_init(&it1, b) &&
        bson_iter_find_descendant(&it1, k, &it2) &&
        BSON_ITER_HOLDS_INT32(&it2)) {
        i = bson_iter_int32(&it2);
    }
    return i;
}

char **bson_extract_collection_names(bson_t *b) {
    bson_iter_t it1, it2, it3, it4;
    char *buf[4096];
    char **pptr = buf;
    int n = 0;
    int max_n = 4096;
    char **namev;
    char *name;
    size_t totalsz;
    if (bson_iter_init(&it1, b) &&
        bson_iter_find_descendant(&it1, "cursor.firstBatch", &it2) &&
        BSON_ITER_HOLDS_ARRAY(&it2) && bson_iter_recurse(&it2, &it3)) {
        while (bson_iter_next (&it3)) {
            bson_iter_recurse(&it3, &it4);
            if (bson_iter_find(&it4, "name") && BSON_ITER_HOLDS_UTF8(&it4) &&
                (name = (char*)bson_iter_utf8(&it4, NULL))) {
                // check if the buffer is enough
                if (n >= max_n) {
                    if (pptr != buf) pptr = realloc(pptr, sizeof(void*)*max_n*2+1);
                    else {
                        pptr = malloc(sizeof(void*)*max_n*2+1);
                        memcpy(pptr, buf, sizeof(buf));
                    }
                    max_n *= 2;
                }
                pptr[n++] = strdup(name);
            }
        }
    }
    if (pptr == buf){
        totalsz = (n+1) * sizeof(void*);
        namev = malloc(totalsz);
        memcpy(namev, pptr, n * sizeof(void *));
    } else {
        namev = pptr;
    }
    namev[n] = NULL;
    return namev;
}

void freeReplyObject(void *reply) {
    replyMsgFree(reply);
}

static mongoContext *mongoContextInit(void) {
    mongoContext *c;

    c = calloc(1,sizeof(mongoContext));
    if (c == NULL)
        return NULL;

    c->err = 0;
    c->errstr[0] = '\0';
    c->obuf = sdsempty();
    c->reader = mongoReaderCreate();
    c->tcp.host = NULL;
    c->tcp.source_addr = NULL;
    c->unix_sock.path = NULL;
    c->timeout = NULL;

    if (c->obuf == NULL || c->reader == NULL) {
        mongoFree(c);
        return NULL;
    }

    return c;
}

void mongoFree(mongoContext *c) {
    if (c == NULL)
        return;
    if (c->fd > 0)
        close(c->fd);
    if (c->obuf != NULL)
        sdsfree(c->obuf);
    if (c->reader != NULL)
        mongoReaderFree(c->reader);
    if (c->tcp.host)
        free(c->tcp.host);
    if (c->tcp.source_addr)
        free(c->tcp.source_addr);
    if (c->unix_sock.path)
        free(c->unix_sock.path);
    if (c->timeout)
        free(c->timeout);
    free(c);
}

int mongoFreeKeepFd(mongoContext *c) {
    int fd = c->fd;
    c->fd = -1;
    mongoFree(c);
    return fd;
}

int mongoReconnect(mongoContext *c) {
    c->err = 0;
    memset(c->errstr, '\0', strlen(c->errstr));

    if (c->fd > 0) {
        close(c->fd);
    }

    sdsfree(c->obuf);
    mongoReaderFree(c->reader);

    c->obuf = sdsempty();
    c->reader = mongoReaderCreate();

    if (c->connection_type == MONGO_CONN_TCP) {
        return mongoContextConnectBindTcp(c, c->tcp.host, c->tcp.port,
                c->timeout, c->tcp.source_addr);
    } else if (c->connection_type == MONGO_CONN_UNIX) {
        return mongoContextConnectUnix(c, c->unix_sock.path, c->timeout);
    } else {
        /* Something bad happened here and shouldn't have. There isn't
           enough information in the context to reconnect. */
        __mongoSetError(c,MONGO_ERR_OTHER,"Not enough information to reconnect");
    }

    return MONGO_ERR;
}

/* Connect to a Mongo instance. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
mongoContext *mongoConnect(const char *ip, int port) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= MONGO_BLOCK;
    mongoContextConnectTcp(c,ip,port,NULL);
    return c;
}

mongoContext *mongoConnectWithTimeout(const char *ip, int port, const struct timeval tv) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= MONGO_BLOCK;
    mongoContextConnectTcp(c,ip,port,&tv);
    return c;
}

mongoContext *mongoConnectNonBlock(const char *ip, int port) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags &= ~MONGO_BLOCK;
    mongoContextConnectTcp(c,ip,port,NULL);
    return c;
}

mongoContext *mongoConnectBindNonBlock(const char *ip, int port,
                                       const char *source_addr) {
    mongoContext *c = mongoContextInit();
    c->flags &= ~MONGO_BLOCK;
    mongoContextConnectBindTcp(c,ip,port,NULL,source_addr);
    return c;
}

mongoContext *mongoConnectBindNonBlockWithReuse(const char *ip, int port,
                                                const char *source_addr) {
    mongoContext *c = mongoContextInit();
    c->flags &= ~MONGO_BLOCK;
    c->flags |= MONGO_REUSEADDR;
    mongoContextConnectBindTcp(c,ip,port,NULL,source_addr);
    return c;
}

mongoContext *mongoConnectUnix(const char *path) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= MONGO_BLOCK;
    mongoContextConnectUnix(c,path,NULL);
    return c;
}

mongoContext *mongoConnectUnixWithTimeout(const char *path, const struct timeval tv) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= MONGO_BLOCK;
    mongoContextConnectUnix(c,path,&tv);
    return c;
}

mongoContext *mongoConnectUnixNonBlock(const char *path) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->flags &= ~MONGO_BLOCK;
    mongoContextConnectUnix(c,path,NULL);
    return c;
}

mongoContext *mongoConnectFd(int fd) {
    mongoContext *c;

    c = mongoContextInit();
    if (c == NULL)
        return NULL;

    c->fd = fd;
    c->flags |= MONGO_BLOCK | MONGO_CONNECTED;
    return c;
}

/* Set read/write timeout on a blocking socket. */
int mongoSetTimeout(mongoContext *c, const struct timeval tv) {
    if (c->flags & MONGO_BLOCK)
        return mongoContextSetTimeout(c,tv);
    return MONGO_ERR;
}

/* Enable connection KeepAlive. */
int mongoEnableKeepAlive(mongoContext *c) {
    if (mongoKeepAlive(c, MONGO_KEEPALIVE_INTERVAL) != MONGO_OK)
        return MONGO_ERR;
    return MONGO_OK;
}

/* Use this function to handle a read event on the descriptor. It will try
 * and read some bytes from the socket and feed them to the reply parser.
 *
 * After this function is called, you may use mongoContextReadReply to
 * see if there is a reply available. */
int mongoBufferRead(mongoContext *c) {
    char buf[1024*16];
    int nread;

    /* Return early when the context has seen an error. */
    if (c->err)
        return MONGO_ERR;

    nread = read(c->fd,buf,sizeof(buf));
    if (nread == -1) {
        if ((errno == EAGAIN && !(c->flags & MONGO_BLOCK)) || (errno == EINTR)) {
            /* Try again later */
        } else {
            __mongoSetError(c,MONGO_ERR_IO,NULL);
            return MONGO_ERR;
        }
    } else if (nread == 0) {
        __mongoSetError(c,MONGO_ERR_EOF,"Server closed the connection");
        return MONGO_ERR;
    } else {
        if (mongoReaderFeed(c->reader,buf,nread) != MONGO_OK) {
            __mongoSetError(c,c->reader->err,c->reader->errstr);
            return MONGO_ERR;
        }
    }
    return MONGO_OK;
}

/* Write the output buffer to the socket.
 *
 * Returns MONGO_OK when the buffer is empty, or (a part of) the buffer was
 * successfully written to the socket. When the buffer is empty after the
 * write operation, "done" is set to 1 (if given).
 *
 * Returns MONGO_ERR if an error occurred trying to write and sets
 * c->errstr to hold the appropriate error string.
 */
int mongoBufferWrite(mongoContext *c, int *done) {
    int nwritten;

    /* Return early when the context has seen an error. */
    if (c->err)
        return MONGO_ERR;

    if (sdslen(c->obuf) > 0) {
        nwritten = write(c->fd,c->obuf,sdslen(c->obuf));
        if (nwritten == -1) {
            if ((errno == EAGAIN && !(c->flags & MONGO_BLOCK)) || (errno == EINTR)) {
                /* Try again later */
            } else {
                __mongoSetError(c,MONGO_ERR_IO,NULL);
                return MONGO_ERR;
            }
        } else if (nwritten > 0) {
            if (nwritten == (signed)sdslen(c->obuf)) {
                sdsfree(c->obuf);
                c->obuf = sdsempty();
            } else {
                sdsrange(c->obuf,nwritten,-1);
            }
        }
    }
    if (done != NULL) *done = (sdslen(c->obuf) == 0);
    return MONGO_OK;
}

/* Internal helper function to try and get a reply from the reader,
 * or set an error in the context otherwise. */
int mongoGetReplyFromReader(mongoContext *c, void **reply) {
    if (mongoReaderGetReply(c->reader,reply) == MONGO_ERR) {
        __mongoSetError(c,c->reader->err,c->reader->errstr);
        return MONGO_ERR;
    }
    return MONGO_OK;
}

int mongoGetReply(mongoContext *c, void **reply) {
    int wdone = 0;
    void *aux = NULL;

    /* Try to read pending replies */
    if (mongoGetReplyFromReader(c,&aux) == MONGO_ERR)
        return MONGO_ERR;

    /* For the blocking context, flush output buffer and read reply */
    if (aux == NULL && c->flags & MONGO_BLOCK) {
        /* Write until done */
        do {
            if (mongoBufferWrite(c,&wdone) == MONGO_ERR)
                return MONGO_ERR;
        } while (!wdone);

        /* Read until there is a reply */
        do {
            if (mongoBufferRead(c) == MONGO_ERR)
                return MONGO_ERR;
            if (mongoGetReplyFromReader(c,&aux) == MONGO_ERR)
                return MONGO_ERR;
        } while (aux == NULL);
    }

    /* Set reply object */
    if (reply != NULL) *reply = aux;
    return MONGO_OK;
}


/*
 * Write a formatted command to the output buffer.
 */
int mongoAppendReqeustRaw(mongoContext *c, int32_t req_id, int32_t opCode, char *m, size_t len) {
    sds newbuf;
    int32_t totallen = (int32_t)(16 + len);
    if (req_id <= 0) req_id = ++(c->req_id);

    //TODO size should be size_t
    newbuf = sdscatpack(c->obuf, "<iiiim",totallen, req_id, 0, opCode, m, len);
    if (newbuf == NULL) {
        __mongoSetError(c,MONGO_ERR_OOM,"Out of memory");
        return MONGO_ERR;
    }
    c->obuf = newbuf;
    return MONGO_OK;
}

static inline int __mongoAppendReqeust(mongoContext *c, int32_t opCode, char *m, size_t len) {
    return mongoAppendReqeustRaw(c, ++(c->req_id), opCode, m, len);
}

/*
 * struct OP_UPDATE {
 *     MsgHeader header;             // standard message header
 *     int32     ZERO;               // 0 - reserved for future use
 *     cstring   fullCollectionName; // "dbname.collectionname"
 *     int32     flags;              // bit vector. see below
 *     document  selector;           // the query to select the document
 *     document  update;             // specification of the update to perform
 * }
 */
int mongoAppendUpdateMsg(mongoContext *c, char *db, char *col, int32_t flags,
                         bson_t *selector, bson_t *update)
{
    char buf[BUFSIZ];
    int status;
    sds s;
    size_t len = 0;
    uint8_t *s_data = (uint8_t *)bson_get_data(selector);
    uint8_t *u_data = (uint8_t *)bson_get_data(update);
    size_t s_len = selector->len;
    size_t u_len = update->len;
    status = snpack(buf, len, BUFSIZ, "<issSimm",
                    0, db, ".", col, flags,
                    s_data, s_len, u_data, u_len);
    if (status < 0) {
        s = sdsempty();
        s = sdscatpack(s, "<issSimm",
                       0, db, ".", col, flags,
                       s_data, s_len, u_data, u_len);
        if (s == NULL) {
            __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
            return MONGO_OK;
        }
        status = __mongoAppendReqeust(c, OP_UPDATE, s, sdslen(s));
        sdsfree(s);
    } else {
        len = (size_t)status;
        status = __mongoAppendReqeust(c, OP_UPDATE, buf, len);
    }
    return status;
}

/*
 * OP_INSERT message format.
 *
 * struct {
 *     MsgHeader header;             // standard message header
 *     int32     flags;              // bit vector - see below
 *     cstring   fullCollectionName; // "dbname.collectionname"
 *     document* documents;          // one or more documents to insert into the collection
 * }
 */
int mongoAppendInsertMsg(mongoContext *c, int32_t flags, char *db, char *col,
                         bson_t **docs, size_t nr_docs)
{
    char buf[BUFSIZ];
    int status;
    sds s;
    size_t len = 0;
    uint8_t *d_data;
    size_t d_len;
    status = snpack(buf, len, BUFSIZ, "<issS",
                    flags, db, ".", col);
    assert(status > 0);
    len = (size_t)status;

    for (size_t i = 0; i < nr_docs; ++i) {
        d_data = (uint8_t *)bson_get_data(docs[i]);
        d_len = docs[i]->len;
        status = snpack(buf, len, BUFSIZ-len, "<m", d_data, d_len);
        if (status < 0) break;
        len = (size_t)status;
    }
    if (status < 0) {
        s = sdsempty();
        s = sdscatpack(s, "<issS",
                       flags, db, ".", col);
        if (s == NULL) {
            __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
            return MONGO_OK;
        }
        for (size_t i = 0; i < nr_docs; ++i) {
            d_data = (uint8_t *)bson_get_data(docs[i]);
            d_len = docs[i]->len;
            s= sdscatpack(s, "<m", d_data, d_len);
            if (s == NULL) {
                __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
                return MONGO_OK;
            }
        }
        status = __mongoAppendReqeust(c, OP_INSERT, s, sdslen(s));
        sdsfree(s);
    } else {
        status = __mongoAppendReqeust(c, OP_INSERT, buf, len);
    }
    return status;
}

/*
 * struct OP_QUERY {
 *     MsgHeader header;                 // standard message header
 *     int32     flags;                  // bit vector of query options.  See below for details.
 *     cstring   fullCollectionName ;    // "dbname.collectionname"
 *     int32     numberToSkip;           // number of documents to skip
 *     int32     numberToReturn;         // number of documents to return
 *                                       //  in the first OP_REPLY batch
 *     document  query;                  // query object.  See below for details.
 *   [ document  returnFieldsSelector; ] // Optional. Selector indicating the fields
 *                                       //  to return.  See below for details.
 * }
 */
int mongoAppendQueryMsg(mongoContext *c, int32_t flags, char *db, char *col,
                        int nrSkip, int nrReturn, bson_t *q, bson_t *rfields)
{
    bson_t empty;
    bson_init(&empty);
    if (!q) q = &empty;

    char buf[BUFSIZ];
    int status;
    sds s;
    size_t len = 0;
    size_t remain = BUFSIZ;
    uint8_t *q_data = (uint8_t *)bson_get_data(q);
    uint8_t *rf_data = rfields? (uint8_t *)bson_get_data(rfields): NULL;
    size_t q_len = q->len;
    size_t rf_len = rfields? rfields->len: 0;
    if (col == NULL) {
        status = snpack(buf, len, remain, "<iSiimm",
                        flags, db, nrSkip, nrReturn,
                        q_data, q_len, rf_data, rf_len);
    } else {
        status = snpack(buf, len, remain, "<issSiimm",
                        flags, db, ".", col, nrSkip, nrReturn,
                        q_data, q_len, rf_data, rf_len);
    }
    if (status < 0) {
        s = sdsempty();
        if (col == NULL) {
            s = sdscatpack(s, "<iSiimm",
                           flags, db, nrSkip, nrReturn,
                           q_data, q_len, rf_data, rf_len);
        } else {
            s = sdscatpack(s, "<issSiimm",
                           flags, db, ".", col, nrSkip, nrReturn,
                           q_data, q_len, rf_data, rf_len);
        }
        if (s == NULL) {
            __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
            return MONGO_OK;
        }
        status = __mongoAppendReqeust(c, OP_QUERY, s, sdslen(s));
        sdsfree(s);
    } else {
        len = (size_t)status;
        status = __mongoAppendReqeust(c, OP_QUERY, buf, len);
    }
    return status;
}

/*
 * OP_GET_MORE message format
 *
 * struct {
 *     MsgHeader header;             // standard message header
 *     int32     ZERO;               // 0 - reserved for future use
 *     cstring   fullCollectionName; // "dbname.collectionname"
 *     int32     numberToReturn;     // number of documents to return
 *     int64     cursorID;           // cursorID from the OP_REPLY
 * }
 */
int mongoAppendGetMoreMsg(mongoContext *c, char *db, char *col, int32_t nrReturn, int64_t cursorID) {
    char buf[BUFSIZ];
    int status;
    sds s;
    size_t len = 0;
    size_t remain = BUFSIZ;
    status = snpack(buf, len, remain, "<issSiq",
                    0, db, ".", col, nrReturn, cursorID);
    if (status < 0) {
        s = sdsempty();
        s = sdscatpack(s, "<issSiq",
                       0, db, ".", col, nrReturn, cursorID);
        if (s == NULL) {
            __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
            return MONGO_OK;
        }
        status = __mongoAppendReqeust(c, OP_GET_MORE, s, sdslen(s));
        sdsfree(s);
    } else {
        len = (size_t)status;
        status = __mongoAppendReqeust(c, OP_GET_MORE, buf, len);
    }
    return status;
}

/*
 * OP_DELETE message format
 *
 * struct {
 *     MsgHeader header;             // standard message header
 *     int32     ZERO;               // 0 - reserved for future use
 *     cstring   fullCollectionName; // "dbname.collectionname"
 *     int32     flags;              // bit vector - see below for details.
 *     document  selector;           // query object.  See below for details.
 * }
 */
int mongoAppendDeleteMsg(mongoContext *c, char *db, char *col, int32_t flags,
                         bson_t *selector)
{
    char buf[BUFSIZ];
    int status;
    sds s;
    size_t len = 0;
    size_t remain = BUFSIZ;
    uint8_t *s_data = (uint8_t *)bson_get_data(selector);
    size_t s_len = selector->len;
    status = snpack(buf, len, remain, "<issSim",
                    0, db, ".", col, flags,
                    s_data, s_len);
    if (status < 0) {
        s = sdsempty();
        s = sdscatpack(s, "<issSim",
                       0, db, ".", col, flags,
                       s_data, s_len);
        if (s == NULL) {
            __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
            return MONGO_OK;
        }
        status = __mongoAppendReqeust(c, OP_DELETE, s, sdslen(s));
        sdsfree(s);
    } else {
        len = (size_t)status;
        status = __mongoAppendReqeust(c, OP_DELETE, buf, len);
    }
    return status;
}

/*
 * OP_KILL_CURSORS message format.
 *
 * struct {
 *     MsgHeader header;            // standard message header
 *     int32     ZERO;              // 0 - reserved for future use
 *     int32     numberOfCursorIDs; // number of cursorIDs in message
 *     int64*    cursorIDs;         // sequence of cursorIDs to close
 * }
 */
int mongoAppendKillCursorsMsg(mongoContext *c, int32_t nrID, int64_t *IDs)
{
    char buf[BUFSIZ];
    int status = 0;
    sds s;
    size_t len = 0;
    size_t remain = BUFSIZ;
    status = snpack(buf, len, remain, "<ii", 0, nrID);
    assert(status > 0);
    len = (size_t)status;
    for (int32_t i = 0; i < nrID; ++i) {
        status = snpack(buf, len, BUFSIZ-len, "<q", IDs[i]);
        if (status < 0) break;
        len = (size_t )status;
    }
    if (status < 0) {
        s = sdsempty();
        for (int32_t i = 0; i < nrID; ++i) {
            if (i == 0) s = sdscatpack(s, "<iiq", 0, nrID, IDs[i]);
            else s = sdscatpack(s, "<q", IDs[i]);
            if (s == NULL) {
                __mongoSetError(c, MONGO_ERR_OOM, "Out of memory.");
                return MONGO_OK;
            }
        }
        status = __mongoAppendReqeust(c, OP_KILL_CURSORS, s, sdslen(s));
        sdsfree(s);
    } else {
        status = __mongoAppendReqeust(c, OP_KILL_CURSORS, buf, len);
    }
    return status;
}

int mongoAppendCmdRequst(mongoContext *c, int32_t flags, char *db, char *q_js){
    bson_error_t error;
    bson_t *q;
    q = bson_new_from_json((uint8_t *)q_js, -1, &error);
    if (!q) {
        __mongoSetError(c, MONGO_ERR_PROTOCOL, error.message);
        return MONGO_ERR;
    }
    return mongoAppendQueryMsg(c, flags, db, (char *)"$cmd", 0, -1, q, NULL);
}

/* Helper function for the mongoCommand* family of functions.
 *
 * Write a formatted command to the output buffer. If the given context is
 * blocking, immediately read the reply into the "reply" pointer. When the
 * context is non-blocking, the "reply" pointer will not be used and the
 * command is simply appended to the write buffer.
 *
 * Returns the reply when a reply was successfully retrieved. Returns NULL
 * otherwise. When NULL is returned in a blocking context, the error field
 * in the context will be set.
 */
static void *__mongoBlockForReply(mongoContext *c) {
    void *reply;

    if (c->flags & MONGO_BLOCK) {
        if (mongoGetReply(c,&reply) != MONGO_OK)
            return NULL;
        return reply;
    }
    return NULL;
}

void *mongoQuery(mongoContext *c, int32_t flags, char *db, char *col,
                 int nrSkip, int nrReturn, bson_t *q, bson_t *rfields)
{
    int status = mongoAppendQueryMsg(c, flags, db, col, nrSkip, nrReturn, q, rfields);
    if (status != MONGO_OK) {
        return NULL;
    }
    return __mongoBlockForReply(c);
}

void *mongoQueryWithJson(mongoContext *c, int32_t flags, char *db, char *col,
                         int nrSkip, int nrReturn, char *q_js, char *rf_js)
{
    bson_error_t error;
    bson_t *q, *rf=NULL;
    void *rpl;
    q = bson_new_from_json((uint8_t *)q_js, -1, &error);
    if (!q) {
        __mongoSetError(c, MONGO_ERR_PROTOCOL, error.message);
        return NULL;
    }
    if (rf_js) {
        rf = bson_new_from_json((uint8_t *)rf_js, -1, &error);
        if (!rf) {
            __mongoSetError(c, MONGO_ERR_PROTOCOL, error.message);
            return NULL;
        }
    }
    rpl = mongoQuery(c, flags, db, col, nrSkip, nrReturn, q, rf);
    bson_destroy(q);
    if (rf) bson_destroy(rf);
    return rpl;
}

void *mongoFind(mongoContext *c, char *db, char *col, bson_t *q, bson_t* rfield, int32_t nrPerQuery) {
    return mongoQuery(c, 0, db, col, 0, nrPerQuery, q, rfield);
}

void * mongoFindOne(mongoContext *c, char *db, char *col, bson_t *q, bson_t *rfield) {
    return mongoQuery(c, 0, db, col, 0, -1, q, rfield);
}

void **mongoFindAll(mongoContext *c, char *db, char *col, bson_t *q, bson_t *rfield, int32_t nrPerQuery) {
    void *buf[4096];
    void **pptr = buf;
    int n = 0;
    int max_n = 4096;
    void **retv;

    struct replyMsg *rpl = mongoQuery(c, QUERY_FLAG_EXHAUST, db, col, 0, nrPerQuery, q, rfield);
    while (rpl->cursorID != 0) {
        rpl = __mongoBlockForReply(c);
        if (n >= max_n) {
            if (pptr != buf) pptr = realloc(pptr, sizeof(void*)*max_n*2);
            else {
                pptr = malloc(sizeof(void*)*max_n*2);
                memcpy(pptr, buf, sizeof(buf));
            }
            max_n *= 2;
        }
        pptr[n++] = rpl;
    }
    pptr[n] = NULL;
    if (pptr == buf) {
        retv = malloc(n * sizeof(void*));
        memcpy(retv, pptr, n);
    } else {
        retv = pptr;
    }
    return retv;
}

void* mongoDbCmd(mongoContext *c, int32_t flags, char *db, int32_t nrReturn, bson_t *q) {
    struct replyMsg *m = mongoQuery(c, flags, db, (char *)"$cmd", 0, nrReturn, q, NULL);
    return m;
}

void* mongoDbJsonCmd(mongoContext *c, int flags, char *db, int32_t nrReturn, char *q_js) {
    struct replyMsg *m = mongoQueryWithJson(c, flags, db, (char *)"$cmd", 0, nrReturn, q_js, NULL);
    return m;
}

void *mongoListCollections(mongoContext *c, char *db) {
    void *rpl = mongoDbJsonCmd(c, 0, db, -1, (char*)"{\"listCollections\": 1}");
    return rpl;
}

void *mongoNextBatch(mongoContext *c, int64_t cursor_id, char *db, char *col) {
    bson_t *b = BCON_NEW("getMore",
                         BCON_INT64(cursor_id),
                         "collection",
                         BCON_UTF8(col));
    return mongoQuery(c, 0, db, (char *)"$cmd", 0, -1, b, NULL);
}

void *mongoGetCollectionNames(mongoContext *c, char *db) {
    char **namev;
    char *ns;
    int64_t cursor_id;
    struct replyMsg *rpl = mongoDbJsonCmd(c, 0, db, -1, (char*)"{\"listCollections\": 1}");
    cursor_id = bson_extract_int64(rpl->docs[0], (char *)"cursor.id");
    ns = bson_extract_string(rpl->docs[0], (char *)"cursor.ns");
    namev = bson_extract_collection_names(rpl->docs[0]);

    freeReplyObject(rpl);
    // while (cursor_id != 0) {
    //
    // }

    // printf("cursor_id: %d, ns: %s\n", cursor_id, ns);
    return namev;
}

void *mongoGetLastError(mongoContext *c, char *db) {
    void *rpl = mongoDbJsonCmd(c, 0, db, -1, (char*)"{\"getlasterror\": 1}");
    return rpl;
}

void *mongoDropDatabase(mongoContext *c, char *db) {
    void *rpl = mongoDbJsonCmd(c, 0, db, -1, (char*)"{\"dropDatabase\": 1}");
    return rpl;
}