# convert sapf editor to common editor

Converting sapf to ScriptEngine is a medium-sized job, not a quick one. The engine API fits sapf; the shared editor would need features it lacks, and some keys would change. I'm still researching fsyn and will report when that finishes.

What fits. A SapfEngine subclass of ScriptEngine would just wrap sapf::Session, at about 100 lines:

┌─────────────────────┬─────────────────────────────────────────────────────────────────────┐ │    ScriptEngine │                            sapf::Session                            │ ├─────────────────────┼─────────────────────────────────────────────────────────────────────┤ │ process(in, out, n) │ process(in, out, n), the same signature (Session.h:43) │ ├─────────────────────┼─────────────────────────────────────────────────────────────────────┤ │ compile(text) │ stop(), then evaluate(text) │ ├─────────────────────┼─────────────────────────────────────────────────────────────────────┤ │ runPart (Ctrl-E)    │ evaluate(block) │ ├─────────────────────┼─────────────────────────────────────────────────────────────────────┤ │ dropAll (Ctrl-.)    │ stop(). evaluate returns at once, so the worker thread is never │ │ │ blocked and Ctrl-. gets through (ScriptEngine.cpp:197). │ ├─────────────────────┼─────────────────────────────────────────────────────────────────────┤ │ canReset, status, │ state(), renderLoad(), lateFrames() │ │ runState            │ │ └─────────────────────┴─────────────────────────────────────────────────────────────────────┘

What doesn't fit.

1. Output pane. sapf shows 400 lines of output: stack results and errors (Session.h:43, PluginEditor.cpp:171). The code view shows one note line. The fix is an optional output pane in the code view, which chuk and csnd could also use. This is the biggest piece, about 100 lines. The status-line layout test would need updating.

2. Errors arrive later. A ScriptEngine reports compile errors when compile() returns. sapf reports them later, in its output, so the code view would show "running" for code that failed. The output pane is the only place they would appear.

3. Ctrl-Enter changes meaning. In sapf it runs the block around the cursor. In ScriptProcessor it runs the whole text and Ctrl-E runs the block. Running a whole file of examples with Ctrl-Enter would be wrong for sapf. Either swap the two keys for sapf (a new CodeKeys flag) or retrain your hands.

4. Esc. It stops sound in sapf. In the code view it returns to the pages; Ctrl-. still stops.

5. Built-in word colours. sapf colours built-in words using the session's word list (PluginEditor.cpp:158). CodeKeys has no hook for that.

6. Meters. The in/out meters (PluginEditor.h:44) would be lost, or would move to the pages view.

7. File types. Ctrl-O lists one extension (ScriptProcessor.cpp:99). sapf also needs .txt for sapf-examples.txt.

8. Old patches. Old patches store CODE and FILE; they would need migrating.

9. p1-p16. You'd get 16 controls that do nothing unless sapf code can read them, which would be a new feature.

Size. About 300-400 lines across sapf and the shared engine. Then:

- test_session.py, test_sapf_engine.py and test_engine_plugins.py need updating;

- the sapf docs and README need updating;

- the device test needs running again.

Items 1, 3 and 5 change the shared editor, so chuk, csnd and fstr need retesting too.

Alternative. Add Ctrl-L, Ctrl-R, Ctrl-Shift-S and the key hint to sapf's own editor, at about 100 lines. That's quick, but it leaves two editors to maintain.

I'd do the full conversion only if the output pane is worth having in chuk and csnd as well. If not, the quick fix is cheaper.
