//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-24
//
#include "himongo.h"
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
    char **namev = mongoGetCollectionNames(c, (char *)"local");
    pp = namev;
    for (pp=namev; *pp != NULL; ++pp) printf("%s\n", *pp);

    reply = mongoDropDatabase(c, (char *)"test");
    replyMsgToStr(reply, buf, 4096);
    printf("%s", buf);
    return 0;
}
