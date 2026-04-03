# thing left to do ...

the refactoring was all complete and tested and released.
really this todo is left to capture some ideas, ,and also for a few things to verify
that came about whilst refactoring for the xmx, and so going over a lot of the code

mostly, the idea is to reduce the complexity of the editors and views, 
there are quite a few subtle variations.




TO DO  - HIGH PRIO
------------------


TO DO  - MED PRIO
------------------



TO DO  - LOW PRIO
------------------
- cart ui - gates are too short to see if using a trig on clk, not really a bug, more a feature
cart gate length is len of trig input... so ui may poll at 'wrong time' and never sees gate.
we could 'cache' the gate, in processor for a while, so UI sees it.
alt: support configurable gate length, or nearly as long as step?
alt2: UI could just show the value of gate step... i.e. not the output (not nice !?)
- seen this on clkd too
perhaps, this might be something to do in DATA?


OTHER
------------------
- FileSelector - either update FileBrowser to use, or updates plugin to use
- TextControl - either update TextEdit to use, or updates plugin to use
- FileSelector - support compact and non-compact mode (currently compact only)
- TextControl - support compact and non-compact mode (currently compact only)
- ListControl - review if other possible use cases
- ensure correct background colours for new controls (FileSelector/TextControl/ListControl)

- review Editor / View hierarchies ... reduce number of variations

- review ValueButton vs ParamButton, as this can cause issues missing the types eg. with ButtonBox
- use ButtonBox control in editor/view (do we need a ParamButtonBox , see above ValueButton vs ParamButton)




# view/editor refactoring - lower priority - post demo/release?
code refactor... the views need refactoring... too much code duplication
too many variations possible, around how much of the UI and editor wants to draw, or just default 'and work'
this includes if buttons are parameters, or are just ui elements.
also perhaps there is an over use of 'multi views' to allow for switching'

I think a better approach is using more components. 
I recently introduced button box... 
the idea is the editor can then just include particular components it needs, 
and then manage the connection between these and buttons presses with minimal code.

needs to be thought thru though, e.g. do we have pages of buttons and paramters do they get linked.
the potentially issue being, we just end up movin the 'mess' from the view to components ;) 
