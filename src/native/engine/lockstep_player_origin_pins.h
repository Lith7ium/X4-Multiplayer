/* Full exact-image range and slot pins from the reviewed origin audit. */
static int origin_pins(void) {
    return hash_ok(image+0x001aba30,246,"ec38c42516226029599055391ce481f2aea48f21ad0d4e305a712b60d08b207c") &&
        hash_ok(image+0x00716d80,160,"799f73503d1970523fe2e4f5e9d49765d5f064ee1075a5165a386c928d13d1db") &&
        hash_ok(image+0x0099a210,468,"0e3ed2e1d468f72dd886645a3270366cf4eca25fb29f06fb58e0128d53c4dede") &&
        hash_ok(image+0x0099a3f0,807,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d") &&
        hash_ok(image+0x0099bb50,2331,"f6b47d17c23e03b5fe0ff5d6f36f5bbac8cd7cfa168ff3ed211df8795c39c9ff") &&
        hash_ok(image+0x0099c470,467,"b605dc6a10b27c1d26b2ecd6f30d8e7c93641c56162a89c9c995eee83d3447f5") &&
        hash_ok(image+0x0099c740,875,"c088ffa6885ba388e1085f5332d0905d65ede6eb7f0dcb47fec6eec9c237c1b7") &&
        hash_ok(image+0x00998700,24,"0d525161ea656c9990495b0d656c7e69ce2a0dd980143ef620f4a7431096ff09") &&
        hash_ok(image+0x00152860,6,"554d8cb4ae56115973e1129c5c379e14a498f16f858b7b88e9de1686fd9eb948") &&
        hash_ok(image+0x00152870,19,"b89c9991cf33644216ce830713b3291b1e3923e58aa08fccffee4f3a6d87d657") &&
        hash_ok(image+0x00706cc0,57,"4348afd1cc87ae418a286394a41325b058e4cb045d8a1110274e125998802718") &&
        hash_ok(image+0x00716e20,1481,"010ba9e22844f7a03046316572ee102ea8e689ec94e42ec048be93428f1d0852") &&
        hash_ok(image+0x000f04b0,166,"a63156dd2a7794da525f86b9847e8cbcab99bd1b1eba58baef2aef845dd67632") &&
        hash_ok(image+0x0018a150,1037,"10df659b91bf44068dcb3fcb88f2b72e00a3e091df8fb6eff072be75839e227b") &&
        hash_ok(image+0x00199db0,586,"570305e24c596b9a5c65f8cc98a044a9a303850816eee1a169ab7065acf3066c") &&
        hash_ok(image+0x001b5450,685,"b376a99d502c950f04c797ed9fb236843c0fe8ad16db3db16ddf02a00e7e69b9") &&
        hash_ok(image+0x003508e0,93,"3a48fd72ded5585551dcaa1396bc9c0400b3e5083bb99ce7b370a70b75c104fc") &&
        hash_ok(image+0x006a1a60,8,"785f88f0575511d53e4c60b6a077b7167c083c7dbe962ef4b2970aa0ad1e50b1") &&
        hash_ok(image+0x0035b8a0,1332,"21cf3cb1237650c26747101f4d94f94b8a13205850af9ec3d0bcae9da596d357") &&
        *(void**)(image+0x02a85278)==image+0x000f04b0 &&
        *(void**)(image+0x02a85280)==image+0x00152860 &&
        *(void**)(image+0x02a85288)==image+0x00152870 &&
        *(void**)(image+0x02b329c8)==image+0x00998700 &&
        *(void**)(image+0x02b33a70)==image+0x00706cc0 &&
        *(void**)(image+0x02abd728)==image+0x003508e0 &&
        *(void**)(image+0x02b45558)==image+0x0035b8a0 &&
        *(uint64_t*)(image+0x02cbe610)==0x3f847ae147ae147bULL &&
        *(uint64_t*)(image+0x02cbe470)==0x3f1a36e2eb1c432dULL;
}
