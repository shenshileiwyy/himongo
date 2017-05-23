#ifndef __HIMONGO_GLIB_H__
#define __HIMONGO_GLIB_H__

#include <glib.h>

#include "../himongo.h"
#include "../async.h"

typedef struct
{
    GSource source;
    mongoAsyncContext *ac;
    GPollFD poll_fd;
} MongoSource;

static void
mongo_source_add_read (gpointer data)
{
    MongoSource *source = (MongoSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
mongo_source_del_read (gpointer data)
{
    MongoSource *source = (MongoSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_IN;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
mongo_source_add_write (gpointer data)
{
    MongoSource *source = (MongoSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events |= G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
mongo_source_del_write (gpointer data)
{
    MongoSource *source = (MongoSource *)data;
    g_return_if_fail(source);
    source->poll_fd.events &= ~G_IO_OUT;
    g_main_context_wakeup(g_source_get_context((GSource *)data));
}

static void
mongo_source_cleanup (gpointer data)
{
    MongoSource *source = (MongoSource *)data;

    g_return_if_fail(source);

    mongo_source_del_read(source);
    mongo_source_del_write(source);
    /*
     * It is not our responsibility to remove ourself from the
     * current main loop. However, we will remove the GPollFD.
     */
    if (source->poll_fd.fd >= 0) {
        g_source_remove_poll((GSource *)data, &source->poll_fd);
        source->poll_fd.fd = -1;
    }
}

static gboolean
mongo_source_prepare (GSource *source,
                      gint    *timeout_)
{
    MongoSource *mongo = (MongoSource *)source;
    *timeout_ = -1;
    return !!(mongo->poll_fd.events & mongo->poll_fd.revents);
}

static gboolean
mongo_source_check (GSource *source)
{
    MongoSource *mongo = (MongoSource *)source;
    return !!(mongo->poll_fd.events & mongo->poll_fd.revents);
}

static gboolean
mongo_source_dispatch (GSource      *source,
                       GSourceFunc   callback,
                       gpointer      user_data)
{
    MongoSource *mongo = (MongoSource *)source;

    if ((mongo->poll_fd.revents & G_IO_OUT)) {
        mongoAsyncHandleWrite(mongo->ac);
        mongo->poll_fd.revents &= ~G_IO_OUT;
    }

    if ((mongo->poll_fd.revents & G_IO_IN)) {
        mongoAsyncHandleRead(mongo->ac);
        mongo->poll_fd.revents &= ~G_IO_IN;
    }

    if (callback) {
        return callback(user_data);
    }

    return TRUE;
}

static void
mongo_source_finalize (GSource *source)
{
    MongoSource *mongo = (MongoSource *)source;

    if (mongo->poll_fd.fd >= 0) {
        g_source_remove_poll(source, &mongo->poll_fd);
        mongo->poll_fd.fd = -1;
    }
}

static GSource *
mongo_source_new (mongoAsyncContext *ac)
{
    static GSourceFuncs source_funcs = {
        .prepare  = mongo_source_prepare,
        .check     = mongo_source_check,
        .dispatch = mongo_source_dispatch,
        .finalize = mongo_source_finalize,
    };
    mongoContext *c = &ac->c;
    MongoSource *source;

    g_return_val_if_fail(ac != NULL, NULL);

    source = (MongoSource *)g_source_new(&source_funcs, sizeof *source);
    source->ac = ac;
    source->poll_fd.fd = c->fd;
    source->poll_fd.events = 0;
    source->poll_fd.revents = 0;
    g_source_add_poll((GSource *)source, &source->poll_fd);

    ac->ev.addRead = mongo_source_add_read;
    ac->ev.delRead = mongo_source_del_read;
    ac->ev.addWrite = mongo_source_add_write;
    ac->ev.delWrite = mongo_source_del_write;
    ac->ev.cleanup = mongo_source_cleanup;
    ac->ev.data = source;

    return (GSource *)source;
}

#endif /* __HIMONGO_GLIB_H__ */
