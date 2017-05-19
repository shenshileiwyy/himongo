#ifndef __NET_H
#define __NET_H

#include "himongo.h"

#if defined(__sun)
#define AF_LOCAL AF_UNIX
#endif

int mongoCheckSocketError(mongoContext *c);
int mongoContextSetTimeout(mongoContext *c, const struct timeval tv);
int mongoContextConnectTcp(mongoContext *c, const char *addr, int port, const struct timeval *timeout);
int mongoContextConnectBindTcp(mongoContext *c, const char *addr, int port,
                               const struct timeval *timeout,
                               const char *source_addr);
int mongoContextConnectUnix(mongoContext *c, const char *path, const struct timeval *timeout);
int mongoKeepAlive(mongoContext *c, int interval);

#endif
