//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-18
//

#ifndef _PROTO_H_
#define _PROTO_H_ 1

#include <stdint.h>
#include <bson.h>

#define OP_REPLY	1
#define OP_MSG	1000
#define OP_UPDATE	2001
#define OP_INSERT	2002
#define RESERVED	2003
#define OP_QUERY	2004
#define OP_GET_MORE	2005
#define OP_DELETE	2006
#define OP_KILL_CURSORS	2007
#define OP_COMMAND	2010
#define OP_COMMANDREPLY	2011

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

struct queryMsg {
    MSG_HEADER;
    int32_t flags;
    char *coll_name;
    int32_t nrSkip;
    int32_t nrReturn;
    bson_t *query;
    bson_t *returnFields;
};

struct replyMSg {
    MSG_HEADER;
    int32_t responseFlags;
    int64_t cursorID;
    int32_t startingFrom;
    int32_t numberReturned;
    bson_t **docs;
} replyMSg;

struct replyMSg* replyMsgCreateFromBytes(char *buf, size_t size);
void replyMsgFree(struct replyMsg *m);
#endif /* _PROTO_H_ */
