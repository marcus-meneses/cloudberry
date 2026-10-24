/* Test lifecycle ownership is retained by PID even after public withdrawal. */
static int pair_stop_provider(kernel_surface_call inspect,const char *name,uint64_t pid){
    log_text("GPU_PAIR RESTORE stop ");log_text(name);log_text(" pid=");log_number(pid);log_text("\n");
    kernel_surface_call terminate=kernel_surface_find(state.hub,"process.terminate"),reap=kernel_surface_find(state.hub,"process.reap"),release=kernel_surface_find(state.hub,"bootstrap.release");
    if(!terminate || !reap || !release || CALL(terminate,pid,0,0).status){log_text("GPU_PAIR RESTORE terminate failed\n");return 0;}
    uint64_t deadline=now()+UINT64_C(10000000000);
    while(CALL(reap,pid,0,0).status)if(now()>=deadline || !wait_tick()){log_text("GPU_PAIR RESTORE reap failed\n");return 0;}
    struct handle_publication_info after;
    if(!CALL(inspect,name,&after,0).status){log_text("GPU_PAIR RESTORE publication retained\n");return 0;}
    if(CALL(release,pid,0,0).status){log_text("GPU_PAIR RESTORE bootstrap release failed\n");return 0;}
    return 1;
}
static int pair_stop_pipeline(kernel_surface_call inspect){
    /* Capture every identity before releasing any dependency. Graphics exit may
     * cause display to exit and unpublish itself before its explicit stop. */
    const char *names[]={"/@text","/@mouse","/@graphics","/@display"};
    uint64_t pids[4];struct handle_publication_info publication;
    for(unsigned i=0;i<4;++i){
        if(CALL(inspect,names[i],&publication,0).status || !publication.process){log_text("GPU_PAIR RESTORE identity unavailable\n");return 0;}
        pids[i]=publication.process;
    }
    for(unsigned i=0;i<4;++i)if(!pair_stop_provider(inspect,names[i],pids[i]))return 0;
    return 1;
}
