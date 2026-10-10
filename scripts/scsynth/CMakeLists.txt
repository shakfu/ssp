# libscsynth as a static PIC archive without an audio driver, and SC's core UGens as plugin modules.
# Built by scripts/build_deps.sh. The SSP module supplies the driver (SC_NewAudioDriver) and the
# timing functions SC_PortAudio.cpp would; UGens load from the card, so STATIC_PLUGINS is not used.
#
# SC's own CMake is not used: it needs an audio API, finds boost and FFTW, and builds sclang's tree.

cmake_minimum_required(VERSION 3.15)
project(scsynth_ssp C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(SC_PATH "" CACHE PATH "SuperCollider source tree")
set(SNDFILE_PREFIX "" CACHE PATH "prefix holding include/sndfile.h and lib/libsndfile.a")
if (NOT EXISTS "${SC_PATH}/server/scsynth/SC_World.cpp")
    message(FATAL_ERROR "set SC_PATH to the SuperCollider source tree")
endif ()

include("${SC_PATH}/SCVersion.txt")
set(GIT_REF_TYPE "")
set(GIT_BRANCH_OR_TAG "")
configure_file("${SC_PATH}/common/SC_Version.hpp.in" "${CMAKE_BINARY_DIR}/gen/SC_Version.hpp")

# SC_AUDIO_API selects headers only; no SC source is compiled for this value
add_compile_definitions(SC_AUDIO_API=SC_AUDIO_API_SSP SC_AUDIO_API_SSP=100 SC_MEMORY_ALIGNMENT=32
    SC_FFT_GREEN NOVA_SIMD SC_PLUGIN_EXT=".so" BOOST_CHRONO_HEADER_ONLY BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE
    _REENTRANT)
# SC's flags for gcc; plain -ffast-math would break its NaN and denormal handling
add_compile_options(-fno-math-errno -fno-signaling-nans -fsigned-zeros -fno-associative-math
    -fno-finite-math-only -pthread)

set(SC "${SC_PATH}")
set(EXT "${SC}/external_libraries")
include_directories(
    "${CMAKE_BINARY_DIR}/gen"
    "${SC}/include/common" "${SC}/common" "${SC}/include/server" "${SC}/include/plugin_interface"
    "${SC}/server/scsynth"
    "${EXT}/boost" "${EXT}/boost_sync/include" "${EXT}/nova-simd" "${EXT}/nova-tt" "${EXT}/TLSF-2.4.6/src"
    "${SNDFILE_PREFIX}/include")

set(S "${SC}/server/scsynth")
add_library(scsynth STATIC
    ${S}/SC_BufGen.cpp ${S}/SC_ComPort.cpp ${S}/SC_CoreAudio.cpp ${S}/SC_Graph.cpp ${S}/SC_GraphDef.cpp
    ${S}/SC_Group.cpp ${S}/SC_Lib_Cintf.cpp ${S}/SC_Lib.cpp ${S}/SC_MiscCmds.cpp ${S}/SC_Node.cpp
    ${S}/SC_Rate.cpp ${S}/SC_SequencedCommand.cpp ${S}/SC_Str4.cpp ${S}/SC_Unit.cpp ${S}/SC_UnitDef.cpp
    ${S}/SC_World.cpp ${S}/Rendezvous.cpp
    ${SC}/common/SC_Filesystem_unix.cpp ${SC}/common/SC_fftlib.cpp ${SC}/common/fftlib.c
    ${SC}/common/SC_AllocPool.cpp ${SC}/common/SC_Errors.cpp ${SC}/common/SC_Reply.cpp
    ${SC}/common/SC_StringBuffer.cpp ${SC}/common/SC_StringParser.cpp ${SC}/common/Samp.cpp
    ${SC}/common/sc_popen.cpp
    ${EXT}/TLSF-2.4.6/src/tlsf.c)
set_source_files_properties(${EXT}/TLSF-2.4.6/src/tlsf.c PROPERTIES COMPILE_DEFINITIONS TLSF_STATISTIC=1)

# core UGens, grouped as SC groups them
set(P "${SC}/server/plugins")
set(simple BinaryOpUGens ChaosUGens DelayUGens DemandUGens DemoUGens DynNoiseUGens FilterUGens GendynUGens
    GrainUGens IOUGens LFUGens MulAddUGens NoiseUGens OscUGens PanUGens PhysicalModelingUGens ReverbUGens
    TestUGens TriggerUGens UnaryOpUGens UnpackFFTUGens)
set(plugins ${simple} FFT_UGens PV_ThirdParty ML_UGens DiskIO_UGens)
foreach (p ${simple})
    add_library(${p} MODULE ${P}/${p}.cpp)
endforeach ()
add_library(FFT_UGens MODULE ${P}/FFTInterfaceTable.cpp ${P}/FFT_UGens.cpp ${P}/PV_UGens.cpp
    ${P}/PartitionedConvolution.cpp)
add_library(PV_ThirdParty MODULE ${P}/Convolution.cpp ${P}/FFT2InterfaceTable.cpp ${P}/FeatureDetection.cpp
    ${P}/PV_ThirdParty.cpp)
add_library(ML_UGens MODULE ${P}/ML.cpp ${P}/Loudness.cpp ${P}/BeatTrack.cpp ${P}/Onsets.cpp ${P}/onsetsds.c
    ${P}/KeyTrack.cpp ${P}/MFCC.cpp ${P}/BeatTrack2.cpp ${P}/ML_SpecStats.cpp)
add_library(DiskIO_UGens MODULE ${P}/DiskIO_UGens.cpp)
target_link_libraries(DiskIO_UGens PRIVATE "${SNDFILE_PREFIX}/lib/libsndfile.a")
foreach (p ${plugins})
    set_target_properties(${p} PROPERTIES PREFIX "")
    # a static library linked into a plugin stays private to it
    target_link_options(${p} PRIVATE -Wl,--exclude-libs,ALL -pthread)
endforeach ()

install(TARGETS scsynth ARCHIVE DESTINATION lib)
install(TARGETS ${plugins} LIBRARY DESTINATION scsynth/plugins)
