/* Execute the shared pin checker against an independent read-only copy of the
 * native image, including its actual owned-patch reconstruction path. */
static void test_registry_origin_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt));
    assert(nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.OptionalHeader.SizeOfImage==0x6fa9000 && nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64));
    unsigned char *prior_image=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);assert(!ai_installed && !interrupt_installed && !registry_origin_enabled);
    test_thunk_native_image();
    test_tu_native_image();
    test_portable_clock_image();
    test_creation_native_image();
    memcpy(&ai_original_word,image+AI_WORD,16);memcpy(&interrupt_original_word,image+INTERRUPT_ENTRY,8);
    ai_patched_word=ai_original_word^1;interrupt_patched_word=interrupt_original_word^1;
    memcpy(image+AI_WORD,&ai_patched_word,16);memcpy(image+INTERRUPT_ENTRY,&interrupt_patched_word,8);
    ai_installed=interrupt_installed=1;
    assert(registry_origin_enable() && registry_origin_enabled && interrupt_origin_ready);
    unsigned identity_tails[]={0x352b10+7,0x745e00+710,0xea4a00+374,0xea8cf0+1331,0xeac530+581,0xe85360+1367};
    for(unsigned i=0;i<sizeof(identity_tails)/sizeof(*identity_tails);i++) {
        image[identity_tails[i]]^=1;assert(!registry_origin_identity_pins() && !registry_origin_enable());
        image[identity_tails[i]]^=1;assert(registry_origin_identity_pins());
    }
    for(unsigned i=0;i<sizeof(interrupt_callsites)/sizeof(*interrupt_callsites);i++) {
        interrupt_origin origin=interrupt_origin_read((uintptr_t)(image+interrupt_callsites[i].caller));
        assert(origin.source==1 && origin.raw==interrupt_callsites[i].caller && origin.caller==origin.raw);
    }
    unsigned tails[]={0x106d250+5133,INTERRUPT_ENTRY+742,0x10661b0+741,0x1066ba0+612,0x106afb0+459,0x106fd90+638,0x10a7930+463};
    for(unsigned i=0;i<sizeof(tails)/sizeof(*tails);i++) {
        image[tails[i]]^=1;assert(!interrupt_origin_prepare() && !interrupt_origin_ready);
        assert(interrupt_origin_read((uintptr_t)(image+0x1066dc6)).source==4);
        image[tails[i]]^=1;assert(interrupt_origin_prepare());
    }
    image[AI_WORD]^=2;assert(!interrupt_origin_prepare());image[AI_WORD]^=2;
    image[INTERRUPT_ENTRY]^=2;assert(!interrupt_origin_prepare());image[INTERRUPT_ENTRY]^=2;
    assert(interrupt_origin_prepare());registry_origin_close();assert(!registry_origin_enable());
    ai_installed=interrupt_installed=interrupt_origin_ready=registry_origin_enabled=registry_origin_closing=0;
    ai_original_word=ai_patched_word=0;interrupt_original_word=interrupt_patched_word=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior_image;
    puts("native IRQ origin: seven caller spans, six identity layout spans, nine calls, owned reconstruction and tamper refusals verified");
}
