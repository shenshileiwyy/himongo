//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-24
//
#include "../himongo.h"
#include <bson.h>

int test_json ()
{
    bson_t bson;
    char *str;

    bson_init (&bson);
    BSON_APPEND_UTF8 (&bson, "0", "foo");
    BSON_APPEND_UTF8 (&bson, "1", "bar");

    str = bson_as_json (&bson, NULL);
    /* Prints
     * { "0" : "foo", "1" : "bar" }
     */
    printf ("%s\n", str);
    bson_free (str);

    bson_destroy (&bson);
}

int main(int argc, char *argv[])
{
    test_json();
    char buf[4096];
    struct replyMsg *reply;
    struct mongoContext *c = mongoConnect("127.0.0.1", 27017);
    char **pp;
    if (c == NULL || c->err) {
        if (c) {
            printf("Error: %s\n", c->errstr);
            // handle error
        } else {
            printf("Can't allocate mongo context\n");
        }
    }
    reply = mongoListCollections(c, (char *)"zone");
    replyMsgToStr(reply, buf, 4096);
    printf("=============\n%s\n========\n", buf);
    // reply = mongoListCollections(c, (char *)"test");
    // bson_iter_t it;
    // bson_iter_t it2;
    // bson_iter_t it3;
    // bson_iter_t it4;
    // reply = mongoListCollections(c, (char *)"test");
    // char *name;
    // if (!bson_iter_init(&it, reply->docs[0]))
    //     printf("init iter error\n");
    // if (!bson_iter_find_descendant(&it, "cursor.firstBatch", &it2))
    //     printf("can't find name field\n");
    // bson_iter_recurse (&it2, &it3);
    // while (bson_iter_next (&it3)) {
    //     bson_iter_recurse(&it3, &it4);
    //     char *name;
    //     if (bson_iter_find(&it4, "name") &&
    //         BSON_ITER_HOLDS_UTF8(&it4) && (name = (char*)bson_iter_utf8(&it4, NULL))) {
    //         printf("name: %s\n", name);
    //     }
    //     // if (BSON_ITER_HOLDS_DOCUMENT(&it3) &&
    //     //         bson_iter_find())
    //     printf ("Found key \"%s\" in sub document.\n", bson_iter_key (&it3));
    // }
    // else {
    //     if (BSON_ITER_HOLDS_UTF8(&it2) && (name = (char*)bson_iter_utf8(&it2, NULL))) {
    //         printf("find name %s\n", name);
    //     }
    // }

    // char **namev = mongoGetCollectionNames(c, (char *)"test");
    // int i;
    // for (i = 0,pp=namev; *pp != NULL; ++pp, ++i) printf("%d: %s\n", i, *pp);

    void **rev = mongoFindAll(c, "zone", "example.com", NULL, NULL, 2);
    for (void **p = rev; *p != NULL; ++p) {
        struct replyMsg *reply = *p;
        for (int i = 0; i < reply->numberReturned; ++i) {
            bson_t *b = reply->docs[i];
            printf(" %d, %d, %p\n", i, reply->numberReturned, reply);
            char *name = bson_extract_string(b, "name");
            int32_t ttl = (uint32_t)bson_extract_int32(b, "ttl");
            char *type = bson_extract_string(b, "type");
            char *rdata = bson_extract_string(b, "rdata");
            printf("RR: %s, %d, %s, %s\n", name, ttl, type, rdata);
        }
        // replyMsgToStr(*p, buf, 4096);
        // printf("=============\n%s\n========\n", buf);
    }

    // reply = mongoDropDatabase(c, (char *)"test");
    // replyMsgToStr(reply, buf, 4096);
    // printf("=============\n%s", buf);
    // reply = mongoGetLastError(c, (char *)"test");
    // replyMsgToStr(reply, buf, 4096);
    // printf("=============\n%s", buf);
    return 0;
}
