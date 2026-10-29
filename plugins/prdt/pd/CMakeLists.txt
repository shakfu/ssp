# libpd from external/libpd with multiple instances (PDINSTANCE, PDTHREADS), and Pd's extra/ objects
# without pd~, which forks a pd process. prdt's CMakeLists.txt adds this; the host tests build it alone.
cmake_minimum_required(VERSION 3.25)
project(prdt_pd C)

set(LIBPD "${CMAKE_CURRENT_LIST_DIR}/../../../external/libpd")
set(PD_MULTI ON CACHE BOOL "" FORCE)
set(PD_EXTRA OFF CACHE BOOL "" FORCE)
set(PD_UTILS ON CACHE BOOL "" FORCE)
set(LIBPD_STATIC ON CACHE BOOL "" FORCE)
set(LIBPD_SHARED OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(${LIBPD} libpd EXCLUDE_FROM_ALL)

# PdEngine.cpp calls each one's setup function
add_library(pd_extra STATIC
        ${LIBPD}/pure-data/extra/bob~/bob~.c
        ${LIBPD}/pure-data/extra/bonk~/bonk~.c
        ${LIBPD}/pure-data/extra/choice/choice.c
        ${LIBPD}/pure-data/extra/fiddle~/fiddle~.c
        ${LIBPD}/pure-data/extra/loop~/loop~.c
        ${LIBPD}/pure-data/extra/lrshift~/lrshift~.c
        ${LIBPD}/pure-data/extra/pique/pique.c
        ${LIBPD}/pure-data/extra/sigmund~/sigmund~.c
        ${LIBPD}/pure-data/extra/stdout/stdout.c)
target_link_libraries(pd_extra PRIVATE libpd_private_settings PUBLIC libpd_static)

# cyclone from external/pd-cyclone, its objects compiled in: cyclone_setup() registers them all. The list is
# cyclone's own CMake's (no loadmess, which defines Pd's sys_noloadbang, nor scope_dialog), less the objects
# that read the mouse: mousestate, mousefilter and active.
set(CYCLONE "${CMAKE_CURRENT_LIST_DIR}/../../../external/pd-cyclone")
file(GLOB CYCLONE_CONTROL "${CYCLONE}/cyclone_objects/binaries/control/*.c")
file(GLOB CYCLONE_SIGNAL "${CYCLONE}/cyclone_objects/binaries/audio/*.c")
list(FILTER CYCLONE_CONTROL EXCLUDE REGEX "/(loadmess|mousestate|mousefilter|active)\\.c$")
list(FILTER CYCLONE_SIGNAL EXCLUDE REGEX "/scope_dialog\\.c$")
# Each source's exported setups, lowercase only: a capitalised one (Clip_tilde_setup) is a deprecated alias that
# reassigns the same class pointer. zl's zl.MODE aliases come from a macro and go first, so zl_setup() leaves
# zl_class as [zl].
set(SETUP_DECLS "")
set(SETUP_CALLS "")
foreach (F ${CYCLONE_CONTROL} ${CYCLONE_SIGNAL})
    file(STRINGS ${F} LINES REGEX "^[ \t]*(extern[ \t]+)?CYCLONE_OBJ_API[ \t]+(extern[ \t]+)?void[ \t]+[a-z][A-Za-z_0-9]*setup[ \t]*\\(|^ZL_ALIAS_SETUP\\(")
    foreach (L ${LINES})
        if (L MATCHES "^ZL_ALIAS_SETUP\\(([a-z]+)\\)")
            set(FN "setup_zl0x2e${CMAKE_MATCH_1}")
            string(PREPEND SETUP_CALLS "    ${FN}();\n")
        elseif (L MATCHES "void[ \t]+([a-z][A-Za-z_0-9]*setup)[ \t]*\\(")
            set(FN ${CMAKE_MATCH_1})
            string(APPEND SETUP_CALLS "    ${FN}();\n")
        endif ()
        string(APPEND SETUP_DECLS "void ${FN}(void);\n")
    endforeach ()
endforeach ()
configure_file(${CYCLONE}/cyclone_objects/binaries/single_lib.c.in ${CMAKE_CURRENT_BINARY_DIR}/cyclone_single_lib.c)
add_library(pd_cyclone STATIC
        ${CYCLONE_CONTROL} ${CYCLONE_SIGNAL}
        ${CYCLONE}/cyclone_objects/binaries/cyclone_lib.c
        ${CMAKE_CURRENT_BINARY_DIR}/cyclone_single_lib.c
        ${CYCLONE}/shared/common/file.c
        ${CYCLONE}/shared/common/grow.c
        ${CYCLONE}/shared/common/magicbit.c
        ${CYCLONE}/shared/common/random.c
        ${CYCLONE}/shared/control/gui.c
        ${CYCLONE}/shared/control/mifi.c
        ${CYCLONE}/shared/control/rand.c
        cyclone_utf8.c  # in place of shared/control/s_cycloneutf8.c
        ${CYCLONE}/shared/control/tree.c
        ${CYCLONE}/shared/signal/cybuf.c)
target_include_directories(pd_cyclone PRIVATE ${CYCLONE}/shared)
target_compile_definitions(pd_cyclone PRIVATE CYCLONE_SINGLE_LIBRARY=1 HAVE_UNISTD_H=1)
target_link_libraries(pd_cyclone PRIVATE libpd_private_settings PUBLIC libpd_static)

# ELSE from external/pd-else, its objects compiled in: else_prdt_setup() registers them all. Left out: objects
# that need ffmpeg, opus, aubio or a network thread (play.file~, streamin~, streamout~, sfload, sfinfo, pdlink,
# pdlink~, beat~), the ones that read the mouse or keyboard or open a dialog (mouse, keycode, properties), and
# [else], whose setup loads Tcl plugins and pdlua. [click] stays: most of ELSE's abstractions contain one.
# plaits~, sfont~, sfz~ and circuit~ are not vendored.
set(ELSE "${CMAKE_CURRENT_LIST_DIR}/../../../external/pd-else/Source")
file(GLOB ELSE_OBJECTS "${ELSE}/Control/*.c" "${ELSE}/Audio/*.c" "${ELSE}/Extra/Aliases/*.c")
list(FILTER ELSE_OBJECTS EXCLUDE REGEX
        "/(play\\.file~|streamin~|streamout~|sfload|sfinfo|pdlink~?|beat~|mouse|keycode|properties|else)\\.c$")
# Each source's exported setups, from the source less its comments (bl.square~.c keeps an old one in a comment).
# A source's own setup goes last: an alias's setup (cartopol_setup in car2pol.c) reassigns the class pointer.
set(ELSE_DECLS "")
set(ELSE_CALLS "")
foreach (F ${ELSE_OBJECTS})
    file(READ ${F} TEXT)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" TEXT "${TEXT}")
    string(REGEX MATCHALL "\n(extern[ \t]+)?void[ \t]+[A-Za-z_0-9]*setup[A-Za-z_0-9]*[ \t]*\\([ \t]*(void)?[ \t]*\\)[ \t\r\n]*{" FOUND "${TEXT}")
    get_filename_component(STEM ${F} NAME_WLE)
    string(REPLACE "~" "_tilde" OWN "${STEM}")
    if (OWN MATCHES "\\.")
        string(REPLACE "." "0x2e" OWN "setup_${OWN}")
    else ()
        set(OWN "${OWN}_setup")
    endif ()
    set(LAST "")
    foreach (L ${FOUND})
        string(REGEX REPLACE "^\n(extern[ \t]+)?void[ \t]+([A-Za-z_0-9]+).*" "\\2" FN "${L}")
        string(APPEND ELSE_DECLS "void ${FN}(void);\n")
        if (FN STREQUAL OWN)
            set(LAST "    ${FN}();\n")
        else ()
            string(APPEND ELSE_CALLS "    ${FN}();\n")
        endif ()
    endforeach ()
    if (NOT LAST)
        message(FATAL_ERROR "ELSE: no ${OWN}() in ${F}")
    endif ()
    string(APPEND ELSE_CALLS "${LAST}")
endforeach ()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/else_setup.c CONTENT
        "// generated by plugins/prdt/pd/CMakeLists.txt\n${ELSE_DECLS}\nvoid else_prdt_setup(void) {\n${ELSE_CALLS}}\n")
file(GLOB ELSE_SHARED "${ELSE}/Shared/*.c" "${ELSE}/Shared/fftease/*.c")
list(FILTER ELSE_SHARED EXCLUDE REGEX "/mouse_gui\\.c$")  # [mouse]'s, and [knob]'s without PDINSTANCE
add_library(pd_else STATIC ${ELSE_OBJECTS} ${ELSE_SHARED} ${CMAKE_CURRENT_BINARY_DIR}/else_setup.c else_prdt.c)
target_include_directories(pd_else PRIVATE ${ELSE}/Shared ${ELSE}/Shared/kiss_fft)
target_compile_options(pd_else PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/else_prdt.h)
target_compile_definitions(pd_else PRIVATE HAVE_UNISTD_H=1)
target_link_libraries(pd_else PRIVATE libpd_private_settings PUBLIC libpd_static)
