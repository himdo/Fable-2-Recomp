// Adds the recomp's items to the front-end main menu.
//
// FrontMenu_Populate builds the main menu with one ExpandableMenu_SpawnButton
// call per item. When an item is chosen, FrontMenu_HandleEvent receives an
// event whose first word points at the item's name string.

#include "fable_2_pch.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "menu_items.h"
#include "menu_labels.h"
#include "port_settings_state.h"

extern "C" void ConstructRefCounted_8222CF18(PPCContext& ctx, uint8_t* base);
extern "C" void DestructRefCounted_82214F08(PPCContext& ctx, uint8_t* base);
extern "C" void ExpandableMenu_SpawnButton(PPCContext& ctx, uint8_t* base);

namespace fable2::menu {
namespace {

std::mutex g_quit_mutex;
std::function<void()> g_quit_handler;

// Guest stack frame for one add-item call.
constexpr uint32_t kFrameSize = 0x200;
constexpr uint32_t kNameSlot = 96;
constexpr uint32_t kIconSlot = 100;
constexpr uint32_t kParentSlot = 104;
constexpr uint32_t kTextSlot = 108;
constexpr uint32_t kTextHandle = 112;
constexpr uint32_t kNameChars = 0x100;
constexpr uint32_t kParentChars = 0x120;
constexpr uint32_t kIconChars = 0x130;
constexpr uint32_t kTextChars = 0x160;
constexpr size_t kMaxTextLength = (kFrameSize - kTextChars) / 2 - 1;

// The game's main-menu items all use event type 2, hang off the "Root" item
// and show an icon.
constexpr uint32_t kMenuButtonEventType = 2;
constexpr std::string_view kRootParent = "Root";
constexpr std::string_view kIcon = "Art\\GUI\\icon_options.tga";

void WriteString(uint8_t* base, uint32_t addr, std::string_view text, size_t max_length) {
  text = text.substr(0, max_length);
  for (size_t i = 0; i < text.size(); ++i) {
    REX_STORE_U8(addr + i, static_cast<uint8_t>(text[i]));
  }
  REX_STORE_U8(addr + text.size(), 0);
}

void WriteWideString(uint8_t* base, uint32_t addr, std::u16string_view text) {
  text = text.substr(0, kMaxTextLength);
  for (size_t i = 0; i < text.size(); ++i) {
    REX_STORE_U16(addr + i * 2, static_cast<uint16_t>(text[i]));
  }
  REX_STORE_U16(addr + text.size() * 2, 0);
}

void ConstructString(PPCContext& ctx, uint8_t* base, uint32_t slot, uint32_t chars) {
  ctx.r3.u64 = slot;
  ctx.r4.u64 = chars;
  ctx.r5.s64 = -1;
  ConstructRefCounted_8222CF18(ctx, base);
}

void DestructString(PPCContext& ctx, uint8_t* base, uint32_t slot) {
  ctx.r3.u64 = slot;
  DestructRefCounted_82214F08(ctx, base);
}

// Mirrors one add-item call in FrontMenu_Populate. The label is handed over as
// the already-localized wide string the game would get from its string table.
void AddItem(PPCContext& ctx, uint8_t* base, uint32_t menu, const MenuItem& item) {
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t sp = caller_sp - kFrameSize;
  REX_STORE_U32(sp, caller_sp);
  REX_STORE_U32(sp + 84, 0);
  WriteString(base, sp + kNameChars, item.id, kParentChars - kNameChars - 1);
  WriteString(base, sp + kParentChars, kRootParent, kIconChars - kParentChars - 1);
  WriteString(base, sp + kIconChars, kIcon, kTextChars - kIconChars - 1);
  WriteWideString(base, sp + kTextChars, LabelText(item.label));
  REX_STORE_U32(sp + kTextSlot, sp + kTextHandle);
  REX_STORE_U32(sp + kTextHandle, sp + kTextChars);
  ctx.r1.u64 = sp;

  ConstructString(ctx, base, sp + kNameSlot, sp + kNameChars);
  ConstructString(ctx, base, sp + kIconSlot, sp + kIconChars);
  ConstructString(ctx, base, sp + kParentSlot, sp + kParentChars);

  ctx.r3.u64 = menu;
  ctx.r4.u64 = sp + kNameSlot;
  ctx.r5.u64 = sp + kTextSlot;
  ctx.r6.u64 = sp + kIconSlot;
  ctx.r7.u64 = sp + kParentSlot;
  ctx.r8.u64 = 0;
  ctx.r9.u64 = kMenuButtonEventType;
  ctx.r10.u64 = REX_LOAD_U32(menu + 20);
  ExpandableMenu_SpawnButton(ctx, base);

  DestructString(ctx, base, sp + kParentSlot);
  DestructString(ctx, base, sp + kIconSlot);
  DestructString(ctx, base, sp + kNameSlot);
  ctx.r1.u64 = caller_sp;
}

const MenuItem* FindChosenItem(uint8_t* base, uint32_t event) {
  if (event == 0) return nullptr;
  // These event types never carry an item name (see the original handler).
  const uint32_t type = REX_LOAD_U32(event + 4);
  if (type == 302 || type == 141 || type == 140) return nullptr;
  const uint32_t name_ref = REX_LOAD_U32(event);
  if (name_ref == 0) return nullptr;
  const uint32_t name = REX_LOAD_U32(name_ref);
  if (name == 0) return nullptr;
  for (const MenuItem& item : kMainMenuItems) {
    bool match = true;
    for (size_t i = 0; match && i <= item.id.size(); ++i) {
      const char expected = i < item.id.size() ? item.id[i] : '\0';
      match = static_cast<char>(REX_LOAD_U8(name + i)) == expected;
    }
    if (match) return &item;
  }
  return nullptr;
}

}  // namespace

void SetQuitHandler(std::function<void()> handler) {
  std::lock_guard lock(g_quit_mutex);
  g_quit_handler = std::move(handler);
}

void QuitGame() {
  std::lock_guard lock(g_quit_mutex);
  if (g_quit_handler) g_quit_handler();
}

void OpenPortSettings() {
  fable2::portsettings::from_main_menu().store(false);
  fable2::portsettings::open_request().store(true);
}

void OpenPortSettingsFromMainMenu() {
  fable2::portsettings::from_main_menu().store(true);
  fable2::portsettings::open_request().store(true);
}

}  // namespace fable2::menu

namespace {
// Set while the game's own FrontMenu_Populate runs, so the SpawnButton filter
// below only touches the main menu.
thread_local bool t_populating_front_menu = false;

std::string GuestName(uint8_t* base, uint32_t slot) {
  std::string s;
  if (slot == 0) return s;
  // The slot holds the string object; its first word points at the chars.
  const uint32_t object = REX_LOAD_U32(slot);
  if (object == 0) return s;
  const uint32_t chars = REX_LOAD_U32(object);
  if (chars == 0) return s;
  for (uint32_t i = 0; i < 64; ++i) {
    const char c = static_cast<char>(REX_LOAD_U8(chars + i));
    if (c == '\0') break;
    s.push_back(c);
  }
  return s;
}
}  // namespace

// The main menu's Downloadable Content row is left out: the port has no
// marketplace to show. The game's populate still runs; only that button's
// spawn is skipped.
extern "C" void __imp__ExpandableMenu_SpawnButton(PPCContext& ctx, uint8_t* base);
extern "C" void ExpandableMenu_SpawnButton(PPCContext& ctx, uint8_t* base) {
  if (t_populating_front_menu && GuestName(base, ctx.r4.u32) == "Downloadable Content") {
    REXSYS_INFO("[main-menu] left out Downloadable Content");
    return;
  }
  __imp__ExpandableMenu_SpawnButton(ctx, base);
}

extern "C" void __imp__FrontMenu_Populate(PPCContext& ctx, uint8_t* base);
extern "C" void FrontMenu_Populate(PPCContext& ctx, uint8_t* base) {
  const uint32_t menu = ctx.r3.u32;
  t_populating_front_menu = true;
  __imp__FrontMenu_Populate(ctx, base);
  t_populating_front_menu = false;
  const uint64_t result = ctx.r3.u64;
  for (const auto& item : fable2::menu::kMainMenuItems) {
    fable2::menu::AddItem(ctx, base, menu, item);
  }
  ctx.r3.u64 = result;
}

extern "C" void __imp__FrontMenu_HandleEvent(PPCContext& ctx, uint8_t* base);
extern "C" void FrontMenu_HandleEvent(PPCContext& ctx, uint8_t* base) {
  if (const auto* item = fable2::menu::FindChosenItem(base, ctx.r4.u32)) {
    item->activate();
    return;
  }
  __imp__FrontMenu_HandleEvent(ctx, base);
}

// The pause menu's Options screen: its rows come from GameFace Lua, where
// src/core/port_settings_lua.h adds the Advanced row. A on a row reaches
// OptionsMenu_HandleInput, which asks ExpandableMenu_GetHighlightedItem for
// the row and opens options page (row event type - 70). For the Advanced row
// the Advanced Options page opens instead and the game's handler is skipped.
extern "C" void ExpandableMenu_GetHighlightedItem(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__OptionsMenu_HandleInput(PPCContext& ctx, uint8_t* base);

namespace fable2::menu {
namespace {

constexpr uint32_t kInputA = 0x24;
constexpr std::string_view kAdvancedRow = "RecompSettings";  // port_settings_lua.h NAME

// Whether the Options menu's highlighted row is the Advanced row, read the
// same way OptionsMenu_HandleInput reads it.
bool AdvancedRowHighlighted(PPCContext& ctx, uint8_t* base) {
  const uint32_t gui = REX_LOAD_U32(0x834968A4u);
  const uint32_t screens = gui ? REX_LOAD_U32(gui + 0x1C) : 0;
  const uint32_t owner = screens ? REX_LOAD_U32(screens + 0x3C) : 0;
  if (owner == 0) return false;

  const PPCContext saved = ctx;
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t sp = caller_sp - 0x100;
  const uint32_t data = sp + 0x60;  // {type, id, name string, text string}
  REX_STORE_U32(sp, caller_sp);
  REX_STORE_U32(data, 0);
  REX_STORE_U32(data + 4, 0);
  ctx.r1.u64 = sp;
  ConstructString(ctx, base, data + 8, 0);
  ConstructString(ctx, base, data + 12, 0);

  ctx.r3.u64 = owner;
  ctx.r4.u64 = data;
  ExpandableMenu_GetHighlightedItem(ctx, base);

  bool match = false;
  if (const uint32_t name_ref = REX_LOAD_U32(data + 8)) {
    if (const uint32_t name = REX_LOAD_U32(name_ref)) {
      match = true;
      for (size_t i = 0; match && i <= kAdvancedRow.size(); ++i) {
        const char expected = i < kAdvancedRow.size() ? kAdvancedRow[i] : '\0';
        match = static_cast<char>(REX_LOAD_U8(name + i)) == expected;
      }
    }
  }
  DestructString(ctx, base, data + 12);
  DestructString(ctx, base, data + 8);
  ctx = saved;
  return match;
}

}  // namespace
}  // namespace fable2::menu

extern "C" void OptionsMenu_HandleInput(PPCContext& ctx, uint8_t* base) {
  const uint32_t screen = ctx.r3.u32;
  // A on the menu (no options page open: the byte at screen+0x18 is 0).
  if (ctx.r4.u32 == fable2::menu::kInputA && screen != 0 && REX_LOAD_U8(screen + 0x18) == 0 &&
      fable2::menu::AdvancedRowHighlighted(ctx, base)) {
    fable2::menu::OpenPortSettings();
    return;
  }
  __imp__OptionsMenu_HandleInput(ctx, base);
}
