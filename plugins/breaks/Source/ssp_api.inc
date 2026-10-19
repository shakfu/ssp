// Copyright 2026 shakfu. SPDX-License-Identifier: GPL-3.0-only
// brks's additions to _core's desktop engine (core_engine.h), for functions the hardware reaches only
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

// Tap + Break: volume to the knob's middle, then saturation, then fuzz (the firmware's break_set).
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

// Banks to step, from Left/Right and in 5. The Bank button's tap counts only if released within 200 ms,
// and only goes forward. The step changes input_handling's persistent locals, which only code inside it
// can reach: fetch_core.sh calls ssp_input_handling at the top of each pass of its loop.
static int8_t ssp_bank_steps;

void core_engine_ssp_step_bank(CoreEngine *e,int steps) {
    CoreEngine *old=core_enter(e);
    int s=ssp_bank_steps+steps;
    ssp_bank_steps=(int8_t)(s<-16?-16:s>16?16:s);
    core_leave(old);
}

// The Bank tap's branch, stepping from the bank already asked for: presses before the loop's end add
// up, and stepping back to the bank playing cancels the change. Held while a switch is under way.
static void ssp_input_handling(uint8_t *bank_next_new,uint8_t *sample_next_new,SampleCVState *cv) {
    if(!ssp_bank_steps||fil_current_change)return;
    int steps=ssp_bank_steps;ssp_bank_steps=0;
    if(banks_with_samples_num<2)return;
    int bank_i=0,n=banks_with_samples_num;
    for(int k=0;k<n;k++)
        if(*bank_next_new==banks_with_samples[k]){bank_i=k;break;}
    uint8_t bank_num=banks_with_samples[((bank_i+steps)%n+n)%n];
    if(banks[bank_num]->num_samples==0||*bank_next_new==bank_num)return;
    *bank_next_new=bank_num;
    bool cv_1voct=global_sample_cv_mapping==SAMPLE_CV_MAPPING_1VOCT&&cv_plugged[CV_SAMPLE]&&
                  cv_reset_override!=CV_SAMPLE;
    if(!(cv_1voct&&sample_cv_index(cv,banks[bank_num]->num_samples,sample_next_new))){
        *sample_next_new=roundf((float)(sel_sample_cur*banks[bank_num]->num_samples)/banks[sel_bank_cur]->num_samples);
        if(*sample_next_new>=banks[bank_num]->num_samples)*sample_next_new=banks[bank_num]->num_samples-1;
    }
    debounce_file_change=DEBOUNCE_FILE_SWITCH;
}

// Mult's tap and hold: the clock output's division, 0 (8 per beat) to 6 (one per 2 bars).
void core_engine_ssp_division(CoreEngine *e,uint8_t division) {
    CoreEngine *old=core_enter(e);
    ectocore_clock_selected_division=division<ECTOCORE_CLOCK_NUM_DIVISIONS?division:ECTOCORE_CLOCK_NUM_DIVISIONS-1;
    core_leave(old);
}

// Tempo, as Tap sets it: ignored while a clock is at In 1, which sets the tempo itself.
void core_engine_ssp_tempo(CoreEngine *e,uint16_t bpm) {
    CoreEngine *old=core_enter(e);
    if(clock_input_absent)sf->bpm_tempo=bpm<30?30:bpm>300?300:bpm;
    core_leave(old);
}
