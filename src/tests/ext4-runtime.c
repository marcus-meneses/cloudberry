#define memcpy cb_memcpy
#define memmove cb_memmove
#define memset cb_memset
#define memcmp cb_memcmp
#define strlen cb_strlen
#define strnlen cb_strnlen
#define strcmp cb_strcmp
#define strncmp cb_strncmp
#define strcpy cb_strcpy
#define strncpy cb_strncpy
#define strcat cb_strcat
#define strchr cb_strchr
#define qsort cb_qsort
#include "../kmodules/shared/ext4-fs/strings.c"
#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strlen
#undef strnlen
#undef strcmp
#undef strncmp
#undef strcpy
#undef strncpy
#undef strcat
#undef strchr
#undef qsort
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
static int compare(const void *a,const void *b){int x=*(const int *)a,y=*(const int *)b;return (x>y)-(x<y);}
int main(void){
    unsigned seed=7;for(unsigned n=0;n<200;++n){int a[200],b[200];for(unsigned i=0;i<n;++i){seed=seed*1664525+1013904223;a[i]=b[i]=seed%91;}cb_qsort(a,n,sizeof(*a),compare);qsort(b,n,sizeof(*b),compare);assert(!cb_memcmp(a,b,n*sizeof(*a)));}
    char s[32]="abcdef";cb_memmove(s+2,s,6);assert(!cb_memcmp(s,"ababcdef",8));cb_memmove(s,s+2,6);assert(!cb_memcmp(s,"abcdef",6));
    puts("PASS freestanding ext4 runtime: sorting against libc and overlapping byte moves");return 0;
}
