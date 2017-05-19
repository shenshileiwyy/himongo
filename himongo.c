#include "fmacros.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "himongo.h"
#include "net.h"
#include "sds.h"

static mongoReply *createReplyObject(int type);
static void *createStringObject(const mongoReadTask *task, char *str, size_t len);
static void *createArrayObject(const mongoReadTask *task, int elements);
static void *createIntegerObject(const mongoReadTask *task, long long value);
static void *createNilObject(const mongoReadTask *task);

/* Default set of functions to build the reply. Keep in mind that such a
 * function returning NULL is interpreted as OOM. */
static mongoReplyObjectFunctions defaultFunctions = {
    createStringObject,
    createArrayObject,
    createIntegerObject,
    createNilObject,
    freeReplyObject
};

/* Create a reply object */
static mongoReply *createReplyObject(int type) {
    mongoReply *r = calloc(1,sizeof(*r));

    if (r == NULL)
        return NULL;

    r->type = type;
    return r;
}

/* Free a reply object */
void freeReplyObject(void *reply) {
    mongoReply *r = reply;
    size_t j;

    if (r == NULL)
        return;

    switch(r->type) {
    case MONGO_REPLY_INTEGER:
        break; /* Nothing to free */
    case MONGO_REPLY_ARRAY:
        if (r->element != NULL) {
            for (j = 0; j < r->elements; j++)
                if (r->element[j] != NULL)
                    freeReplyObject(r->element[j]);
            free(r->element);
        }
        break;
    case MONGO_REPLY_ERROR:
    case MONGO_REPLY_STATUS:
    case MONGO_REPLY_STRING:
        if (r->str != NULL)
            free(r->str);
        break;
    }
    free(r);
}

static void *createStringObject(const mongoReadTask *task, char *str, size_t len) {
    mongoReply *r, *parent;
    char *buf;

    r = createReplyObject(task->type);
    if (r == NULL)
        return NULL;

    buf = malloc(len+1);
    if (buf == NULL) {
        freeReplyObject(r);
        return NULL;
    }

    assert(task->type == MONGO_REPLY_ERROR  ||
           task->type == MONGO_REPLY_STATUS ||
           task->type == MONGO_REPLY_STRING);

    /* Copy string value */
    memcpy(buf,str,len);
    buf[len] = '\0';
    r->str = buf;
    r->len = len;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == MONGO_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createArrayObject(const mongoReadTask *task, int elements) {
    mongoReply *r, *parent;

    r = createReplyObject(MONGO_REPLY_ARRAY);
    if (r == NULL)
        return NULL;

    if (elements > 0) {
        r->element = calloc(elements,sizeof(mongoReply*));
        if (r->element == NULL) {
            freeReplyObject(r);
            return NULL;
        }
    }

    r->elements = elements;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == MONGO_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createIntegerObject(const mongoReadTask *task, long long value) {
    mongoReply *r, *parent;

    r = createReplyObject(MONGO_REPLY_INTEGER);
    if (r == NULL)
        return NULL;

    r->integer = value;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == MONGO_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createNilObject(const mongoReadTask *task) {
    mongoReply *r, *parent;

    r = createReplyObject(MONGO_REPLY_NIL);
    if (r == NULL)
        return NULL;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == MONGO_REPLY_ARRAY);
        parent->element[task->idx] = r;
    }
    return r;
}

/* Return the number of digits of 'v' when converted to string in radix 10.
 * Implementation borrowed from link in mongo/src/util.c:string2ll(). */
static uint32_t countDigits(uint64_t v) {
  uint32_t result = 1;
  for (;;) {
    if (v < 10) return result;
    if (v < 100) return result + 1;
    if (v < 1000) return result + 2;
    if (v < 10000) return result + 3;
    v /= 10000U;
    result += 4;
  }
}

/* Helper that calculates the bulk length given a certain string length. */
static size_t bulklen(size_t len) {
    return 1+countDigits(len)+2+len+2;
}

int mongovFormatCommand(char **target, const char *format, va_list ap) {
    const char *c = format;
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    sds curarg, newarg; /* current argument */
    int touched = 0; /* was the current argument touched? */
    char **curargv = NULL, **newargv = NULL;
    int argc = 0;
    int totlen = 0;
    int error_type = 0; /* 0 = no error; -1 = memory error; -2 = format error */
    int j;

    /* Abort if there is not target to set */
    if (target == NULL)
        return -1;

    /* Build the command string accordingly to protocol */
    curarg = sdsempty();
    if (curarg == NULL)
        return -1;

    while(*c != '\0') {
        if (*c != '%' || c[1] == '\0') {
            if (*c == ' ') {
                if (touched) {
                    newargv = realloc(curargv,sizeof(char*)*(argc+1));
                    if (newargv == NULL) goto memory_err;
                    curargv = newargv;
                    curargv[argc++] = curarg;
                    totlen += bulklen(sdslen(curarg));

                    /* curarg is put in argv so it can be overwritten. */
                    curarg = sdsempty();
                    if (curarg == NULL) goto memory_err;
                    touched = 0;
                }
            } else {
                newarg = sdscatlen(curarg,c,1);
                if (newarg == NULL) goto memory_err;
                curarg = newarg;
                touched = 1;
            }
        } else {
            char *arg;
            size_t size;

            /* Set newarg so it can be checked even if it is not touched. */
            newarg = curarg;

            switch(c[1]) {
            case 's':
                arg = va_arg(ap,char*);
                size = strlen(arg);
                if (size > 0)
                    newarg = sdscatlen(curarg,arg,size);
                break;
            case 'b':
                arg = va_arg(ap,char*);
                size = va_arg(ap,size_t);
                if (size > 0)
                    newarg = sdscatlen(curarg,arg,size);
                break;
            case '%':
                newarg = sdscat(curarg,"%");
                break;
            default:
                /* Try to detect printf format */
                {
                    static const char intfmts[] = "diouxX";
                    static const char flags[] = "#0-+ ";
                    char _format[16];
                    const char *_p = c+1;
                    size_t _l = 0;
                    va_list _cpy;

                    /* Flags */
                    while (*_p != '\0' && strchr(flags,*_p) != NULL) _p++;

                    /* Field width */
                    while (*_p != '\0' && isdigit(*_p)) _p++;

                    /* Precision */
                    if (*_p == '.') {
                        _p++;
                        while (*_p != '\0' && isdigit(*_p)) _p++;
                    }

                    /* Copy va_list before consuming with va_arg */
                    va_copy(_cpy,ap);

                    /* Integer conversion (without modifiers) */
                    if (strchr(intfmts,*_p) != NULL) {
                        va_arg(ap,int);
                        goto fmt_valid;
                    }

                    /* Double conversion (without modifiers) */
                    if (strchr("eEfFgGaA",*_p) != NULL) {
                        va_arg(ap,double);
                        goto fmt_valid;
                    }

                    /* Size: char */
                    if (_p[0] == 'h' && _p[1] == 'h') {
                        _p += 2;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,int); /* char gets promoted to int */
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: short */
                    if (_p[0] == 'h') {
                        _p += 1;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,int); /* short gets promoted to int */
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: long long */
                    if (_p[0] == 'l' && _p[1] == 'l') {
                        _p += 2;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,long long);
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: long */
                    if (_p[0] == 'l') {
                        _p += 1;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,long);
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                fmt_invalid:
                    va_end(_cpy);
                    goto format_err;

                fmt_valid:
                    _l = (_p+1)-c;
                    if (_l < sizeof(_format)-2) {
                        memcpy(_format,c,_l);
                        _format[_l] = '\0';
                        newarg = sdscatvprintf(curarg,_format,_cpy);

                        /* Update current position (note: outer blocks
                         * increment c twice so compensate here) */
                        c = _p-1;
                    }

                    va_end(_cpy);
                    break;
                }
            }

            if (newarg == NULL) goto memory_err;
            curarg = newarg;

            touched = 1;
            c++;
        }
        c++;
    }

    /* Add the last argument if needed */
    if (touched) {
        newargv = realloc(curargv,sizeof(char*)*(argc+1));
        if (newargv == NULL) goto memory_err;
        curargv = newargv;
        curargv[argc++] = curarg;
        totlen += bulklen(sdslen(curarg));
    } else {
        sdsfree(curarg);
    }

    /* Clear curarg because it was put in curargv or was free'd. */
    curarg = NULL;

    /* Add bytes needed to hold multi bulk count */
    totlen += 1+countDigits(argc)+2;

    /* Build the command at protocol level */
    cmd = malloc(totlen+1);
    if (cmd == NULL) goto memory_err;

    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        pos += sprintf(cmd+pos,"$%zu\r\n",sdslen(curargv[j]));
        memcpy(cmd+pos,curargv[j],sdslen(curargv[j]));
        pos += sdslen(curargv[j]);
        sdsfree(curargv[j]);
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    cmd[pos] = '\0';

    free(curargv);
    *target = cmd;
    return totlen;

format_err:
    error_type = -2;
    goto cleanup;

memory_err:
    error_type = -1;
    goto cleanup;

cleanup:
    if (curargv) {
        while(argc--)
            sdsfree(curargv[argc]);
        free(curargv);
    }

    sdsfree(curarg);

    /* No need to check cmd since it is the last statement that can fail,
     * but do it anyway to be as defensive as possible. */
    if (cmd != NULL)
        free(cmd);

    return error_type;
}

/* Format a command according to the Mongo protocol. This function
 * takes a format similar to printf:
 *
 * %s represents a C null terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes as a size_t. Examples:
 *
 * len = mongoFormatCommand(target, "GET %s", mykey);
 * len = mongoFormatCommand(target, "SET %s %b", mykey, myval, myvallen);
 */
int mongoFormatCommand(char **target, const char *format, ...) {
    va_list ap;
    int len;
    va_start(ap,format);
    len = mongovFormatCommand(target,format,ap);
    va_end(ap);

    /* The API says "-1" means bad result, but we now also return "-2" in some
     * cases.  Force the return value to always be -1. */
    if (len < 0)
        len = -1;

    return len;
}

/* Format a command according to the Mongo protocol using an sds string and
 * sdscatfmt for the processing of arguments. This function takes the
 * number of arguments, an array with arguments and an array with their
 * lengths. If the latter is set to NULL, strlen will be used to compute the
 * argument lengths.
 */
int mongoFormatSdsCommandArgv(sds *target, int argc, const char **argv,
                              const size_t *argvlen)
{
    sds cmd;
    unsigned long long totlen;
    int j;
    size_t len;

    /* Abort on a NULL target */
    if (target == NULL)
        return -1;

    /* Calculate our total size */
    totlen = 1+countDigits(argc)+2;
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        totlen += bulklen(len);
    }

    /* Use an SDS string for command construction */
    cmd = sdsempty();
    if (cmd == NULL)
        return -1;

    /* We already know how much storage we need */
    cmd = sdsMakeRoomFor(cmd, totlen);
    if (cmd == NULL)
        return -1;

    /* Construct command */
    cmd = sdscatfmt(cmd, "*%i\r\n", argc);
    for (j=0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        cmd = sdscatfmt(cmd, "$%u\r\n", len);
        cmd = sdscatlen(cmd, argv[j], len);
        cmd = sdscatlen(cmd, "\r\n", sizeof("\r\n")-1);
    }

    assert(sdslen(cmd)==totlen);

    *target = cmd;
    return totlen;
}

void mongoFreeSdsCommand(sds cmd) {
    sdsfree(cmd);
}

/* Format a command according to the Mongo protocol. This function takes the
 * number of arguments, an array with arguments and an array with their
 * lengths. If the latter is set to NULL, strlen will be used to compute the
 * argument lengths.
 */
int mongoFormatCommandArgv(char **target, int argc, const char **argv, const size_t *argvlen) {
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    size_t len;
    int totlen, j;

    /* Abort on a NULL target */
    if (target == NULL)
        return -1;

    /* Calculate number of bytes needed for the command */
    totlen = 1+countDigits(argc)+2;
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        totlen += bulklen(len);
    }

    /* Build the command at protocol level */
    cmd = malloc(totlen+1);
    if (cmd == NULL)
        return -1;

    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        pos += sprintf(cmd+pos,"$%zu\r\n",len);
        memcpy(cmd+pos,argv[j],len);
        pos += len;
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    cmd[pos] = '\0';

    *target = cmd;
    return totlen;
}

void mongoFreeCommand(char *cmd) {
    free(cmd);
}

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

mongoReader *mongoReaderCreate(void) {
    return mongoReaderCreateWithFunctions(&defaultFunctions);
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


/* Helper function for the mongoAppendCommand* family of functions.
 *
 * Write a formatted command to the output buffer. When this family
 * is used, you need to call mongoGetReply yourself to retrieve
 * the reply (or replies in pub/sub).
 */
int __mongoAppendCommand(mongoContext *c, const char *cmd, size_t len) {
    sds newbuf;

    newbuf = sdscatlen(c->obuf,cmd,len);
    if (newbuf == NULL) {
        __mongoSetError(c,MONGO_ERR_OOM,"Out of memory");
        return MONGO_ERR;
    }

    c->obuf = newbuf;
    return MONGO_OK;
}

int mongoAppendFormattedCommand(mongoContext *c, const char *cmd, size_t len) {

    if (__mongoAppendCommand(c, cmd, len) != MONGO_OK) {
        return MONGO_ERR;
    }

    return MONGO_OK;
}

int mongovAppendCommand(mongoContext *c, const char *format, va_list ap) {
    char *cmd;
    int len;

    len = mongovFormatCommand(&cmd,format,ap);
    if (len == -1) {
        __mongoSetError(c,MONGO_ERR_OOM,"Out of memory");
        return MONGO_ERR;
    } else if (len == -2) {
        __mongoSetError(c,MONGO_ERR_OTHER,"Invalid format string");
        return MONGO_ERR;
    }

    if (__mongoAppendCommand(c,cmd,len) != MONGO_OK) {
        free(cmd);
        return MONGO_ERR;
    }

    free(cmd);
    return MONGO_OK;
}

int mongoAppendCommand(mongoContext *c, const char *format, ...) {
    va_list ap;
    int ret;

    va_start(ap,format);
    ret = mongovAppendCommand(c,format,ap);
    va_end(ap);
    return ret;
}

int mongoAppendCommandArgv(mongoContext *c, int argc, const char **argv, const size_t *argvlen) {
    sds cmd;
    int len;

    len = mongoFormatSdsCommandArgv(&cmd,argc,argv,argvlen);
    if (len == -1) {
        __mongoSetError(c,MONGO_ERR_OOM,"Out of memory");
        return MONGO_ERR;
    }

    if (__mongoAppendCommand(c,cmd,len) != MONGO_OK) {
        sdsfree(cmd);
        return MONGO_ERR;
    }

    sdsfree(cmd);
    return MONGO_OK;
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

void *mongovCommand(mongoContext *c, const char *format, va_list ap) {
    if (mongovAppendCommand(c,format,ap) != MONGO_OK)
        return NULL;
    return __mongoBlockForReply(c);
}

void *mongoCommand(mongoContext *c, const char *format, ...) {
    va_list ap;
    void *reply = NULL;
    va_start(ap,format);
    reply = mongovCommand(c,format,ap);
    va_end(ap);
    return reply;
}

void *mongoCommandArgv(mongoContext *c, int argc, const char **argv, const size_t *argvlen) {
    if (mongoAppendCommandArgv(c,argc,argv,argvlen) != MONGO_OK)
        return NULL;
    return __mongoBlockForReply(c);
}
