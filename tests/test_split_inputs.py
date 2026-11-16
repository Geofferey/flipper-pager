#!/usr/bin/env python3
"""Check the shared numeric scene in both RX and TX builds, without hardware.

Uses CC (default cc), or CC=cl in a Visual Studio developer environment.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

SHIMS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define furi_assert assert
#define UNUSED(x) (void)(x)
enum { POCSAGPagerSceneCapcodeInput, POCSAGPagerViewNumberInput,
       SceneManagerEventTypeCustom, PCSGCustomEventSceneCapcodeInputDone };
typedef struct { int type; uint32_t event; } SceneManagerEvent;
typedef struct { uint32_t frequency; } Preset;
typedef struct { Preset* preset; void* radio_device; bool rx_restart; } Radio;
typedef struct {
    uint32_t capcode_min, capcode_max, frame_min, frame_max, ric_min, ric_max;
    int32_t frequency_correction_hz;
    PCSGCapcodeFilter capcode_filter, frame_filter, ric_filter;
} Filter;
typedef struct {
    Radio* txrx;
    struct { uint32_t frequency; } tx_config;
    Filter filter;
    void *scene_manager, *notifications, *view_dispatcher, *number_input;
} POCSAGPagerApp;
static uint32_t field, done, errors;
static int previous, current, minimum, maximum;
static const char* header;
static void (*callback)(void*,int32_t);
static void* callback_context;
static int sequence_error;
static uint32_t scene_manager_get_scene_state(void* scene,int id) {
    (void)scene; (void)id; return field;
}
static void scene_manager_previous_scene(void* scene) { (void)scene; previous++; }
static bool subghz_devices_is_frequency_valid(void* radio,uint32_t hz) {
    (void)radio; return hz==439987500U;
}
static void notification_message(void* n,const void* seq) { (void)n; (void)seq; errors++; }
static void view_dispatcher_send_custom_event(void* v,uint32_t event) {
    (void)v; assert(event==PCSGCustomEventSceneCapcodeInputDone); done++;
}
static void view_dispatcher_switch_to_view(void* v,int id) {
    (void)v; assert(id==POCSAGPagerViewNumberInput);
}
static void number_input_set_header_text(void* n,const char* text) { (void)n; header=text; }
static void number_input_set_result_callback(void* n,void (*cb)(void*,int32_t),void* ctx,
        int32_t value,int32_t min,int32_t max) {
    (void)n; callback=cb; callback_context=ctx; current=value; minimum=min; maximum=max;
}
'''

TESTS = r'''
static void open_field(POCSAGPagerApp* app,uint32_t id,const char* title,
        int32_t value,int32_t min,int32_t max) {
    field=id;
    pocsag_pager_scene_capcode_input_on_enter(app);
    assert(!strcmp(header,title));
    assert(current==value && minimum==min && maximum==max);
}
int main(void) {
    Preset preset={433920000U}; Radio radio={.preset=&preset};
    POCSAGPagerApp app={.txrx=&radio,.tx_config={433920000U}};
#ifdef PCSG_TX_APP
    const uint32_t frequency_field=PCSGNumberFieldTxFrequency;
    const char* frequency_header="TX frequency, Hz";
    uint32_t* frequency=&app.tx_config.frequency;
#else
    const uint32_t frequency_field=PCSGNumberFieldRxFrequency;
    const char* frequency_header="RX frequency, Hz";
    uint32_t* frequency=&preset.frequency;
    struct { uint32_t field; const char* exact; const char* range; uint32_t* value; int32_t max; }
    filters[]={
        {PCSGNumberFieldCapcodeMin,"Capcode","Capcode min",&app.filter.capcode_min,2147483647},
        {PCSGNumberFieldCapcodeMax,"Capcode max","Capcode max",&app.filter.capcode_max,2147483647},
        {PCSGNumberFieldFrameMin,"Frame (0-127)","Frame min (0-127)",&app.filter.frame_min,127},
        {PCSGNumberFieldFrameMax,"Frame max (0-127)","Frame max (0-127)",&app.filter.frame_max,127},
        {PCSGNumberFieldRicMin,"RIC","RIC min",&app.filter.ric_min,2097151},
        {PCSGNumberFieldRicMax,"RIC max","RIC max",&app.filter.ric_max,2097151},
    };
    for(unsigned range=0;range<2;range++) {
        app.filter.capcode_filter=app.filter.frame_filter=app.filter.ric_filter=
            range ? PCSGCapcodeFilterRange : PCSGCapcodeFilterExact;
        for(unsigned i=0;i<sizeof(filters)/sizeof(filters[0]);i++) {
            *filters[i].value=12;
            open_field(&app,filters[i].field,range ? filters[i].range : filters[i].exact,
                12,0,filters[i].max);
            callback(callback_context,filters[i].max);
            assert(*filters[i].value==(uint32_t)filters[i].max);
        }
    }
#endif
    open_field(&app,frequency_field,frequency_header,433920000,300000000,950000000);
    errors=done=0;
    callback(callback_context,500000000);
    assert(*frequency==433920000U && errors==1 && done==1 && !radio.rx_restart);
    callback(callback_context,439987500);
    assert(*frequency==439987500U && errors==1 && done==2);
#ifdef PCSG_TX_APP
    assert(!radio.rx_restart && preset.frequency==433920000U);
#else
    assert(radio.rx_restart && app.tx_config.frequency==433920000U);
#endif
    radio.rx_restart=false;
    app.filter.frequency_correction_hz=-200;
    open_field(&app,PCSGNumberFieldFreqCorrection,"Correction, Hz +/-",-200,-15000,15000);
    const int32_t values[]={-15000,-151,-150,-149,0,149,150,151,15000};
    const int32_t expected[]={-15000,-200,-200,-100,0,100,200,200,15000};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++) {
        callback(callback_context,values[i]);
        assert(app.filter.frequency_correction_hz==expected[i] && !radio.rx_restart);
    }
    assert(pocsag_pager_scene_capcode_input_on_event(&app,(SceneManagerEvent){
        SceneManagerEventTypeCustom,PCSGCustomEventSceneCapcodeInputDone}));
    assert(previous==1);
    assert(!pocsag_pager_scene_capcode_input_on_event(&app,(SceneManagerEvent){-1,0}));
    puts("PASS: numeric headers, bounds, destination fields, frequency validation, rounding and events");
}
'''


def main():
    root = Path(__file__).resolve().parents[1]
    settings = (root / 'pocsag_pager_settings.h').read_text()
    definitions = settings[:settings.index('/** FLEX phase')]
    definitions = re.sub(r'^\s*#(?:include|pragma once)[^\n]*', '', definitions, flags=re.M)
    definitions += '\n'.join(re.findall(
        r'^#define PCSG_FREQ_CORRECTION_(?:MIN_HZ|MAX_HZ|STEP_HZ)\s+[^\n]+', settings, re.M))
    scene = (root / 'scenes/pocsag_pager_scene_capcode_input.c').read_text()
    scene = re.sub(r'^#include[^\n]*', '', scene, flags=re.M)
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='pager-input-') as tmp:
        folder = Path(tmp)
        for tx in (False, True):
            source = folder / 'test.c'
            source.write_text('#include <stdint.h>\n' + definitions + '\n' +
                              ('#define PCSG_TX_APP\n' if tx else '') + SHIMS + scene + TESTS)
            exe = folder / ('test.exe' if os.name == 'nt' else 'test')
            if Path(cc).stem.lower() == 'cl':
                args = [cc, '/nologo', '/std:c11', '/Od', '/Zi', '/fsanitize=address',
                        str(source), '/Fe:' + str(exe)]
            else:
                args = [cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                        str(source), '-o', str(exe)]
            subprocess.run(args, cwd=folder, check=True)
            print('TX' if tx else 'RX', flush=True)
            subprocess.run([str(exe)], cwd=folder, check=True)


if __name__ == '__main__':
    main()
