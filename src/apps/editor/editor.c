/* Native peer of script/samples/editor.js. UI policy stays in this app;
 * input focus, storage and composition stay behind ordinary named handles. */
#include "model.h"
#include "view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
struct field {
    uint32_t *text;
    size_t n,cursor;
    int all;
};
struct entry {
    char *name;
    int directory;
};
struct browser {
    char directory[4096];
    struct entry *entries;
    size_t count,selected,top;
    int hidden,save,focus,button,new_folder,page,after;
    struct field name,path,saved_name;
    char error[512];
    size_t last_click;
    uint64_t last_time;
};
enum {
    D_NONE,D_NOTICE,D_UNSAVED,D_FIND,D_FILE,D_REPLACE
};
enum {
    A_NONE,A_NEW,A_OPEN,A_QUIT
};
enum {
    T_MENU=1,T_ACTION,T_BUTTON,T_UP,T_CWD,T_FOLDER,T_HIDDEN,T_PATH,T_NAME,T_ENTRY
};
struct target {
    int x,y,w,h,action,index;
};
static char *ed_duplicate(const char *s){
    size_t n=strlen(s)+1;
    char *copy=malloc(n);
    if(copy)memcpy(copy,s,n);
    return copy;
}
static struct ed_doc doc;
static struct browser browser;
static struct field query;
static uint32_t *needle;
static size_t needle_n;
static int running=1,menu=-1,selected,dialog,pending,button,reveal=1,last_dialog,last_menu=-1;
static size_t last_top;
static char message[512]="Ready",notice_title[80],notice_text[1024],replace_path[4096];
static struct target *targets;
static size_t target_count,target_capacity;
static double wheel;
static const char *menu_names[]={
    "File","Edit","Search","Help"
};
static const char *labels[4][5]={
    {
        "New","Open...","Save","Save as...","Quit"
    },{
        "Undo","Redo","Copy line","Cut line","Paste"
    },{
        "Find...","Find next"
    },{
        "About"
    }
};
static const char *shortcuts[4][5]={
    {
        "Ctrl+N","Ctrl+O","Ctrl+S","F4","Ctrl+Q"
    },{
        "Ctrl+Z","Ctrl+Y","Ctrl+C","Ctrl+X","Ctrl+V"
    },{
        "Ctrl+F","F3"
    },{
        "F1"
    }
};
static const int counts[]={
    5,5,2,1
};
static size_t smaller(size_t a,size_t b){
    return a<b?a:b;
}
static int at_least(int a,int b){
    return a>b?a:b;
}
static int at_most(int a,int b){
    return a<b?a:b;
}
static void field_drop(struct field *f){
    free(f->text);
    memset(f,0,sizeof(*f));
}
static int field_set(struct field *f,const char *s){
    uint32_t *text;
    size_t n;
    if(!ed_decode((const unsigned char *)s,strlen(s),&text,&n))return 0;
    field_drop(f);
    *f=(struct field){
        text,n,n,0
    };
    return 1;
}
static char *field_string(const struct field *f){
    return ed_utf8(f->text,f->n,NULL);
}
static void notice(const char *title,const char *text){
    menu=-1;
    dialog=D_NOTICE;
    button=0;
    snprintf(notice_title,sizeof(notice_title),"%s",title);
    snprintf(notice_text,sizeof(notice_text),"%s",text);
}
static void error_notice(const char *title,cb_status_t status){
    char text[256];
    snprintf(text,sizeof(text),"Handle operation failed (status %u).\nThe document has not been discarded.",status);
    notice(title,text);
}
static void memory_error(void){
    notice("Not enough memory","The operation could not be completed.\nThe previous document is retained.");
}
static void target(int x,int y,int w,int h,int action,int index){
    if(target_count<target_capacity)targets[target_count++]=(struct target){
        x,y,w,h,action,index
    };
}
static void parent(char *path){
    char *last=strrchr(path,'/');
    if(last&&last!=path)*last=0;
    else strcpy(path,"/");
}
static const char *basename_of(const char *path){
    const char *last=strrchr(path,'/');
    return last?last+1:path;
}
static int canonical(char out[4096],const char *base,const char *name){
    return cb_path(out,4096,base,name);
}
static void entries_drop(struct entry *entries,size_t n){
    for(size_t i=0;i<n;i++)free(entries[i].name);
    free(entries);
}
static void browser_drop(void){
    entries_drop(browser.entries,browser.count);
    field_drop(&browser.name);
    field_drop(&browser.path);
    field_drop(&browser.saved_name);
    memset(&browser,0,sizeof(browser));
    browser.last_click=SIZE_MAX;
}
static int entry_compare(const void *va,const void *vb){
    const struct entry *a=va,*b=vb;
    if(a->directory!=b->directory)return b->directory-a->directory;
    const unsigned char *x=(const unsigned char *)a->name,*y=(const unsigned char *)b->name;
    while(*x&&*y){
        int diff=tolower(*x)-tolower(*y);
        if(diff)return diff;
        x++;
        y++;
    }
    int diff=*x-*y;
    return diff?diff:strcmp(a->name,b->name);
}
static cb_status_t browser_go(const char *path){
    char full[4096];
    if(!canonical(full,browser.directory[0]?browser.directory:cb_libc_cwd(),path))return CB_BAD_REQUEST;
    struct cb_filesystem_stat stat;
    size_t bytes;
    cb_status_t s=ep_fs(CB_FILESYSTEM_STAT,full,0,&stat,sizeof(stat),&bytes);
    if(s)return s;
    if(bytes!=sizeof(stat)||!stat.directory)return CB_BAD_REQUEST;
    struct entry *entries=NULL;
    size_t n=0,capacity=0;
    uint64_t cursor=0;
    for(;;){
        struct cb_filesystem_entry e;
        s=ep_fs(CB_FILESYSTEM_LIST,full,cursor,&e,sizeof(e),&bytes);
        if(s)break;
        if(!bytes)break;
        if(bytes!=sizeof(e)||!e.name_bytes||e.name_bytes>sizeof(e.name)||e.cursor<=cursor||memchr(e.name,0,e.name_bytes)||memchr(e.name,'/',e.name_bytes)){
            s=CB_BAD_REQUEST;
            break;
        }
        cursor=e.cursor;
        char name[sizeof(e.name)+1];
        memcpy(name,e.name,e.name_bytes);
        name[e.name_bytes]=0;
        if(!strcmp(name,".")||!strcmp(name,"..")||(!browser.hidden&&name[0]=='_'))continue;
        if(n==capacity){
            size_t next=capacity?capacity*2:32;
            if(next<capacity||next>SIZE_MAX/sizeof(*entries)){
                s=CB_RESOURCE_FAILURE;
                break;
            }
            struct entry *more=realloc(entries,next*sizeof(*entries));
            if(!more){
                s=CB_RESOURCE_FAILURE;
                break;
            }
            entries=more;
            capacity=next;
        }
        entries[n].name=ed_duplicate(name);
        if(!entries[n].name){
            s=CB_RESOURCE_FAILURE;
            break;
        }
        entries[n++].directory=!!e.directory;
    }
    struct field path_field={
        0
    };
    if(!s&&!field_set(&path_field,full))s=CB_RESOURCE_FAILURE;
    if(s){
        entries_drop(entries,n);
        field_drop(&path_field);
        return s;
    }
    qsort(entries,n,sizeof(*entries),entry_compare);
    entries_drop(browser.entries,browser.count);
    browser.entries=entries;
    browser.count=n;
    browser.selected=browser.top=0;
    browser.last_click=SIZE_MAX;
    strcpy(browser.directory,full);
    field_drop(&browser.path);
    browser.path=path_field;
    browser.focus=1;
    browser.error[0]=0;
    return CB_OK;
}
static void file_go(const char *path){
    cb_status_t s=browser_go(path);
    if(s)snprintf(browser.error,sizeof(browser.error),"Cannot open folder (status %u)",s);
    else if(!browser.save)(void)field_set(&browser.name,"");
}
static void start_action(int);
static void file_dialog(int save,int after){
    browser_drop();
    browser.save=save;
    browser.after=after;
    browser.page=at_most(view.rows-4,28)-10;
    char directory[4096];
    if(doc.path&&doc.path[0]){
        if(!canonical(directory,cb_libc_cwd(),doc.path)){
            error_notice("Folder unavailable",CB_BAD_REQUEST);
            return;
        }
        parent(directory);
    }
    else snprintf(directory,sizeof(directory),"%s",cb_libc_cwd());
    cb_status_t s=browser_go(directory);
    if(s){
        error_notice("Folder unavailable",s);
        return;
    }
    if(!field_set(&browser.name,doc.path&&doc.path[0]?basename_of(doc.path):save?"untitled.txt":"")){
        memory_error();
        return;
    }
    browser.name.all=1;
    browser.focus=0;
    menu=-1;
    dialog=D_FILE;
}
static void write_document(const char *path,int after){
    size_t bytes;
    char *data=ed_serialize(&doc,&bytes);
    if(!data){
        memory_error();
        return;
    }
    /* Allocate the saved-state snapshot before any destructive file operation. */
    struct ed_doc backup={
        .saved=doc.saved,.path=doc.path
    };
    memset(&doc.saved,0,sizeof(doc.saved));
    doc.path=NULL;
    if(!ed_saved(&doc,path)){
        doc.saved=backup.saved;
        doc.path=backup.path;
        free(data);
        memory_error();
        return;
    }
    cb_status_t s=ep_save(path,data,bytes);
    free(data);
    if(s){
        struct ed_doc failed={
            .saved=doc.saved,.path=doc.path
        };
        doc.saved=backup.saved;
        doc.path=backup.path;
        ed_destroy(&failed);
        error_notice("Save failed; changes remain unsaved",s);
        return;
    }
    ed_destroy(&backup);
    snprintf(message,sizeof(message),"Saved %s",path);
    dialog=D_NONE;
    pending=A_NONE;
    if(after)start_action(after);
}
static void save_document(int as,int after){
    if(as||!doc.path||!doc.path[0])file_dialog(1,after);
    else {
        char path[4096];
        snprintf(path,sizeof(path),"%s",doc.path);
        write_document(path,after);
    }
}
static void start_action(int action){
    menu=-1;
    dialog=D_NONE;
    if(action==A_NEW){
        if(!ed_open(&doc,"",0,""))memory_error();
        else strcpy(message,"Ready");
    }
    else if(action==A_OPEN)file_dialog(0,0);
    else if(action==A_QUIT)running=0;
}
static void request(int action){
    menu=-1;
    if(!ed_dirty(&doc)){
        start_action(action);
        return;
    }
    pending=action;
    dialog=D_UNSAVED;
    button=0;
}
static void file_accept(int select){
    if(select){
        if(browser.selected>=browser.count)return;
        struct entry *e=&browser.entries[browser.selected];
        if(e->directory){
            char path[4096];
            if(canonical(path,browser.directory,e->name))file_go(path);
            return;
        }
        if(!field_set(&browser.name,e->name)){
            memory_error();
            return;
        }
        if(browser.save){
            browser.focus=0;
            browser.name.all=1;
            return;
        }
    }
    char *name=field_string(&browser.name);
    if(!name){
        memory_error();
        return;
    }
    if(!*name){
        free(name);
        strcpy(browser.error,"Enter a file name or select a file.");
        return;
    }
    char path[4096];
    int valid=canonical(path,browser.directory,name);
    free(name);
    if(!valid){
        strcpy(browser.error,"Path is too long.");
        return;
    }
    struct cb_filesystem_stat stat;
    size_t bytes;
    cb_status_t s=ep_fs(CB_FILESYSTEM_STAT,path,0,&stat,sizeof(stat),&bytes);
    if(s&&s!=CB_NOT_FOUND){
        snprintf(browser.error,sizeof(browser.error),"Cannot inspect file (status %u)",s);
        return;
    }
    int exists=!s;
    if(exists&&bytes!=sizeof(stat)){
        strcpy(browser.error,"Invalid filesystem response.");
        return;
    }
    if(exists&&stat.directory){
        file_go(path);
        return;
    }
    if(browser.new_folder){
        s=ep_fs(CB_FILESYSTEM_MKDIR,path,0,NULL,0,NULL);
        if(s){
            snprintf(browser.error,sizeof(browser.error),"Cannot create folder (status %u)",s);
            return;
        }
        browser.new_folder=0;
        field_drop(&browser.name);
        browser.name=browser.saved_name;
        memset(&browser.saved_name,0,sizeof(browser.saved_name));
        file_go(path);
        return;
    }
    if(!browser.save){
        unsigned char *data;
        size_t n;
        s=ep_load(path,&data,&n);
        if(s){
            snprintf(browser.error,sizeof(browser.error),"Open failed (status %u)",s);
            return;
        }
        int ok=ed_open(&doc,data,n,path);
        ep_free_file(data);
        if(!ok){
            strcpy(browser.error,"Open failed: invalid UTF-8 or insufficient memory.");
            return;
        }
        snprintf(message,sizeof(message),"Opened %s",path);
        dialog=D_NONE;
        reveal=1;
        return;
    }
    if(exists){
        strcpy(replace_path,path);
        dialog=D_REPLACE;
        button=1;
        return;
    }
    write_document(path,browser.after);
}
static void new_folder(void){
    if(browser.new_folder){
        field_drop(&browser.name);
        browser.name=browser.saved_name;
        memset(&browser.saved_name,0,sizeof(browser.saved_name));
        browser.new_folder=0;
    }
    else {
        struct field empty={
            0
        };
        if(!field_set(&empty,"")){
            memory_error();
            return;
        }
        browser.saved_name=browser.name;
        browser.name=empty;
        browser.new_folder=1;
    }
    browser.focus=0;
    browser.name.all=1;
    browser.error[0]=0;
}
static void toggle_hidden(void){
    int old=browser.hidden;
    browser.hidden=!old;
    cb_status_t s=browser_go(browser.directory);
    if(s){
        browser.hidden=old;
        snprintf(browser.error,sizeof(browser.error),"Cannot list folder (status %u)",s);
    }
}
static void find_dialog(void){
    char *value=ed_utf8(needle,needle_n,NULL);
    if(!value||!field_set(&query,value)){
        free(value);
        memory_error();
        return;
    }
    free(value);
    dialog=D_FIND;
    menu=-1;
    button=0;
}
static void find_next(void){
    reveal=1;
    if(!needle_n){
        find_dialog();
        return;
    }
    char *value=ed_utf8(needle,needle_n,NULL);
    if(!value){
        memory_error();
        return;
    }
    snprintf(message,sizeof(message),"%s: %s",ed_find(&doc,needle,needle_n)?"Found":"Not found",value);
    free(value);
}
static void action(int m,int item){
    menu=-1;
    reveal=1;
    if(m==0){
        if(item==0)request(A_NEW);
        else if(item==1)request(A_OPEN);
        else if(item==2)save_document(0,0);
        else if(item==3)save_document(1,0);
        else request(A_QUIT);
    }
    else if(m==1){
        int ok=1;
        if(item==0)ed_undo(&doc);
        else if(item==1)ed_redo(&doc);
        else if(item==2)ok=ed_copy(&doc);
        else if(item==3)ok=ed_cut(&doc);
        else ok=ed_insert(&doc,doc.clipboard,doc.clipboard_length);
        if(!ok)memory_error();
    }
    else if(m==2){
        if(!item)find_dialog();
        else find_next();
    }
    else notice("About","Cloudberry native editor\nGraphics and input through handles.\nUTF-8 files; line clipboard; undo/redo.\nEsc closes a menu/dialog. Ctrl+Q quits.");
}
static void choose(int choice){
    int which=dialog;
    if(which==D_NOTICE){
        dialog=D_NONE;
        return;
    }
    if(which==D_REPLACE){
        if(!choice)write_document(replace_path,browser.after);
        else dialog=D_FILE;
        return;
    }
    if(which==D_UNSAVED){
        int after=pending;
        pending=0;
        dialog=D_NONE;
        if(!choice)save_document(0,after);
        else if(choice==1)start_action(after);
        return;
    }
    if(which==D_FIND){
        dialog=D_NONE;
        if(!choice){
            free(needle);
            needle=query.text;
            needle_n=query.n;
            memset(&query,0,sizeof(query));
            find_next();
        }
    }
}
static int edit_field(struct field *f,const struct cb_keyboard_event *e){
    int press=e->kind==CB_KEY_DOWN||e->kind==CB_KEY_REPEAT,ctrl=!!(e->modifiers&0x11);
    unsigned key=e->usage;
    if(press&&ctrl&&key==4){
        f->all=1;
        return 1;
    }
    int insert=e->kind==CB_KEY_TEXT&&!ctrl&&e->codepoint>=32&&e->codepoint!=127;
    int del=press&&(key==42||key==76);
    if(f->all&&(insert||del)){
        f->n=f->cursor=0;
        f->all=0;
    }
    if(press&&(key==80||key==79||key==74||key==77)){
        if(key==80&&f->cursor)--f->cursor;
        if(key==79&&f->cursor<f->n)++f->cursor;
        if(key==74)f->cursor=0;
        if(key==77)f->cursor=f->n;
        f->all=0;
    }
    else if(press&&key==42&&f->cursor){
        memmove(f->text+f->cursor-1,f->text+f->cursor,(f->n-f->cursor)*4);
        --f->cursor;
        --f->n;
    }
    else if(press&&key==76&&f->cursor<f->n){
        memmove(f->text+f->cursor,f->text+f->cursor+1,(f->n-f->cursor-1)*4);
        --f->n;
    }
    else if(insert){
        if(f->n>=SIZE_MAX/4-1)return 0;
        uint32_t *more=realloc(f->text,(f->n+1)*4);
        if(!more)return 0;
        f->text=more;
        memmove(more+f->cursor+1,more+f->cursor,(f->n-f->cursor)*4);
        more[f->cursor++]=e->codepoint;
        ++f->n;
        f->all=0;
    }
    return 1;
}
static void browser_select(size_t index){
    if(!browser.count){
        browser.selected=browser.top=0;
        return;
    }
    browser.selected=smaller(index,browser.count-1);
    if(browser.selected<browser.top)browser.top=browser.selected;
    if(browser.selected>=browser.top+(size_t)browser.page)browser.top=browser.selected-browser.page+1;
}
static void file_key(const struct cb_keyboard_event *e){
    int press=e->kind==CB_KEY_DOWN||e->kind==CB_KEY_REPEAT,ctrl=!!(e->modifiers&0x11);
    unsigned key=e->usage;
    if(press&&ctrl&&key==15){
        browser.focus=3;
        browser.path.all=1;
        return;
    }
    if(press&&ctrl&&key==11){
        toggle_hidden();
        return;
    }
    if(press&&key==41){
        if(browser.new_folder)new_folder();
        else {
            dialog=D_NONE;
            pending=0;
        }
        return;
    }
    if(press&&(e->modifiers&0x44)&&key==82){
        char path[4096];
        strcpy(path,browser.directory);
        parent(path);
        file_go(path);
        return;
    }
    if(press&&key==43){
        browser.focus=(browser.focus+((e->modifiers&0x22)?3:1))%4;
        return;
    }
    if(press&&key==40){
        if(browser.focus==3){
            char *path=field_string(&browser.path);
            if(path){
                file_go(path);
                free(path);
            }
            else memory_error();
        }
        else if(browser.focus==2&&browser.button==1)dialog=D_NONE;
        else file_accept(browser.focus==1);
        return;
    }
    if(browser.focus==1){
        if(press&&key==42){
            char path[4096];
            strcpy(path,browser.directory);
            parent(path);
            file_go(path);
            return;
        }
        if(press&&(key==82||key==81||key==75||key==78)){
            size_t amount=key==75||key==78?browser.page:1;
            size_t next=key==82||key==75?(amount>browser.selected?0:browser.selected-amount):browser.selected+amount;
            browser_select(next);
            return;
        }
        if(press&&(key==74||key==77)){
            browser_select(key==74?0:browser.count?browser.count-1:0);
            return;
        }
        if(e->kind!=CB_KEY_TEXT||ctrl||e->codepoint<32)return;
        browser.focus=0;
        browser.name.all=1;
    }
    if(browser.focus==2){
        if(press&&(key==80||key==79))browser.button=1-browser.button;
        return;
    }
    if(!edit_field(browser.focus==3?&browser.path:&browser.name,e))memory_error();
}
static int key_event(const struct cb_keyboard_event *e){
    if(e->kind==CB_KEY_RESET){
        menu=-1;
        strcpy(message,"Input resynchronized");
        return 1;
    }
    int press=e->kind==CB_KEY_DOWN||e->kind==CB_KEY_REPEAT,ctrl=!!(e->modifiers&0x11);
    unsigned key=e->usage;
    if(!press&&e->kind!=CB_KEY_TEXT)return 0;
    if(dialog==D_FILE){
        file_key(e);
        return 1;
    }
    if(dialog){
        if(press&&key==41){
            dialog=D_NONE;
            pending=0;
            return 1;
        }
        if(press&&key==43){
            int n=dialog==D_UNSAVED?3:dialog==D_NOTICE?1:2;
            button=(button+1)%n;
            return 1;
        }
        if(press&&key==40){
            choose(button);
            return 1;
        }
        if(dialog==D_FIND&&!edit_field(&query,e))memory_error();
        return 1;
    }
    if(press&&key==67){
        menu=menu<0?0:-1;
        selected=0;
        return 1;
    }
    if(menu>=0){
        if(!press)return 0;
        if(key==41)menu=-1;
        else if(key==80||key==79){
            menu=(menu+(key==80?3:1))%4;
            selected=0;
        }
        else if(key==82||key==81)selected=(selected+(key==82?counts[menu]-1:1))%counts[menu];
        else if(key==40)action(menu,selected);
        return 1;
    }
    if(press&&ctrl){
        int m=-1,item=0;
        switch(key){
            case 17:m=0;
            item=0;
            break;
            case 18:m=0;
            item=1;
            break;
            case 22:m=0;
            item=2;
            break;
            case 20:m=0;
            item=4;
            break;
            case 9:m=2;
            item=0;
            break;
            case 29:m=1;
            item=0;
            break;
            case 28:m=1;
            item=1;
            break;
            case 6:m=1;
            item=2;
            break;
            case 27:m=1;
            item=3;
            break;
            case 25:m=1;
            item=4;
            break;
        }
        if(m>=0){
            action(m,item);
            return 1;
        }
        return 0;
    }
    if(press&&(key==75||key==78)){
        ed_scroll(&doc,key==75?-view.body:view.body);
        reveal=0;
        return 1;
    }
    int ok=1;
    reveal=1;
    if(press){
        if(key==80||key==79||key==82||key==81||key==74||key==77)ed_move(&doc,key);
        else if(key==40){
            uint32_t c='\n';
            ok=ed_insert(&doc,&c,1);
        }
        else if(key==42)ok=ed_backspace(&doc);
        else if(key==76)ok=ed_delete(&doc);
        else if(key==43){
            uint32_t spaces[4]={
                ' ',' ',' ',' '
            };
            ok=ed_insert(&doc,spaces,4);
        }
        else if(key==73)doc.overwrite=!doc.overwrite;
        else if(key==58)action(3,0);
        else if(key==60)find_next();
        else if(key==61)save_document(1,0);
        else {
            reveal=0;
            return 0;
        }
    }
    else if(!ctrl&&e->codepoint>=32&&e->codepoint!=127)ok=ed_insert(&doc,&e->codepoint,1);
    else return 0;
    if(!ok)memory_error();
    return 1;
}
static int gutter(void){
    size_t n=doc.state.count;
    int digits=1;
    while(n>=10){
        n/=10;
        digits++;
    }
    return at_least(5,digits+1);
}
static void bar(int y,const char *text,uint32_t color,uint32_t background){
    ev_fill(0,y,view.cols,color,background);
    ev_put(0,y,text,color,background,view.cols);
}
static void field_draw(int x,int y,const char *label,const struct field *f,int active,int available){
    ev_put(x,y,label,ED_DIM,ED_PANEL,-1);
    x+=(int)strlen(label);
    ev_fill(x,y,available,ED_TEXT,ED_BG);
    size_t start=active?(f->cursor>=(size_t)available?f->cursor-available+1:0):(f->n>(size_t)available?f->n-available:0);
    for(size_t i=start;i<f->n&&i-start<(size_t)available;i++)ev_code(x+i-start,y,f->text[i],ED_TEXT,active&&f->all?ED_SELECTED:ED_BG);
    if(active&&f->cursor>=start&&f->cursor-start<(size_t)available)ev_caret(x+f->cursor-start,y);
}
static void panel(int x,int y,int w,int h,const char *title){
    for(int n=0;n<h;n++)ev_fill(x,y+n,w,ED_TEXT,ED_PANEL);
    for(int n=0;n<w;n++)ev_code(x+n,y,n==0||n==w-1?'+':'-',ED_ACCENT,ED_PANEL);
    ev_put(x+2,y,title,ED_ACCENT,ED_PANEL,w-4);
}
static void file_draw(void){
    target_count=0;
    int w=at_most(view.cols-4,96),h=at_most(view.rows-4,28),x=(view.cols-w)/2,y=(view.rows-h)/2;
    browser.page=h-10;
    panel(x,y,w,h,browser.save?"Save file":"Open file");
    char hidden[32];
    snprintf(hidden,sizeof(hidden),"Hidden: %s",browser.hidden?"on":"off");
    const char *toolbar[]={
        "Up","Working dir","New folder",hidden
    };
    int bx=x+2;
    for(int i=0;i<4;i++){
        char label[48];
        snprintf(label,sizeof(label),"[ %s ]",toolbar[i]);
        int n=strlen(label);
        ev_put(bx,y+1,label,ED_TEXT,ED_SELECTED,-1);
        target(bx,y+1,n,1,T_UP+i,0);
        bx+=n+1;
    }
    field_draw(x+2,y+3,"Path: ",&browser.path,browser.focus==3,w-10);
    target(x+2,y+3,w-4,1,T_PATH,0);
    ev_put(x+2,y+4,"Folders and files | Enter opens folder/file; Backspace goes up",ED_DIM,ED_PANEL,w-4);
    for(int n=0;n<browser.page;n++){
        size_t index=browser.top+n;
        uint32_t bg=index==browser.selected?ED_SELECTED:ED_BG;
        ev_fill(x+2,y+5+n,w-4,ED_TEXT,bg);
        if(index<browser.count){
            ev_put(x+3,y+5+n,browser.entries[index].directory?"[D] ":"    ",ED_TEXT,bg,4);
            ev_put(x+7,y+5+n,browser.entries[index].name,ED_TEXT,bg,w-10);
            target(x+2,y+5+n,w-4,1,T_ENTRY,n);
        }
    }
    field_draw(x+2,y+h-4,browser.new_folder?"Folder: ":"Name: ",&browser.name,browser.focus==0,w-12);
    target(x+2,y+h-4,w-4,1,T_NAME,0);
    char hint[128];
    snprintf(hint,sizeof(hint),"%zu entries | Ctrl+L path, Ctrl+H hidden, Tab focus",browser.count);
    ev_put(x+2,y+h-3,browser.error[0]?browser.error:hint,browser.error[0]?0xffa090:ED_DIM,ED_PANEL,w-4);
    const char *buttons[]={
        browser.new_folder?"Create folder":browser.save?"Save":"Open","Cancel"
    };
    bx=x+2;
    for(int i=0;i<2;i++){
        char label[32];
        snprintf(label,sizeof(label),"[ %s ]",buttons[i]);
        int n=strlen(label);
        ev_put(bx,y+h-2,label,ED_TEXT,browser.focus==2&&browser.button==i?ED_SELECTED:ED_BAR,-1);
        target(bx,y+h-2,n,1,T_BUTTON,i);
        bx+=n+2;
    }
}
static void dialog_draw(void){
    if(dialog==D_FILE){
        file_draw();
        return;
    }
    target_count=0;
    const char *title=dialog==D_UNSAVED?"Unsaved changes":dialog==D_FIND?"Find text":dialog==D_REPLACE?"Replace file?":notice_title;
    char content[sizeof(notice_text)+4096];
    if(dialog==D_UNSAVED)strcpy(content,"Save your changes before continuing?");
    else if(dialog==D_FIND)strcpy(content,"Search wraps through the document.");
    else if(dialog==D_REPLACE)snprintf(content,sizeof(content),"%s\nThis file already exists. Replace it?",replace_path);
    else strcpy(content,notice_text);
    int lines=1;
    for(const char *p=content;*p;p++)if(*p=='\n')lines++;
    int w=at_most(view.cols-4,76),h=at_least(8,lines+6),x=(view.cols-w)/2,y=(view.rows-h)/2;
    panel(x,y,w,h,title);
    char *p=content;
    for(int n=0;n<lines;n++){
        char *end=strchr(p,'\n');
        if(end)*end=0;
        ev_put(x+2,y+2+n,p,ED_TEXT,ED_PANEL,w-4);
        if(!end)break;
        p=end+1;
    }
    if(dialog==D_FIND)field_draw(x+2,y+h-4,"",&query,1,w-5);
    const char *buttons[3];
    int count;
    if(dialog==D_UNSAVED){
        buttons[0]="Save";
        buttons[1]="Discard";
        buttons[2]="Cancel";
        count=3;
    }
    else if(dialog==D_NOTICE){
        buttons[0]="OK";
        count=1;
    }
    else {
        buttons[0]=dialog==D_REPLACE?"Replace":"OK";
        buttons[1]="Cancel";
        count=2;
    }
    int bx=x+2;
    for(int i=0;i<count;i++){
        char label[32];
        snprintf(label,sizeof(label),"[ %s ]",buttons[i]);
        int n=strlen(label);
        ev_put(bx,y+h-2,label,ED_TEXT,button==i?ED_SELECTED:ED_PANEL,-1);
        target(bx,y+h-2,n,1,T_BUTTON,i);
        bx+=n+2;
    }
}
static cb_status_t render_frame(void){
    int g=gutter(),width=view.cols-g;
    doc.top=smaller(doc.top,doc.state.count-1);
    if(reveal&&!dialog&&menu<0){
        if(doc.state.row<doc.top)doc.top=doc.state.row;
        if(doc.state.row>=doc.top+(size_t)view.body)doc.top=doc.state.row-view.body+1;
        doc.left=smaller(doc.left,doc.state.col);
        if(doc.state.col>=doc.left+(size_t)width)doc.left=doc.state.col-width+1;
    }
    if(!dialog&&menu<0&&last_dialog==dialog&&last_menu==menu&&doc.top!=last_top){
        size_t amount=doc.top>last_top?doc.top-last_top:last_top-doc.top;
        if(amount<(size_t)view.body){
            cb_status_t s=ev_scroll(doc.top>last_top?(int)amount:-(int)amount);
            if(s)return s;
        }
    }
    ev_begin();
    target_count=0;
    char line[4608];
    snprintf(line,sizeof(line),"Cloudberry native editor | %s%s",doc.path&&doc.path[0]?doc.path:"Untitled",ed_dirty(&doc)?" *":"");
    bar(0,line,ED_ACCENT,ED_BAR);
    bar(1,"",ED_TEXT,ED_BAR);
    int x=1;
    for(int m=0;m<4;m++){
        snprintf(line,sizeof(line)," %s ",menu_names[m]);
        int n=strlen(line);
        ev_put(x,1,line,ED_TEXT,menu==m?ED_SELECTED:ED_BAR,-1);
        target(x,1,n,1,T_MENU,m);
        x+=n+2;
    }
    bar(2,"Ctrl+O Open   Ctrl+S Save   Ctrl+F Find   F10 Menus   Ctrl+Q Quit",ED_DIM,ED_BG);
    for(int y=0;y<view.body;y++){
        size_t row=doc.top+y;
        if(row>=doc.state.count)continue;
        snprintf(line,sizeof(line),"%*zu ",g-1,row+1);
        ev_put(0,y+3,line,ED_DIM,ED_BG,g);
        struct ed_line *l=doc.state.lines[row];
        for(size_t i=doc.left;i<l->length&&i-doc.left<(size_t)width;i++)ev_code(g+i-doc.left,y+3,l->text[i],ED_TEXT,ED_BG);
    }
    snprintf(line,sizeof(line),"%s | Ln %zu, Col %zu | %s | %s | %s",ed_dirty(&doc)?"Modified":"Saved",doc.state.row+1,doc.state.col+1,doc.overwrite?"OVR":"INS",doc.crlf?"CRLF":"LF",message);
    bar(view.rows-2,line,ED_ACCENT,ED_BAR);
    bar(view.rows-1,"UTF-8 | Mouse: menus/caret, wheel: scroll | Copy/Cut operate on the current line",ED_DIM,ED_BAR);
    if(!dialog&&menu<0&&doc.state.row>=doc.top&&doc.state.row-doc.top<(size_t)view.body&&doc.state.col>=doc.left&&doc.state.col-doc.left<(size_t)width)ev_caret(g+doc.state.col-doc.left,3+doc.state.row-doc.top);
    if(menu>=0){
        int left=1;
        for(int i=0;i<menu;i++)left+=(int)strlen(menu_names[i])+4;
        int w=30;
        for(int i=0;i<counts[menu];i++){
            uint32_t bg=selected==i?ED_SELECTED:ED_PANEL;
            ev_fill(left,i+2,w,ED_TEXT,bg);
            ev_put(left+1,i+2,labels[menu][i],ED_TEXT,bg,-1);
            ev_put(left+w-strlen(shortcuts[menu][i])-1,i+2,shortcuts[menu][i],ED_DIM,bg,-1);
            target(left,i+2,w,1,T_ACTION,i);
        }
    }
    if(dialog)dialog_draw();
    cb_status_t s=ev_flush();
    if(!s){
        last_top=doc.top;
        last_dialog=dialog;
        last_menu=menu;
        reveal=0;
    }
    return s;
}
static cb_status_t render(void){
    cb_status_t s=ep_cursor(1);
    if(s)return s;
    s=render_frame();
    cb_status_t shown=ep_cursor(0);
    return s?s:shown;
}
static void click(struct target t,uint64_t time){
    if(t.action==T_MENU){
        menu=menu==t.index?-1:t.index;
        selected=0;
    }
    else if(t.action==T_ACTION)action(menu,t.index);
    else if(t.action==T_BUTTON){
        if(dialog==D_FILE){
            if(t.index==1)dialog=D_NONE;
            else file_accept(0);
        }
        else choose(t.index);
    }
    else if(t.action==T_UP){
        char path[4096];
        strcpy(path,browser.directory);
        parent(path);
        file_go(path);
    }
    else if(t.action==T_CWD)file_go(cb_libc_cwd());
    else if(t.action==T_FOLDER)new_folder();
    else if(t.action==T_HIDDEN)toggle_hidden();
    else if(t.action==T_PATH){
        browser.focus=3;
        browser.path.all=1;
    }
    else if(t.action==T_NAME){
        browser.focus=0;
        browser.name.all=1;
    }
    else if(t.action==T_ENTRY){
        size_t index=browser.top+t.index;
        if(index>=browser.count)return;
        int repeated=browser.last_click==index&&time>=browser.last_time&&time-browser.last_time<500000000;
        browser.selected=index;
        browser.focus=1;
        if(!browser.entries[index].directory&&!field_set(&browser.name,browser.entries[index].name)){
            memory_error();
            return;
        }
        if(repeated){
            browser.last_click=SIZE_MAX;
            file_accept(1);
        }
        else {
            browser.last_click=index;
            browser.last_time=time;
        }
    }
}
static int mouse_event(const struct cb_mouse_cursor_event *record){
    const struct cb_mouse_event *e=&record->event;
    if(e->kind!=CB_MOUSE_INPUT)return 0;
    int changed=0;
    if(e->wheel_y){
        wheel+=(double)e->wheel_y/(e->resolution_y?e->resolution_y:1);
        double whole=wheel<0?-(double)(uint64_t)(-wheel):(double)(uint64_t)wheel;
        wheel-=whole;
        if(whole){
            changed=1;
            double rows=-whole*3;
            if(dialog==D_FILE){
                size_t maximum=browser.count>(size_t)browser.page?browser.count-browser.page:0;
                double next=browser.top+rows;
                browser.top=next<0?0:next>(double)maximum?maximum:(size_t)next;
            }
            else if(!dialog){
                size_t max=doc.state.count-1;
                double next=doc.top+rows;
                doc.top=next<0?0:next>(double)max?max:(size_t)next;
                reveal=0;
            }
        }
    }
    if((e->changed&1)&&(e->buttons&1)){
        int x=record->cursor.x/view.cw,y=record->cursor.y/view.ch;
        for(size_t i=target_count;i;i--){
            struct target t=targets[i-1];
            if(x>=t.x&&x<t.x+t.w&&y>=t.y&&y<t.y+t.h){
                click(t,e->timestamp_ns);
                return 1;
            }
        }
        if(!dialog){
            menu=-1;
            if(y>=3&&y<view.rows-2){
                reveal=1;
                doc.state.row=smaller(doc.top+y-3,doc.state.count-1);
                int offset=x-gutter();
                size_t col=offset<0?doc.left>=(size_t)-offset?doc.left+offset:0:doc.left+offset;
                doc.state.col=smaller(col,doc.state.lines[doc.state.row]->length);
            }
            return 1;
        }
    }
    return changed;
}
static cb_status_t input(unsigned which,int *repaint){
    struct cb_client *client=which?&platform.moves:&platform.keys;
    cb_status_t s=cb_collect(client);
    if(!client->collected)return s;
    if(s!=CB_OK&&s!=CB_INPUT_RESYNC)return s;
    struct cb_reply_header reply;
    memcpy(&reply,client->result,sizeof(reply));
    size_t size=which?sizeof(struct cb_mouse_batch):sizeof(struct cb_keyboard_batch_focus);
    if(reply.result_bytes<size)return CB_BAD_REQUEST;
    uint32_t count;
    size_t stride;
    if(which){
        struct cb_mouse_batch b;
        memcpy(&b,cb_result(client),sizeof(b));
        if(b.version.bytes!=sizeof(b)||b.version.major!=1||b.version.minor!=1||b.count>32)return CB_BAD_REQUEST;
        platform.mouseread.provider_epoch=b.provider_epoch;
        platform.mouseread.after_sequence=b.next_sequence;
        count=b.count;
        stride=sizeof(struct cb_mouse_cursor_event);
    }
    else {
        struct cb_keyboard_batch_focus b;
        memcpy(&b,cb_result(client),sizeof(b));
        if(b.base.version.bytes!=sizeof(b)||b.base.version.major!=1||b.base.version.minor!=1||b.base.count>32)return CB_BAD_REQUEST;
        platform.keyread.base.provider_epoch=b.base.provider_epoch;
        platform.keyread.base.after_sequence=b.base.next_sequence;
        platform.keyread.focus_generation=b.focus_generation;
        count=b.base.count;
        stride=sizeof(struct cb_keyboard_event);
    }
    if(size+(size_t)count*stride!=reply.result_bytes)return CB_BAD_REQUEST;
    if(s==CB_INPUT_RESYNC){
        menu=-1;
        strcpy(message,"Input resynchronized");
        *repaint=1;
        return CB_OK;
    }
    for(uint32_t i=0;i<count&&running;i++){
        const unsigned char *p=(const unsigned char *)cb_result(client)+size+i*stride;
        if(which){
            struct cb_mouse_cursor_event e;
            memcpy(&e,p,sizeof(e));
            if(e.event.version.major!=1||e.event.version.bytes!=sizeof(e)||e.event.version.minor!=1||!e.event.sequence||e.cursor.reserved)return CB_BAD_REQUEST;
            *repaint|=mouse_event(&e);
        }
        else {
            struct cb_keyboard_event e;
            memcpy(&e,p,sizeof(e));
            if(!cb_keyboard_event_valid(&e))return CB_BAD_REQUEST;
            *repaint|=key_event(&e);
        }
    }
    return CB_OK;
}
int main(int argc,char **argv){
    const char *phase="handle acquisition";
    cb_status_t status=ep_init();
    if(!status){
        phase="surface initialization";
        status=ev_init();
    }
    if(!status){
        target_capacity=(size_t)view.rows+128;
        targets=calloc(target_capacity,sizeof(*targets));
        if(!targets||!ed_open(&doc,"",0,""))status=CB_RESOURCE_FAILURE;
    }
    if(!status&&argc>1){
        char path[4096];
        if(!canonical(path,cb_libc_cwd(),argv[1]))error_notice("Open failed",CB_BAD_REQUEST);
        else {
            unsigned char *data;
            size_t n;
            cb_status_t s=ep_load(path,&data,&n);
            if(s)error_notice("Open failed",s);
            else {
                if(!ed_open(&doc,data,n,path))notice("Open failed","Invalid UTF-8 or insufficient memory.");
                ep_free_file(data);
            }
        }
    }
    if(!status&&!platform.mouse)strcpy(message,"Mouse unavailable; keyboard controls active");
    if(!status){
        phase="initial frame";
        status=render();
    }
    while(!status&&running){
        phase="input submission";
        status=ep_arm();
        unsigned which=0;
        if(!status){
            phase="input wait";
            status=ep_wait(&which);
        }
        int repaint=0;
        if(!status){
            phase="input completion";
            status=input(which,&repaint);
        }
        if(!status&&running&&repaint){
            phase="frame";
            status=render();
        }
    }
    unsigned failed_op=ep_failed_op;
    uint64_t actual=ep_actual,expected=ep_expected;
    ev_close();
    ep_close();
    ed_destroy(&doc);
    browser_drop();
    field_drop(&query);
    free(needle);
    free(targets);
    if(status)fprintf(stderr,"editor: handle operation failed (status %u, %s, graphics op %u, reply %llu/%llu)\n",status,phase,failed_op,(unsigned long long)actual,(unsigned long long)expected);
    return status?1:0;
}
