// Included before every ELSE source (pd/CMakeLists.txt). ELSE links into prdt.so beside cyclone: the global names
// both define get an else_ prefix, and every class also answers to "else/<name>", the name patches use for ELSE
// installed as a folder. Where cyclone has a class of the same name, cyclone keeps the bare name and ELSE's class
// is named "else/<name>" only.
#pragma once
#include "m_pd.h"

t_symbol* prdt_else_name(t_symbol* s);   // the name to give the class
t_symbol* prdt_else_alias(t_symbol* s);  // the "else/" name to add, or 0 if the class has it already

#define class_new(s, newm, freem, size, flags, ...)                                          \
    ({                                                                                       \
        t_symbol* prdt_s = (s);                                                              \
        t_class* prdt_c = class_new(prdt_else_name(prdt_s), newm, freem, size, flags, __VA_ARGS__); \
        t_symbol* prdt_a = prdt_else_alias(prdt_s);                                          \
        if ((newm) && prdt_a) class_addcreator((t_newmethod)(newm), prdt_a, __VA_ARGS__);   \
        prdt_c;                                                                              \
    })

// libpd has no MIDI device: Pd's own byte output reaches libpd's MIDI hook
#define sys_putmidibyte outmidi_byte

// ELSE's copy of cyclone's mifi.c, and the setups of the classes both have
#define atan2_tilde_setup else_atan2_tilde_setup
#define cartopol_setup else_cartopol_setup
#define cartopol_tilde_setup else_cartopol_tilde_setup
#define pink_tilde_setup else_pink_tilde_setup
#define poltocar_setup else_poltocar_setup
#define poltocar_tilde_setup else_poltocar_tilde_setup
#define scope_tilde_setup else_scope_tilde_setup
#define tanh_new else_tanh_new
#define tanh_setup else_tanh_setup
#define tanh_tilde_setup else_tanh_tilde_setup
#define trunc_tilde_setup else_trunc_tilde_setup
#define mifiread_close else_mifiread_close
#define mifiread_doit else_mifiread_doit
#define mifiread_free else_mifiread_free
#define mifiread_getbarindex else_mifiread_getbarindex
#define mifiread_getbarspan else_mifiread_getbarspan
#define mifiread_getbeatticks else_mifiread_getbeatticks
#define mifiread_getchannel else_mifiread_getchannel
#define mifiread_getdata1 else_mifiread_getdata1
#define mifiread_getdata2 else_mifiread_getdata2
#define mifiread_getdeftempo else_mifiread_getdeftempo
#define mifiread_getformat else_mifiread_getformat
#define mifiread_gethdtracks else_mifiread_gethdtracks
#define mifiread_getmscoef else_mifiread_getmscoef
#define mifiread_getnevents else_mifiread_getnevents
#define mifiread_getnframes else_mifiread_getnframes
#define mifiread_getntempi else_mifiread_getntempi
#define mifiread_getowner else_mifiread_getowner
#define mifiread_getscoretime else_mifiread_getscoretime
#define mifiread_getstatus else_mifiread_getstatus
#define mifiread_gettempo else_mifiread_gettempo
#define mifiread_gettick else_mifiread_gettick
#define mifiread_gettrackname else_mifiread_gettrackname
#define mifiread_new else_mifiread_new
#define mifiread_open else_mifiread_open
#define mifiread_setuserticks else_mifiread_setuserticks
#define mifiwrite_channelevent else_mifiwrite_channelevent
#define mifiwrite_close else_mifiwrite_close
#define mifiwrite_closetrack else_mifiwrite_closetrack
#define mifiwrite_free else_mifiwrite_free
#define mifiwrite_new else_mifiwrite_new
#define mifiwrite_open else_mifiwrite_open
#define mifiwrite_opentrack else_mifiwrite_opentrack
#define mifiwrite_sethardticks else_mifiwrite_sethardticks
#define mifiwrite_setusertempo else_mifiwrite_setusertempo
#define mifiwrite_setuserticks else_mifiwrite_setuserticks
#define mifiwrite_textevent else_mifiwrite_textevent
