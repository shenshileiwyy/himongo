/*-
 * Copyright (C) 2014 Pietro Cerutti <gahr@gahr.ch>
 *
 * Mongotribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Mongotributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Mongotributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __HIMONGO_QT_H__
#define __HIMONGO_QT_H__
#include <QSocketNotifier>
#include "../async.h"

static void MongoQtAddRead(void *);
static void MongoQtDelRead(void *);
static void MongoQtAddWrite(void *);
static void MongoQtDelWrite(void *);
static void MongoQtCleanup(void *);

class MongoQtAdapter : public QObject {

    Q_OBJECT

    friend
    void MongoQtAddRead(void * adapter) {
        MongoQtAdapter * a = static_cast<MongoQtAdapter *>(adapter);
        a->addRead();
    }

    friend
    void MongoQtDelRead(void * adapter) {
        MongoQtAdapter * a = static_cast<MongoQtAdapter *>(adapter);
        a->delRead();
    }

    friend
    void MongoQtAddWrite(void * adapter) {
        MongoQtAdapter * a = static_cast<MongoQtAdapter *>(adapter);
        a->addWrite();
    }

    friend
    void MongoQtDelWrite(void * adapter) {
        MongoQtAdapter * a = static_cast<MongoQtAdapter *>(adapter);
        a->delWrite();
    }

    friend
    void MongoQtCleanup(void * adapter) {
        MongoQtAdapter * a = static_cast<MongoQtAdapter *>(adapter);
        a->cleanup();
    }

    public:
        MongoQtAdapter(QObject * parent = 0)
            : QObject(parent), m_ctx(0), m_read(0), m_write(0) { }

        ~MongoQtAdapter() {
            if (m_ctx != 0) {
                m_ctx->ev.data = NULL;
            }
        }

        int setContext(mongoAsyncContext * ac) {
            if (ac->ev.data != NULL) {
                return MONGO_ERR;
            }
            m_ctx = ac;
            m_ctx->ev.data = this;
            m_ctx->ev.addRead = MongoQtAddRead;
            m_ctx->ev.delRead = MongoQtDelRead;
            m_ctx->ev.addWrite = MongoQtAddWrite;
            m_ctx->ev.delWrite = MongoQtDelWrite;
            m_ctx->ev.cleanup = MongoQtCleanup;
            return MONGO_OK;
        }

    private:
        void addRead() {
            if (m_read) return;
            m_read = new QSocketNotifier(m_ctx->c.fd, QSocketNotifier::Read, 0);
            connect(m_read, SIGNAL(activated(int)), this, SLOT(read()));
        }

        void delRead() {
            if (!m_read) return;
            delete m_read;
            m_read = 0;
        }

        void addWrite() {
            if (m_write) return;
            m_write = new QSocketNotifier(m_ctx->c.fd, QSocketNotifier::Write, 0);
            connect(m_write, SIGNAL(activated(int)), this, SLOT(write()));
        }

        void delWrite() {
            if (!m_write) return;
            delete m_write;
            m_write = 0;
        }

        void cleanup() {
            delRead();
            delWrite();
        }

    private slots:
        void read() { mongoAsyncHandleRead(m_ctx); }
        void write() { mongoAsyncHandleWrite(m_ctx); }

    private:
        mongoAsyncContext * m_ctx;
        QSocketNotifier * m_read;
        QSocketNotifier * m_write;
};

#endif /* !__HIMONGO_QT_H__ */
