//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-19
//

#ifndef __HIMONGO_UTILS_H_
#define __HIMONGO_UTILS_H_ 1

#include "sds.h"

#define UNUSED(x) (void)(x)
#define UNUSED2(x1, x2) (void)(x1), (void)(x2)
#define UNUSED3(x1, x2, x3) (void)(x1), (void)(x2), (void)(x3)

void *memdup(void *s, size_t sz);
sds sdscatpack(sds s, char const *fmt, ...);
int snpack(char *buf, size_t offset, size_t size, char const *fmt, ...);
int snunpack(char *buf, size_t offset, size_t size, char const *fmt, ...);

void freev(void **v);

#endif /* _UTILS_H_ */
