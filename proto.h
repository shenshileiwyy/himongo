//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-18
//

#ifndef _PROTO_H_
#define _PROTO_H_ 1

#include <stdint.h>
#include <bson.h>

#define MONGO_MAX_DBNAME_LEN  64
#define MONGO_MAX_NS_LEN      120

#define OP_REPLY	1
#define OP_MSG	1000
#define OP_UPDATE	2001
#define OP_INSERT	2002
#define RESERVED	2003
#define OP_QUERY	2004
#define OP_GET_MORE	2005
#define OP_DELETE	2006
#define OP_KILL_CURSORS	2007

#define INSERT_FLAG_CONT_ON_ERR      (1 << 31)

#define UPDATE_FLAG_UPSERT           (1 << 31)
#define UPDATE_FLAG_MULTIPLE         (1 << 30)

#define QUERY_FLAG_TAILABLE_CURSOR   (1 << 30)
#define QUERY_FLAG_SLAVE_OK          (1 << 29)
#define QUERY_FLAG_OPLOG_REPLAY      (1 << 28)
#define QUERY_FLAG_NO_CURSOR_TIMEOUT (1 << 27)
#define QUERY_FLAG_AWAIT_DATA        (1 << 26)
#define QUERY_FLAG_EXHAUST           (1 << 25)
#define QUERY_FLAG_PARTIAL           (1 << 24)
#define QUERY_FLAG_MASK              0xFF000000

#define DELETE_FLAG_SINGLE           (1 << 31)

#define REPLY_FLAG_CURSOR_NOT_FOUND  (1 << 31)
#define REPLY_FLAG_QUERY_FAILURE      (1 << 30)
#define REPLY_FLAG_SHARD_CONFIG_STALE (1 << 29)
#define REPLY_FLAG_AWAIT_CAPABLE      (1 << 28)
#define REPLY_FLAG_MASK               0xF0FFFFFF
#define MSG_HEADER                              \
    int32_t messageLength;                      \
    int32_t requestID;                          \
    int32_t responseTo;                         \
    int32_t opCode

struct updateMsg {
    MSG_HEADER;
    int32_t zero;
    char *coll_name;
    int32_t flags;
    bson_t *selector;
    bson_t *update;
};

struct insertMsg {
    MSG_HEADER;
    int32_t flags;
    char *coll_name;
    bson_t *docs;
};

struct queryMsg {
    MSG_HEADER;
    int32_t flags;
    char *coll_name;
    int32_t nrSkip;
    int32_t nrReturn;
    bson_t *query;
    bson_t *returnFields;
};

struct replyMsg {
    MSG_HEADER;
    int32_t responseFlags;
    int64_t cursorID;
    int32_t startingFrom;
    int32_t numberReturned;
    bson_t **docs;
} replyMsg;

struct mongoCursor {
    char *namespace[MONGO_MAX_NS_LEN];
    int32_t numberToReturn;
    int64_t cursorID;
};

struct replyMsg* replyMsgCreateFromBytes(char *buf, size_t size);
void replyMsgFree(struct replyMsg *m);
int replyMsgToStr(struct replyMsg *m, char *buf, size_t len);
#endif /* _PROTO_H_ */