# SSP framework change - aka JUCE removal


## hard requirements

SSP - confirm to SSP SDK API
ability to write to framebuffer or use openGL

2d graphics primitive - I dont really want to be writing openGL shaders etc.


paramter model , alot of code is based on this.

cross platform - SSP / XMX / macOS

## soft requirements

VST macOS
this is soft, as I could create an SSP host, or use trax.
but ts been a useful way to test quickly.

lightweight
my main reason to move away from juce would be to get something lighter




## possibilities

### IPlug2 
an atlernative, but lighter framework, Id need to consider how to tie into SSP API, but it'd likely be simiar to currnt approach.




### avoid a framework, use  multiple libs

VST :
use Steinberg VST3 SDK 

Parameters:
PluginParamters - https://github.com/teragonaudio/PluginParameters

Graphics : 
FBGraphics - ghttps://github.com/grz0zrg/fbg, 
light, low level, mainly linux focused, but says crossplatform with GLFW

NanoVG - https://github.com/memononen/nanovg
open gl based, quite extensive, allows for hardware accelleration . popular, active dev

Cario - another renderer, though, Id probably go NanoVG over it.


