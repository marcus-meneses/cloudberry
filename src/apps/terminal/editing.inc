/* Terminal input policy; text owns character rendering and cursor placement. */
struct history_entry {struct history_entry *previous,*next;char text[];};
static struct history_entry *history_first,*history_last,*history_current;
static char history_draft[sizeof(line)];static size_t draft_cursor;
static char *paint;
static struct cb_text_cursor input_origin;
static size_t input_width;
static struct cb_client blink_client;
static cb_handle_t blink_clock;
static unsigned cursor_visible=1;
static uint64_t blink_at;
static void input_cursor_reset(void){cursor_visible=1;blink_at=0;}
static uint64_t blink_now(void){
    if(!blink_clock)blink_clock=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);
    CB_REQUIRE(cb_invoke(&blink_client,blink_clock,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_NOW_NS,NULL,0,0)==CB_OK);
    uint64_t time;cb_copy(&time,cb_result(&blink_client),sizeof(time));return time;
}
static void input_cursor(int visible){
    size_t position;term_view(&editor,input_width,&position);
    struct cb_text_cell c={.column=input_origin.column+(uint32_t)position,.row=input_origin.row,.codepoint=term_codepoint(line,editor.length,editor.cursor),.foreground=visible?0:0xdddddd,.background=visible?0xdddddd:0};
    CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_CELL,&c,sizeof(c),0)==CB_OK);
}
static uint64_t input_blink_deadline(void){
    if(!input_active || !editor.overwrite){input_cursor_reset();return 0;}
    if(!blink_at)blink_at=blink_now()+500000000;
    return blink_at;
}
static void input_blink_tick(void){
    if(!input_active || !editor.overwrite || !blink_at)return;
    uint64_t time=blink_now();if(time<blink_at)return;
    cursor_visible=!cursor_visible;blink_at=time+500000000;input_cursor(cursor_visible);
}
static void cursor_at(uint32_t column,uint32_t row){struct cb_text_cursor c={column,row};CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_CURSOR,&c,sizeof(c),0)==CB_OK);}
static void input_render(int cursor){
    if(!input_active)return;
    size_t position,end=term_view(&editor,input_width,&position),bytes=end-editor.view,cells=0;
    cb_copy(paint,line+editor.view,bytes);for(size_t i=editor.view;i<end;i=term_next(line,editor.length,i))++cells;
    while(cells++<input_width)paint[bytes++]=' ';
    cursor_at(input_origin.column,input_origin.row);raw_output(paint,bytes);
    cursor_at(input_origin.column+(uint32_t)position,input_origin.row);
    if(cursor && (!editor.overwrite || cursor_visible))input_cursor(1);
}
static void input_begin(void){
    struct cb_text_info grid;CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_INFO,NULL,0,0)==CB_OK);cb_copy(&grid,cb_result(&client),sizeof(grid));
    if(grid.column==grid.columns){raw_output("\n",1);CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_INFO,NULL,0,0)==CB_OK);cb_copy(&grid,cb_result(&client),sizeof(grid));}
    CB_REQUIRE(grid.column<grid.columns);input_origin=(struct cb_text_cursor){grid.column,grid.row};input_width=grid.columns-grid.column;input_active=1;input_cursor_reset();input_render(1);
}
static void input_pause(void){
    if(!input_active)return;
    /* A foreground app may have cleared or repositioned the shared text grid.
     * Do not restore a stale input origin over its final screen/cursor. */
    struct cb_text_info grid;CB_REQUIRE(cb_invoke(&client,text,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,CB_TEXT_INFO,NULL,0,0)==CB_OK);cb_copy(&grid,cb_result(&client),sizeof(grid));size_t position;term_view(&editor,input_width,&position);
    if(grid.row!=input_origin.row || grid.column!=input_origin.column+position){input_active=0;return;}
    cursor_at(input_origin.column,input_origin.row);for(size_t i=0;i<input_width;++i)paint[i]=' ';raw_output(paint,input_width);cursor_at(input_origin.column,input_origin.row);input_active=0;
}
static void input_finish(void){
    if(input_active){input_render(0);cursor_at(columns-1,input_origin.row);input_active=0;}raw_output("\n",1);
}
static void prompt(void){input_active=0;raw_output(cwd,cb_length(cwd));raw_output(" > ",3);input_begin();}
static void reset_line(void){term_reset(&editor);history_current=NULL;history_draft[0]=0;}
static void remember(void){
    history_current=NULL;if(!editor.length)return;if(history_last && !__builtin_strcmp(history_last->text,line))return;
    struct history_entry *entry=allocate(sizeof(*entry)+editor.length+1);cb_copy(entry->text,line,editor.length+1);entry->next=NULL;entry->previous=history_last;if(history_last)history_last->next=entry;else history_first=entry;history_last=entry;
}
static void recall(int older){
    if(older){if(!history_current){if(!history_last)return;cb_copy(history_draft,line,editor.length+1);draft_cursor=editor.cursor;history_current=history_last;}else if(history_current->previous)history_current=history_current->previous;}
    else {if(!history_current)return;history_current=history_current->next;}
    const char *value=history_current?history_current->text:history_draft;term_reset(&editor);CB_REQUIRE(term_replace(&editor,0,0,value,cb_length(value)));if(!history_current)editor.cursor=draft_cursor;
}
static int completion_entry(struct cb_filesystem_entry *entry,const char *stem,size_t bytes){
    if(!entry->name_bytes || entry->name_bytes>255 || entry->name_bytes<bytes)return 0;
    if((entry->name_bytes==1 && entry->name[0]=='.') || (entry->name_bytes==2 && entry->name[0]=='.' && entry->name[1]=='.'))return 0;
    if(entry->name[0]=='_' && (!bytes || stem[0]!='_'))return 0;
    for(size_t i=0;i<entry->name_bytes;++i)if((unsigned char)entry->name[i]<32 || entry->name[i]==127)return 0;
    for(size_t i=0;i<bytes;++i)if(entry->name[i]!=stem[i])return 0;
    return 1;
}
static cb_status_t completion_next(const char *path,uint64_t *cursor,struct cb_filesystem_entry *entry,int *end){
    cb_status_t status=cb_fs_call(&client,filesystem,CB_FILESYSTEM_LIST,path,0,*cursor);if(status)return status;
    struct cb_reply_header h;cb_copy(&h,client.result,sizeof(h));if(!h.result_bytes){*end=1;return CB_OK;}
    if(h.result_bytes!=sizeof(*entry))return CB_BAD_REQUEST;
    cb_copy(entry,cb_result(&client),sizeof(*entry));if(entry->cursor<=*cursor || entry->name_bytes>255)return CB_BAD_REQUEST;*cursor=entry->cursor;return CB_OK;
}
static void completion_list(const char *path,const char *stem,size_t bytes){
    input_finish();unsigned column=0;uint64_t cursor=0;int end=0;
    while(!end){struct cb_filesystem_entry entry;cb_status_t status=completion_next(path,&cursor,&entry,&end);if(status){error(status);break;}if(end)break;if(!completion_entry(&entry,stem,bytes))continue;
        output(entry.name,entry.name_bytes);size_t used=entry.name_bytes;if(entry.directory){print("/");++used;}if(++column==4){print("\n");column=0;}else {size_t width=columns/4,padding=used<width?width-used:1;while(padding--)print(" ");}
    }if(column)print("\n");prompt();
}
static void autocomplete(void){
    char prefix[4096],path[4096],parent[4096],best[256],expanded[4096],encoded[8192];size_t start,end;
    if(!term_completion(&editor,&start,&end,prefix,sizeof(prefix)))return;
    size_t bytes=cb_length(prefix),slash=0;for(size_t i=0;i<bytes;++i)if(prefix[i]=='/')slash=i+1;
    cb_copy(parent,prefix,slash);parent[slash]=0;if(!cb_path(path,sizeof(path),cwd,slash?parent:"."))return;
    const char *stem=prefix+slash;size_t stem_bytes=bytes-slash,common=0,matches=0;unsigned directory=0;uint64_t cursor=0;int eof=0;
    while(!eof){struct cb_filesystem_entry entry;cb_status_t status=completion_next(path,&cursor,&entry,&eof);if(status){input_finish();error(status);prompt();return;}if(eof)break;if(!completion_entry(&entry,stem,stem_bytes))continue;
        if(!matches){common=entry.name_bytes;cb_copy(best,entry.name,common);best[common]=0;directory=entry.directory;}else {size_t n=0;while(n<common && n<entry.name_bytes && best[n]==entry.name[n])++n;common=n;}++matches;
    }
    if(!matches)return;
    while(common && ((unsigned char)best[common]&0xc0)==0x80)--common;
    if(matches>1 && common<=stem_bytes){completion_list(path,stem,stem_bytes);return;}
    if(slash+common+2>sizeof(expanded))return;
    cb_copy(expanded,prefix,slash);cb_copy(expanded+slash,best,common);size_t n=slash+common;if(matches==1 && directory)expanded[n++]='/';
    size_t escaped=term_escape(encoded,sizeof(encoded),expanded,n);if(escaped==SIZE_MAX)return;
    if(matches==1 && !directory && end==editor.length)encoded[escaped++]=' ';
    if(term_replace(&editor,start,end,encoded,escaped))history_current=NULL;
}
static int edit(const struct cb_keyboard_event *e,int command_mode){
    if((e->kind==CB_KEY_DOWN || e->kind==CB_KEY_REPEAT) && e->usage_page==7){
        if(e->usage==40){input_finish();return 1;}
        if(e->usage==7 && (e->modifiers&0x11)){if(!editor.length)return 2;term_delete(&editor);}
        else switch(e->usage){
        case 42:term_backspace(&editor);break;
        case 76:term_delete(&editor);break;
        case 80:editor.cursor=term_previous(line,editor.cursor);break;
        case 79:editor.cursor=term_next(line,editor.length,editor.cursor);break;
        case 74:editor.cursor=0;break;
        case 77:editor.cursor=editor.length;break;
        case 73:if(e->kind==CB_KEY_DOWN)editor.overwrite=!editor.overwrite;break;
        case 82:if(command_mode)recall(1);break;
        case 81:if(command_mode)recall(0);break;
        case 43:if(command_mode)autocomplete();break;
        default:return 0;
        }
        input_cursor_reset();input_render(1);
    }else if(e->kind==CB_KEY_TEXT && term_insert(&editor,e->codepoint)){history_current=NULL;input_cursor_reset();input_render(1);}
    return 0;
}
