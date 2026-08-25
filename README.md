# Snip

Snip is a small native Windows text snippet manager. It runs in the notification area and opens the configured snippet hierarchy with `Ctrl+Shift+Space`. Choosing a snippet puts it in the clipboard and pastes it into the text field that had focus before the menu opened. Normal Windows right-click menus are not modified.

## Build

Requirements: Windows, CMake 3.20+, and a C++17 compiler (Visual Studio or MinGW).

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run `build\Release\Snip.exe` (or `build\Snip.exe` with a single-config generator). Keep `snippets.txt` beside the executable; CMake copies the sample there after a build.

## Configure snippets

Edit `snippets.txt` as UTF-8. Each entry is one line with three tab-separated fields:

```text
item<TAB>Menu/Submenu/Snippet name<TAB>Text to paste
```

Set the shortcut with a `hotkey` line. The default is:

```text
hotkey<TAB>Ctrl+Shift+Space
```

Supported modifiers are `Ctrl`, `Shift`, `Alt`, and `Win`. Supported keys include letters, digits, `Space`, `Tab`, `Enter`, arrow keys, and `F1` through `F12`.

Menus are created automatically from the path. Use `\\n` for a newline and `\\t` for a tab in pasted text. Lines beginning with `#` are comments. Reload from the tray icon or the root context menu after editing.

Example:

```text
item	Support/Replies/Welcome	Hello, how can I help?
item	Support/Replies	Thanks for getting in touch.\\n\\nBest regards,
```

Snip must be running for the shortcut to work. Some elevated applications may reject simulated paste input; run Snip elevated only when you explicitly trust the target application.

Snip uses the registered `Ctrl+Shift+Space` Windows hotkey rather than a global mouse hook. If another application already owns that shortcut, Snip will show an error at startup; change `ID_HOTKEY` in `src/main.cpp` to select another shortcut.

Hovering over a snippet in the menu shows its full text in a preview popup. The preview does not take keyboard focus.
