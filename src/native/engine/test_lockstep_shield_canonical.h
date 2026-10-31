/* native_gate_world_hash_shields: the pinned read and getters the canonical form reproduces. */
static void test_shield_canonical_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    /* The source's slots hold preferred-base addresses in the file; the loader relocates them. */
    const unsigned slots[3]={0x2b19708,0x2b19710,0x2b19718},targets[3]={0x790950,0x790990,0x7909b0};
    for(unsigned i=0;i<3;i++) {
        uint64_t stored;memcpy(&stored,image+slots[i],8);assert(stored==0x140000000ULL+targets[i]);
        void *relocated=image+targets[i];memcpy(image+slots[i],&relocated,8);
    }
    assert(shield_canonical_pins());
    /* What the read does: max from slot 1 ([rax+8]), the ready time changed (rbx-0x18) + slot 2, the rate from slot 3,
     * the 1e-4 rate floor, and the write when full; value at rbx-0x20, the source at rbx-0x10. */
    assert(!memcmp(image+0x79ddbe,"\x48\x8b\x49\xf0",4) && !memcmp(image+0x79ddc8,"\xff\x50\x08",3));
    assert(!memcmp(image+0x79ddf1,"\xf2\x0f\x10\x4b\xe0",5) && !memcmp(image+0x79de13,"\xff\x50\x10",3) && !memcmp(image+0x79de16,"\xf2\x0f\x10\x4b\xe8",5));
    assert(!memcmp(image+0x79de47,"\xff\x50\x18",3));
    int32_t rel;memcpy(&rel,image+0x79de5c,4);float floor_;memcpy(&floor_,image+0x79de60+rel,4);assert(floor_==9.99999975e-05f);
    memcpy(&rel,image+0x79ddda,4);double minimum;memcpy(&minimum,image+0x79ddde+rel,8);assert(minimum==1e-7);
    /* The thunk: rcx -= [rcx-4], then the read. */
    assert(!memcmp(image+0x7c5f0c,"\x48\x63\x41\xfc\x48\x2b\xc8\xe9",8));memcpy(&rel,image+0x7c5f14,4);assert(0x7c5f18+rel==0x79ddb0);
    /* Slot 2 and 3 jump to the delay and rate bodies; the default modifier of slot 1 is 1.0. */
    memcpy(&rel,image+0x790999,4);assert(0x79099d+rel==0x791660);
    memcpy(&rel,image+0x7909b9,4);assert(0x7909bd+rel==0x7916d0);
    memcpy(&rel,image+0x790977,4);float one;memcpy(&one,image+0x79097b+rel,4);assert(one==1.0f);
    /* Tampering anywhere refuses. */
    const unsigned tamper[]={0x79ddb0,0x79de5b,0x79df27,0x790950,0x790983,0x790990,0x7909b0,0x791660,0x7916cc,0x7916d0,0x79173d,0x7c5f0c};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!shield_canonical_pins());image[tamper[i]]^=1;}
    void *other=image+0x790930;memcpy(image+slots[1],&other,8);assert(!shield_canonical_pins());
    void *back=image+targets[1];memcpy(image+slots[1],&back,8);assert(shield_canonical_pins());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("world hash shields native pins, the read's layout and constants, the getter jumps and tamper refusals verified");
}
