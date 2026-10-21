#ifndef CB_PASSWORD_H
#define CB_PASSWORD_H
#include <stdint.h>
#include <stddef.h>
void id_wipe(void *,size_t);
int id_password_valid(const unsigned char *,size_t);
int id_password_derive(const void *,size_t,const unsigned char [32],uint32_t,unsigned char [32]);
int id_password_equal(const unsigned char [32],const unsigned char [32]);
#endif
