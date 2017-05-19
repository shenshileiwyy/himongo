/* endinconv.c -- Endian conversions utilities.
 *
 * This functions are never called directly, but always using the macros
 * defined into endianconv.h, this way we define everything is a non-operation
 * if the arch is already little endian.
 *
 * Redis tries to encode everything as little endian (but a few things that need
 * to be backward compatible are still in big endian) because most of the
 * production environments are little endian, and we have a lot of conversions
 * in a few places because ziplists, intsets, zipmaps, need to be endian-neutral
 * even in memory, since they are serialied on RDB files directly with a single
 * write(2) without other additional steps.
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2011-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <string.h>
#include <stdint.h>

#include "endianconv.h"

/* Toggle the 16 bit unsigned integer pointed by *p from little endian to
 * big endian */
void memrev16(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[1];
    x[1] = t;
}

/* Toggle the 32 bit unsigned integer pointed by *p from little endian to
 * big endian */
void memrev32(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[3];
    x[3] = t;
    t = x[1];
    x[1] = x[2];
    x[2] = t;
}

/* Toggle the 64 bit unsigned integer pointed by *p from little endian to
 * big endian */
void memrev64(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[7];
    x[7] = t;
    t = x[1];
    x[1] = x[6];
    x[6] = t;
    t = x[2];
    x[2] = x[5];
    x[5] = t;
    t = x[3];
    x[3] = x[4];
    x[4] = t;
}

uint16_t intrev16(uint16_t v) {
    memrev16(&v);
    return v;
}

uint32_t intrev32(uint32_t v) {
    memrev32(&v);
    return v;
}

uint64_t intrev64(uint64_t v) {
    memrev64(&v);
    return v;
}

void dump16be(uint16_t v, char *buf) {
    v = intrev16ifle(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 2);
}

void dump32be(uint32_t v, char *buf) {
    v = intrev32ifle(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 4);
}

void dump64be(uint64_t v, char *buf) {
    v = intrev64ifle(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 8);
}

uint16_t load16be(char *buf) {
    uint16_t v = *((uint16_t *)buf);
    return intrev16ifle(v);
}

uint32_t load32be(char *buf) {
    uint32_t v = *((uint32_t *)buf);
    return intrev32ifle(v);
}

uint64_t load64be(char *buf) {
    uint64_t v = *((uint64_t *)buf);
    return intrev64ifle(v);
}

// little endian
void dump16le(uint16_t v, char *buf) {
    v = intrev16ifbe(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 2);
}

void dump32le(uint32_t v, char *buf) {
    v = intrev32ifbe(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 4);
}

void dump64le(uint64_t v, char *buf) {
    v = intrev64ifbe(v);
    char *p = (char *)(&v);
    memcpy(buf, p, 8);
}

uint16_t load16le(char *buf) {
    uint16_t v = *((uint16_t *)buf);
    return intrev16ifbe(v);
}

uint32_t load32le(char *buf) {
    uint32_t v = *((uint32_t *)buf);
    return intrev32ifbe(v);
}

uint64_t load64le(char *buf) {
    uint64_t v = *((uint64_t *)buf);
    return intrev64ifbe(v);
}

#if defined(ENDIANCONV_TEST_MAIN) || defined(CDNS_TEST)
#include <stdio.h>
#include <stdlib.h>
#include "testhelp.h"

#define UNUSED(x) (void)(x)

int endianconvTest(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);
    {
        char buf[32];
        sprintf(buf,"12345678");
        memrev16(buf);
        test_cond("memrev16: ", strcmp(buf, "21345678") == 0);

        sprintf(buf,"12345678");
        memrev32(buf);
        test_cond("memrev32: ", strcmp(buf, "43215678") == 0);

        sprintf(buf,"12345678");
        memrev64(buf);
        test_cond("memrev64: ", strcmp(buf, "87654321") == 0);
    }
    {
        uint16_t d = 0x1234;
        uint16_t data;
        dump16be(d, (char *)(&data));
#if (BYTE_ORDER == BIG_ENDIAN)
        test_cond("dump16be: ", data == 0x1234);
        test_cond("load16be: ", load16be((char *)(&data)) == 0x1234);
#else
        test_cond("dump16be: ", data == 0x3412);
        test_cond("load16be: ", load16be((char *)(&data)) == 0x1234);
#endif
    }

    {
        uint32_t d = 0x12345678;
        uint32_t data;
        dump32be(d, (char *)(&data));
#if (BYTE_ORDER == BIG_ENDIAN)
        test_cond("dump32be: ", data == 0x12345678);
        test_cond("load32be: ", load32be((char *)(&data)) == 0x12345678);
#else
        test_cond("dump32be: ", data == 0x78563412);
        test_cond("load32be: ", load32be((char *)(&data)) == 0x12345678);
#endif
    }
    test_report()
    return 0;
}
#endif

#if defined(ENDIANCONV_TEST_MAIN)
int main(int argc, char *argv[]) {
    endianconvTest(argc, argv);
}
#endif
