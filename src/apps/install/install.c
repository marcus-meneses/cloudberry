#include "../../sdk/application.h"
#include "../../sdk/stream.h"
#include "../../interfaces/installer.h"
static struct cb_client calls,console;
static cb_handle_t service,out,in;
static unsigned char frame[CB_FRAME_LIMIT];
static char approval_path[4096];
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
    size_t bytes=sizeof(*r)+r->name_bytes;
    if(r->options&CB_INSTALL_APPROVAL_FILE){size_t n=cb_length(approval_path);cb_reg_put(frame+bytes,n);bytes+=8;cb_copy(frame+bytes,approval_path,n);bytes+=n;}
    return cb_invoke(&calls,service,(struct cb_interface_id)CB_INSTALLER_INTERFACE_ID,op,frame,bytes,0);
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
    if(!op){say("install inspect/add/upgrade/repair PATH | remove/permissions/approve NAME | list | status\nOptions: --system | --user; --yes; --permissions FILE\nadd --reuse-data; remove --delete-data; upgrade --allow-downgrade\n");
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
    int named=equal(op,"remove") || equal(op,"permissions") || equal(op,"approve");
    if(!name || (!named && !equal(op,"inspect") && !equal(op,"add") && !equal(op,"upgrade") && !equal(op,"repair")))finish(CB_BAD_REQUEST);
    struct cb_installer_request r={0};
    int yes=0;
    char cwd[4096],path[4096];
    if(start.header.cwd_bytes>4095)finish(CB_BAD_REQUEST);
    cb_copy(cwd,start.cwd,start.header.cwd_bytes);cwd[start.header.cwd_bytes]=0;
    for(uint64_t i=3;i<start.header.argc;++i){const char *a=cb_application_argument(&start,i);
        if(equal(a,"--system") && !r.scope)r.scope=CB_INSTALL_SCOPE_SYSTEM;
        else if(equal(a,"--user") && !r.scope)r.scope=CB_INSTALL_SCOPE_USER;
        else if(equal(a,"--yes"))yes=1;
        else if(equal(a,"--reuse-data") && equal(op,"add"))r.options|=CB_INSTALL_REUSE_DATA;
        else if(equal(a,"--delete-data") && equal(op,"remove"))r.options|=CB_INSTALL_DELETE_DATA;
        else if(equal(a,"--allow-downgrade") && equal(op,"upgrade"))r.options|=CB_INSTALL_ALLOW_DOWNGRADE;
        else if(equal(a,"--permissions") && !equal(op,"remove") && !(r.options&CB_INSTALL_APPROVAL_FILE)){
            const char *file=cb_application_argument(&start,++i);if(!file || !cb_path(approval_path,sizeof(approval_path),cwd,file))finish(CB_BAD_REQUEST);r.options|=CB_INSTALL_APPROVAL_FILE;
        }else finish(CB_BAD_REQUEST);
    }
    if(named){if(cb_length(name)>220)finish(CB_BAD_REQUEST);cb_copy(path,name,cb_length(name)+1);}
    else if(!cb_path(path,sizeof(path),cwd,name))finish(CB_BAD_REQUEST);
    r.name_bytes=cb_length(path);
    if(equal(op,"remove")){say("Remove ");say(name);say(r.options&CB_INSTALL_DELETE_DATA?" and delete private data.\n":"; retain private data.\n");
        if(!yes && !confirm())cb_exit(0);
        cb_status_t status=request(CB_INSTALLER_REMOVE,&r,path);if(status)finish(status);say("Removed.\n");cb_exit(0);
    }
    if(equal(op,"approve") && !(r.options&CB_INSTALL_APPROVAL_FILE))finish(CB_BAD_REQUEST);
    if(!named){struct cb_installer_request meta=r;meta.options=0;
        cb_status_t status=request(CB_INSTALLER_METADATA,&meta,path);if(status)finish(status);
        struct cb_release_info info;cb_copy(&info,cb_result(&calls),sizeof(info));
        say("Signed revision ");number(info.revision);if(info.installed_revision){say("; installed ");number(info.installed_revision);}say("\nIssuer ");
        static const char hex[]="0123456789abcdef";char key[65];for(unsigned i=0;i<32;++i){key[i*2]=hex[info.issuer[i]>>4];key[i*2+1]=hex[info.issuer[i]&15];}key[64]=0;say(key);say("\n");
        if(equal(op,"upgrade") && info.revision<info.installed_revision){if(!(r.options&CB_INSTALL_ALLOW_DOWNGRADE))finish(CB_DENIED);say("Explicit downgrade: retained private data may be incompatible.\n");}
    }
    unsigned char fingerprint[64];
    for(unsigned view=0;view<2;++view){struct cb_installer_request inspect=r;inspect.offset=0;inspect.options&=CB_INSTALL_APPROVAL_FILE;
        if(!view)inspect.options|=CB_INSTALL_SHOW_REQUESTED;
        int first=1;
        if(!yes || equal(op,"inspect") || equal(op,"permissions"))say(view?"Approved permissions:\n":"Signed requested permissions:\n");
        for(;;){cb_status_t status=request(named?CB_INSTALLER_PERMISSIONS:CB_INSTALLER_INSPECT,&inspect,path);if(status)finish(status);
            struct cb_reply_header h;cb_copy(&h,calls.result,sizeof(h));struct cb_bundle_info info;
            if(h.result_bytes<sizeof(info))finish(CB_BAD_REQUEST);
            cb_copy(&info,cb_result(&calls),sizeof(info));
            if(info.name_bytes>220 || h.result_bytes<sizeof(info)+info.name_bytes || info.next>info.manifest_bytes || info.next<inspect.offset || info.next-inspect.offset!=h.result_bytes-sizeof(info)-info.name_bytes)finish(CB_BAD_REQUEST);
            if(first && !view){cb_copy(fingerprint,info.fingerprint,64);first=0;}
            else if(!cb_install_equal(fingerprint,info.fingerprint,64))finish(CB_DENIED);
            if(!yes || equal(op,"inspect") || equal(op,"permissions"))CB_REQUIRE(cb_write_all(&console,out,(unsigned char *)cb_result(&calls)+sizeof(info)+info.name_bytes,h.result_bytes-sizeof(info)-info.name_bytes)==CB_OK);
            inspect.offset=info.next;if(info.next==info.manifest_bytes)break;
        }
        if(!yes || equal(op,"inspect") || equal(op,"permissions"))say("\n");
    }
    if(equal(op,"inspect") || equal(op,"permissions"))cb_exit(0);
    if(!yes){say("Apply this approved policy to the inactive application?\n");if(!confirm())cb_exit(0);}
    cb_copy(r.expected_hash,fingerprint,64);
    unsigned operation=equal(op,"upgrade")?CB_INSTALLER_UPGRADE:equal(op,"repair")?CB_INSTALLER_REPAIR:equal(op,"approve")?CB_INSTALLER_APPROVE:CB_INSTALLER_INSTALL;
    cb_status_t status=request(operation,&r,path);if(status)finish(status);
    say(equal(op,"upgrade")?"Upgraded.\n":equal(op,"repair")?"Repaired.\n":equal(op,"approve")?"Permissions updated.\n":"Installed.\n");cb_exit(0);
}
