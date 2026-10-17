#ifndef CLOUDBERRY_MANIFEST_H
#define CLOUDBERRY_MANIFEST_H
#include <stddef.h>
#include <stdint.h>
/* Strict boot-policy subset: version=1; resources[] contain name, operations[],
 * optional scope (self/keyboard/boot-input), and publish. No count ceiling.
 * Callback receives decoded, NUL-terminated strings in the mutable owned input.
 * Unicode escapes in identifiers are restricted to ASCII; names are ASCII.
 */
struct manifest_parser {char *at,*end;void *context;int (*resource)(void *,const char *,const char *,int);int (*operation)(void *,const char *);};
static inline void mf_space(struct manifest_parser *p){while(p->at<p->end && (*p->at==' ' || *p->at=='\n' || *p->at=='\r' || *p->at=='\t'))++p->at;}
static inline int mf_take(struct manifest_parser *p,char c){mf_space(p);if(p->at==p->end || *p->at!=c)return 0;++p->at;return 1;}
static inline int mf_equal(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static inline int mf_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline char *mf_string(struct manifest_parser *p){
    if(!mf_take(p,'"'))return NULL;
    char *out=p->at,*start=out;
    while(p->at<p->end){unsigned char c=*p->at++;if(c=='"'){*out=0;return start;}if(c<32 || c>=128)return NULL;
        if(c=='\\'){if(p->at==p->end)return NULL;c=*p->at++;
            if(c=='u'){unsigned value=0;for(unsigned i=0;i<4;++i){if(p->at==p->end)return NULL;int n=mf_hex(*p->at++);if(n<0)return NULL;value=value*16+n;}if(!value || value>=128)return NULL;c=value;}
            else if(c!='"' && c!='\\' && c!='/')return NULL;
        }*out++=c;
    }return NULL;
}
/* Operations may precede the name in valid JSON. Collect their strings in the
 * captured input, then replay them once resource metadata has been admitted. */
static inline int mf_resource(struct manifest_parser *p){
    if(!mf_take(p,'{'))return 0;
    char *name=NULL,*scope=NULL,*ops=NULL,*ops_end=NULL;unsigned seen=0;int publish=0;
    for(;;){char *key=mf_string(p);if(!key || !mf_take(p,':'))return 0;
        unsigned bit=mf_equal(key,"name")?1:mf_equal(key,"scope")?2:mf_equal(key,"operations")?4:mf_equal(key,"publish")?8:0;if(!bit || (seen&bit))return 0;seen|=bit;
        if(bit==1 || bit==2){char *value=mf_string(p);if(!value || !*value)return 0;if(bit==1)name=value;else scope=value;}
        else if(bit==8){mf_space(p);size_t left=p->end-p->at;if(left>=4 && p->at[0]=='t'&&p->at[1]=='r'&&p->at[2]=='u'&&p->at[3]=='e'){publish=1;p->at+=4;}else if(left>=5 && p->at[0]=='f'&&p->at[1]=='a'&&p->at[2]=='l'&&p->at[3]=='s'&&p->at[4]=='e')p->at+=5;else return 0;}
        else{if(!mf_take(p,'['))return 0;mf_space(p);ops=p->at;if(p->at<p->end && *p->at!=']')for(;;){char *value=mf_string(p);if(!value || !*value)return 0;mf_space(p);if(p->at<p->end && *p->at==']')break;if(!mf_take(p,','))return 0;}
            ops_end=p->at;if(!mf_take(p,']'))return 0;}
        mf_space(p);if(p->at<p->end && *p->at=='}'){++p->at;break;}if(!mf_take(p,','))return 0;
    }
    if((seen&5)!=5 || !p->resource(p->context,name,scope?scope:"self",publish))return 0;
    /* mf_string already terminated decoded strings, retaining JSON delimiters
     * after the original quote. Walk original token starts and skip to NUL. */
    char *at=ops;while(at<ops_end){while(at<ops_end && (*at==' '||*at=='\t'||*at=='\r'||*at=='\n'||*at==','))++at;if(at==ops_end)break;if(*at++!='"')return 0;
    char *value=at;if(!p->operation(p->context,value))return 0;while(at<ops_end && *at)++at;if(at==ops_end)return 0;++at;/* escaped tokens leave unused source bytes: handled below */
        while(at<ops_end && *at!=',' && *at!=']')++at;
    }return 1;
}
static inline int manifest_parse(struct manifest_parser *p){
    if(!mf_take(p,'{'))return 0;
    unsigned seen=0;
    for(;;){char *key=mf_string(p);if(!key || !mf_take(p,':'))return 0;
        unsigned bit=mf_equal(key,"version")?1:mf_equal(key,"resources")?2:0;if(!bit || (seen&bit))return 0;seen|=bit;
        if(bit==1){mf_space(p);if(p->at==p->end || *p->at++!='1')return 0;}
        else{if(!mf_take(p,'['))return 0;mf_space(p);if(p->at<p->end && *p->at!=']')for(;;){if(!mf_resource(p))return 0;mf_space(p);if(p->at<p->end && *p->at==']')break;if(!mf_take(p,','))return 0;}if(!mf_take(p,']'))return 0;}
        mf_space(p);if(p->at<p->end && *p->at=='}'){++p->at;break;}if(!mf_take(p,','))return 0;
    }mf_space(p);return seen==3 && p->at==p->end;
}
#endif
