#define STRESS_CHECK(x) do{if(!(x)){text("FAIL handle stress line ");number(__LINE__);text(" app ");number(probe->app_code);text("\n");return 0;}}while(0)
/* Real user processes; no test-only application syscall or shared memory. */
static int handle_stress(const struct kernel_surfaces *hub,uint64_t filesystem,struct probe_process *probe){
    kernel_surface_call load=kernel_surface_find(hub,"process.load"),start=kernel_surface_find(hub,"process.start"),clock=kernel_surface_find(hub,"clock.now_ns"),control=kernel_surface_find(hub,"threads.control"),available=kernel_surface_find(hub,"handles.available"),inspect=kernel_surface_find(hub,"handles.inspect"),pages=kernel_surface_find(hub,"mem.pages_info"),profiling=kernel_surface_find(hub,"handles.profile");
    uint64_t heap_before=0,heap_after=0;struct mem_pages_info warm={0},after;
    STRESS_CHECK(!(profiling && CALL(profiling,HP_LIVE,&heap_before,0).status));
    for(unsigned cycle=0;cycle<2;++cycle){
        uint64_t ids[7],deadline=CALL(clock,0,0,0).value+UINT64_C(30000000000);
        struct process_request r={.abi=1,.filesystem=filesystem,.path="/system/kmodules/drivers/handle-stress.elf",.stack_bytes=131072};
        /* Construct every actor before starting: no ELF/manifest I/O in the
           short dispatched-timeout window. Spread clients across available CPUs. */
        for(unsigned i=0;i<7;++i){r.cpu=i%probe->cpus;r.argument=i+1;struct kernel_surface_result p=CALL(load,&r,0,0);STRESS_CHECK(!(p.status || !admit_file(hub,filesystem,p.value,i?"/system/kmodules/drivers/stress-client.json":"/system/kmodules/drivers/stress-server.json")));ids[i]=p.value;}
        STRESS_CHECK(!(CALL(start,ids[0],0,0).status));
        while(!CALL(available,"@stress",0,0).value){uint64_t time=CALL(clock,0,0,0).value;STRESS_CHECK(!(time>=deadline || CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status));}
        for(unsigned i=1;i<=3;++i)STRESS_CHECK(!(CALL(start,ids[i],0,0).status));
        for(unsigned i=1;i<=3;++i)STRESS_CHECK(!(!finished(hub,ids[i],deadline,probe)));
        STRESS_CHECK(!(CALL(start,ids[4],0,0).status));
        struct handle_publication_info state;
        for(;;){STRESS_CHECK(!(CALL(inspect,"@stress",&state,1).status));if(state.dispatched==1)break;uint64_t time=CALL(clock,0,0,0).value;STRESS_CHECK(time<deadline && !CALL(control,THREAD_BLOCK_UNTIL,time+1000000,0).status);}
        /* A queued call must time out without dispatch; unrelated kernel work
           must finish while the slow dispatched operation is still outstanding. */
        STRESS_CHECK(!(CALL(start,ids[5],0,0).status || CALL(start,ids[6],0,0).status));
        STRESS_CHECK(!(!finished(hub,ids[6],deadline,probe) || CALL(inspect,"@stress",&state,1).status || state.completed!=36));
        STRESS_CHECK(!(!finished(hub,ids[5],deadline,probe) || !finished(hub,ids[4],deadline,probe) || !finished(hub,ids[0],deadline,probe)));
        STRESS_CHECK(!(CALL(available,"@stress",0,0).value || CALL(pages,&after,0,0).status));
        STRESS_CHECK(!(profiling && (CALL(profiling,HP_LIVE,&heap_after,0).status || heap_after!=heap_before)));
        STRESS_CHECK(!(cycle && (after.allocations!=warm.allocations || after.physical_pages!=warm.physical_pages || after.stacks!=warm.stacks || after.stack_pages!=warm.stack_pages)));
        warm=after;
    }
    text("PASS handle concurrency: three clients, 4096-byte integrity, slow provider, queued/dispatched deadlines, late retained reply, unrelated progress and two restart cycles\n");
    text("PASS handle resources: post-warmup page/stack baseline restored");if(profiling)text("; broker heap blocks restored each cycle");text("\n");return 1;
}

#undef STRESS_CHECK
