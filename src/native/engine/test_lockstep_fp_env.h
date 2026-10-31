/* native_gate_fp_env: another thread's MXCSR set under suspension and restored, the gate
 * hook's thread repaired every frame whichever thread is the UI thread, the calling
 * thread's recorded value restored, the g_ThreadStartHook swap state, arm refusals, status;
 * with X4.exe: the pins, the hook pointer's reader in the thread procedure and the FTZ
 * setter's call from GameClient::Init. */
static volatile LONG test_fpenv_stop;
static volatile unsigned test_fpenv_seen;
static DWORD WINAPI test_fpenv_worker(LPVOID start) {
    fpenv_setcsr((unsigned)(uintptr_t)start);
    while(!InterlockedCompareExchange(&test_fpenv_stop,0,0)) {test_fpenv_seen=fpenv_getcsr();Sleep(1);}
    return 0;
}
static void test_fp_env(void) {
    uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase;
    unsigned saved_csr=fpenv_getcsr();
    /* Other threads: with the test executable standing in for X4.exe its threads count as X4 threads. */
    unsigned char *prior=image;image=(unsigned char*)GetModuleHandleW(NULL);
    test_fpenv_stop=0;test_fpenv_seen=0;
    HANDLE worker=CreateThread(NULL,0,test_fpenv_worker,(LPVOID)(uintptr_t)0x9fc0u,0,NULL);assert(worker);
    while(test_fpenv_seen==0) Sleep(1);
    assert((test_fpenv_seen&FPENV_CONTROL)==0x9fc0u);
    fpenv_record_count=0;fpenv_value_kinds=0;
    assert(pmath_freeze());unsigned changed=fpenv_visit(1,1);pmath_thaw();
    assert(changed>=1 && fpenv_threads>=1 && fpenv_record_count>=1);
    test_fpenv_seen=0;while(test_fpenv_seen==0) Sleep(1);Sleep(5);
    assert((test_fpenv_seen&FPENV_CONTROL)==FPENV_TARGET);
    int found=0;for(unsigned i=0;i<fpenv_value_kinds;i++) found|=fpenv_values[i]==0x9fc0u;assert(found);
    /* observe does not write */
    assert(pmath_freeze());assert(fpenv_visit(0,0)==0);pmath_thaw();
    /* restore puts the recorded control bits back */
    assert(fpenv_restore_threads()>=1);test_fpenv_seen=0;while(test_fpenv_seen==0) Sleep(1);Sleep(5);
    assert((test_fpenv_seen&FPENV_CONTROL)==0x9fc0u);
    InterlockedExchange(&test_fpenv_stop,1);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    image=prior;
    /* The UI thread: repaired every tick while installed in 'default'; the hook pointer must stay ours. */
    void *hook=image+FPENV_HOOK_FN;memcpy(image+FPENV_HOOK_PTR,&hook,8);assert(fpenv_hook_state()==0);
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    assert(!fpenv_arm(FPENV_DEFAULT) && !strcmp(fpenv_stage,"pins"));
    assert(!fpenv_arm(FPENV_OFF) && !fpenv_arm(7));
    stats.issued=1;assert(!fpenv_arm(FPENV_DEFAULT) && !strcmp(fpenv_stage,"state"));stats.issued=0;
    hook=(void*)fpenv_thread_start;memcpy(image+FPENV_HOOK_PTR,&hook,8);assert(fpenv_hook_state()==1);
    fpenv_installed=1;fpenv_mode=FPENV_DEFAULT;fpenv_ui_before=0x9f80u;fpenv_repairs_ui=0;fpenv_last_sample=0;fpenv_record_count=0;fpenv_samples=0;
    ui_thread=GetCurrentThreadId()+1; /* live: the hook runs on the main thread, the UI on the MoveWorker */
    fpenv_setcsr(FPENV_TARGET);assert(fpenv_tick(0) && !fpenv_samples && !fpenv_repairs_ui); /* no suspension during the load */
    fpenv_setcsr(0x9f80u);assert(fpenv_tick(5) && (fpenv_getcsr()&FPENV_CONTROL)==FPENV_TARGET && fpenv_repairs_ui==1);
    assert(fpenv_samples==1 && fpenv_tick(6) && fpenv_repairs_ui==1 && fpenv_samples==1);
    assert(fpenv_owned());hook=(void*)0x1234;memcpy(image+FPENV_HOOK_PTR,&hook,8);
    assert(!fpenv_owned() && !fpenv_tick(7) && !strcmp(fpenv_stage,"hook_changed"));
    hook=(void*)fpenv_thread_start;memcpy(image+FPENV_HOOK_PTR,&hook,8);fpenv_failures=0;
    char out[512];assert(fpenv_status(out,sizeof(out))>0 && strstr(out,"installed=1 mode=default ui_before=9f80 ") && strstr(out," repairs_ui=1 "));
    /* Stopping on the main thread: it was 0x9f80 when armed (a frozen thread then, so recorded). */
    fpenv_records[0]=(fpenv_record){GetCurrentThreadId(),0x9f80u};fpenv_record_count=1;
    fpenv_restore();assert(!fpenv_installed && fpenv_restored && fpenv_hook_state()==0 && (fpenv_getcsr()&FPENV_CONTROL)==0x9f80u);
    fpenv_record_count=0;ui_thread=GetCurrentThreadId();
    /* observe: no pointer, no repair */
    fpenv_installed=1;fpenv_mode=FPENV_OBSERVE;fpenv_setcsr(0x9f80u);assert(fpenv_tick(700) && (fpenv_getcsr()&FPENV_CONTROL)==0x9f80u);
    fpenv_restore();assert(!fpenv_installed && fpenv_hook_state()==0);
    memset(image+FPENV_HOOK_PTR,0,8);fpenv_restored=0;fpenv_stage="idle";fpenv_value_kinds=0;fpenv_record_count=0;fpenv_samples=0;fpenv_repairs_ui=0;
    fpenv_setcsr(saved_csr);
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("fp env: other thread set/observed/restored under suspension, UI repair, hook state, arm refusals PASS\n");
}
static void test_fp_env_native_image(const char *path) {
    unsigned char *prior=image;image=test_pmath_map(path);
    assert(fpenv_pins());
    /* GameClient::Init calls the FTZ setter, which sets MXCSR bit 15 (bts eax, 0xf; ldmxcsr). */
    assert(image[0x1225ee7]==0xe8 && 0x1225eecu+*(int32_t*)(image+0x1225ee8)==0x14dd0e0u);
    assert(!memcmp(image+0x14dd0fd,"\x0f\xba\xe8\x0f",4) && !memcmp(image+0x14dd105,"\x0f\xae\x54\x24",4));
    /* The thread procedure reads g_ThreadStartHook (mov rax,[rip+..]; test; je; call rax). */
    assert(image[0x14b7783]==0x48 && image[0x14b7784]==0x8b && image[0x14b7785]==0x05 &&
           0x14b778au+*(int32_t*)(image+0x14b7786)==FPENV_HOOK_PTR && !memcmp(image+0x14b778f,"\xff\xd0",2));
    /* X4's own initialiser stores X4::ThreadStartHook there (lea rax,[rip-0x91]; mov [rip+..],rax). */
    assert(0x963e1u+*(int32_t*)(image+0x963dd)==FPENV_HOOK_FN && 0x963e8u+*(int32_t*)(image+0x963e4)==FPENV_HOOK_PTR);
    image[FPENV_HOOK_FN+0x10]^=1;assert(!fpenv_pins());image[FPENV_HOOK_FN+0x10]^=1;assert(fpenv_pins());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("fp env native pins, FTZ setter call, thread procedure hook read and initialiser verified");
}
