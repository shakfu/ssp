// Copyright 2026 shakfu. SPDX-License-Identifier: GPL-3.0-only
// ezep's additions to _core's desktop engine (core_engine.h), for functions the hardware reaches only
// with Tap held. scripts/fetch_core.sh appends this file to the firmware source before prepare.py
// moves the globals into CoreEngine, so these bodies are rewritten with the firmware's.
//
// Pressing Tap through the virtual panel instead would also tap the tempo: two edits within a second
// set it. These apply the state the firmware's Tap branches apply, without the press. Each value is
// on the firmware's 0-1023 knob scale. Call on the engine's thread, as core_engine_process.

// The trigger and clock outputs, without the rest of core_engine_display, for every frame.
void core_engine_ssp_outputs(CoreEngine *e,bool *trigger,bool *clock) {
    CoreEngine *old=core_enter(e);
    *trigger=host_pins[GPIO_TRIG_OUT];*clock=host_pins[GPIO_CLOCK_OUT];
    core_leave(old);
}

// Tap + Break: volume to the knob's middle, then saturation, then fuzz (ectocore.h break_set).
void core_engine_ssp_volume(CoreEngine *e,int16_t val) {
    CoreEngine *old=core_enter(e);
    uint8_t tap=gpio_btn_taptempo_val;gpio_btn_taptempo_val=0;  // 0: held
    break_set(val,false,false);
    gpio_btn_taptempo_val=tap;
    core_leave(old);
}

// Tap + Amen: low-pass left of the middle, high-pass right of it, none within 60 of 512.
void core_engine_ssp_filter(CoreEngine *e,int16_t val) {
    CoreEngine *old=core_enter(e);
    const uint16_t val_mid=60;
    if(val<512-val_mid){global_filter_index=val*(resonantfilter_fc_max)/(512-val_mid);global_filter_lphp=0;}
    else if(val>512+val_mid){global_filter_index=(val-(512+val_mid))*(resonantfilter_fc_max)/(512-val_mid);global_filter_lphp=1;}
    else{global_filter_index=resonantfilter_fc_max;global_filter_lphp=0;}
    for(uint8_t channel=0;channel<2;channel++){
        ResonantFilter_setFilterType(resFilter[channel],global_filter_lphp);
        ResonantFilter_setFc(resFilter[channel],global_filter_index);
    }
    core_leave(old);
}

// Tap + Sample: slice gating, off below 128.
void core_engine_ssp_gating(CoreEngine *e,int16_t val) {
    CoreEngine *old=core_enter(e);
    if(val<128){if(sf->fx_active[FX_TIGHTEN])toggle_fx(FX_TIGHTEN);}
    else{
        if(!sf->fx_active[FX_TIGHTEN])toggle_fx(FX_TIGHTEN);
        sf->fx_param[FX_TIGHTEN][0]=linlin(val,128,1024,0,200);
        Gate_set_amount(audio_gate,45+200-sf->fx_param[FX_TIGHTEN][0]);
    }
    core_leave(old);
}

// Tap + Mult: stop, or start when stopped.
void core_engine_ssp_toggle_playback(CoreEngine *e) {
    CoreEngine *old=core_enter(e);
    if(!playback_stopped&&!do_stop_playback){
        if(!button_mute)trigger_button_mute=true;
        do_stop_playback=true;
    }else if(playback_stopped&&!do_restart_playback){
        timer_reset();
    }
    TapTempo_reset(taptempo);
    core_leave(old);
}
