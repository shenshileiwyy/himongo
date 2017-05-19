//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-19
//

#ifndef __HIMONGO_UTILS_H_
#define __HIMONGO_UTILS_H_ 1

#include "sds.h"

sds sdscatpack(sds s, char const *fmt, ...);
int snunpack(char *buf, size_t offset, size_t size, char const *fmt, ...);

#endif /* _UTILS_H_ */
