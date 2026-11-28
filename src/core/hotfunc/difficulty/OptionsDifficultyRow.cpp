// Difficulty settings (docs/feature/difficulty_settings/ technical.md 24): a sixth row on the
// pause menu's Options > Game page, "Difficulty", with an Easy / Medium / Hard picker like the
// game's Glowing Trail Brightness. On a Hardcore hero it shows "Hardcore" (or "Hardcore+")
// and can't be changed (decision 2). F8 still opens the cards.
//
// The page (GameFace presentation art/gui/gameface/optionsscreen.bgf, page 1) has five rows,
// each a text element and a "<name>_pickbox" element under
// Foreground.Book.Pages.Page1.Contents.Options. The native page object (`self`) keeps one
// picker object per row in a map at self+28 (index -> shared handle) and the focused row at
// self+40 (read in the generated code):
//   - OptionsGame_Populate_828D7588 clears the map, OptionsGame_CreatePickers_828D7290 makes
//     the pickers (rows 1-4 only in game), OptionsGame_LoadSettings_828D84E0 reads the
//     settings, then each row's filler binds its picker to the element path
//     (Picker_Bind_828860E8), pushes the choices (StringList_PushBack_82356488 on picker+44)
//     and selects the value (Picker_Select_828862E8).
//   - OptionsGame_HandleInput_828D8E80: up stops at row 0, down at row 4 (a constant), left /
//     right go to the focused picker, Accept is OptionsGame_Accept_828D8AF0 (reads rows 0-4).
//
// So the row needs:
//   1. the elements: the Auto_joinable label and pickbox are cloned in GameFace Lua (clone()
//      keeps the old name, so they're renamed) and the six rows are respaced 70 apart (85
//      before) so the last one stays above Cancel / Accept (seen in game 2026-11-28). GUI Lua
//      only runs safely on the GUI thread, on the state calling a native, so it's posted as
//      an auto chunk (src/core/gui_auto_chunk.h);
//   2. a sixth picker at index 5, bound to "...Options.Difficulty_pickbox", with our choices;
//   3. the label's text set natively (GuiElement_SetText_823F8268): GameFace's textstring
//      doesn't show; the game localizes each text element from its text ID;
//   4. Down from row 4 to row 5, and the level applied on Accept, right away (the pause menu
//      stops the world update).
#include "fable_2_pch.h"

#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include <rex/logging/macros.h>
#include <rex/ppc/context.h>

#include "difficulty.h"
#include "fable2_patches.h"   // fable2::patches::GuestBase
#include "gui_auto_chunk.h"

extern "C" void __imp__OptionsGame_CreatePickers_828D7290(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsGame_Populate_828D7588(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsGame_HandleInput_828D8E80(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsGame_Accept_828D8AF0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsGame_SetFocus_828D8DE8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MenuInput_Translate_82841730(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsPage_GetPicker_828D9FE8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsPage_SetPicker_8247BDD0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__SharedHandle_Make_8247D448(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Picker_Construct_82886070(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Picker_Bind_828860E8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Picker_Select_828862E8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__StringList_PushBack_82356488(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__GuiScreens_Find_823381B8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Allocate_OrDbgBreak_8221F388(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__WideString_Assign_82B39DD8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Release_RefCounted_821C67D8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Release_RefCounted_829FF648(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ConstructRefCounted_8222CF18(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__CopyAssign_RefCounted_821F0108(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Gui_FindElement_823F8C30(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Speech_PlayByKey_82306098(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Handle_Release_822ADE08(PPCContext& ctx, uint8_t* base);
// The difficulty feature's override: it sets the text unless it's a slot label.
extern "C" void GuiElement_SetText_823F8268(PPCContext& ctx, uint8_t* base);

namespace {

namespace d = fable2::difficulty;

// --- Guest memory (big-endian) ------------------------------------------------------

uint8_t* Mem(uint32_t a) {
  return fable2::patches::GuestBase() + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
uint32_t U32(uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Mem(a), 4);
  return __builtin_bswap32(v);
}
uint8_t U8(uint32_t a) { return *Mem(a); }
void W32(uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Mem(a), &v, 4);
}
void W16(uint32_t a, uint16_t v) {
  *Mem(a) = static_cast<uint8_t>(v >> 8);
  *Mem(a + 1) = static_cast<uint8_t>(v & 0xFF);
}
bool Ptr(uint32_t a) { return a >= 0x10000u; }

size_t ThreadKey() { return std::hash<std::thread::id>{}(std::this_thread::get_id()); }

// --- The page ---------------------------------------------------------------------

constexpr uint32_t kRow = 5;           // our picker's index (the game's rows are 0-4)
constexpr uint32_t kGameRows = 5;
constexpr uint32_t kDownCode = 29;     // MenuInput_Translate_82841730: 28 up, 29 down, 36 accept
constexpr int32_t kDownSound = -30852; // the key OptionsGame_HandleInput plays on down (0x834A0000+)

constexpr const char* kOptionsPath = "Foreground.Book.Pages.Page1.Contents.Options";

// The page's GUI: *(*(*(0x834968A4)+56)+152), as OptionsGame_Populate passes it to the fillers.
uint32_t PageGui() {
  uint32_t a = U32(0x834968A4u);
  if (!Ptr(a)) return 0;
  a = U32(a + 56);
  return Ptr(a) ? U32(a + 152) : 0;
}

std::atomic<uint32_t> g_row_page{0};      // the page that got our picker (index 5) last
std::atomic<uint32_t> g_page{0};          // the last populated page object
std::atomic<size_t> g_page_thread{0};     // the thread that populated it
std::atomic<bool> g_elements_ready{false};
std::atomic<bool> g_bind_pending{false};
thread_local int32_t t_last_input = -1;   // MenuInput_Translate_82841730's last result

// Permanent guest strings: the two element paths as ref-string handles (built once).
uint32_t g_pickbox_path = 0;  // address of a handle
uint32_t g_label_path = 0;

// A fresh guest frame below the caller's: back chain at sp, scratch from sp + 0x100.
struct Frame {
  PPCContext& ctx;
  const PPCContext saved;
  uint32_t sp;
  explicit Frame(PPCContext& c, uint32_t size = 0x300) : ctx(c), saved(c) {
    sp = (c.r1.u32 - size) & ~0xFu;
    W32(sp, c.r1.u32);
    c.r1.u64 = sp;
  }
  ~Frame() { ctx = saved; }
  uint32_t at(uint32_t off) const { return sp + 0x100u + off; }
};

uint32_t GuestAlloc(PPCContext& ctx, uint8_t* base, uint32_t size) {
  ctx.r3.u64 = size;
  __imp__Allocate_OrDbgBreak_8221F388(ctx, base);
  return ctx.r3.u32;
}

// A permanent ref-string handle holding `text` (ConstructRefCounted_8222CF18, as the game
// builds its own path globals, e.g. 0x8349CB68 for the Online Orbs pickbox).
uint32_t MakePathHandle(PPCContext& ctx, uint8_t* base, const std::string& text) {
  Frame f(ctx);
  const uint32_t chars = GuestAlloc(ctx, base, static_cast<uint32_t>(text.size()) + 1);
  const uint32_t handle = GuestAlloc(ctx, base, 4);
  if (!Ptr(chars) || !Ptr(handle)) return 0;
  std::memcpy(Mem(chars), text.c_str(), text.size() + 1);
  W32(handle, 0);
  ctx.r3.u64 = handle;
  ctx.r4.u64 = chars;
  ctx.r5.s64 = -1;
  __imp__ConstructRefCounted_8222CF18(ctx, base);
  return handle;
}

// A new wide string record with one reference (the way the slot label builds one,
// Difficulty.cpp), stored in the handle at `handle`. Release it with Release_RefCounted_821C67D8.
bool MakeWide(PPCContext& ctx, uint8_t* base, uint32_t handle, const std::string& text) {
  Frame f(ctx, 0x200 + 2 * static_cast<uint32_t>(text.size()));
  const uint32_t chars = f.at(0);
  for (size_t i = 0; i < text.size(); ++i) W16(chars + 2 * static_cast<uint32_t>(i), static_cast<uint8_t>(text[i]));
  W16(chars + 2 * static_cast<uint32_t>(text.size()), 0);
  const uint32_t rec = GuestAlloc(ctx, base, 16);
  if (!Ptr(rec)) return false;
  W32(rec, 0);
  W32(rec + 4, 0);
  W32(rec + 8, 0);
  ctx.r3.u64 = rec;
  ctx.r4.u64 = chars;
  ctx.r5.u64 = text.size();
  __imp__WideString_Assign_82B39DD8(ctx, base);
  W32(rec + 12, 1);
  W32(handle, rec);
  return true;
}

void ReleaseWide(PPCContext& ctx, uint8_t* base, uint32_t handle) {
  Frame f(ctx);
  ctx.r3.u64 = handle;
  __imp__Release_RefCounted_821C67D8(ctx, base);
}

// Picker `index` of the page: OptionsPage_GetPicker_828D9FE8(&tmp, self+28, index) gives a
// shared handle {object, control}; the object is returned and the handle released.
// Only asked for our index on a page that got our picker: whether a missing index would be
// added by the lookup wasn't read.
uint32_t PickerAt(PPCContext& ctx, uint8_t* base, uint32_t self, uint32_t index) {
  if (index == kRow && self != g_row_page.load()) return 0;
  Frame f(ctx);
  const uint32_t tmp = f.at(0);
  W32(tmp, 0);
  W32(tmp + 4, 0);
  ctx.r3.u64 = tmp;
  ctx.r4.u64 = self + 28;
  ctx.r5.u64 = index;
  __imp__OptionsPage_GetPicker_828D9FE8(ctx, base);
  const uint32_t picker = U32(tmp);
  ctx.r3.u64 = tmp;
  __imp__Release_RefCounted_829FF648(ctx, base);
  return picker;
}

bool PickerBound(uint32_t picker) { return Ptr(picker) && U8(picker + 12) != 0; }

// The GUI Lua that makes the row (it runs in the options presentation's state): clone the
// Auto_joinable label and pickbox, rename them, respace the six rows. Returns "ready".
std::string CloneChunk() {
  return std::string(
             "local p = getElement('Scene.Layer.") + kOptionsPath + "') or getElement('Scene." + kOptionsPath +
         "')\n"
         "local function kid(n) for _, c in ipairs(getChildren(p)) do if getAttribute(c, 'name') == n then "
         "return c end end end\n"
         "if not kid('Difficulty_pickbox') then\n"
         "  local lab = clone(kid('Auto_joinable'), 'Difficulty', p)\n"
         "  local pick = clone(kid('Auto_joinable_pickbox'), 'Difficulty_pickbox', p)\n"
         "  if not lab or not pick then return 'clone failed' end\n"
         // clone() keeps the old name and the copies come last among the children: rename the
         // last of each.
         "  local L, P\n"
         "  for _, c in ipairs(getChildren(p)) do local n = getAttribute(c, 'name')\n"
         "    if n == 'Auto_joinable' then L = c elseif n == 'Auto_joinable_pickbox' then P = c end end\n"
         "  setAttribute(L, 'name', 'Difficulty') setAttribute(P, 'name', 'Difficulty_pickbox')\n"
         "end\n"
         "local rows = {'Subtitles', 'BreadcrumbSize', 'TutorialBoxes', 'MultiplayerOrbs', 'Auto_joinable', "
         "'Difficulty'}\n"
         "for i, n in ipairs(rows) do local y = 95 - 70 * (i - 1)\n"
         "  local a, b = kid(n), kid(n .. '_pickbox')\n"
         "  if a then setAttribute(a, 'position.y', y) end if b then setAttribute(b, 'position.y', y - 40) end end\n"
         "return 'ready'";
}

std::string WhereOptionsPage() {
  return std::string("(getElement('Scene.Layer.") + kOptionsPath + "') or getElement('Scene." + kOptionsPath +
         "')) ~= nil";
}

void EnsurePaths(PPCContext& ctx, uint8_t* base) {
  if (g_pickbox_path == 0) g_pickbox_path = MakePathHandle(ctx, base, std::string(kOptionsPath) + ".Difficulty_pickbox");
  if (g_label_path == 0) g_label_path = MakePathHandle(ctx, base, std::string(kOptionsPath) + ".Difficulty");
}

// Sets the cloned label's text: Gui_FindElement_823F8C30(copy of the path, gui), then
// GuiElement_SetText_823F8268(element, gui, &wide).
void SetLabel(PPCContext& ctx, uint8_t* base, uint32_t gui) {
  Frame f(ctx);
  const uint32_t name = f.at(0), wide = f.at(8);
  W32(name, 0);
  ctx.r3.u64 = name;
  ctx.r4.u64 = g_label_path;
  __imp__CopyAssign_RefCounted_821F0108(ctx, base);
  ctx.r4.u64 = gui;  // r3 = the name handle; Gui_FindElement destroys it
  __imp__Gui_FindElement_823F8C30(ctx, base);
  const uint32_t element = ctx.r3.u32;
  if (!Ptr(element) || !MakeWide(ctx, base, wide, "Difficulty")) {
    REXSYS_INFO("[difficulty] Options > Game: the Difficulty label wasn't found");
    return;
  }
  ctx.r3.u64 = element;
  ctx.r4.u64 = gui;
  ctx.r5.u64 = wide;
  GuiElement_SetText_823F8268(ctx, base);
  ReleaseWide(ctx, base, wide);
}

// Binds picker 5 to the cloned pickbox and fills it. Runs on the page's thread.
void BindAndFill(PPCContext& ctx, uint8_t* base, uint32_t self) {
  g_bind_pending.store(false);
  if (!Ptr(self) || d::CurrentIndex() < 0) return;
  const uint32_t gui = PageGui();
  if (!Ptr(gui)) return;
  EnsurePaths(ctx, base);
  if (g_pickbox_path == 0 || g_label_path == 0) return;
  const uint32_t picker = PickerAt(ctx, base, self, kRow);
  if (!Ptr(picker)) return;
  {
    Frame f(ctx);
    ctx.r3.u64 = picker;
    ctx.r4.u64 = gui;
    ctx.r5.u64 = g_pickbox_path;
    __imp__Picker_Bind_828860E8(ctx, base);
  }
  if (!PickerBound(picker)) {
    REXSYS_INFO("[difficulty] Options > Game: Difficulty_pickbox not there yet (picker {:08X})", picker);
    return;
  }
  const int now = d::CurrentIndex();
  const bool locked = now == 3;  // Hardcore can't be changed (decision 2)
  std::array<std::string, 3> choices = {d::Level(0).name, d::Level(1).name, d::Level(2).name};
  const size_t count = locked ? 1 : choices.size();
  if (locked) choices[0] = d::CurrentLevelName();
  {
    Frame f(ctx);
    const uint32_t wide = f.at(0);
    for (size_t i = 0; i < count; ++i) {
      if (!MakeWide(ctx, base, wide, choices[i])) continue;
      ctx.r3.u64 = picker + 44;
      ctx.r4.u64 = wide;
      __imp__StringList_PushBack_82356488(ctx, base);  // takes its own reference
      ReleaseWide(ctx, base, wide);
    }
    ctx.r3.u64 = picker;
    ctx.r4.u64 = locked ? 0 : static_cast<uint32_t>(now);
    __imp__Picker_Select_828862E8(ctx, base);
  }
  SetLabel(ctx, base, gui);
  REXSYS_INFO("[difficulty] Options > Game: Difficulty row ready ({}, picker {:08X}, thread {:X})",
              locked ? "locked" : d::Level(now).name, picker, ThreadKey());
}

void PostCloneChunk() {
  fable2::guilua::PostAutoChunk(
      WhereOptionsPage(), CloneChunk(), [](PPCContext& ctx, uint8_t* base, const std::string& r) {
        const bool ok = r == "ok ready";
        g_elements_ready.store(ok);
        REXSYS_INFO("[difficulty] Options > Game: clone chunk -> {} (GUI thread {:X}, page thread {:X})", r,
                    ThreadKey(), g_page_thread.load());
        if (!ok) return;
        if (ThreadKey() == g_page_thread.load()) {
          BindAndFill(ctx, base, g_page.load());
        } else {
          g_bind_pending.store(true);  // the next input on the page binds it
        }
      });
}

}  // namespace

// r3 = the page. Makes the pickers, rows 1-4 only when GuiScreens_Find_823381B8(*(*(0x834968A4)
// +28), 0) is absent or its +4 is 0 (in game). Adds a sixth, the same way, at index 5.
extern "C" void OptionsGame_CreatePickers_828D7290(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  __imp__OptionsGame_CreatePickers_828D7290(ctx, base);
  if (!Ptr(self) || d::CurrentIndex() < 0) return;
  Frame f(ctx);
  {
    const uint32_t root = U32(0x834968A4u);
    ctx.r3.u64 = Ptr(root) ? U32(root + 28) : 0;
    ctx.r4.u64 = 0;
    if (!Ptr(ctx.r3.u32)) return;
    __imp__GuiScreens_Find_823381B8(ctx, base);
    const uint32_t screen = ctx.r3.u32;
    if (Ptr(screen) && U32(screen + 4) != 0) return;  // the game's rows 1-4 aren't there either
  }
  const uint32_t obj = GuestAlloc(ctx, base, 64);
  const uint32_t h = f.at(0);
  W32(h, 0);
  W32(h + 4, 0);
  if (Ptr(obj)) {
    ctx.r3.u64 = obj;
    __imp__Picker_Construct_82886070(ctx, base);
    W32(h, ctx.r3.u32);
    ctx.r3.u64 = h;
    __imp__SharedHandle_Make_8247D448(ctx, base);
  }
  ctx.r3.u64 = self + 28;
  ctx.r4.u64 = h;
  ctx.r5.u64 = kRow;
  __imp__OptionsPage_SetPicker_8247BDD0(ctx, base);
  if ((ctx.r3.u32 & 0xFFu) != 0 && Ptr(obj)) g_row_page.store(self);
}

// r3 = the page. After the game's rows: the elements (once per presentation), then the row.
extern "C" void OptionsGame_Populate_828D7588(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  __imp__OptionsGame_Populate_828D7588(ctx, base);
  if (!Ptr(self) || d::CurrentIndex() < 0) return;
  g_page.store(self);
  g_page_thread.store(ThreadKey());
  BindAndFill(ctx, base, self);  // the elements may already be there (the page was open before)
  const uint32_t picker = PickerAt(ctx, base, self, kRow);
  if (!PickerBound(picker)) PostCloneChunk();
}

// r3 = the repeat state at page+72; returns the input code (28 up, 29 down, 36 accept, ...).
// It keeps auto-repeat state, so it is only recorded here, never called twice.
extern "C" void MenuInput_Translate_82841730(PPCContext& __restrict ctx, uint8_t* base) {
  __imp__MenuInput_Translate_82841730(ctx, base);
  t_last_input = ctx.r3.s32;
}

// r3 = the page. Down from row 4 goes to our row; everything else is the game's.
extern "C" void OptionsGame_HandleInput_828D8E80(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  if (g_bind_pending.load() && self == g_page.load()) BindAndFill(ctx, base, self);
  const uint32_t before = Ptr(self) ? U32(self + 40) : 0;
  t_last_input = -1;
  __imp__OptionsGame_HandleInput_828D8E80(ctx, base);
  if (!Ptr(self) || t_last_input != static_cast<int32_t>(kDownCode)) return;
  if (before != kGameRows - 1 || U32(self + 40) != kGameRows - 1) return;
  if (!PickerBound(PickerAt(ctx, base, self, kRow))) return;
  Frame f(ctx);
  ctx.r3.u64 = self;
  ctx.r4.u64 = kRow;
  __imp__OptionsGame_SetFocus_828D8DE8(ctx, base);
  // The down sound, as the game plays it (Speech_PlayByKey_82306098 into a handle, released).
  const uint32_t handle = f.at(0);
  W32(handle, 0);
  W32(handle + 4, 0);
  ctx.r3.u64 = handle;
  ctx.r4.u64 = U32(0x834968A4u);
  ctx.r5.u64 = 0x834A0000u + static_cast<uint32_t>(kDownSound);
  ctx.r6.u64 = 0;
  __imp__Speech_PlayByKey_82306098(ctx, base);
  ctx.r3.u64 = handle;
  __imp__Handle_Release_822ADE08(ctx, base);
}

// r3 = the page. The game stores rows 0-4; then our row's choice is applied.
extern "C" void OptionsGame_Accept_828D8AF0(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  __imp__OptionsGame_Accept_828D8AF0(ctx, base);
  if (!Ptr(self) || d::CurrentIndex() < 0) return;
  const uint32_t picker = PickerAt(ctx, base, self, kRow);
  if (!PickerBound(picker)) return;
  const int32_t chosen = static_cast<int32_t>(U32(picker + 36));
  if (!d::ApplyLevelNow(ctx, base, chosen)) {
    REXSYS_INFO("[difficulty] Options > Game: Accept, level kept ({})", d::CurrentLevelName());
  }
}
