// else_prdt.h's names. Called at class setup only, in the main Pd instance.
#include <stdio.h>
#include <string.h>

#include "m_pd.h"

// cyclone's classes that ELSE also has: test_prdt.py checks the list against both libraries' sources
static const char* const CYCLONE_OWNS[] = { "atan2~", "cartopol", "cartopol~", "pink~", "poltocar", "poltocar~",
                                            "scope~", "tanh", "tanh~", "trunc~" };

static int cyclone_owns(t_symbol* s) {
    for (size_t i = 0; i < sizeof(CYCLONE_OWNS) / sizeof(*CYCLONE_OWNS); i++)
        if (!strcmp(s->s_name, CYCLONE_OWNS[i])) return 1;
    return 0;
}

static t_symbol* prefixed(t_symbol* s) {
    char buf[MAXPDSTRING];
    snprintf(buf, sizeof(buf), "else/%s", s->s_name);
    return gensym(buf);
}

t_symbol* prdt_else_name(t_symbol* s) {
    return s && cyclone_owns(s) ? prefixed(s) : s;
}

t_symbol* prdt_else_alias(t_symbol* s) {
    return s && !cyclone_owns(s) ? prefixed(s) : 0;
}
