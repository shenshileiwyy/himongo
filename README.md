# HIMONGO

Himongo is a light weight C client library for the [Mongodb](https://www.mongodb.com) database.

The library comes with two APIs: *synchronous API* and *asynchronous API*.

## Synchronous API

To consume the synchronous API, there are only a few function calls that need to be introduced:

```c
mongoContext *mongoConnect(const char *ip, int port);
void **mongoFindAll(mongoContext *c, char *db, char *col,
                    bson_t *q, bson_t *rfield, int32_t nrPerQuery);
void freeReplyObject(void *reply);
```

### Connecting

The function `mongoConnect` is used to create a so-called `mongoContext`. The
context is where Himongo holds state for a connection. The `mongoContext`
struct has an integer `err` field that is non-zero when the connection is in
an error state. The field `errstr` will contain a string with a description of
the error. More information on errors can be found in the **Errors** section.
After trying to connect to Mongo using `mongoConnect` you should
check the `err` field to see if establishing the connection was successful:
```c
mongoContext *c = mongoConnect("127.0.0.1", 6379);
if (c == NULL || c->err) {
    if (c) {
        printf("Error: %s\n", c->errstr);
        // handle error
    } else {
        printf("Can't allocate mongo context\n");
    }
}
```

*Note: A `mongoContext` is not thread-safe.*

### Sending commands

the following API is used to do CRUD operations synchronously.
```c
reply = mongoCommand(context, "SET foo bar");
```

### Using replies

The return value holds a reply when the database operation was
successfully executed. When an error occurs, the return value is `NULL` and
the `err` field in the context will be set (see section on **Errors**).
Once an error is returned the context cannot be reused and you should set up
a new connection.

The standard replies of database operation are of the type `mongoReply`.
`mongoReply` has the same fields as `OP_REPLY` message.

```c
typedef struct {
    int32_t messageLength;                      
    int32_t requestID;                          
    int32_t responseTo;                         
    int32_t opCode
    
    int32_t responseFlags;
    int64_t cursorID;
    int32_t startingFrom;
    int32_t numberReturned;
    bson_t **docs;
} mongoReply;
```

Replies should be freed using the `freeReplyObject()` function.
Note that this function will take care of freeing the bson documents 
contained in this object.

**Important:** the current version of himongo (0.10.0) frees replies when the
asynchronous API is used. This means you should not call `freeReplyObject` when
you use this API. The reply is cleaned up by himongo _after_ the callback
returns. 

### Cleaning up

To disconnect and free the context the following function can be used:
```c
void mongoFree(mongoContext *c);
```
This function immediately closes the socket and then frees the allocations done in
creating the context.

### Pipelining

you can use the following API to append request message to the
output buffer of mongoContext object.

```c 
int mongoAppendUpdateMsg(mongoContext *c, char *db, char *col, int32_t flags,
                         bson_t *selector, bson_t *update);
int mongoAppendInsertMsg(mongoContext *c, int32_t flags, char *db, char *col,
                         bson_t **docs, size_t nr_docs);
int mongoAppendQueryMsg(mongoContext *c, int32_t flags, char *db, char *col,
                        int nrSkip, int nrReturn, bson_t *q, bson_t *rfields);
int mongoAppendGetMoreMsg(mongoContext *c, char *db, char *col, int32_t nrReturn, int64_t cursorID);
int mongoAppendDeleteMsg(mongoContext *c, char *db, char *col, int32_t flags, bson_t *selector);
int mongoAppendKillCursorsMsg(mongoContext *c, int32_t nrID, int64_t *IDs);
```

after appending the request message, you can use `mongoGetReply`
to get reply.

**pls note: only Query message has response**

The following examples shows a simple pipeline (resulting in only a single call to `write(2)` and
a single call to `read(2)`):
```c
mongoReply *reply;
mongoAppendQueryMsg(c, 0, "test, "col1, 0, 0, NULL, NULL);
mongoAppendQueryMsg(c, 0, "test, "col2, 0, 0, NULL, NULL);
mongoGetReply(context,&reply); // reply for SET
freeReplyObject(reply);
mongoGetReply(context,&reply); // reply for GET
freeReplyObject(reply);
```

### Errors

When a function call is not successful, depending on the function either `NULL` or `MONGO_ERR` is
returned. The `err` field inside the context will be non-zero and set to one of the
following constants:

* **`MONGO_ERR_IO`**:
    There was an I/O error while creating the connection, trying to write
    to the socket or read from the socket. If you included `errno.h` in your
    application, you can use the global `errno` variable to find out what is
    wrong.

* **`MONGO_ERR_EOF`**:
    The server closed the connection which resulted in an empty read.

* **`MONGO_ERR_PROTOCOL`**:
    There was an error while parsing the protocol.

* **`MONGO_ERR_OTHER`**:
    Any other error. Currently, it is only used when a specified hostname to connect
    to cannot be resolved.

In every case, the `errstr` field in the context will be set to hold a string representation
of the error.

## Asynchronous API

Himongo comes with an asynchronous API that works easily with any event library.
Examples are bundled that show using Himongo with [libev](http://software.schmorp.de/pkg/libev.html)
and [libevent](http://monkey.org/~provos/libevent/).

### Connecting

The function `mongoAsyncConnect` can be used to establish a non-blocking connection to
Mongo. It returns a pointer to the newly created `mongoAsyncContext` struct. The `err` field
should be checked after creation to see if there were errors creating the connection.
Because the connection that will be created is non-blocking, the kernel is not able to
instantly return if the specified host and port is able to accept a connection.

*Note: A `mongoAsyncContext` is not thread-safe.*

```c
mongoAsyncContext *c = mongoAsyncConnect("127.0.0.1", 6379);
if (c->err) {
    printf("Error: %s\n", c->errstr);
    // handle error
}
```

The asynchronous context can hold a disconnect callback function that is called when the
connection is disconnected (either because of an error or per user request). This function should
have the following prototype:
```c
void(const mongoAsyncContext *c, int status);
```
On a disconnect, the `status` argument is set to `MONGO_OK` when disconnection was initiated by the
user, or `MONGO_ERR` when the disconnection was caused by an error. When it is `MONGO_ERR`, the `err`
field in the context can be accessed to find out the cause of the error.

The context object is always freed after the disconnect callback fired. When a reconnect is needed,
the disconnect callback is a good point to do so.

Setting the disconnect callback can only be done once per context. For subsequent calls it will
return `MONGO_ERR`. The function to set the disconnect callback has the following prototype:
```c
int mongoAsyncSetDisconnectCallback(mongoAsyncContext *ac, mongoDisconnectCallback *fn);
```
### Sending commands and their callbacks

In an asynchronous context, commands are automatically pipelined due to the nature of an event loop.
Therefore, unlike the synchronous API, there is only a single way to send commands.
Because commands are sent to Mongo asynchronously, issuing a command requires a callback function
that is called when the reply is received. Reply callbacks should have the following prototype:
```c
void(mongoAsyncContext *c, void *reply, void *privdata);
```
The `privdata` argument can be used to curry arbitrary data to the callback from the point where
the command is initially queued for execution.

The functions that can be used to issue commands in an asynchronous context are:
```c
int mongoAsyncCommand(
  mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata,
  const char *format, ...);
int mongoAsyncCommandArgv(
  mongoAsyncContext *ac, mongoCallbackFn *fn, void *privdata,
  int argc, const char **argv, const size_t *argvlen);
```
Both functions work like their blocking counterparts. The return value is `MONGO_OK` when the command
was successfully added to the output buffer and `MONGO_ERR` otherwise. Example: when the connection
is being disconnected per user-request, no new commands may be added to the output buffer and `MONGO_ERR` is
returned on calls to the `mongoAsyncCommand` family.

If the reply for a command with a `NULL` callback is read, it is immediately freed. When the callback
for a command is non-`NULL`, the memory is freed immediately following the callback: the reply is only
valid for the duration of the callback.

All pending callbacks are called with a `NULL` reply when the context encountered an error.

### Disconnecting

An asynchronous connection can be terminated using:
```c
void mongoAsyncDisconnect(mongoAsyncContext *ac);
```
When this function is called, the connection is **not** immediately terminated. Instead, new
commands are no longer accepted and the connection is only terminated when all pending commands
have been written to the socket, their respective replies have been read and their respective
callbacks have been executed. After this, the disconnection callback is executed with the
`MONGO_OK` status and the context object is freed.

### Hooking it up to event library *X*

There are a few hooks that need to be set on the context object after it is created.
See the `adapters/` directory for bindings to *libev* and *libevent*.

## Reply parsing API

Himongo comes with a reply parsing API that makes it easy for writing higher
level language bindings.

The reply parsing API consists of the following functions:
```c
mongoReader *mongoReaderCreate(void);
void mongoReaderFree(mongoReader *reader);
int mongoReaderFeed(mongoReader *reader, const char *buf, size_t len);
int mongoReaderGetReply(mongoReader *reader, void **reply);
```
The same set of functions are used internally by himongo when creating a
normal Mongo context, the above API just exposes it to the user for a direct
usage.

### Usage

The function `mongoReaderCreate` creates a `mongoReader` structure that holds a
buffer with unparsed data and state for the protocol parser.

Incoming data -- most likely from a socket -- can be placed in the internal
buffer of the `mongoReader` using `mongoReaderFeed`. This function will make a
copy of the buffer pointed to by `buf` for `len` bytes. This data is parsed
when `mongoReaderGetReply` is called. This function returns an integer status
and a reply object (as described above) via `void **reply`. The returned status
can be either `MONGO_OK` or `MONGO_ERR`, where the latter means something went
wrong (either a protocol error, or an out of memory error).

The parser limits the level of nesting for multi bulk payloads to 7. If the
multi bulk nesting level is higher than this, the parser returns an error.

### Customizing replies

The function `mongoReaderGetReply` creates `mongoReply` and makes the function
argument `reply` point to the created `mongoReply` variable. For instance, if
the response of type `MONGO_REPLY_STATUS` then the `str` field of `mongoReply`
will hold the status as a vanilla C string. However, the functions that are
responsible for creating instances of the `mongoReply` can be customized by
setting the `fn` field on the `mongoReader` struct. This should be done
immediately after creating the `mongoReader`.

### Reader max buffer

Both when using the Reader API directly or when using it indirectly via a
normal Mongo context, the mongoReader structure uses a buffer in order to
accumulate data from the server.
Usually this buffer is destroyed when it is empty and is larger than 16
KiB in order to avoid wasting memory in unused buffers

However when working with very big payloads destroying the buffer may slow
down performances considerably, so it is possible to modify the max size of
an idle buffer changing the value of the `maxbuf` field of the reader structure
to the desired value. The special value of 0 means that there is no maximum
value for an idle buffer, so the buffer will never get freed.

For instance if you have a normal Mongo context you can set the maximum idle
buffer to zero (unlimited) just with:
```c
context->reader->maxbuf = 0;
```
This should be done only in order to maximize performances when working with
large payloads. The context should be set back to `MONGO_READER_MAX_BUF` again
as soon as possible in order to prevent allocation of useless memory.

## AUTHORS

Himongo was written by Yu Yang (yyangplus at gmail) and is released under the BSD license. himongo borrows a lot of code from hiredis, many thanks to hiredis' authors.
