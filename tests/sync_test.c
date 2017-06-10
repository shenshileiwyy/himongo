//
// Created by Yu Yang <yyangplus@NOSPAM.gmail.com> on 2017-05-24
//
#include "../himongo.h"

int main(int argc, char *argv[])
{
    char buf[4096];
    mongoReply *reply;
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
    mongoReplyToStr(reply, buf, 4096);
    printf("=============\n%s\n========\n", buf);

    char **namev = mongoGetCollectionNames(c, (char *)"test");
    int i;
    for (i = 0,pp=namev; *pp != NULL; ++pp, ++i) printf("%d: %s\n", i, *pp);

    void **rev = mongoFindAll(c, "zone", "example.com", NULL, NULL, 2);
    for (void **p = rev; *p != NULL; ++p) {
        mongoReply *reply = *p;
        mongoReplyToStr(reply, buf, 4096);
        printf("=============\n%s\n========\n", buf);
    }

    // reply = mongoDropDatabase(c, (char *)"test");
    // mongoReplyToStr(reply, buf, 4096);
    // printf("=============\n%s", buf);
    // reply = mongoGetLastError(c, (char *)"test");
    // mongoReplyToStr(reply, buf, 4096);
    // printf("=============\n%s", buf);
    return 0;
}
