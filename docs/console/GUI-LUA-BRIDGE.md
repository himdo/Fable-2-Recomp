# GameFace (GUI) Lua bridge

`f2 gui <lua>` and the attribute-editing `f2 ui` sub-commands run Lua inside the
GameFace GUI Lua state. Code: `src/core/fable2_gui_lua.h`; commands in
`src/core/fable2_console.h` (`run_gui_command`).

## Why a bridge

Fable 2's HUD and menus are Anark GameFace 4.0.5 presentations (`art/gui/gameface/*.bgf`
in `guiscripts.bnk`). Their scripts run in GameFace's own Lua 5.1, a separate VM from the
game's Lua (two base libraries are linked into the image: GameFace's around `0x82BD6xxx`,
the game's around `0x82A2xxxx`). Only the GameFace state has the element API:

| Global | Use |
|---|---|
| `getElement(path[, parent])` | e.g. `getElement("Scene.Layer")`, `getElement("SafeFrame", SCENE)` |
| `getAttribute(el, name)` / `setAttribute(el, name, value)` | attributes below |
| `getChildren(el)` | array of child elements |

Attributes the shipped scripts read or write: `position.x/y/z`, `scale.x/y`,
`rotation.x/y/z`, `pivot.x/y`, `opacity` (0-100), `active`, `diffuse.r/g/b/a`,
`textstring`, `textcolor.r/g/b`, `size`, `boxwidth`, `boxheight`, `TintingAmount`, `name`.
The HUD's own `Enable`/`Disable` are just `setAttribute(getElement("Scene.Layer"), "opacity", 100/0)`.

## Addresses

Found in the decrypted image (`tools/xex_image.py default.xex image.bin`, then
`tools/image_refs.py` / `tools/image_dis.py`):

| Address | What | How it was identified |
|---|---|---|
| `0x82BB2528` | GameFace registers its globals; `lua_State* = *(r3 + 4)` | references the strings `getElement`, `setAttribute`, ... at `0x820FD3E4..`; each entry is `pushcclosure` + `setfield(L, -10002, name)` |
| `0x8227B7E0` | `lua_pushcclosure(L, fn, 0)` | registration pattern |
| `0x82BC5EB0` | `lua_setfield(L, LUA_GLOBALSINDEX, name)` | `li r4, -0x2712` |
| `0x82BB68D8` / `0x82BB6CA0` / `0x82BB7100` | natives `getElement` / `getAttribute` / `setAttribute` | registration pattern |
| `0x82BD62F8` / `0x82BD6AE0` / `0x82BD6698` | GameFace `luaB_loadstring` / `luaB_pcall` / `luaB_dofile` | base library `luaL_Reg` table at `0x820903B8` (`{name ptr, fn ptr}`) |
| `0x82BC6130` | `lua_load(L, reader, data, chunkname)` | called by `luaB_loadstring`; defaults chunkname to `"?"` |
| `0x82A1FF68` | `getS` reader, `data = {const char* s; size_t size}` | passed by `luaB_loadstring`; body matches lauxlib `getS` |
| `0x822C05F8` | `luaD_pcall(L, func, ud, oldtop, ef)` | called by `luaB_pcall` and `lua_load` |
| `0x82BC6120` | `f_call(L, CallS* {StkId func; int nresults})` | the `func` `luaB_pcall` passes |
| `0x8219EE00` | `luaD_call(L, func, nresults)` | `luaB_dofile` (not used by the bridge) |

`lua_State` layout (32-bit, float numbers, 8-byte `TValue {value, tt}`): `tt` byte at +4
(8 = thread), `top` +8, `base` +0xC, `l_G` +0x10, `ci` +0x14, `stack_last` +0x1C,
`stack` +0x20. `TString` text at +16, length at +12. `LUA_TSTRING` = 4.

## How a command runs

1. **Capture.** Hooks (weak-symbol overrides, no codegen change) on `0x82BB2528`
   record every GameFace `lua_State`; hooks on native `getElement`/`setAttribute` record
   which state is live and which host thread runs GUI Lua.
2. **Queue.** `f2 ui ...` / `f2 gui ...` build a Lua chunk and queue it.
3. **Run** on the GUI thread: from the console's per-frame poll if GameFace runs on the
   same thread, otherwise inside the next native `getElement`/`setAttribute` call.
   `lua_load(L, getS, {src,len}, "=f2gui")`, then `luaD_pcall(L, f_call, {func, 1}, func - stack, 0)`
   — the same call `pcall` makes — so errors come back as a status, never as an unwind
   through host code.
4. **Result.** Every chunk is wrapped so it returns one string (`ok ...` / `error ...`);
   the host reads it from `top - 1`, pops it, and prints it as `[gui]` lines.

The first chunk in each state defines a helper table `__f2` (element lookup by name with a
tree search, `set` that remembers originals for `ui reset`, `desc`, `tree`).

Which state: the one whose globals contain `g_HUD` (probed automatically on first use),
else the one chosen with `gui use`, else the last one that called a native.

## Risks / unknowns (untested in game)

- If GameFace Lua runs on a different thread than the console and no HUD script calls
  `getElement`/`setAttribute` for a while, queued commands wait (`gui status` shows
  `pending`).
- A presentation that unloads frees its `lua_State`; the bridge re-checks the state's
  type byte and stack bounds before each use but cannot prove the memory is still that
  state.
- Element names: `ui tree` lists the live hierarchy; `ui elements` lists names from
  `hud.bgf` offline.
- Changes are not saved by the game; `ui export` prints `ui attr` lines for
  `console\autoexec.txt`. The HUD may re-set some attributes itself (widescreen
  adapter, centre toggle, slides).
