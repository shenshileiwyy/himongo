//
//  Created by Дмитрий Бахвалов on 13.07.15.
//  Copyright (c) 2015 Dmitry Bakhvalov. All rights reserved.
//

#ifndef __HIMONGO_MACOSX_H__
#define __HIMONGO_MACOSX_H__

#include <CoreFoundation/CoreFoundation.h>

#include "../himongo.h"
#include "../async.h"

typedef struct {
    mongoAsyncContext *context;
    CFSocketRef socketRef;
    CFRunLoopSourceRef sourceRef;
} MongoRunLoop;

static int freeMongoRunLoop(MongoRunLoop* mongoRunLoop) {
    if( mongoRunLoop != NULL ) {
        if( mongoRunLoop->sourceRef != NULL ) {
            CFRunLoopSourceInvalidate(mongoRunLoop->sourceRef);
            CFRelease(mongoRunLoop->sourceRef);
        }
        if( mongoRunLoop->socketRef != NULL ) {
            CFSocketInvalidate(mongoRunLoop->socketRef);
            CFRelease(mongoRunLoop->socketRef);
        }
        free(mongoRunLoop);
    }
    return MONGO_ERR;
}

static void mongoMacOSAddRead(void *privdata) {
    MongoRunLoop *mongoRunLoop = (MongoRunLoop*)privdata;
    CFSocketEnableCallBacks(mongoRunLoop->socketRef, kCFSocketReadCallBack);
}

static void mongoMacOSDelRead(void *privdata) {
    MongoRunLoop *mongoRunLoop = (MongoRunLoop*)privdata;
    CFSocketDisableCallBacks(mongoRunLoop->socketRef, kCFSocketReadCallBack);
}

static void mongoMacOSAddWrite(void *privdata) {
    MongoRunLoop *mongoRunLoop = (MongoRunLoop*)privdata;
    CFSocketEnableCallBacks(mongoRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void mongoMacOSDelWrite(void *privdata) {
    MongoRunLoop *mongoRunLoop = (MongoRunLoop*)privdata;
    CFSocketDisableCallBacks(mongoRunLoop->socketRef, kCFSocketWriteCallBack);
}

static void mongoMacOSCleanup(void *privdata) {
    MongoRunLoop *mongoRunLoop = (MongoRunLoop*)privdata;
    freeMongoRunLoop(mongoRunLoop);
}

static void mongoMacOSAsyncCallback(CFSocketRef __unused s, CFSocketCallBackType callbackType, CFDataRef __unused address, const void __unused *data, void *info) {
    mongoAsyncContext* context = (mongoAsyncContext*) info;

    switch (callbackType) {
        case kCFSocketReadCallBack:
            mongoAsyncHandleRead(context);
            break;

        case kCFSocketWriteCallBack:
            mongoAsyncHandleWrite(context);
            break;

        default:
            break;
    }
}

static int mongoMacOSAttach(mongoAsyncContext *mongoAsyncCtx, CFRunLoopRef runLoop) {
    mongoContext *mongoCtx = &(mongoAsyncCtx->c);

    /* Nothing should be attached when something is already attached */
    if( mongoAsyncCtx->ev.data != NULL ) return MONGO_ERR;

    MongoRunLoop* mongoRunLoop = (MongoRunLoop*) calloc(1, sizeof(MongoRunLoop));
    if( !mongoRunLoop ) return MONGO_ERR;

    /* Setup mongo stuff */
    mongoRunLoop->context = mongoAsyncCtx;

    mongoAsyncCtx->ev.addRead  = mongoMacOSAddRead;
    mongoAsyncCtx->ev.delRead  = mongoMacOSDelRead;
    mongoAsyncCtx->ev.addWrite = mongoMacOSAddWrite;
    mongoAsyncCtx->ev.delWrite = mongoMacOSDelWrite;
    mongoAsyncCtx->ev.cleanup  = mongoMacOSCleanup;
    mongoAsyncCtx->ev.data     = mongoRunLoop;

    /* Initialize and install read/write events */
    CFSocketContext socketCtx = { 0, mongoAsyncCtx, NULL, NULL, NULL };

    mongoRunLoop->socketRef = CFSocketCreateWithNative(NULL, mongoCtx->fd,
                                                       kCFSocketReadCallBack | kCFSocketWriteCallBack,
                                                       mongoMacOSAsyncCallback,
                                                       &socketCtx);
    if( !mongoRunLoop->socketRef ) return freeMongoRunLoop(mongoRunLoop);

    mongoRunLoop->sourceRef = CFSocketCreateRunLoopSource(NULL, mongoRunLoop->socketRef, 0);
    if( !mongoRunLoop->sourceRef ) return freeMongoRunLoop(mongoRunLoop);

    CFRunLoopAddSource(runLoop, mongoRunLoop->sourceRef, kCFRunLoopDefaultMode);

    return MONGO_OK;
}

#endif
