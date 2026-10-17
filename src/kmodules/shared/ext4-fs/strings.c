#include <string.h>
void *memcpy(void *to,const void *from,size_t n){unsigned char *d=to;const unsigned char *s=from;while(n--)*d++=*s++;return to;}
void *memmove(void *to,const void *from,size_t n){unsigned char *d=to;const unsigned char *s=from;if(d>s){while(n){--n;d[n]=s[n];}}else memcpy(to,from,n);return to;}
void *memset(void *p,int value,size_t n){unsigned char *d=p;while(n--)*d++=value;return p;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *p=a,*q=b;while(n--){int difference=*p++-*q++;if(difference)return difference;}return 0;}
size_t strlen(const char *s){size_t n=0;while(s[n])++n;return n;}
size_t strnlen(const char *s,size_t limit){size_t n=0;while(n<limit && s[n])++n;return n;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char *a,const char *b,size_t n){while(n--){int d=(unsigned char)*a-(unsigned char)*b;if(d || !*a)return d;++a;++b;}return 0;}
char *strcpy(char *out,const char *in){char *p=out;do{*p++=*in;}while(*in++);return out;}
char *strncpy(char *out,const char *in,size_t n){size_t i=0;while(i<n && in[i]){out[i]=in[i];++i;}while(i<n)out[i++]=0;return out;}
char *strcat(char *out,const char *in){strcpy(out+strlen(out),in);return out;}
char *strchr(const char *s,int c){do{if((unsigned char)*s==(unsigned char)c)return (char *)s;}while(*s++);return NULL;}
static void swap_bytes(unsigned char *a,unsigned char *b,size_t size){while(size--){unsigned char c=*a;*a++=*b;*b++=c;}}
static void sift(unsigned char *base,size_t root,size_t count,size_t size,int (*compare)(const void *,const void *)){
    while(root<count/2){size_t child=root*2+1;if(child+1<count && compare(base+child*size,base+(child+1)*size)<0)++child;if(compare(base+root*size,base+child*size)>=0)return;swap_bytes(base+root*size,base+child*size,size);root=child;}
}
void qsort(void *base,size_t count,size_t size,int (*compare)(const void *,const void *)){
    if(!size || count<2 || count>(size_t)-1/size)return;
    unsigned char *p=base;
    for(size_t n=count/2;n;--n)sift(p,n-1,count,size,compare);
    for(size_t n=count-1;n;--n){swap_bytes(p,p+n*size,size);sift(p,0,n,size,compare);}
}
