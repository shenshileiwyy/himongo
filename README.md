# HIMONGO

Himongo is a light weight C client library for the [Mongodb](https://www.mongodb.com) database.

The library comes with two APIs: *synchronous API* and *asynchronous API*.

## Synchronous API

To consume the synchronous API, there are only a few function calls that need to be introduced:

```c
mongoContext *mongoConnect(const char *ip, int port);
void *mongoCommand(mongoContext *c, const char *format, ...);
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

There are several ways to issue commands to Mongo. The first that will be introduced is
`mongoCommand`. This function takes a format similar to printf. In the simplest form,
it is used like this:
```c
reply = mongoCommand(context, "SET foo bar");
```

The specifier `%s` interpolates a string in the command, and uses `strlen` to
determine the length of the string:
```c
reply = mongoCommand(context, "SET foo %s", value);
```
When you need to pass binary safe strings in a command, the `%b` specifier can be
used. Together with a pointer to the string, it requires a `size_t` length argument
of the string:
```c
reply = mongoCommand(context, "SET foo %b", value, (size_t) valuelen);
```
Internally, Himongo splits the command in different arguments and will
convert it to the protocol used to communicate with Mongo.
One or more spaces separates arguments, so you can use the specifiers
anywhere in an argument:
```c
reply = mongoCommand(context, "SET key:%s %s", myid, value);
```

### Using replies

The return value of `mongoCommand` holds a reply when the command was
successfully executed. When an error occurs, the return value is `NULL` and
the `err` field in the context will be set (see section on **Errors**).
Once an error is returned the context cannot be reused and you should set up
a new connection.

The standard replies that `mongoCommand` are of the type `mongoReply`. The
`type` field in the `mongoReply` should be used to test what kind of reply
was received:

* **`MONGO_REPLY_STATUS`**:
    * The command replied with a status reply. The status string can be accessed using `reply->str`.
      The length of this string can be accessed using `reply->len`.

* **`MONGO_REPLY_ERROR`**:
    *  The command replied with an error. The error string can be accessed identical to `MONGO_REPLY_STATUS`.

* **`MONGO_REPLY_INTEGER`**:
    * The command replied with an integer. The integer value can be accessed using the
      `reply->integer` field of type `long long`.

* **`MONGO_REPLY_NIL`**:
    * The command replied with a **nil** object. There is no data to access.

* **`MONGO_REPLY_STRING`**:
    * A bulk (string) reply. The value of the reply can be accessed using `reply->str`.
      The length of this string can be accessed using `reply->len`.

* **`MONGO_REPLY_ARRAY`**:
    * A multi bulk reply. The number of elements in the multi bulk reply is stored in
      `reply->elements`. Every element in the multi bulk reply is a `mongoReply` object as well
      and can be accessed via `reply->element[..index..]`.
      Mongo may reply with nested arrays but this is fully supported.

Replies should be freed using the `freeReplyObject()` function.
Note that this function will take care of freeing sub-reply objects
contained in arrays and nested arrays, so there is no need for the user to
free the sub replies (it is actually harmful and will corrupt the memory).

**Important:** the current version of himongo (0.10.0) frees replies when the
asynchronous API is used. This means you should not call `freeReplyObject` when
you use this API. The reply is cleaned up by himongo _after_ the callback
returns. This behavior will probably change in future releases, so make sure to
keep an eye on the changelog when upgrading (see issue #39).

### Cleaning up

To disconnect and free the context the following function can be used:
```c
void mongoFree(mongoContext *c);
```
This function immediately closes the socket and then frees the allocations done in
creating the context.

### Sending commands (cont'd)

Together with `mongoCommand`, the function `mongoCommandArgv` can be used to issue commands.
It has the following prototype:
```c
void *mongoCommandArgv(mongoContext *c, int argc, const char **argv, const size_t *argvlen);
```
It takes the number of arguments `argc`, an array of strings `argv` and the lengths of the
arguments `argvlen`. For convenience, `argvlen` may be set to `NULL` and the function will
use `strlen(3)` on every argument to determine its length. Obviously, when any of the arguments
need to be binary safe, the entire array of lengths `argvlen` should be provided.

The return value has the same semantic as `mongoCommand`.

### Pipelining

To explain how Himongo supports pipelining in a blocking connection, there needs to be
understanding of the internal execution flow.

When any of the functions in the `mongoCommand` family is called, Himongo first formats the
command according to the Mongo protocol. The formatted command is then put in the output buffer
of the context. This output buffer is dynamic, so it can hold any number of commands.
After the command is put in the output buffer, `mongoGetReply` is called. This function has the
following two execution paths:

1. The input buffer is non-empty:
    * Try to parse a single reply from the input buffer and return it
    * If no reply could be parsed, continue at *2*
2. The input buffer is empty:
    * Write the **entire** output buffer to the socket
    * Read from the socket until a single reply could be parsed

The function `mongoGetReply` is exported as part of the Himongo API and can be used when a reply
is expected on the socket. To pipeline commands, the only things that needs to be done is
filling up the output buffer. For this cause, two commands can be used that are identical
to the `mongoCommand` family, apart from not returning a reply:
```c
void mongoAppendCommand(mongoContext *c, const char *format, ...);
void mongoAppendCommandArgv(mongoContext *c, int argc, const char **argv, const size_t *argvlen);
```
After calling either function one or more times, `mongoGetReply` can be used to receive the
subsequent replies. The return value for this function is either `MONGO_OK` or `MONGO_ERR`, where
the latter means an error occurred while reading a reply. Just as with the other commands,
the `err` field in the context can be used to find out what the cause of this error is.

The following examples shows a simple pipeline (resulting in only a single call to `write(2)` and
a single call to `read(2)`):
```c
mongoReply *reply;
mongoAppendCommand(context,"SET foo bar");
mongoAppendCommand(context,"GET foo");
mongoGetReply(context,&reply); // reply for SET
freeReplyObject(reply);
mongoGetReply(context,&reply); // reply for GET
freeReplyObject(reply);
```
This API can also be used to implement a blocking subscriber:
```c
reply = mongoCommand(context,"SUBSCRIBE foo");
freeReplyObject(reply);
while(mongoGetReply(context,&reply) == MONGO_OK) {
    // consume message
    freeReplyObject(reply);
}
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

For example, [himongo-rb](https://github.com/pietern/himongo-rb/blob/master/ext/himongo_ext/reader.c)
uses customized reply object functions to create Ruby objects.

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
