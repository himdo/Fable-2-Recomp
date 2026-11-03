// fable_2 - ReXGlue Recompiled Project
//
// Fit the game to the window, part 2: the HUD on a wide window.
//
// The HUD, the title screen and the menus are Anark Gameface presentations
// (Art\GUI\Gameface\*.bgf, Lua in guiscripts.bnk). They are laid out on a
// 1280x720 canvas, and the GUI camera follows the same aspect as the 3D view,
// so on a window of aspect A the visible canvas runs from 640 - 360*A to
// 640 + 360*A. Nothing moves the HUD out of the centre 0..1280 part, so it
// sits in a 16:9 box in the middle of a wide window.
//
// The HUD already has a widescreen layout. Each edge element carries a
// WidescreenAdapter behaviour (Scripts\Gameface\HUD\WidescreenAdapter.lua):
// on the "setWidescreenMode" event it moves its parent from the 4:3 position
// to the authored 16:9 position in widescreenXPos/widescreenYPos (and sets
// scale.x from WidescreenXWidth). This pushes each of those elements a
// further dx = 360*A - 640 canvas units the same way, so the HUD sits at the
// real screen edges.
//
// How the Lua gets in: every presentation goes through
// ProcessAndProcessAndProcess21_823FE988 when it loads, which runs short Lua
// strings in it ("GUI:StoreScriptsDirectoryAndLoadAppropriateScripts()") with
// ConstructRefCounted_8222CF18 + ProcessAndVcallAndProcess16_823F8908. The
// hook below runs one more string the same way, after the game's own.
//
// Layout log (fit_window_ui_log = true): writes [ui-layout] lines to the main
// game log with each presentation's element tree (names, positions, scales)
// and every message Gameface reports (its Lua "output" function and its
// script errors). Gameface sends those to a callback pointer that the game
// leaves at NoOp_Stub_829CE870, so the stub is hooked and only messages from
// Gameface code (0x82BA0000-0x82BE0000) are kept.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

REXCVAR_DEFINE_BOOL(fit_window_hud_to_edges, true, "Display",
                    "With fit_game_to_window: move the HUD's edge elements to "
                    "the edges of a wide window");
REXCVAR_DEFINE_BOOL(fit_window_native_16x9, true, "Display",
                    "With fit_game_to_window: keep the game's own 2D screens "
                    "(loading screens, the saving banner) at 16:9 in the "
                    "middle of a wide window instead of stretching them");
REXCVAR_DEFINE_BOOL(fit_window_ui_log, true, "Display",
                    "Log [ui-layout] lines: the element tree of each menu "
                    "and HUD screen as it loads, plus Gameface script messages");

namespace fable2::fitwindow {
inline float CurrentAspect();
inline bool WriteGuestAspect(float aspect);
}

namespace fable2::fitwindow::ui {

namespace detail {

// Test builds log the layout whatever fit_window_ui_log says, because L's
// config file does not reach this cvar. Bump kBuild with each test zip.
inline constexpr bool kAlwaysLogLayout = true;
inline constexpr int kBuild = 35;
inline bool LogLayout() {
  return kAlwaysLogLayout || REXCVAR_GET(fit_window_ui_log);
}

inline std::atomic<rex::memory::Memory*> g_memory{nullptr};
inline uint32_t g_script_buffer = 0;  // guest address, kScriptBufferSize bytes
inline constexpr uint32_t kScriptBufferSize = 32 * 1024;  // build 24 script was 16.1 KB and silently never ran
inline std::atomic<uint32_t> g_presentations{0};

inline constexpr uint32_t kGamefaceCodeBegin = 0x82BA0000;
inline constexpr uint32_t kGamefaceCodeEnd = 0x82BE0000;
inline constexpr uint32_t kMaxLogLines = 50000;

// Layout lines go to the main game log with a [ui-layout] prefix.
inline void Log(const char* format, ...) {
  static std::atomic<uint32_t> lines{0};
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  const uint32_t n = lines.fetch_add(1) + 1;
  if (n < kMaxLogLines) {
    REXSYS_INFO("[ui-layout] {}", buffer);
  } else if (n == kMaxLogLines) {
    REXSYS_INFO("[ui-layout] (line limit reached, no more layout lines)");
  }
}

// Copies a NUL-terminated guest string, or returns false if it is not in
// readable guest memory.
inline bool ReadGuestString(uint32_t address, std::string& out) {
  rex::memory::Memory* memory = g_memory.load();
  if (!memory || address < 0x10000) return false;
  auto* heap = memory->LookupHeap(address);
  if (!heap) return false;
  constexpr uint32_t kMax = 1000;
  // Stay inside the page the string starts on plus the next one.
  const uint32_t page = heap->page_size();
  const uint32_t limit = (address & ~(page - 1)) + 2 * page - address;
  const uint32_t length = std::min(kMax, limit);
  if (heap->QueryRangeAccess(address, address + length - 1) ==
      rex::memory::PageAccess::kNoAccess) {
    return false;
  }
  const char* text = memory->TranslateVirtual<const char*>(address);
  out.assign(text, strnlen(text, length));
  return true;
}

// The Lua run in each presentation, after a first line that sets A (window
// aspect), dx (extra canvas units per side), moveHud and logTree. Lua 5.1; every Gameface call is wrapped in pcall so a missing element or
// attribute cannot stop the rest of the presentation's scripts.
inline constexpr const char kScript[] = R"lua(
local function out(s) if logTree then pcall(output, "[fitwin] " .. s) end end
local ok, scene = pcall(getElement, "Scene")
if not ok or not scene then scene = SCENE end
if not scene then out("no Scene element") return end
local function attr(e, n)
  local okh, has = pcall(hasAttribute, e, n)
  if not okh or not has then return nil end
  local okg, v = pcall(getAttribute, e, n)
  if okg then return v end
  return nil
end
local shown = { "position.x", "position.y", "position.z", "scale.x",
  "scale.y", "pivot.x", "active", "opacity", "widescreenXPos",
  "widescreenYPos", "WidescreenXWidth", "fov", "aspect", "orthographic",
  "width", "height" }
-- Live aspect: the host sets __fitwin_A in every GUI state when the window
-- or `f2 ws` changes the aspect, and each per-frame update re-reads it, so
-- every offset below follows it without reloading the screen (16:9 gives
-- dx 0 and r 1: the authored layout). This screen's load value goes in
-- first so a state never keeps an older one.
rawset(_G, "__fitwin_A", A)
local r = 360 * A / 640
local function live()
  local g = rawget(_G, "__fitwin_A")
  if type(g) == "number" and g > 0 and g ~= A then
    A = g
    dx = 360 * A - 640
    r = 360 * A / 640
  end
end
-- Tuning factors, also read every frame, so `f2 gui __fitwin_tune.x = v`
-- can adjust them live. Each is a multiple of dx, so 16:9 stays authored.
local tune = rawget(_G, "__fitwin_tune")
if type(tune) ~= "table" then
  tune = {}
  rawset(_G, "__fitwin_tune", tune)
end
local function tk(name, default)
  local v = tune[name]
  if type(v) == "number" then return v end
  tune[name] = default
  return default
end
local moved, lines = 0, 0
-- HUD edge elements: recorded once, then re-applied with the current dx/r.
local hudItems = {}
local function applyHud(it)
  if it.side ~= 0 then
    local nx = it.wx + it.side * dx
    if it.nx ~= nx then
      pcall(setAttribute, it.b, "widescreenXPos", nx)
      if it.moveParent then pcall(setAttribute, it.p, "position.x", nx) end
      it.nx = nx
    end
  end
  if it.ww then
    local w = it.ww * r
    if it.w ~= w then
      pcall(setAttribute, it.b, "WidescreenXWidth", w)
      if it.scaleParent then pcall(setAttribute, it.p, "scale.x", w) end
      it.w = w
    end
  end
end
local function adapt(b, depth)
  local wx = attr(b, "widescreenXPos")
  if not wx then return end
  local okp, p = pcall(getElement, "parent", b)
  if not okp or not p then return end
  local pname = tostring(attr(p, "name"))
  local it = { b = b, p = p, wx = wx, side = 0 }
  if wx ~= 0 then
    local cx = attr(p, "position.x") or wx
    if math.abs(wx - cx) > 1 then
      if wx > cx then it.side = 1 else it.side = -1 end
    elseif wx < 440 then it.side = -1
    elseif wx > 840 then it.side = 1 end
    it.moveParent = math.abs(cx - wx) <= 1
    if it.side ~= 0 then
      moved = moved + 1
      out(string.format("move %s: x now %.1f, 16:9 %.1f -> %.1f",
        pname, cx, wx, wx + it.side * dx))
    else
      out(string.format("keep %s: 16:9 x %.1f is near the centre", pname, wx))
    end
  end
  local ww = attr(b, "WidescreenXWidth")
  if ww and ww ~= 0 then
    local cs = attr(p, "scale.x") or ww
    it.ww = ww
    it.scaleParent = math.abs(cs - ww) < 0.001
    out(string.format("widen %s: scale.x %.3f -> %.3f", pname, ww, ww * r))
  end
  hudItems[#hudItems + 1] = it
  applyHud(it)
end
local function walk(e, depth)
  local okc, kids = pcall(getChildren, e)
  if not okc or type(kids) ~= "table" then return end
  for i = 1, #kids do
    local c = kids[i]
    local okt, t = pcall(getElementType, c)
    t = okt and t or "?"
    if logTree and lines < 3000 then
      lines = lines + 1
      local line = string.rep("  ", depth) .. tostring(attr(c, "name")) .. " <" .. tostring(t) .. ">"
      for j = 1, #shown do
        local v = attr(c, shown[j])
        if v ~= nil then line = line .. " " .. shown[j] .. "=" .. tostring(v) end
      end
      out(line)
    end
    if t == "behavior" then
      if moveHud then adapt(c, depth) end
    elseif t ~= "material" and t ~= "texture" then
      walk(c, depth + 1)
    end
  end
end
if logTree then
  local oks, w, h, m = pcall(getScreenInfo)
  out("screen info " .. tostring(w) .. " " .. tostring(h) .. " " .. tostring(m))
  out(string.format("aspect %.4f, extra width per side %.1f", A, dx))
end
walk(scene, 0)
out("moved " .. moved .. " widescreen elements")

-- Menus. They have no widescreen layout, so each known screen gets its own
-- fix, found by element names from the layout log.
if not moveHud then return end
local function kids(e)
  local okc, k = pcall(getChildren, e)
  if okc and type(k) == "table" then return k end
  return {}
end
local function child(e, name)
  local k = kids(e)
  for i = 1, #k do
    if attr(k[i], "name") == name then return k[i] end
  end
  return nil
end
local function find(e, name, depth)
  if depth > 12 then return nil end
  local k = kids(e)
  for i = 1, #k do
    if attr(k[i], "name") == name then return k[i] end
  end
  for i = 1, #k do
    local okt, t = pcall(getElementType, k[i])
    if okt and t ~= "material" and t ~= "texture" and t ~= "behavior" then
      local f = find(k[i], name, depth + 1)
      if f then return f end
    end
  end
  return nil
end
-- Logs at most 40 lines from the per-frame re-apply, so a flapping value
-- cannot flood the log.
local fixLines = 0
local function fout(s)
  if fixLines < 40 then fixLines = fixLines + 1 out(s) end
end
-- Sets attribute `name` of an element to a value derived from its authored
-- one (the first value seen, at load): first + p (nudge) or first * p
-- (scaleBy). This runs every frame and only touches the value while it sits
-- at the authored one or at the last value set here, so a slide change gets
-- re-applied, a new aspect retargets it, and a running animation is left
-- alone. At 16:9 the target is the authored value.
local first, applied, last = {}, {}, {}
local fixTick = 0  -- per-frame counter (the onUpdate hook below)
local function track(e, key, name, mul, p)
  if not e then return end
  local v = attr(e, name)
  if v == nil then return end
  local k = key .. "." .. name
  if first[k] == nil then first[k] = v applied[k] = 0 end
  -- A value the game puts back every frame (build 8: the Boy and Girl
  -- cards) is one it animates; fighting it only flickers, so give up.
  if applied[k] > 10 then return end
  local want = mul and first[k] * p or first[k] + p
  if math.abs(v - want) < 0.01 then
    -- Already reads as wanted, but a value set while the element's own
    -- animation runs is overridden when it draws, and once that animation
    -- ends its last frame stays (console test: the main menu covers read
    -- -466 but drew at -236). Setting the same value again does nothing, and
    -- so does a hair off and back within one frame (build 31: the cards), so
    -- every 15 frames it goes a hair off on one frame and back on the next.
    if want ~= first[k] and fixTick % 15 == 0 then
      pcall(setAttribute, e, name, want + 0.05)
      last[k] = want + 0.05
    end
    return
  elseif last[k] and math.abs(want + 0.05 - v) < 0.001 then
    pcall(setAttribute, e, name, want)  -- the frame after the hair-off write
    last[k] = want
    return
  end
  local atFirst = math.abs(v - first[k]) < 0.01
  if not atFirst and not (last[k] and math.abs(v - last[k]) < 0.01) then return end
  if atFirst then applied[k] = applied[k] + 1 end
  pcall(setAttribute, e, name, want)
  last[k] = want
  if applied[k] > 10 then
    out(key .. " " .. name .. " is driven by the game every frame, left alone")
  else
    fout(string.format("%s %s %.2f -> %.2f", key, name, v, want))
  end
end
local function nudge(e, key, name, delta) track(e, key, name, false, delta) end
local function scaleBy(e, key, name, f) track(e, key, name, true, f) end
-- A full-screen plane authored at 1280 wide: widen it to the window.
local function widen(layer, name)
  local e = child(layer, name)
  local k = name .. ".scale.x"
  local sx = first[k] or (e and attr(e, "scale.x"))
  if not sx or math.abs(sx - 12.8) > 0.01 then return false end
  scaleBy(e, name, "scale.x", r)
  return true
end
-- Book screens spread to the window: the left cover and the menu column
-- (everything in the ExpandableMenu component) move left by dx, the right
-- cover and the paper pages move right by dx. Every move is re-applied each
-- frame with the current dx. cover/page/card are dx times a tuning factor
-- (fixMenus sets them each frame; __fitwin_tune.cover/page/card).
local cover, page, card = 0, 0, 0
local staticParts = { Buttons = true, KeyLight = true, MenuHighlight = true,
  Money = true }
-- The covers and the character cards are keyframe-animated on their own
-- node, which the engine rewrites after the scripts run (build 12: the
-- cards were reset every frame, the right cover never moved). Their child
-- nodes are not animated, so the move goes on a child.
local function childMove(parent, key, wantName, delta, deeper)
  if not parent then return end
  local k = kids(parent)
  for i = 1, #k do
    local n = tostring(attr(k[i], "name"))
    if n:find(wantName) then
      local path = key .. "/" .. n
      nudge(k[i], path, "position.x", delta)
      if deeper and (applied[path .. ".position.x"] or 0) > 10 then
        childMove(k[i], path, ".", delta, false)
      end
    end
  end
end
local screen = "other"
local function spreadMenu(menu)
  if not menu then return end
  local k = kids(menu)
  for i = 1, #k do
    local okt, t = pcall(getElementType, k[i])
    if okt and (t == "node" or t == "component") then
      local n = tostring(attr(k[i], "name"))
      if n == "LeftFrame" and screen == "mainmenu" then
        -- Build 20: on the main menu the frame pieces are animated and the
        -- moves on them never showed, so the frame node moves instead.
        nudge(k[i], "LeftFrame", "position.x", -cover)
      elseif n == "LeftFrame" then
        childMove(k[i], "LeftFrame", "^frame_new", -cover)
      else
        -- Build 13 also shifted g_MenuPositionSlots, but those slots are
        -- relative to Buttons, so items moved twice and their labels slid
        -- under the cover.
        nudge(k[i], "ExpandableMenu/" .. n, "position.x", -dx)
      end
    end
  end
end
local dumped = false
local function dumpMenu(root, label)
  if dumped or not logTree or not root then return end
  dumped = true
  local count = 0
  local function walk2(e, depth)
    local k = kids(e)
    for i = 1, #k do
      local okt, t = pcall(getElementType, k[i])
      if okt and t ~= "material" and t ~= "texture" and count < 400 then
        count = count + 1
        local line = string.rep("  ", depth) .. tostring(attr(k[i], "name")) .. " <" .. tostring(t) .. ">"
        for _, a in ipairs({ "position.x", "position.y", "position.z", "scale.x",
          "opacity", "active", "textstring" }) do
          local v = attr(k[i], a)
          if v ~= nil then line = line .. " " .. a .. "=" .. tostring(v) end
        end
        out("dump " .. label .. ": " .. line)
        if depth < 5 then walk2(k[i], depth + 1) end
      end
    end
  end
  walk2(root, 0)
end
local layer = child(scene, "Layer")
if not layer then return end
screen = "other"
if child(layer, "PressStart") then screen = "title"
elseif child(layer, "choosecard") and child(layer, "Foreground") then screen = "mainmenu"
end
out("screen kind: " .. screen)
-- The title's black box stayed 16:9 in build 6 although its White plane
-- was widened, so something clips that layer. Log every layer attribute
-- from this list that exists, to find the one that sizes it.
if logTree then
  local names = { "background", "backgroundcolor", "backgroundcolour",
    "backgroundcolor.r", "bgcolor", "left", "top", "right", "bottom",
    "width", "height", "leftunits", "topunits", "rightunits", "bottomunits",
    "widthunits", "heightunits", "horzfields", "vertfields",
    "horizontalfields", "verticalfields", "size", "layerwidth",
    "layerheight", "clearcolor", "color", "colour", "disabledepthtest",
    "progressiveaa", "multisampleaa", "aamode", "temporalaa",
    "fullscreen", "anchor", "scalemode", "stretch" }
  local found = ""
  for i = 1, #names do
    local v = attr(layer, names[i])
    if v ~= nil then found = found .. " " .. names[i] .. "=" .. tostring(v) end
  end
  out("layer attributes:" .. (found == "" and " none of the probed names" or found))
  local okc, cam = pcall(getElement, "Camera", layer)
  if okc and cam then
    local cn = { "aspect", "aspectratio", "scalemode", "scaleanchor", "clipnear", "clipfar", "fov", "orthographic", "width", "height" }
    found = ""
    for i = 1, #cn do
      local v = attr(cam, cn[i])
      if v ~= nil then found = found .. " " .. cn[i] .. "=" .. tostring(v) end
    end
    out("camera attributes:" .. found)
  end
end
-- Every book screen (main menu, pause, shop, ...) has a LeftFrame and a
-- RightFrame somewhere under Layer.
local leftFrame = find(layer, "LeftFrame", 0)
local rightFrame = find(layer, "RightFrame", 0)
local menu = find(layer, "ExpandableMenu", 0)
local pages = find(layer, "Pages", 0)
local cards = child(layer, "choosecard")
local boy = cards and child(cards, "Boy")
local girl = cards and child(cards, "Girl")
if screen == "other" and (leftFrame or rightFrame) then screen = "book" end
out("screen kind now: " .. screen)
local masks = nil
-- Pressing Start shows a 16:9 black box before the book: BackgroundTint
-- fading in from black. The game draws it at its animated scale while that
-- runs, so the camera zooms in by the window's extra width while the tint is
-- dark and goes back once it lights up. Logs the first frames to tune it.
local fadeCam, fadeZ, fadeLogs, fadeSet = nil, nil, 0, nil
local function pauseFade()
  local tint = child(layer, "BackgroundTint")
  if not tint then return end
  if not fadeCam then
    fadeCam = child(layer, "Camera")
    fadeZ = fadeCam and attr(fadeCam, "position.z")
    if not fadeZ or fadeZ >= 0 then fadeCam = nil return end
  end
  local m = kids(tint)[1]
  local d = m and attr(m, "diffuse.r")
  local o = attr(tint, "opacity")
  local dark = d ~= nil and d < 200
  if fadeLogs < 60 then
    fadeLogs = fadeLogs + 1
    out(string.format("pause fade: tint diffuse.r %s opacity %s -> %s",
      tostring(d), tostring(o), dark and "zoomed" or "normal"))
  end
  -- Zoom only for a window wider than 16:9 (r > 1); the z follows r live.
  local want = (dark and r > 1) and fadeZ / r or fadeZ
  if want ~= fadeSet then
    fadeSet = want
    pcall(setAttribute, fadeCam, "position.z", want)
  end
end
local function fixMenus()
  -- Tuned live against vanilla 16:9 (build 29 screenshots at 4:3, 2.39 and
  -- 32:9): the covers keep their vanilla visible width at the window edges
  -- (dx), the paper stays under the right cover (dx), and the waiting
  -- cards, in a 0.75-scaled parent, stay off-screen (1.8 dx).
  cover = dx * tk("cover", 1)
  page = dx * tk("page", 1)
  card = dx * tk("card", 1.8)
  if screen == "title" then
    -- Title: left as authored. Its 16:9 box (White) is on the title's
    -- timeline: scale, position.z and the title camera are put back when it
    -- draws (console tests, build 26-27), and clone() refuses it ("different
    -- time context"); only its opacity sticks. The main menu's panorama runs
    -- in another Lua state and shows at the box's sides until the box fades
    -- out after the legal text. Open: finding the box's keyframe data.
  elseif screen ~= "other" then
    spreadMenu(menu)
    if not menu and leftFrame then childMove(leftFrame, "LeftFrame", "^frame_new", -cover) end
    if screen == "mainmenu" then
      nudge(rightFrame, "RightFrame", "position.x", cover)
    else
      childMove(rightFrame, "RightFrame", "^Group$", cover)
    end
    -- The paper follows the right cover, which also hides the shop's next
    -- page behind it again. Build 15 moved the pages' children, but the
    -- page backgrounds are animated and stayed put, so move Pages itself.
    -- Build 18: the paper stopped about 35 units short of the right cover
    -- (a dark gap), so it moves a little further than the cover.
    -- Build 16 moved Pages itself and nothing moved, so it is the children
    -- again (build 15, right for pause); a child the game keeps putting back
    -- (Page1_Background) gets its own children moved instead.
    childMove(pages, "Pages", ".", page, true)
    -- On the main menu the character cards wait just off a 16:9 screen, so
    -- they move out too (Boy left, Girl right), but only while they wait
    -- there: on New Game the game slides Boy and Girl in to the middle, and
    -- there the offset goes back to 0 so they land where vanilla puts them
    -- at any aspect (no overlap at 4:3). Waiting = Boy far out to the side
    -- (it rests at -831 after loading and -880 after New Game is backed out
    -- of; the slide-in spots are near +-150). Their parents' scale is
    -- animated, so stretching them distorts the cards (build 31), and the
    -- Group x did not draw right after loading, so the card's two faces
    -- (front, back) are moved.
    if boy and girl then
      local bx = attr(boy, "position.x")
      local waiting = bx and math.abs(bx) > 500
      local bg, gg = child(boy, "Group"), child(girl, "Group")
      -- The back face is turned, so its x runs the other way: Boy's back
      -- moves +card, its front -card (console tests: the back shows after
      -- loading, the front after backing out of New Game).
      childMove(bg, "Boy/Group", "^front$", waiting and -card or 0)
      childMove(bg, "Boy/Group", "^back$", waiting and card or 0)
      childMove(gg, "Girl/Group", "^front$", waiting and card or 0)
      childMove(gg, "Girl/Group", "^back$", waiting and -card or 0)
    end
    -- Build 19: widening BackgroundTint stretched the pause emblem (Options,
    -- which leaves it alone, looked right), so it is not touched any more.
    -- The minimap's quest markers are clones of ReusableTokens, which sits
    -- outside Pages, so it moves with the paper.
    -- Page text is clipped by Mask planes that sit outside Pages (Options:
    -- the settings were cut off), so every Mask outside Pages moves too.
    if not masks then
      masks = {}
      local function walkMasks(e, inPages)
        local k = kids(e)
        for i = 1, #k do
          local n = tostring(attr(k[i], "name"))
          local ip = inPages or n == "Pages" or n == "ExpandableMenu"
          if n:find("Mask") and not ip then masks[#masks + 1] = { k[i], n } end
          walkMasks(k[i], ip)
        end
      end
      walkMasks(layer, false)
    end
    for i = 1, #masks do
      nudge(masks[i][1], "Mask" .. i .. "/" .. masks[i][2], "position.x", page)
    end
    local tokens = find(layer, "ReusableTokens", 0)
    if tokens then nudge(tokens, "ReusableTokens", "position.x", page) end
    pauseFade()
  end
end
fixMenus()
-- Re-apply every frame from the UIContract behaviour's update, since a slide
-- change restores the authored values. GUI is that behaviour's self table.
if type(GUI) == "table" and not GUI.fitwinHooked then
  GUI.fitwinHooked = true
  local prev = GUI.onUpdate
  local frames = 0
  GUI.onUpdate = function(...)
    if prev then pcall(prev, ...) end
    frames = frames + 1
    fixTick = frames
    if frames == 1 then out("per-frame re-apply running (" .. screen .. ")") end
    if frames == 90 then dumpMenu(menu or find(layer, "Foreground", 0), screen) end
    live()
    for i = 1, #hudItems do pcall(applyHud, hudItems[i]) end
    pcall(fixMenus)
  end
  out("hooked GUI.onUpdate (" .. screen .. ")")
else
  out("no GUI table to hook: " .. tostring(GUI))
end
)lua";

inline std::string BuildScript(float aspect, bool move_hud, bool log_tree) {
  char header[160];
  std::snprintf(header, sizeof(header),
                "local A, dx, moveHud, logTree = %.4f, %.4f, %s, %s",
                aspect, 360.0 * aspect - 640.0, move_hud ? "true" : "false",
                log_tree ? "true" : "false");
  return std::string(header) + kScript;
}

}  // namespace detail

// Called from fitwindow::Install once the aspect hook is live.
inline void Install(rex::memory::Memory* memory) {
  if (!memory) return;
  detail::g_memory.store(memory);
  REXSYS_INFO("[fit-window] HUD edge script ready, test build {} "
              "(fit_window_hud_to_edges = {}, layout log = {})",
              detail::kBuild, REXCVAR_GET(fit_window_hud_to_edges),
              detail::LogLayout());
}

// Runs the layout script in a presentation that just loaded. ctx is the
// state right after the game's own ProcessAndProcessAndProcess21_823FE988.
inline void OnPresentationLoaded(PPCContext& ctx, uint8_t* base,
                                 uint32_t presentation) {
  rex::memory::Memory* memory = detail::g_memory.load();
  if (!memory || !presentation) return;
  const bool move_hud = REXCVAR_GET(fit_window_hud_to_edges);
  const bool log_tree = detail::LogLayout();
  // 0 = the fit is off right now (`f2 ws off`): load the script at 16:9 so
  // the screen still follows a later `ws on` or `ws aspect`.
  float aspect = fable2::fitwindow::CurrentAspect();
  if (aspect <= 0.0f) aspect = 16.0f / 9.0f;
  if (!move_hud && !log_tree) return;

  const uint32_t index = detail::g_presentations.fetch_add(1) + 1;
  REXSYS_INFO("[fit-window] UI screen #{} loaded (0x{:08X}): running the HUD "
              "edge script, aspect {:.4f}",
              index, presentation, aspect);
  const std::string script = detail::BuildScript(aspect, move_hud, log_tree);
  if (script.empty() || script.size() >= detail::kScriptBufferSize) {
    REXSYS_WARN("[fit-window] HUD script is {} bytes, buffer is {}: not run",
                script.size(), detail::kScriptBufferSize);
    return;
  }

  // Same calls the game makes for its own strings, on a scratch frame below
  // the current stack pointer. Registers are restored afterwards.
  const PPCContext saved = ctx;
  if (!detail::g_script_buffer) {
    // One buffer for the script text, from the game's own allocator
    // (Allocate_SizeBucketed_Thunk_8221F3F0), kept for the whole run.
    ctx.r3.u64 = detail::kScriptBufferSize;
    REX_CALL_INDIRECT_FUNC(0x8221F3F0);
    detail::g_script_buffer = ctx.r3.u32;
    ctx = saved;
    if (!detail::g_script_buffer) {
      REXSYS_WARN("[fit-window] no guest memory for the HUD script; the HUD "
                  "stays in the middle");
      return;
    }
  }
  std::memcpy(memory->TranslateVirtual<char*>(detail::g_script_buffer),
              script.c_str(), script.size() + 1);
  const uint32_t frame = uint32_t(saved.r1.u64) - 256;
  const uint32_t string_object = frame + 96;
  *memory->TranslateVirtual<uint32_t*>(frame) =
      __builtin_bswap32(uint32_t(saved.r1.u64));  // back chain
  *memory->TranslateVirtual<uint32_t*>(string_object) = 0;
  ctx.r1.u64 = frame;

  ctx.r3.u64 = string_object;
  ctx.r4.u64 = detail::g_script_buffer;
  ctx.r5.s64 = -1;
  REX_CALL_INDIRECT_FUNC(0x8222CF18);  // ConstructRefCounted_8222CF18

  ctx.r3.u64 = string_object;
  ctx.r4.u64 = presentation;
  REX_CALL_INDIRECT_FUNC(0x823F8908);  // ProcessAndVcallAndProcess16_823F8908

  ctx.r3.u64 = string_object;
  REX_CALL_INDIRECT_FUNC(0x82214F08);  // DestructRefCounted_82214F08

  ctx = saved;
}

// A message from Gameface's report callback (r3 = level, r4 = text).
inline void OnGamefaceMessage(uint32_t level, uint32_t text) {
  if (!detail::LogLayout()) return;
  std::string message;
  if (!detail::ReadGuestString(text, message) || message.empty()) return;
  while (!message.empty() &&
         (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  detail::Log("%s%s", level ? "[gameface] " : "", message.c_str());
}

}  // namespace fable2::fitwindow::ui

extern "C" void ProcessAndProcessAndProcess21_823FE988(PPCContext& ctx,
                                                       uint8_t* base) {
  const uint32_t presentation = ctx.r4.u32;
  __imp__ProcessAndProcessAndProcess21_823FE988(ctx, base);
  fable2::fitwindow::ui::OnPresentationLoaded(ctx, base, presentation);
}

extern "C" void NoOp_Stub_829CE870(PPCContext& ctx, uint8_t* base) {
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller - fable2::fitwindow::ui::detail::kGamefaceCodeBegin <
      fable2::fitwindow::ui::detail::kGamefaceCodeEnd -
          fable2::fitwindow::ui::detail::kGamefaceCodeBegin) {
    fable2::fitwindow::ui::OnGamefaceMessage(ctx.r3.u32, ctx.r4.u32);
  }
  __imp__NoOp_Stub_829CE870(ctx, base);
}

// The game's own 2D screens (loading screens, the "Saving content" banner)
// are not Gameface. sub_8235B570 queues one textured quad for them and
// sub_8235B110 (its only caller's helper) writes the quad's 4 vertices: 64
// bytes each, position first, laid out on the 1280x720 render target. That
// target is stretched to the window, so these screens come out stretched,
// while the game's 2D text already corrects for the aspect (sub_8235CF68
// scales x by (1280/720)/A around the centre). Squeezing the quads the same
// way keeps them 16:9 in the middle, with the rest of the window black.
namespace fable2::fitwindow::ui::detail {
inline std::atomic<uint32_t> g_quad_logs{0};
inline thread_local uint32_t g_quad_caller = 0;  // who called sub_8235B570
inline float LoadBEFloat(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
inline void StoreBEFloat(uint8_t* p, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}
inline void SqueezeQuad(uint8_t* base, uint32_t rect, uint32_t verts,
                        uint32_t caller) {
  if (!verts) return;
  const float aspect = fable2::fitwindow::CurrentAspect();
  const float kWide = 16.0f / 9.0f;
  uint8_t* v = base + verts;
  float x[4];
  for (int i = 0; i < 4; ++i) x[i] = LoadBEFloat(v + i * 64);
  const float lo = std::min(std::min(x[0], x[1]), std::min(x[2], x[3]));
  const float hi = std::max(std::max(x[0], x[1]), std::max(x[2], x[3]));
  // Build 15 log: the quads come out in clip space, and a full-width one
  // spans about +-A/(16/9), so 16:9 needs that factor squared.
  const bool squeeze = REXCVAR_GET(fit_window_native_16x9) && aspect > kWide + 0.01f;
  if (squeeze) {
    const float s = (kWide / aspect) * (kWide / aspect);
    for (int i = 0; i < 4; ++i) StoreBEFloat(v + i * 64, x[i] * s);
  }
  if (LogLayout() && g_quad_logs.fetch_add(1) < 200) {
    float r[4] = {0, 0, 0, 0};
    if (rect) for (int i = 0; i < 4; ++i) r[i] = LoadBEFloat(base + rect + i * 4);
    Log("[native-2d] quad from 0x%08X rect {%.0f, %.0f, %.0f, %.0f}: x %.2f..%.2f%s",
        caller, r[0], r[1], r[2], r[3], lo, hi, squeeze ? " (squeezed)" : "");
  }
}
}  // namespace fable2::fitwindow::ui::detail

DECLARE_REX_FUNC(sub_8235B110);
DECLARE_REX_FUNC(sub_8235B570);
DECLARE_REX_FUNC(sub_8235B708);
DECLARE_REX_FUNC(sub_8235B350);
DECLARE_REX_FUNC(sub_8235CF68);

// The game's 2D text (sub_8235CF68, x in f1 on the 1280 canvas) already
// draws its letters at the right shape, but build 19-20 showed the text
// staying where it was while the banner behind it shrank to 16:9. So the x
// it is given moves toward the centre by (16/9)/A, matching the banner.
extern "C" void sub_8235CF68(PPCContext& ctx, uint8_t* base) {
  const float a = fable2::fitwindow::CurrentAspect();
  const float wide = 16.0f / 9.0f;
  if (REXCVAR_GET(fit_window_native_16x9) && a > wide + 0.01f) {
    ctx.f1.f64 = 640.0 + (ctx.f1.f64 - 640.0) * double(wide / a);
  }
  __imp__sub_8235CF68(ctx, base);
}

// Build 16 log: sub_8235B570 only drew four corner pieces on a loading
// screen. Its sibling sub_8235B708 (two rects) builds its quad with
// sub_8235B350, vertices in r6, the same layout.
extern "C" void sub_8235B708(PPCContext& ctx, uint8_t* base) {
  fable2::fitwindow::ui::detail::g_quad_caller = uint32_t(ctx.lr) | 1;
  __imp__sub_8235B708(ctx, base);
}

extern "C" void sub_8235B350(PPCContext& ctx, uint8_t* base) {
  const uint32_t rect = ctx.r4.u32;
  const uint32_t verts = ctx.r6.u32;
  const uint32_t caller = fable2::fitwindow::ui::detail::g_quad_caller;
  __imp__sub_8235B350(ctx, base);
  fable2::fitwindow::ui::detail::SqueezeQuad(base, rect, verts, caller);
}

extern "C" void sub_8235B570(PPCContext& ctx, uint8_t* base) {
  fable2::fitwindow::ui::detail::g_quad_caller = uint32_t(ctx.lr);
  __imp__sub_8235B570(ctx, base);
}

extern "C" void sub_8235B110(PPCContext& ctx, uint8_t* base) {
  const uint32_t rect = ctx.r4.u32;
  const uint32_t verts = ctx.r5.u32;
  const uint32_t caller = fable2::fitwindow::ui::detail::g_quad_caller;
  __imp__sub_8235B110(ctx, base);
  fable2::fitwindow::ui::detail::SqueezeQuad(base, rect, verts, caller);
}
