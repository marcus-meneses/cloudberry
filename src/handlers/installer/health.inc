/* Diagnostics are owned copies, never pointers into a retired registry. */
struct rejected_app {struct rejected_app *next;uint64_t owner,status,bytes;unsigned char identity[16];char name[];};
static struct rejected_app *rejected;
static unsigned registry_trusted;
static cb_status_t quarantine(uint64_t owner,const struct cb_install_record *r,const unsigned char *name,cb_status_t status){
    struct rejected_app *p=allocate(sizeof(*p)+r->name_bytes);if(!p)return CB_RESOURCE_FAILURE;
    *p=(struct rejected_app){.next=rejected,.owner=owner,.status=status,.bytes=r->name_bytes};cb_copy(p->identity,r->installation,16);cb_copy(p->name,name,r->name_bytes);rejected=p;if(failed<2)failed=1;last_status=status;phase=12;return CB_OK;
}
static int unavailable(uint64_t owner,const unsigned char id[16]){for(struct rejected_app *p=rejected;p;p=p->next)if(p->owner==owner && cb_install_equal(p->identity,id,16))return 1;return 0;}
static void clear_rejected(void){while(rejected){struct rejected_app *p=rejected;rejected=p->next;release(p);}}
static cb_status_t initialize_store(void);

static void rehabilitate(uint64_t owner,const unsigned char identity[16]){struct rejected_app **at=&rejected;while(*at){struct rejected_app *p=*at;if(p->owner==owner && cb_install_equal(p->identity,identity,16)){*at=p->next;release(p);break;}at=&p->next;}failed=rejected?1:0;}
