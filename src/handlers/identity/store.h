#ifndef CLOUDBERRY_IDENTITY_STORE_H
#define CLOUDBERRY_IDENTITY_STORE_H
#include "../../interfaces/identity-format.h"
#include "../../interfaces/identity.h"
struct id_store {
    void *context;void *(*allocate)(void *,size_t);void (*release)(void *,void *);
    cb_status_t (*read)(void *,const char *,struct id_blob *);
    cb_status_t (*write)(void *,const char *,const unsigned char *,size_t);
    struct id_blob files[3];unsigned failed;
};
const char *id_path(unsigned);
cb_status_t id_store_load(struct id_store *);
cb_status_t id_store_commit(struct id_store *,const struct id_blob [3]);
#endif
