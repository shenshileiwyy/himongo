//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-19
//

#include "utils.h"

/*!
 * this function will dump all the arguments to sds, the format is specified by fmt.
 * it is similar to the struct package in python.
 *
 * @param s  the sds object
 * @param fmt: the format of the arguments, format used to specify the byte order and data size
 *        byte order:
 *             1. =(native endian)
 *             2. >(big endian)
 *             3. <(little endian)
 *        data size:
 *             1. b(byte)      1 byte
 *             2. h(short)     2 bytes
 *             3. i(int)       4 bytes
 *             4. q(long long) 8 bytes
 *             5. s(string)    using strlen(s)+1 to get length
 *             6. m(memory)    an extra argument is needed to provide the length
 *       pls note if byte order is ignored, then it will use the previous byte order.
 * @param ...
 * @return the new sds object
 */
sds sdscatpack(sds s, char const *fmt, ...) {
    const char *f = fmt;

    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;

    char *ss;
    size_t mem_len;
    va_list ap;
    va_start(ap, fmt);

    bool need_rev = false;
    while(*f) {
        switch(*f) {
        case '=':
            need_rev = false;
            f++;
            break;
        case '<':  // little endian
#if (BYTE_ORDER == BIG_ENDIAN)
            need_rev = true;
#else
            need_rev = false;
#endif
            f++;
            break;
        case '>':  // big endian
        case '!':
#if (BYTE_ORDER == LITTLE_ENDIAN)
            need_rev = true;
#else
            meed_rev = false;
#endif
            f++;
            break;
        default:
            break;
        }
        switch(*f) {
        case 'b':
        case 'B':
            u8 = (uint8_t )va_arg(ap, int);
            s = sdscatlen(s, &u8, 1);
            break;
        case 'h':  //signed short
        case 'H':
            u16 = (uint16_t )va_arg(ap, int);
            if (need_rev) u16 = intrev16(u16);
            s = sdscatlen(s, &u16, 2);
            break;
        case 'i':
        case 'I':
            u32 = (uint32_t )va_arg(ap, int);
            if (need_rev) u32 = intrev32(u32);
            s = sdscatlen(s, &u32, 4);
            break;
        case 'q':
        case 'Q':
            u64 = (uint64_t )va_arg(ap, long long);
            if (need_rev) u64 = intrev64(u64);
            s = sdscatlen(s, &u64, 8);
            break;
        case 's':
        case 'S':
            ss = va_arg(ap, char *);
            s = sdscat(s, ss);
            break;
        case 'm':
        case 'M':
            ss = va_arg(ap, char *);
            mem_len = va_arg(ap, size_t);
            if (mem_len == 0) break;
            s = sdscatlen(s, ss, mem_len);
            break;
        default:
            LOG_FATAL("BUG: unknown format %s", fmt);
        }
        f++;
    }
    va_end(ap);
    return s;
}

int snunpack(char *buf, size_t offset, size_t size, char const *fmt, ...) {
    char *ptr = buf + offset;
    const char *f = fmt;
    size_t remain = size - offset;
    int result;

    uint8_t*  u8;
    uint16_t* u16;
    uint32_t* u32;
    uint64_t* u64;
    char **ss;
    size_t mem_len, ss_len;

    va_list ap;
    va_start(ap, fmt);

    bool need_rev = false;
    while(*f) {
        switch(*f) {
        case '=':
            need_rev = false;
            f++;
            break;
        case '<':  // little endian
#if (BYTE_ORDER == BIG_ENDIAN)
            need_rev = true;
#else
            need_rev = false;
#endif
            f++;
            break;
        case '>':  // big endian
        case '!':
#if (BYTE_ORDER == LITTLE_ENDIAN)
            need_rev = true;
#else
            need_rev = false;
#endif
            f++;
            break;
        default:
            break;
        }
        switch(*f) {
        case 'b':
        case 'B':
            if (remain < 1) goto error;
            u8 = (uint8_t*)va_arg(ap, int*);
            memcpy(u8, ptr, 1);
            ptr++;
            remain--;
            break;
        case 'h':  //signed short
        case 'H':
            if (remain < 2) goto error;
            u16 = (uint16_t *)va_arg(ap, int*);
            memcpy(u16, ptr, 2);
            if (need_rev) *u16 = intrev16(*u16);
            ptr += 2;
            remain -= 2;
            break;
        case 'i':
        case 'I':
            if (remain < 4) goto error;
            u32 = (uint32_t *)va_arg(ap, int*);
            memcpy(u32, ptr, 4);
            if (need_rev) *u32 = intrev32(*u32);
            ptr += 4;
            remain -= 4;
            break;
        case 'q':
        case 'Q':
            if (remain < 8) goto error;
            u64 = (uint64_t *)va_arg(ap, long long*);
            memcpy(u64, ptr, 8);
            if (need_rev) *u64 = intrev64(*u64);
            ptr += 8;
            remain -= 4;
            break;
        case 's':
            ss = va_arg(ap, char **);
            ss_len = strlen(ptr) + 1;
            *ss = ptr;
            ptr += ss_len;
            remain -= ss_len;
            break;
        case 'S':
            ss = va_arg(ap, char **);
            ss_len = strlen(ptr) + 1;
            *ss = zstrdup(ptr);
            ptr += ss_len;
            remain -= ss_len;
            break;
        case 'm':
            ss = va_arg(ap, char **);
            mem_len = va_arg(ap, size_t);
            if (remain < mem_len) goto error;
            if (mem_len == 0) break;
            *ss = ptr;
            ptr += mem_len;
            remain -= mem_len;
            break;
        case 'M':
            ss = va_arg(ap, char **);
            mem_len = va_arg(ap, size_t);
            if (remain < mem_len) goto error;
            if (mem_len == 0) break;
            *ss = zmemdup(ptr, mem_len);
            ptr += mem_len;
            remain -= mem_len;
            break;
        default:
            LOG_FATAL("BUG: unknown format %s", fmt);
        }
        f++;
    }
    result = (int)(size-remain);
    goto ok;
error:
    result = -1;
ok:
    va_end(ap);
    return result;
}
