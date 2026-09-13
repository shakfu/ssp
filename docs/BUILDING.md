# building

## disclaimer 

I do this in my free time, I do not have the spare time to provide developer support, so I assume if you want to build - you will know how to figure out things like dependancies. 

unfortunately, I setup my build environment a bit adhoc, so I do not currently have instructions for how to do this. 

I can assure you everything that is needed to build IS in this repo, but cannot support building it at this time.

I recognise this may be a bit crap/frustrating, but with my limited time available - id prefer to spend this on developing plugins for ALL ssp users.



## sub modules
get submodules using

```
git submodule update --init --recursive
```

## general build 
I use cmake, so the basic build, assuming you have dependancies etc , is

```
mkdir build
cd build
cmake ..
cmake --build . -- -j 8
```

note: -- passes extra parameters to make -j 8 = number of cores to build on


## cross compile 

brew install llvm clang-format pkg-config 

instructions for how how to cross compile using toolchains is detailed in under the ssp-sdk
see Percussa Forum - [specifically this topic](https://forum.percussa.com/t/creating-modules-for-the-ssp-aka-ssp-sdk-updated)

after its setup is similar to a normal cmake build, except we specify a toolchain file

```
mkdir build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../xcSSP.cmake ..
cmake --build . -- -j 8
 ```

this builds and places plugins in ~/.vst3


note: 
im using a mac m1, running macOS 11. but cross-compilation should be possible on any mac
sorry, I did not take note of exactly what I installed so you will have to figure this out yourself. 

similar, it may be you can get a cross-compilation running under windows, since im using standard cmake/clang cross compilation. (however, Ive never tried, nor wish too ;))  



## cross compile on Linux

Tested on Ubuntu 24.04 with clang 18 and CMake 3.28.

```
sudo apt install cmake clang lld pkg-config curl zip \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxrender-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev
make
```

On first run, `make` downloads the SSP buildroot (608 MB) into `./buildroot`.
It then configures with the `ssp toolchain` preset and builds into `build.cmake.ssp`.
Plugins land in `build.cmake.ssp/technobear/*/*_artefacts/Release/VST3/*.vst3/Contents/armv7l-linux/*.so`.

The X11, freetype and fontconfig headers are for `juceaide`.
JUCE builds this tool for the host during configure.

| target | action |
|-|-|
| `make` | build all plugins |
| `make release` | strip into `releases/ssp/plugins`, package `tb_plugins_ssp.zip` |
| `make deploy` | copy all plugins to `SSP_HOST` |
| `make deploy-mod MOD=attn` | copy one plugin to `SSP_HOST` |
| `make clean` | remove `build.cmake.ssp` |

Variables: `SSP_BUILDROOT` (use an existing buildroot), `SSP_HOST` (default `root@192.168.0.150`), `JOBS`.
`make help` lists all targets.


## testing
I do a good amount of the development on macOS, so you can build the plugins under macOS using cmake. I use Jetbrain's CLion which can import the cmake project and 

depending on platform you may wish to specify the architecture 
e.g.
```
CMAKE_OSX_ARCHITECTURES=arm64; cmake ..
```
