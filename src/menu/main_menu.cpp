// Adds the recomp's items to the front-end main menu and the pause menu.
//
// FrontMenu_Populate builds the main menu with one ExpandableMenu_SpawnButton
// call per item. When an item is chosen, FrontMenu_HandleEvent receives an
// event whose first word points at the item's name string.
//
// The pause (Start) menu is the same kind of menu: PauseMenu_Populate adds
// its items with PauseMenu_SpawnButton (same arguments, event type 300, the
// menu element from a global instead of the menu), and PauseMenu_HandleEvent
// gets the chosen item the same way.

#include "fable_2_pch.h"

#include <mutex>
#include <string_view>
#include <utility>

#include "menu_items.h"
#include "menu_labels.h"
#include "port_settings_state.h"

extern "C" void ConstructRefCounted_8222CF18(PPCContext& ctx, uint8_t* base);
extern "C" void DestructRefCounted_82214F08(PPCContext& ctx, uint8_t* base);
extern "C" void ExpandableMenu_SpawnButton(PPCContext& ctx, uint8_t* base);
extern "C" void PauseMenu_SpawnButton(PPCContext& ctx, uint8_t* base);

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

// How the game adds the items of one menu: its spawn function, the event
// type its items send when chosen, and the icon its options item shows. All
// items hang off the "Root" item.
struct MenuKind {
  void (*spawn)(PPCContext&, uint8_t*);
  uint32_t event_type;
  std::string_view icon;
};
constexpr MenuKind kMainMenu = {ExpandableMenu_SpawnButton, 2, "Art\\GUI\\icon_options.tga"};
constexpr MenuKind kPauseMenu = {PauseMenu_SpawnButton, 300, "Art\\GUI\\WheelIcons\\icon_options.tga"};
constexpr std::string_view kRootParent = "Root";

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

// Mirrors one add-item call in FrontMenu_Populate / PauseMenu_Populate. The
// label is handed over as the already-localized wide string the game would get
// from its string table. `element` is the menu element the populate function
// passes to every spawn call.
void AddItem(PPCContext& ctx, uint8_t* base, uint32_t menu, uint32_t element, const MenuKind& kind,
             const MenuItem& item) {
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t sp = caller_sp - kFrameSize;
  REX_STORE_U32(sp, caller_sp);
  REX_STORE_U32(sp + 84, 0);
  WriteString(base, sp + kNameChars, item.id, kParentChars - kNameChars - 1);
  WriteString(base, sp + kParentChars, kRootParent, kIconChars - kParentChars - 1);
  WriteString(base, sp + kIconChars, kind.icon, kTextChars - kIconChars - 1);
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
  ctx.r9.u64 = kind.event_type;
  ctx.r10.u64 = element;
  kind.spawn(ctx, base);

  DestructString(ctx, base, sp + kParentSlot);
  DestructString(ctx, base, sp + kIconSlot);
  DestructString(ctx, base, sp + kNameSlot);
  ctx.r1.u64 = caller_sp;
}

template <size_t N>
const MenuItem* FindChosenItem(uint8_t* base, uint32_t event, const MenuItem (&items)[N]) {
  if (event == 0) return nullptr;
  // These event types never carry an item name (see the original handlers).
  const uint32_t type = REX_LOAD_U32(event + 4);
  if (type == 302 || type == 141 || type == 140) return nullptr;
  const uint32_t name_ref = REX_LOAD_U32(event);
  if (name_ref == 0) return nullptr;
  const uint32_t name = REX_LOAD_U32(name_ref);
  if (name == 0) return nullptr;
  for (const MenuItem& item : items) {
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

void OpenPortSettings() { fable2::portsettings::open_request().store(true); }

}  // namespace fable2::menu

extern "C" void __imp__FrontMenu_Populate(PPCContext& ctx, uint8_t* base);
extern "C" void FrontMenu_Populate(PPCContext& ctx, uint8_t* base) {
  const uint32_t menu = ctx.r3.u32;
  __imp__FrontMenu_Populate(ctx, base);
  const uint64_t result = ctx.r3.u64;
  const uint32_t element = REX_LOAD_U32(menu + 20);
  for (const auto& item : fable2::menu::kMainMenuItems) {
    fable2::menu::AddItem(ctx, base, menu, element, fable2::menu::kMainMenu, item);
  }
  ctx.r3.u64 = result;
}

extern "C" void __imp__FrontMenu_HandleEvent(PPCContext& ctx, uint8_t* base);
extern "C" void FrontMenu_HandleEvent(PPCContext& ctx, uint8_t* base) {
  if (const auto* item = fable2::menu::FindChosenItem(base, ctx.r4.u32, fable2::menu::kMainMenuItems)) {
    item->activate();
    return;
  }
  __imp__FrontMenu_HandleEvent(ctx, base);
}

extern "C" void __imp__PauseMenu_Populate(PPCContext& ctx, uint8_t* base);
extern "C" void PauseMenu_Populate(PPCContext& ctx, uint8_t* base) {
  const uint32_t menu = ctx.r3.u32;
  __imp__PauseMenu_Populate(ctx, base);
  const uint64_t result = ctx.r3.u64;
  // The element PauseMenu_Populate hands to every spawn call:
  // *(*(*(0x834968A4) + 0x38) + 0xC).
  uint32_t element = 0;
  if (const uint32_t gui = REX_LOAD_U32(0x834968A4u))
    if (const uint32_t screens = REX_LOAD_U32(gui + 0x38)) element = REX_LOAD_U32(screens + 0xC);
  if (element != 0) {
    for (const auto& item : fable2::menu::kPauseMenuItems) {
      fable2::menu::AddItem(ctx, base, menu, element, fable2::menu::kPauseMenu, item);
    }
  }
  ctx.r3.u64 = result;
}

extern "C" void __imp__PauseMenu_HandleEvent(PPCContext& ctx, uint8_t* base);
extern "C" void PauseMenu_HandleEvent(PPCContext& ctx, uint8_t* base) {
  if (const auto* item = fable2::menu::FindChosenItem(base, ctx.r4.u32, fable2::menu::kPauseMenuItems)) {
    item->activate();
    return;
  }
  __imp__PauseMenu_HandleEvent(ctx, base);
}
