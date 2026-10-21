#ifndef CLOUDBERRY_MONITOR_MODEL_H
#define CLOUDBERRY_MONITOR_MODEL_H
#include "../../interfaces/applications.h"
/* One process thread today; 1000 denotes 100.0% of one logical CPU. */
static inline int monitor_cpu(const struct cb_process_sample *old,const struct cb_process_sample *now){
    if(old->process!=now->process || !(old->flags&now->flags&CB_PROCESS_CPU_VALID) || now->timestamp_ns<=old->timestamp_ns || now->cpu_ns<old->cpu_ns)return -1;
    uint64_t elapsed=now->timestamp_ns-old->timestamp_ns,used=now->cpu_ns-old->cpu_ns;if(used>=elapsed)return 1000;
    while(used>UINT64_MAX/1000){used>>=1;elapsed>>=1;}return (int)(used*1000/elapsed);
}
static inline const char *monitor_state(const struct cb_process_sample *s){
    if(s->flags&CB_PROCESS_STOP_REQUESTED)return "STOP";
    switch(s->state){case CB_PROCESS_LOADING:return "LOAD";case CB_PROCESS_READY:return "READY";case CB_PROCESS_EXITED:return "EXIT";case CB_PROCESS_FAULTED:return "FAULT";case CB_PROCESS_REAPING:return "REAP";case CB_PROCESS_CLEANUP:return "CLEAN";default:break;}
    return s->thread_state==3 || s->thread_state==7?"SLEEP":s->thread_state==1?"READY":"RUN";
}
static inline size_t monitor_page(size_t page,size_t count,uint32_t rows,uint16_t usage){
    if(rows<5)return 0;
    size_t pages=count?(count-1)/(rows-4)+1:1;if(page>=pages)page=pages-1;
    if(usage==82 && page)--page;
    else if(usage==81 && page+1<pages)++page;
    return page;
}
#endif
