typedef struct {unsigned rva,size;const char *sha;} world_wait_pin;
static const world_wait_pin world_wait_pin_list[]={
 {0x1069000,4804,"a467e6dfa7fe07f49e9f9424147ffbe54c8f4ab52f024db0ca1b6a0282405969"},
 {0xb5a060,1833,"e47ee5624a5cfc775b5e02dffbdc2836a8646c49374bcbd069e5e2a2d7afdf4b"},
 /* Saved XML time reader, complete numeric parser including jump tables,
  * ASCII digit classifier, and the four binary64 arithmetic constants. */
 {0x12fd30,143,"a05f9ffc3ceaa4253b4642b40f97987ae4054ca43cebb583144bc8ac2f54c44e"},
 {0x14c3090,1204,"2cdc7e8eb8fe0a5de5594a3ef478e1cf3381777a7d3ac0adf7beba54e12ec944"},
 {0x1a05280,166,"276edb93ae96b154bd7f4f6902505a2069679a082313a7b361b248d8ed9b6776"},
 {0x2cbe9f8,8,"24b1f4ef66b650ff816e519b01742ff1753733d36e1b4c3e3b52743168915b1f"},
 {0x2cbea98,8,"a24bdf11de84ffe725eb73b09bf6885651f35425491c211bbad9735bcae67967"},
 {0x2cbe6e8,8,"45d2b662d9d490ae9b932759c910b26b9a874065de620f4ac0a3a23a65e40b8c"},
 {0x2cbe870,8,"6c3c396ed6b5c36dcae172271f462051b1266b851e92df3deea8ac65478fd712"},
 {0x10a4ea0,156,"ce78b1096aca48f82a2f9e66420cdebe2718997d9513f2e9794c366ed69af323"},
 {0x10998f0,319,"6811db36ed68ebe50ac3fcce323aaa712fc9c90265d44516d279af7a2b994e06"},
 {0x10a50d0,93,"eebaef8c3abd745eccb74b4877a66363d994103ac25f834faf6ac5e52d24d954"},
 {0x1062a50,7,"0561483817089c1834dfa134d262316fc3cdb3713cf5853d41dca7cf68772d39"},
 {0x109a490,123,"ede114fdfc8f1f6207f7724956fffa2c2fb16455ab92fe18e7f4dab3fe14fbb7"},
 {0x109a130,861,"e9ed2e8873dd3d959d45208bf325d48ae13186d9b629900977c3ce3e091792f8"},
 {0xb59ed0,87,"52f66c866282323825b2ce7674820b7e2ce891adfde2137e8b75048575995675"},
 {0x10668b0,153,"bb690671b43c8f40ebaf0bff539cf960d47c384ccdca73a964304ae1df2fd851"},
 {0x1063750,3437,"0d1c301108dac632a0163a03f309310e188c2df418a4222694fcec3ca646c056"},
 {0xc59be0,220,"3f483835f3e22ee4087155b5da2a914efcb9dde6235f3a7b96acbafa9bd662e0"},
 {0xc58da0,3224,"6497e4f7003ee60b44682b9ecf4fc83e4bad1370f4ccfd4679d2b1e026480ae0"},
 {0xc588b0,1264,"4e53405584287d07f69a5a7684e33ba24283b5f0e4b257fdb963eb908b15a56c"},
 {0xd1e810,1662,"a4ba7d7bc74858b0d5c1540ac95ec27dc85fb0b6aba68730341b4b918032b47c"},
 {0xd2a1b0,983,"542fbf0b7b19c3aa910a463fd52fb2bec9f1ab23a23dcd5b86d9054df8f83729"},
 {0x14ddd10,257,"1db1ba392a17c5d2bd2cbca21a6d2f7234bbf6e6c726a0ef0d72594c19acf921"},
 {0x14ddc90,120,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e"},
 {0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d"},
 {0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434"},
 {0x106d5b1,40,"98cec6c522690356d88f9c9b8be8ef9e0cd59054dc9c2ea9db26b123bcd5b154"}
};
static int world_wait_pins(void) {
    if(!world_queue_phase_pins()) return 0;
    for(unsigned i=0;i<sizeof(world_wait_pin_list)/sizeof(world_wait_pin_list[0]);i++) {
        const world_wait_pin *p=&world_wait_pin_list[i];if(!hash_ok(image+p->rva,p->size,p->sha)) return 0;
    }
    uintptr_t restore=0,predicate=0;
    return read_local(image+WORLD_WAIT_VTABLE+0x28,&restore,8) && restore==(uintptr_t)(image+WORLD_WAIT_ENTRY) &&
        read_local(image+WORLD_WAIT_VTABLE+8,&predicate,8) && predicate==(uintptr_t)(image+0x1062a50);
}
