# 3. Shortcut apps (type `builtin`)

A `builtin` app has **no data**. It is a named, coloured launcher for one of NovaOS's built-in apps - handy to put
"Notes" or "Photos" in the dock with your own name and colour.

```
id=quick.notes
name=Quick Notes
version=1.0.0
type=builtin
target=notepad
accent=F5B400
```

`target` must be one of `files`, `notepad`, `photos`, `display`, `about`. There is no `data=` line (a builtin
package with data is rejected). Build it like any other app; examples: `examples/notepad-link`, `examples/files-link`.

Notes
* Launching starts that built-in app, or brings its window to the front if it is already open.
* A shortcut can only start built-in apps - it cannot start another installed app.
* `.appd` and `.powor` are the same format. `output=appd` in the project manifest just chooses the extension
  (see `examples/notepad-link`).
