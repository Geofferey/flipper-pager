#!/usr/bin/env python3
"""Exercise actual TX source and decode its payloads on the host, without a radio.

Run: python tests/test_flex_tx.py [pager directory]
Uses CC (default cc), or cl from a Visual Studio developer environment.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# FBT's source glob also matches .cpython-*.pyc files in the app tree.
sys.dont_write_bytecode = True
from test_flex_recovery import SHIMS, without_includes

HARDWARE = r'''
#define furi_check assert
typedef struct { int unused; } GpioPin;
typedef struct { uint32_t start, value; } FuriHalCortexTimer;
typedef struct { int32_t (*entry)(void*); void* context; } FuriThread;
enum { FuriThreadPriorityHighest };
static uint32_t now, last_deadline;
static unsigned trace_count, stop_after;
static uint8_t offset, deviation;
static bool pin;
static struct { uint32_t ticks; uint8_t symbol; } trace[40000];
static void test_write_reg(uint8_t reg,uint8_t value) {
    if(reg==0x0c) offset=value;
    else { assert(reg==0x15); deviation=value; }
    now+=128; // Simulated SPI overhead must not accumulate in frame timing.
}
typedef uintptr_t Elf32_Addr;
typedef struct { int unused; } ElfApiInterface;
const ElfApiInterface* const firmware_api_interface=NULL;
static uint32_t elf_symbolname_hash(const char* s) { (void)s; return 0; }
static bool elf_resolve_from_hashtable(const ElfApiInterface* api,uint32_t hash,Elf32_Addr* out) {
    (void)api; (void)hash; *out=(uintptr_t)test_write_reg; return true;
}
static uint32_t furi_hal_cortex_instructions_per_microsecond(void) { return 64; }
static FuriHalCortexTimer furi_hal_cortex_timer_get(uint32_t us) {
    last_deadline=now; return (FuriHalCortexTimer){now,us*64};
}
static void furi_hal_gpio_write(const GpioPin* data,bool value) { (void)data; pin=value; now+=64; }
static void test_cancel(void);
static void furi_hal_cortex_timer_wait(FuriHalCortexTimer t) {
    assert(trace_count<COUNT_OF(trace));
    assert(t.start==last_deadline);
    assert(now-t.start<t.value);
    assert(offset==1 || offset==255);
    assert(deviation==0x10);
    trace[trace_count].ticks=t.value;
    trace[trace_count++].symbol=pin ? (offset==1 ? 3:2) : (offset==1 ? 1:0);
    now=t.start+t.value;
    last_deadline=now;
    now+=64; // Loop/callback overhead, including across counter wrap.
    if(stop_after && trace_count==stop_after) test_cancel();
}
static void furi_thread_set_current_priority(int priority) { (void)priority; }
static FuriThread* furi_thread_alloc_ex(const char* name,unsigned stack,int32_t (*entry)(void*),void* context) {
    (void)name; (void)stack;
    FuriThread* t=malloc(sizeof(*t)); t->entry=entry; t->context=context; return t;
}
static void furi_thread_start(FuriThread* t) { t->entry(t->context); }
static void furi_thread_join(FuriThread* t) { (void)t; }
static void furi_thread_free(FuriThread* t) { free(t); }
static DateTime rtc={.year=2026,.month=11,.day=15,.hour=12,.minute=0,.second=0,.weekday=7};
static void furi_hal_rtc_get_datetime(DateTime* out) { *out=rtc; }
'''

TESTS = r'''
static void test_cancel(void) { flex_tx_four_level.running=false; }
static unsigned pages;
static char received[4096];
static void on_page(SubGhzProtocolDecoderBase* base,void* context) {
    (void)context;
    SubGhzProtocolDecoderFlex* d=(void*)base;
    assert(d->phase=='A');
    assert(d->capcode==12345);
    pages++;
    strcpy(received,furi_string_get_cstr(d->generic.result_msg));
    if(strncmp(received,"ALN: ",5)==0) memmove(received,received+5,strlen(received+5)+1);
}
static void verify(FlexTxEncoder* e,PocsagTxConfig* c,unsigned mode) {
    static const unsigned frame_bytes[]={375,732,732,1446};
    static const unsigned rates[]={1600,3200,1600,3200};
    static const unsigned levels[]={2,2,4,4};
    static const unsigned markers[]={0x870c,0x7b18,0xb068,0xdea0};
    assert(e->frame_bytes==frame_bytes[mode]);
    assert(e->byte_count==e->frame_base+e->frames*frame_bytes[mode]);
    uint64_t expected_us=e->frame_base*8*625+e->frames*1875000ULL+119*625+48*667+625;
    if(mode>=2) {
        GpioPin data={0}; trace_count=0; stop_after=0; now=0xffff0000U;
        assert(flex_tx_four_level_start(e,&data));
        assert(flex_tx_four_level_complete());
        assert(!flex_tx_four_level_start(e,&data));
        assert(offset==0);
        uint64_t ticks=0;
        for(unsigned i=0;i<trace_count;i++) ticks+=trace[i].ticks;
        assert(ticks==expected_us*64);
        flex_tx_four_level_stop();
        assert(!flex_tx_four_level_complete());
    } else {
        uint64_t us=0; unsigned calls=0;
        for(;;) {
            LevelDuration ld=flex_tx_encoder_yield(e);
            if(level_duration_is_reset(ld)) break;
            us+=level_duration_get_duration(ld);
            assert(++calls<40000);
        }
        assert(us==expected_us+(c->inverted ? 625:0));
    }
    SubGhzProtocolDecoderFlex* d=subghz_protocol_decoder_flex_alloc(NULL);
    d->base.callback=on_page;
    d->four_level_path=mode>=2;
    pages=0; received[0]=0;
    size_t symbol=e->frame_base*8;
    for(unsigned f=0;f<e->frames;f++) {
        const size_t base=e->frame_base+f*frame_bytes[mode];
        assert(((unsigned)e->bytes[base+10]<<8|e->bytes[base+11])==markers[mode]);
        assert((e->bytes[base+4]^e->bytes[base+10])==255);
        assert((e->bytes[base+5]^e->bytes[base+11])==255);
        // Check the entire hardware trace, including each slow header and fast payload.
        for(size_t bit=base*8;bit<(base+frame_bytes[mode])*8;) {
            bool payload=bit>=base*8+144;
            unsigned count=payload && levels[mode]==4 ? 2:1;
            bool a=flex_tx_wire_bit(e,bit), b=count==2 && flex_tx_wire_bit(e,bit+1);
            unsigned s=a ? (b?2:3) : (b?1:0);
            if(mode>=2) {
                assert(trace[symbol].symbol==(c->inverted ? 3-s:s));
                assert(trace[symbol].ticks==(payload && rates[mode]==3200 ? 20000:40000));
            }
            symbol++; bit+=count;
        }
        // Acquire Sync 1, decode FIW, and decode all phases with the real receiver.
        for(size_t bit=base*8;bit<(base+frame_bytes[mode])*8;) {
            bool payload=bit>=base*8+144;
            unsigned count=payload && levels[mode]==4 ? 2:1;
            bool a=flex_tx_wire_bit(e,bit), b=count==2 && flex_tx_wire_bit(e,bit+1);
            unsigned s=a ? (b?2:3) : (b?1:0);
            flex_process_symbol(d,c->inverted ? 3-s:s);
            bit+=count;
        }
        assert(d->stats.fiw_valid==f+1);
        assert(d->stats.frames_completed==f+1);
        assert(d->baud==rates[mode] && d->levels==levels[mode]);
    }
    if(c->message_mode==PagerTxMessageTyped) {
        assert(pages>=1);
        if(c->message[0]) {
            if(strcmp(received,c->message)) fprintf(stderr,"mode=%u len=%zu received=[%s] expected=[%s]\n",mode,strlen(c->message),received,c->message);
            assert(strcmp(received,c->message)==0);
        }
    } else if(c->message_mode==PagerTxMessageBeacon) assert(pages==0);
    else if(e->frames==3) { assert(pages==1); assert(strcmp(received,c->message)==0); }
    else assert(pages==0);
    subghz_protocol_decoder_flex_free(d);
}
int main(void) {
    unsigned cases=0;
    const unsigned lengths[]={0,1,80,81,160,240};
    for(unsigned mode=0;mode<4;mode++) for(unsigned inv=0;inv<2;inv++) {
        PocsagTxConfig c={.protocol=PagerTxProtocolFlex,.ric=12345,
            .flex_3200=mode!=0,.flex_4level=mode>=2,.flex_6400=mode==3,
            .inverted=inv,.clock_biws=PCSG_TX_CLOCK_BOTH};
        FlexTxEncoder* e=flex_tx_encoder_alloc();
        for(unsigned n=0;n<COUNT_OF(lengths);n++) {
            for(unsigned i=0;i<lengths[n];i++) c.message[i]='A'+i%26;
            c.message[lengths[n]]=0;
            assert(flex_tx_encoder_prepare(e,&c)); verify(e,&c,mode); cases++;
        }
        strcpy(c.message,"6400 BEACON SEQUENCE");
        for(unsigned kind=PagerTxMessageBeacon;kind<=PagerTxMessageBeaconSeq;kind++) {
            c.message_mode=kind;
            // One timestamp outside the periodic slot, one inside it.
            for(unsigned t=0;t<2;t++) {
                rtc.minute=t ? 1:0; rtc.second=t ? 53:0;
                assert(flex_tx_encoder_prepare(e,&c)); verify(e,&c,mode); cases++;
                assert(e->frames==(kind==PagerTxMessageBeaconSeq && t ? 3:1));
            }
        }
        rtc.minute=rtc.second=0;
        if(mode>=2) {
            GpioPin data={0}; trace_count=0; stop_after=10; now=0xffff0000U;
            assert(flex_tx_four_level_start(e,&data));
            assert(trace_count==10 && offset==0);
            flex_tx_four_level_stop(); stop_after=0; cases++;
        }
        flex_tx_encoder_free(e);
    }
    printf("PASS: %u TX cases: four modes, both polarities, tone, 1-240 chars, fragmentation,\n"
           "beacons, phase decoding, exact frame timing, timer wrap, cancellation and cleanup.\n",cases);
}
'''

def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    firmware = root.parents[2]
    unit = SHIMS
    if Path(os.environ.get('CC', 'cc')).stem.lower() == 'cl':
        unit += r'''
static unsigned host_popcount(uint64_t n) { unsigned c=0; while(n) { n&=n-1; c++; } return c; }
static unsigned host_ctz(uint32_t n) { unsigned c=0; assert(n); while(!(n&1)) { n>>=1; c++; } return c; }
#define __builtin_popcount(n) host_popcount((uint32_t)(n))
#define __builtin_popcountll host_popcount
#define __builtin_ctz host_ctz
'''
    for name in ['lib/datetime/datetime.h', 'lib/datetime/datetime.c', 'lib/toolbox/level_duration.h']:
        unit += '\n#define furi_check assert\n' + without_includes((firmware / name).read_text())
    unit += HARDWARE
    for name in ['pocsag_tx.h', 'flex_tx.h', 'flex_tx.c', 'protocols/flex.h',
                 'protocols/flex_bch.h', 'protocols/flex_bch.c', 'protocols/flex.c']:
        source = (root / name).read_text()
        if name == 'protocols/flex.c':
            source = source[:source.index('SubGhzProtocolStatus subghz_protocol_decoder_flex_serialize(')]
        unit += '\n#undef TAG\n' + without_includes(source)
    unit += TESTS
    if Path(os.environ.get('CC', 'cc')).stem.lower() == 'cl':
        unit = unit.replace(r'\e', r'\x1b')  # MSVC has no GNU escape-character spelling.
    with tempfile.TemporaryDirectory(prefix='flex-tx-') as tmp:
        path = Path(tmp)
        source = path / 'test.c'
        source.write_text(unit)
        exe = path / ('test.exe' if os.name == 'nt' else 'test')
        cc = os.environ.get('CC', 'cc')
        if Path(cc).stem.lower() == 'cl':
            args = [cc, '/nologo', '/std:c11', '/Od', '/Zi', '/fsanitize=address',
                    '/D_CRT_SECURE_NO_WARNINGS', str(source), '/Fe:' + str(exe)]
        else:
            args = [cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(source), '-o', str(exe)]
        subprocess.run(args, cwd=path, check=True)
        subprocess.run([str(exe)], cwd=path, check=True)

if __name__ == '__main__':
    main()
