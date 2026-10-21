#include "../handlers/installer/bundle.h"
int cb_bundle_check(unsigned char *data,size_t bytes,const unsigned char key[32]){struct bundle b;return bundle_decode(&b,data,bytes,key);}
