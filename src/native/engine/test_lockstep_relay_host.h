/* Remote relay address parsing (native_gate_relay_host). */
static void test_relay_host(void) {
    uint32_t a=0;
    assert(relay_host_parse("203.0.113.7",&a) && a==htonl(0xcb007107u));
    assert(relay_host_parse("127.0.0.1",&a) && a==htonl(INADDR_LOOPBACK));
    const char *bad[]={NULL,"","1.2.3","1.2.3.4.5","256.1.1.1","1..2.3","a.b.c.d","1.2.3.4 ","01234.1.1.1"," 1.2.3.4","1.2.3."};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!relay_host_parse(bad[i],&a));
}
