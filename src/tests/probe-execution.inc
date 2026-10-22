/* Deliberately uneven placement, user spins, blocked pipe and dispatched caller
 * death. Only copied test media contains the executable/policies. */
static struct kernel_surface_result execution_load(const struct kernel_surfaces *hub,uint64_t fs,uint64_t mode,int movable){
    static uint64_t serial;uint64_t startup[2]={mode,++serial};
    struct process_request request={.abi=3,.filesystem=fs,.path="/system/kmodules/drivers/execution-probe.elf",.stack_bytes=131072,.startup=startup,.startup_bytes=sizeof(startup),.flags=movable?PROCESS_MIGRATABLE:0};
    struct kernel_surface_result loaded=CALL(kernel_surface_find(hub,"process.load"),&request,0,0);if(loaded.status)return loaded;
    const char *policy=mode==1?"{\"version\":1,\"resources\":[{\"name\":\"/@streams\",\"operations\":[\"pipe\",\"read\",\"write\"]}]}":mode==2?"{\"version\":1,\"resources\":[{\"name\":\"@execution-test\",\"operations\":[\"test\"]}]}":mode==3?"{\"version\":1,\"resources\":[{\"name\":\"@execution-test\",\"publish\":true,\"operations\":[\"test\"]},{\"name\":\"/@io\",\"operations\":[\"now_ns\"]}]}":"{\"version\":1,\"resources\":[]}";
    size_t bytes=0;while(policy[bytes])++bytes;struct handle_policy admission={.process=loaded.value,.manifest=policy,.bytes=bytes};admission.identity.installation_id[0]=loaded.value;
    if(CALL(kernel_surface_find(hub,"handles.admit"),&admission,0,0).status)return module_result(0,0);
    return loaded;
}
static int execution_finish(const struct kernel_surfaces *hub,uint64_t pid,uint64_t deadline,uint64_t code,struct probe_process *probe){
    kernel_surface_call info=kernel_surface_find(hub,"process.info"),reap=kernel_surface_find(hub,"process.reap"),clock=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control");
    for(;;){struct process_info value;if(CALL(info,pid,&value,0).status)return 0;probe->app_code=value.exit_code;if(value.state==PROCESS_EXITED){if(value.exit_code!=code)return 0;if(!CALL(reap,pid,0,0).status){++probe->completed;return 1;}}else if(value.state==PROCESS_FAULTED)return 0;
        uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
}
static int execution_test(const struct kernel_surfaces *hub,uint64_t fs,struct probe_process *probe){
    kernel_surface_call start=kernel_surface_find(hub,"process.start"),kill=kernel_surface_find(hub,"process.terminate"),clock=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control"),info=kernel_surface_find(hub,"process.info"),inspect=kernel_surface_find(hub,"handles.inspect");
    struct mem_pages_info baseline,after;kernel_surface_call pages=kernel_surface_find(hub,"mem.pages_info");if(CALL(pages,&baseline,0,0).status)return 0;
    uint64_t ids[4],deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000),before=0;struct thread_load load;
    for(unsigned cpu=0;cpu<probe->cpus;++cpu){if(CALL(control,THREAD_LOAD,cpu,&load).status)return 0;before+=load.migrations_in;}
    for(unsigned i=0;i<4;++i){struct kernel_surface_result p=execution_load(hub,fs,0,1);if(p.status || CALL(start,p.value,0,0).status)return 0;ids[i]=p.value;}
    uint64_t began=CALL(clock,0,0,0).value;
    for(;;){uint64_t migrated=0;for(unsigned cpu=0;cpu<probe->cpus;++cpu){if(CALL(control,THREAD_LOAD,cpu,&load).status)return 0;migrated+=load.migrations_in;}
        uint64_t time=CALL(clock,0,0,0).value;if(time-began>=100000000 && (probe->cpus==1 || migrated>before)){text("MEASURE user migrations: ");number(migrated-before);text("\n");
            for(unsigned cpu=0;cpu<probe->cpus;++cpu){if(CALL(control,THREAD_LOAD,cpu,&load).status)return 0;text("MEASURE scheduler cpu/runnable/running/blocked/max_wait_ns/migrations_in: ");number(cpu);text(" ");number(load.runnable);text(" ");number(load.running);text(" ");number(load.blocked);text(" ");number(load.max_wait_ns);text(" ");number(load.migrations_in);text("\n");}break;}
        if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
    for(unsigned i=0;i<4;++i)if(CALL(kill,ids[i],0,0).status || CALL(kill,ids[i],0,0).status || !execution_finish(hub,ids[i],deadline,CB_APPLICATION_TERMINATED,probe))return 0;
    for(unsigned mode=1;mode<=2;++mode){uint64_t server=0;if(mode==2){struct kernel_surface_result p=execution_load(hub,fs,3,0);if(p.status || CALL(start,p.value,0,0).status)return 0;server=p.value;struct handle_publication_info publication;while(CALL(inspect,"@execution-test",&publication,0).status){uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}}
        struct kernel_surface_result p=execution_load(hub,fs,mode,1);if(p.status || CALL(start,p.value,0,0).status)return 0;
        for(;;){struct process_info process;struct thread_info thread;if(CALL(info,p.value,&process,0).status || CALL(control,THREAD_INFO,process.thread,&thread).status)return 0;
            struct handle_publication_info publication;if(thread.state==THREAD_BLOCKED && (mode==1 || (!CALL(inspect,"@execution-test",&publication,0).status && publication.dispatched)))break;
            uint64_t time=CALL(clock,0,0,0).value;if(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status)return 0;}
        if(CALL(kill,p.value,0,0).status || !execution_finish(hub,p.value,deadline,CB_APPLICATION_TERMINATED,probe))return 0;
        if(server && !execution_finish(hub,server,deadline,0,probe))return 0;
    }
    if(CALL(pages,&after,0,0).status || baseline.allocations!=after.allocations || baseline.physical_pages!=after.physical_pages || baseline.stacks!=after.stacks || baseline.stack_pages!=after.stack_pages)return 0;
    text("PASS execution: uneven user migration, preemptive spin termination, blocked pipe cancellation, dispatched caller drain and late reply rejection\n");return 1;
}
