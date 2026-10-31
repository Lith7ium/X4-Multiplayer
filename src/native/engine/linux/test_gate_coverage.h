/* Function coverage (gate_coverage.h): marks at tick 1, the first call of a marked function trapped, restored and
 * run, an entry another hook owns skipped, everything back at the end of the window. Included by test_gate_linux.c. */
static void test_coverage_linux(void) {
    /* Three functions returning 11, 22, 33 (mov eax,imm32; ret), and one entry that is a jmp (another hook's). */
    const uint32_t base=LX_TEXT_VA+0x200000u;
    for(unsigned k=0;k<3;k++) {unsigned char f[6]={0xb8,0,0,0,0,0xc3};const uint32_t value=11u*(k+1);memcpy(f+1,&value,4);tl_write(LX(base+0x40*k),f,6);}
    {unsigned char j[5]={0xe9,0,0,0,0};tl_write(LX(base+0xc0),j,5);}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    cov_count=cov_marked=cov_skipped=cov_invalid=cov_armed=cov_open=cov_done=0;cov_hits=cov_foreign=cov_failures=0;memset(cov_table,0,sizeof(cov_table));
    char list[256];
    snprintf(list,sizeof(list),"# test\n%x\n0x%x\n%x\r\n%x\n10\n%x\n",base,base+0x40,base+0x80,base+0xc0,base);
    assert(cov_load_text(list,strlen(list)) && cov_count==4 && cov_invalid==1); /* 0x10 is outside the code, the repeat dropped */
    assert(!cov_load_text("12zz\n",5));
    assert(cov_arm(3) && cov_arm(3) && !cov_arm(4));
    typedef int (*fn)(void);
    boot_phase=3;stats.issued=1;cov_frame(1);
    assert(cov_marked==3 && cov_skipped==1 && *LX(base)==0xcc && *LX(base+0x40)==0xcc && *LX(base+0xc0)==0xe9 && !strcmp(cov_stage,"marked"));
    assert(((fn)(void*)LX(base))()==11 && *LX(base)==0xb8 && cov_hits==1);
    assert(((fn)(void*)LX(base))()==11 && cov_hits==1); /* once only */
    stats.issued=2;cov_frame(2);
    assert(((fn)(void*)LX(base+0x40))()==22 && cov_hits==2);
    char row[128];assert(!cov_row(0,row,sizeof(row))); /* no rows before the window ends */
    stats.issued=4;cov_frame(4);
    assert(cov_done && *LX(base+0x80)==0xb8 && !cov_failures && !strcmp(cov_stage,"done"));
    assert(((fn)(void*)LX(base+0x80))()==33 && cov_hits==2);
    char perms[5];perms_of(LX(base),perms);assert(!strcmp(perms,"r-xp"));
    assert(cov_row(0,row,sizeof(row)) && strstr(row,"tick=1") && cov_row(1,row,sizeof(row)) && strstr(row,"tick=2") && !cov_row(2,row,sizeof(row)));
    char status[384]; /* invalid=2: 0x10, and the 0x12 of the refused line */
    assert(cov_status(status,sizeof(status)) && strstr(status,"armed=1 ticks=3 listed=4 invalid=2 marked=3 skipped=1 hits=2 foreign=0 failures=0 stage=done"));
    cov_unmark();cov_armed=0;cov_count=0;memset(cov_table,0,sizeof(cov_table));
    printf("coverage PASS\n");
}
