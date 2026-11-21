# icon_button_a.tex — Where It Lives, Where It Loads, What Draws It

Investigation into how the Xbox A button icon texture
(`Art/GUI/Controller/icon_button_a.tex`, packed inside `data/art/gui/gui_textures.bnk`)
is initialized in the recompiled code, whether its runtime memory is ever read, and which
functions call it when the main menu renders the A button.

Companion document: `TEXT_RENDERING_NOTES.md` (UI text pipeline, probe infrastructure).

---

## 1. Short answer

1. **The texture is not addressed by plaintext name at load time.** `.bnk` banks store
   **hashed entry names**; the path `Art/GUI/Controller/icon_button_a.tex` is constructed
   at runtime by the GameFace GUI framework. Nothing in `src/`, `generated/default/`, or
   the image rodata contains the full filename.
2. **It is initialized in three stages:** VFS bank registration at boot
   (`startup.vfsconfig` → `art/gui/gui_textures.bnk`), GameFace presentation load
   (`frontendmainmenu.bgf` → widget named `icon_button_a`, material `Material2270`), and
   on-demand texture streaming of the Bink `.tex` entry from the bank.
3. **Yes, the memory is read every frame** while the main menu is up. Runtime evidence:
   `fable2_heap_scan.log` shows the live parsed objects (below), and
   `fable2_func_summary.log` shows the per-frame UI pipeline executing
   (`UIText_FrameRender` 2775×, `UITextPrompt_Render` 40303×, `FrontMenu_PollEventQueue` 496×
   in one session).
4. **There is no single "DrawIconButtonA" function.** The widget is consumed via vtable
   dispatch from the per-frame render loop. The concrete named functions that touch this
   data each frame are listed in §6.

---

## 2. The asset and its container

- File on disk: `data/art/gui/gui_textures.bnk` (25,185,220 bytes), plus
  `gui_texture_headers.bnk` (135,460 bytes) in the same directory.
- The bnk header is `00 00 80 00` followed by a version field, then a zlib-compressed
  TOC with **plaintext names** (see §10 for the full decode). Raw greps for
  `icon_button_a` find nothing only because the TOC is compressed.
- `data/art/gui/gui.list` contains `Art\GUI\GUI.gdb`; `gui.gdb` is a GUI database file.
- The texture payloads are raw levels (`[u32 pixel_byte_count] + pixels`, §10); the
  engine's texture path is Bink-based (rodata strings `BinkTextures.cpp`,
  `"cached memory for the Bink texture pointers"`, `"Not a Bink file."`).
  `Art/GUI/Controller/icon_button_a.tex` is one 64x64 RGBA entry in the bank.

Related bnk files in `data/art/gui/`:

| File | Role |
|---|---|
| `gui_textures.bnk` | texture data (25 MB) — contains icon_button_a.tex |
| `gui_texture_headers.bnk` | texture headers, loaded `Mode="memory"` |
| `gui_models.bnk` | GUI models, `Required` |
| `gui_streaming.bnk` | GUI streaming assets |
| `guiscripts.bnk` (art/gui/guiscripts) | compiled `.bgf` presentations, `Mode="memory"` |

---

## 3. Stage 1 — VFS bank registration (boot)

`data/config/startup.vfsconfig` (UTF-16) declares the GUI bank group:

```xml
<VFSConfig ...>
  <BankGroup name="art" ...>
    <Bank Name="art\gui\gui_streaming.bnk" .../>
    <Bank Name="art\gui\guiscripts.bnk" ... Mode="memory"/>
    <Bank Name="art\gui\gui_models.bnk" ... Required="true"/>
    <Bank Name="art\gui\gui_texture_headers.bnk" ... Mode="memory"/>
    <Bank Name="art\gui\gui_textures.bnk" ... Optional="true"/>
  </BankGroup>
</VFSConfig>
```

Ro data strings in the image that belong to this machinery (image offset = guest addr − 0x82000000):

| Guest addr | String |
|---|---|
| `0x820A17F0` | `startup.vfsconfig` |
| `0x820C57EA` | `level.vfsconfig` |
| `0x820A17C6` | `Permanent Bank Mounter` |
| `0x820CCE28` | `pvfs_xc_` |
| `0x820A18BD` | `particle_bank.bnk` |
| `0x820F8678` | `Preload GUI` |

The config file itself is data (XML), so the code that reads it cannot be found by
grepping for `gui_textures.bnk` — the bank list comes from parsing, not from literals.
The "Permanent Bank Mounter" is the component that mounts banks referenced by the
vfsconfig files.

---

## 4. Stage 2 — GameFace presentation load (main menu)

The GUI framework is **GameFace 4.0.5** ("Anark" GUI SDK). Its source paths appear verbatim
in image rodata:

```
D:\dev\Fable2\Mainbranch\SourceCode\ExternalLibs\Gameface-4_0_5\RuntimeSDK\Framework\Source\AK...
```

Key GameFace classes/strings in image rodata:

| Guest addr | String | Meaning |
|---|---|---|
| `0x820FE270` | `class CBGFDeserializer` | parses `.bgf` presentation files |
| `0x820FE674` | `BGFLoadRefs` | reference loading during BGF load |
| `0x820FE70C` | `Could not open BGF file` | load error path |
| `0x820FE880` | `BGFDeserializer: Header section size mismatch` | header validation |
| `0x820FE8D0` | `Behavior` / `Element` / `Contract` / `Animation` / `Logic` / `Params table` / `Slide` / `String` | BGF section names |
| `0x820FE49C` | `class CScene` / `class CSceneManager` | presentation scene objects |
| `0x820FE4C0` | `class CLuaEngine` | per-presentation Lua |
| `0x820FE4D8` | `class SGameplanAsset` / `SGameplanState` / `SGameplanTransition` / `SGameplanExpression` | gameplan = menu state machine |
| `0x820FE5A0` | `show` / `hide` / `kill` / `lockupdate` / `unlockupdate` | gameplan events |
| `0x820FEB18` | `Mgr:Assets` / `AssetClone` | AKAssetManager (asset registry) |
| `0x820FEB44` | `Drawlist%ld:Lights` / `:Opaque` / `:Transparent` / `:PostProcess` | per-frame drawlist names |
| `0x820FEBCC` | `BGSGLoaderRefList` | `.bsg` scene-graph loader |

### The widget in the .bgf

Extracted presentations live in `scratch/lua_dump/raw/guiscripts/art/gui/gameface/`.
The main-menu presentation is **`frontendmainmenu.bgf`** (1,480,739 bytes, "QuickMainMenu"
presentation). Inside it (binary search for the ASCII name):

- `icon_button_a` at offsets `0x24041` and `0x25b35`, next to widget names
  (`AButton`, `ButtonText`), text labels (`Travel`, `Learn`), material references
  (`Material2270`, `Material2315`, `Material743`), and sub-path strings (`abyx`, `motifs`).
- `hud.bgf` (1,036,386 bytes) also references `icon_button_a` (offset `0x283b`) with
  `Material2270` nearby.
- `pausemainmenu.bgf` does **not** reference it.

`.bgf` header: `01 04 00 00 00 1a` + `AnarkBGF` + size table. The deserializer validates
section sizes (strings above) and endianness ("Warning! Different endian used between
file and system!").

**What the deserializer builds** (from the runtime heap scan, §5): the widget's short name
`icon_button_a`, the VFS path `gui\controller\icon_button_a`, the bank-relative filename
`icon_button_a.tex`, a material binding (`Material2270` + `DEFAULT_TEX_ID`), an anchor
mode (`bottommiddle`), and gameplan bindings to menu-page states.

The BGF file references textures **by short name**; the engine assembles
`<bank-relative dir>\controller\icon_button_a` → `Art/GUI/Controller/icon_button_a.tex`
at lookup time. That is why the full path never appears as a literal anywhere.

---

## 5. Stage 3 — runtime memory (proof of initialization)

From `out/build/win-amd64-debug/fable2_heap_scan.log` (background arena string scanner,
`FABLE2_HEAP_SCAN=1`; guest addresses, big-endian arena, `host = 0x100000000 + guest`):

| Guest addr | Content | Interpretation |
|---|---|---|
| `0x4010543C` | `icon_button_a\0` + `Foreground.Book.Pages.Page1.Contents.Select.Select` | parsed BGF binding: icon_button_a ↔ page-1 "Select" gameplan state |
| `0x401054BC` | `icon_button_a\0` + `Foreground.Book.Pages.Page3.Contents.Select.Select` | same, page 3 |
| `0x4011707C` | `icon_button_a\0` + `Foreground.Book.Pages.Page2.Contents.Select.Select` | page 2 |
| `0x401170FC` | `icon_button_a\0` + `Foreground.Book.Pages.MoralityPage.Contents.LogBook.Morali…` | morality page |
| `0x4011D2BC` | `icon_button_a\0` + `…Page1.Contents.Select.Replace` | Replace variant |
| `0x4011D4FC` | `icon_button_a\0` + `…Page2.Contents.Select.Replace` | Replace variant |
| `0x40567C1B` | `icon_button_a.tex\0` (+ duplicate at `0x40567CBB`) | bank-relative filename built at load |
| `0x4057C828` | `gui\controller\icon_button_a` (+ name at `0x4057C83B`, `0x4057C85F`) | runtime VFS path for the texture |
| `0x4057E9E0` | `icon_button_a` + `DEFAULT_TEX_ID` + `bottommiddle` | material→texture binding (Material2270 default texture ID) + anchor |
| `0x40588950` | `icon_button_a2` + `DEFAULT_TEX_ID` | alt/pressed variant |
| `0x4058B1E0` | `icon_button_a` + `Material2270` | widget↔material link |
| `0x405EB157` | `icon_button_a` | additional binding |
| `0x42118133` | `icon_button_a` + `Foreground.Book.Pages.Page1.Contents.ModelHider.mode…` | gameplan data (visibility control) |
| `0x42118223` | `icon_button_a` + `Foreground.Book.Pages.Page1.Contents.Footer.Select` | footer select state |
| `0x4010D7F8` | UTF-16 `start.img`, `back.img` | title-prompt image tag names (see §6) |

Also relevant: `0x4012AB3D` `Pressure_Button` (prompt press sound/anim name).

The `0x4010xxxx` region is the early-parsed BGF string/widget table; `0x4057xxxx`–`0x4058xxxx`
is the per-presentation asset/material instantiation; `0x4211xxxx` is gameplan state data.

The actual pixel data lives in a GPU-side Bink texture resource created by the texture
streamer (`Texture Streamer` rodata string at `0x820FA240`); the heap objects above are the
engine-side handles/names that resolve to it.

---

## 6. Stage 4 — what draws it (per-frame chain)

Per `fable2_func_summary.log` (one full session with main menu), the GUI pipeline is live:

```
43011 x UITextItem_Dispatch          1354 x UIText_RenderCurrent
40303 x UITextPrompt_Render            81 x UITextElement_DrawDispatch
 4129 x UIText_FrameRenderIter          25 x UITextElement_Draw
  4062 x UIText_RenderSegment           25 x UITextElement_PostQuery
  2775 x UIText_FrameRender             25 x UIText_RenderElement
  2775 x UITextContainer_ProcessChildren   6 x FrontMenu_PressBitDispatch
  1354 x UIText_RenderCurrentObject       496 x FrontMenu_PollEventQueue
                                          1 x UITextPrompt_RenderElement
```

Disassembly-verified chain for the title prompt (from `TEXT_RENDERING_NOTES.md`):

```
MainRenderLoop_82B9CD68            per-frame, ~30 Hz, VdSwap present
  → (60 fps dispatch 0x82B9C7F8, per-frame helpers, frame limiter 0x82242628)
  → GameFace scene draw: CScene per-frame update → Drawlist Lights/Opaque/Transparent/PostProcess
  → (prompt text chain)
     UIText_FrameRender          0x82C03FA8   (receives game time in f1)
      → UIText_FrameRenderIter   0x82190760   walks item list @ 0x83334AA0
        → UITextItem_Dispatch    0x82BFDB68
          → UITextPrompt_Render  0x82C44CF0   iterates element list @ 0x83334E20 via sub_82B458C0
            → UITextPrompt_RenderElement  0x82C44B50   per element
              → drawable vtable[8] = 0x82C12F18        emits the quads
```

Key facts about the prompt itself:

- The prompt string is UTF-16BE `"Press <a_img> to start"` (heap region `0x4266xxxx`,
  offset moves per run; localized copies in `0x920Cxxxx`).
- `<a_img>` is a **markup tag**, expanded by a static tag→icon mapping array at
  guest `0x820D7B50` (entries `s1`, `s2`, `a_img`, `b_img`, `c_img`, `d_img`).
- The tag expansion and the `icon_button_a` widget are two related but distinct draw
  paths: the tag renders the small icon inline in the prompt text; the widget
  (`AButton` in `frontendmainmenu.bgf`) is the GameFace element with
  `Material2270`/`DEFAULT_TEX_ID` → `icon_button_a`.
- The per-frame draw consumes **pre-laid-out glyph/element data**, not raw strings — the
  string is only consumed at layout/load time (verified by exhaustive GPR scan at every
  hooked pipeline entry over the title window: the prompt pointer never appears at render
  time).

Input side (what happens when A is pressed):

```
FrontMenu_PollEventQueue    0x82185C90   ~1/frame (30 Hz) while menu open
FrontMenu_InputEvent        0x82BE3F30   Up/Down/A/Back incl. clamped presses
FrontMenu_PressBitDispatch  0x82188F20   button mask (A=4096) vs element's supported buttons
FrontMenu_EnsureElement     0x82C217B8   get-or-create front-end UI element node (on actions)
```

---

## 7. Why static cross-referencing from the string failed

- Generated C++ loads all string constants from the data section at runtime (TOC-relative
  addressing); no string literals in `generated/default/`.
- The `.bnk` TOC is zlib-compressed, so no filename literal is visible to raw greps
  (it decodes fine — §10).
- Image references are TOC-relative; the TOC section is at `0x83C1xxxx`, **outside** the
  decrypted image range (`0x82000000`–`0x83620000` = `.text` + first data, in
  `scratch/fable2_image_decrypted_expanded.bin`). Resolving "which function loads which
  string" statically requires the TOC section contents from the encrypted XEX.
- Scanning the expanded image for the absolute LE pointer values of known rodata string
  addresses (e.g. `0x820A17F0`, `0x820C57EA`) yields zero hits — consistent with the
  above.

## 8. Addressing cheat-sheet (guest → file offsets)

- `.fable2_image.bin` = `.text` section only, 21,200,896 bytes, guest
  `0x82000000`–`0x83438000`.
- `scratch/fable2_image_decrypted_expanded.bin` = `.text` + data, 23,166,976 bytes
  (`0x1618000`). File offset `< 0x1438000` → guest `0x82000000 + off`;
  offset `≥ 0x1438000` → guest `0x83438000 + (off − 0x1438000)`.
- Guest is big-endian (PPC 750); `bswap32` for 32-bit reads; wide strings UTF-16BE.
- Host arena base `0x100000000`; `host_ptr = 0x100000000 + guest_addr` for
  `guest_addr < 0xE0000000`. A second image mapping mirrors `0x82xxxxxx` at `0x92xxxxxx+`.

## 9. Implications for the icon-swap goal

Goal (from TEXT_RENDERING_NOTES.md): replace the Xbox A button icon in the title prompt
with a keyboard E keycap, using the game's own font/rendering, anchored to the prompt text,
only visible when the text is present.

Two distinct swap points exist:

1. **Prompt-text tag sprite** — the `<a_img>` tag → mapping table at `0x820D7B50`
   (and/or the `start.img` image referenced by the prompt layout). Affects only the
   inline prompt icon. This matches the requirement best.
2. **Material/texture binding** — `Material2270`'s `DEFAULT_TEX_ID` binding
   (heap `0x4057E9E0`) resolves `gui\controller\icon_button_a` → the Bink texture
   `Art/GUI/Controller/icon_button_a.tex`. Replacing the texture resource it points at
   affects **every** `icon_button_a` instance (main menu pages, HUD, logbook), not just
   the prompt.

Stability constraint: keep per-call probe/hook work register-only; heavy guest-memory
reads per call on the render thread expose a latent AV race (crash ~20 s in). Use
capped/sampled dumps or background threads for memory scans.

## 10. Bank TOC decoding (update — names are NOT opaque hashes)

`data/art/gui/gui_textures.bnk` uses the standard bnk container: zlib-compressed
TOC with **plaintext backslash names** (1079 entries). Earlier "hashed names"
conclusion was wrong — the TOC is compressed, so raw greps found nothing.

TOC layout (big-endian): `u32 entryCount`, then per entry:
`u32 nameLen (incl. NUL)` + `name (NUL-terminated)` + `u32 fileOffset` (relative to
the bank data base 0x8000) + `u32 entrySize`. No per-chunk tails in this bank.

Controller icon entries in `gui_textures.bnk`:

| Name | fileOffset | entrySize |
|---|---|---|
| `Art\GUI\Controller\icon_button_a.tex` | 20993360 (0x1405550) | 16388 |
| `Art\GUI\Controller\icon_button_b.tex` | 3727024 | 16388 |
| `Art\GUI\Controller\icon_button_y.tex` | 6498320 | 16388 |
| `Art\GUI\Controller\icon_button_x.tex` | 18388816 | 16388 |
| `Art\GUI\Controller\icon_button_lt.tex` | 7957032 | 1740 |

`.tex` payload format: `[u32 pixel_byte_count]` + raw pixels. For the button
icons the count is 0x4000 = 16384 = one 64x64 RGBA level (no mips).
`icon_button_a.tex` pixels: bank file offsets **[0x140D554, 0x1411554)**.
The default/background pixel is `00 49 49 49`; the glyph occupies roughly rows
2–61 of the 64x64.

## 11. White-square override (implemented, code-only, no art changed)

Goal: render the A-button icon as a plain white square without touching any art
file. Implemented as an in-flight content substitution:

- **SDK hook point**: `rex::system::XFile::ReadInternal` (thirdparty/rexglue-sdk-src/src/system/xfile.cpp)
  now calls `RexSetXFilePostReadHook(fn)` after every successful synchronous
  read (incl. per-segment reads from ReadScatter) with
  `(vfs_path, host_buffer, bytes_read, byte_offset)`. All guest file reads
  (CRT `_read`, `NtReadFile`) funnel through this one function; bank files are
  read, not mapped (`Entry::can_map()` is false for host-path files).
- **App side**: `src/diagnostics/fable2_white_icon.h` (self-registering at
  process start, like fable2_av_probe.h). On each read of `gui_textures.bnk`
  that overlaps [0x140D554, 0x1411554) it `memset`s the overlap to 0xFF —
  every channel white **and fully opaque** — so the engine decodes a solid
  white 64x64 and the icon quad (square) renders as a white square.
- Disable with `FABLE2_WHITE_ICON=0`. Log: `fable2_white_icon.log` next to the
  exe (logs the first 32 reads of the bank + every patch, so the read pattern
  is verifiable).
- Affects every consumer of that texture: main-menu icon buttons, HUD, and the
  "Press A" prompt icon (they share the same bank entry).

Verification to do at runtime: the log should show reads of
`…gui_textures.bnk` covering offset 0x140D554 and a "PATCHED icon pixels" line
when the main menu presentation loads; the A icon then appears as a white
square. If no read of that range ever appears, the texture is sourced from
elsewhere (e.g. a mapped or cached copy) and the hook must move to the
decode/upload path.

## 12. Open questions / suggested next steps

- Identify the unnamed per-frame GameFace face-draw function (the drawlist renderers
  that consume the `icon_button_a` widget/mesh objects). Approach: register-only hook on
  the per-frame candidates, sample r3/r4 for the parsed-object addresses
  (`0x4010543C`, `0x4057C828`, `0x4057E9E0` + neighbors), confirm with a timed ablation
  (visual confirmation of the icon disappearing).
- Pin down the exact leaf that binds `DEFAULT_TEX_ID` → Bink texture resource
  (texture streamer side), if a runtime texture swap is desired.
- If static string→function resolution is needed, extract the TOC section
  (`0x83C1xxxx`) from the encrypted XEX (SDK loader path: retail XEX2 key + per-section
  opt descriptors), then rebuild TOC-relative reference edges.
- `tools/bnk.lua` can parse the bnk container; combine its hash table with the runtime
  `gui\controller\icon_button_a` string to locate the exact bank entry offset for
  `icon_button_a.tex` inside `gui_textures.bnk`.
