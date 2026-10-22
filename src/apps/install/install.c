#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/installer.h"
static struct cb_client calls,console;
static cb_handle_t service,out,in;
static unsigned char frame[sizeof(struct cb_installer_request)+4096];
static int equal(const char *a,const char *b){return cb_length(a)==cb_length(b) && cb_install_equal(a,b,cb_length(a)+1);
    }
static void say(const char *s){CB_REQUIRE(cb_write_all(&console,out,s,cb_length(s))==CB_OK);
    }
static void number(uint64_t n){char a[24],b[24];
    size_t used=0;
    do{a[used++]='0'+n%10;
        n/=10;
        }while(n);
    for(size_t i=0;i<used;++i)b[i]=a[used-i-1];
    CB_REQUIRE(cb_write_all(&console,out,b,used)==CB_OK);
    }
static __attribute__((noreturn)) void finish(cb_status_t status){if(status){say("install: status ");
        number(status);
        say("\n");
        if(status==CB_BUSY)say("An app may still be running, the name may exist, or retained data needs --reuse-data.\n");
        if(status==CB_RESOURCE_FAILURE)say("Storage outcome may be uncertain; inspect installer status before retrying.\n");
        if(cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,CB_INSTALLER_STATUS,NULL,0,0)==CB_OK){uint64_t state[4];cb_copy(state,cb_result(&calls),sizeof(state));say("Installer phase ");number(state[2]);say("\n");}
        }cb_exit(status);
    }
static cb_status_t request(unsigned op,const struct cb_installer_request *r,const char *name){cb_copy(frame,r,sizeof(*r));
    cb_copy(frame+sizeof(*r),name,r->name_bytes);
    return cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,op,frame,sizeof(*r)+r->name_bytes,0);
    }
static int confirm(void){if(!in)return 0;
    say("Continue? [y/N] ");
    unsigned char c;
    size_t n;
    int accepted=0,first=1;
    for(;;){cb_status_t s=cb_stream_read(&console,in,&c,1,&n,0);
        if(s || !n)return 0;
        if(c=='\n'){say("\n");
            return accepted;
            }if(c=='\r')continue;
        if(first){accepted=c=='y' || c=='Y';
            first=0;
            }}}
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;
    CB_REQUIRE(cb_application_start(packet,&start));
    out=cb_standard(1);
    in=cb_standard(0);
    const char *op=cb_application_argument(&start,1),*name=cb_application_argument(&start,2);
    if(!op){say("install inspect PATH | add PATH | upgrade PATH | remove NAME | list | status\nOptions: --system | --user; --yes; add --reuse-data; remove --delete-data\n");
        cb_exit(0);
        }service=cb_acquire(CB_INSTALLER_NAME,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID);
    if(equal(op,"status")){cb_status_t s=cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,CB_INSTALLER_STATUS,NULL,0,0);
        if(s)finish(s);
        uint64_t values[4];
        cb_copy(values,cb_result(&calls),sizeof(values));
        say("Installer generation ");
        number(values[0]);
        say(values[1]?"; recovery required\n":"; ready\n");say("Last phase ");number(values[2]);say(" status ");number(values[3]);say("\n");
        uint64_t cursor=0;for(;;){cb_status_t d=cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,CB_INSTALLER_DIAGNOSTICS,&cursor,8,0);if(d)finish(d);struct cb_reply_header h;cb_copy(&h,calls.result,sizeof(h));if(!h.result_bytes)break;struct cb_install_diagnostic item;cb_copy(&item,cb_result(&calls),sizeof(item));CB_REQUIRE(h.result_bytes==sizeof(item)+item.name_bytes);say("Unavailable: ");CB_REQUIRE(cb_write_all(&console,out,(unsigned char *)cb_result(&calls)+sizeof(item),item.name_bytes)==CB_OK);say(" status ");number(item.status);say("\n");cursor=item.cursor;}
        cb_exit(0);
        }
    if(equal(op,"list")){uint64_t cursor=0;
        for(;;){cb_status_t s=cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,CB_INSTALLER_LIST,&cursor,8,0);
            if(s)finish(s);
            struct cb_reply_header h;
            cb_copy(&h,calls.result,sizeof(h));
            if(!h.result_bytes)break;
            struct cb_registry_entry e;
            cb_copy(&e,cb_result(&calls),sizeof(e));
            CB_REQUIRE(e.name_bytes<=220 && h.result_bytes==sizeof(e)+e.name_bytes);
            CB_REQUIRE(cb_write_all(&console,out,(unsigned char *)cb_result(&calls)+sizeof(e),e.name_bytes)==CB_OK);
            if(e.dynamic==2)say(" [unavailable]");
            say(e.owner==CB_REGISTRY_SYSTEM?" (system)\n":" (user)\n");
            cursor=e.cursor;
            }cb_exit(0);
        }
    if(!name || (!equal(op,"inspect") && !equal(op,"add") && !equal(op,"upgrade") && !equal(op,"remove")))finish(CB_BAD_REQUEST);
    struct cb_installer_request r={0};
    int yes=0;
    for(uint64_t i=3;i<start.header.argc;++i){const char *a=cb_application_argument(&start,i);
        if(equal(a,"--system") && !r.scope)r.scope=CB_INSTALL_SCOPE_SYSTEM;
        else if(equal(a,"--user") && !r.scope)r.scope=CB_INSTALL_SCOPE_USER;
        else if(equal(a,"--yes"))yes=1;
        else if(equal(a,"--reuse-data") && equal(op,"add"))r.options|=CB_INSTALL_REUSE_DATA;
        else if(equal(a,"--delete-data") && equal(op,"remove"))r.options|=CB_INSTALL_DELETE_DATA;
        else finish(CB_BAD_REQUEST);
        }
    char path[4096];
    if(equal(op,"remove")){if(cb_length(name)>220)finish(CB_BAD_REQUEST);
        r.name_bytes=cb_length(name);
        say("Remove ");
        say(name);
        say(r.options&CB_INSTALL_DELETE_DATA?" and delete private data.\n":"; retain private data.\n");
        if(!yes && !confirm())cb_exit(0);
        cb_status_t s=request(CB_INSTALLER_REMOVE,&r,name);
        if(s)finish(s);
        say("Removed.\n");
        cb_exit(0);
        }
    char cwd[4096];
    if(start.header.cwd_bytes>4095)finish(CB_BAD_REQUEST);
    cb_copy(cwd,start.cwd,start.header.cwd_bytes);
    cwd[start.header.cwd_bytes]=0;
    if(!cb_path(path,sizeof(path),cwd,name))finish(CB_BAD_REQUEST);
    r.name_bytes=cb_length(path);
    struct cb_installer_request inspect=r;
    inspect.options=0;
    unsigned char fingerprint[64];
    int first=1;
    for(;;){cb_status_t s=request(CB_INSTALLER_INSPECT,&inspect,path);
        if(s)finish(s);
        struct cb_reply_header h;
        cb_copy(&h,calls.result,sizeof(h));
        struct cb_bundle_info info;
        if(h.result_bytes<sizeof(info))finish(CB_BAD_REQUEST);
        cb_copy(&info,cb_result(&calls),sizeof(info));
        if(info.name_bytes>220 || h.result_bytes<sizeof(info)+info.name_bytes || info.next>info.manifest_bytes || info.next-inspect.offset!=h.result_bytes-sizeof(info)-info.name_bytes)finish(CB_BAD_REQUEST);
        if(first){cb_copy(fingerprint,info.fingerprint,64);
            say("Verified ");
            CB_REQUIRE(cb_write_all(&console,out,(unsigned char *)cb_result(&calls)+sizeof(info),info.name_bytes)==CB_OK);
            say("\n");
            first=0;
            }else if(!cb_install_equal(fingerprint,info.fingerprint,64))finish(CB_DENIED);
        if(!yes || equal(op,"inspect")){CB_REQUIRE(cb_write_all(&console,out,(unsigned char *)cb_result(&calls)+sizeof(info)+info.name_bytes,h.result_bytes-sizeof(info)-info.name_bytes)==CB_OK);
            }
        inspect.offset=info.next;
        if(info.next==info.manifest_bytes)break;
        }
    if(equal(op,"inspect")){say("\n");
        cb_exit(0);
        }if(!yes){say(equal(op,"upgrade")?"\nUpgrade this inactive app; preserve private data.\n":"\nInstall this manifest in the selected scope.\n");
        if(!confirm())cb_exit(0);
        }cb_copy(r.expected_hash,fingerprint,64);
    cb_status_t s=request(equal(op,"upgrade")?CB_INSTALLER_UPGRADE:CB_INSTALLER_INSTALL,&r,path);
    if(s)finish(s);
    say(equal(op,"upgrade")?"Upgraded.\n":"Installed.\n");
    cb_exit(0);
    }
