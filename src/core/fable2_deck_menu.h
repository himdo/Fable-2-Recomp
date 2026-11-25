// fable_2 - ReXGlue Recompiled Project
//
// "Deck Recomp" settings in the pause menu.
//
// The pause menu is an Anark Gameface expandable menu (scripts\gameface\
// expandablemenu\*.lua in guiscripts.bnk, one Lua state per screen). Its
// entries are spawned by the engine (SpawnButton: CreateMenuItem, AddChild,
// table.insert(g_Menu.SpawnedItems, name)); a "Deck Recomp" folder
// (EVENT_TYPE_SUBMENU, opened in place by SelectMenuItem) goes right after
// "Options", with one item per setting labelled "Name: Value". (The Options
// screen itself was tried first, but its list and pages are run by the
// engine: A and left/right never reach its scripts.)
//
// g_MenuFormatter:UpdateTextTag(item, visual) sets a label through the TextID
// attribute of the visual's "Icon.Title"; a TextID the game has no string for
// is shown as written, so labels are literal text. A on an item is
// SelectMenuItem's GUI:PushButtonEvent(item.Name, ...): for ours the script
// steps to the next value, relabels it and reports "[f2set] key=value"
// through Gameface's output(), which arrives in OnGamefaceMessage; OnMessage()
// then changes the value in memory and in fable2_config.toml and applies it
// to the running game where it can.
//
// While the folder or one of its settings is highlighted, the right-hand page
// shows the setting's name and explanation in place of the map (see the
// script's section 4 for how).

#pragma once

#include "fable2_config.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fable2::deckmenu {

struct Choice {
  const char* value;  // as in fable2_config.toml
  const char* label;  // shown in the menu
};

struct Setting {
  const char* key;      // [section] key
  const char* section;
  const char* label;
  bool restart;         // only takes effect at the next start
  std::vector<Choice> choices;
  std::string (*current)();                    // the value now, as in the file
  void (*set)(fable2::config::Values&, const std::string&);
  const char* extra_key = nullptr;             // set to the same value (same section)
  bool quote = false;                          // a TOML string
  void (*live)(const std::string&) = nullptr;  // applies it to the running game
};

inline const char* BoolText(bool b) { return b ? "true" : "false"; }

inline void SetCvar(const char* name, const std::string& value) {
  if (!rex::cvar::SetFlagByName(name, value)) {
    REXSYS_WARN("[deck-menu] cvar {} missing", name);
  }
}

inline std::string FormatFloat(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

inline const std::vector<Setting>& Settings() {
  using fable2::config::Get;
  using fable2::config::Values;
  static const std::vector<Setting> settings = {
      {"high_tick_rate", "patches", "High Tick Rate", true,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().high_tick_rate && Get().higher_hf_tick_rate)); },
       [](Values& v, const std::string& s) { v.high_tick_rate = v.higher_hf_tick_rate = s == "true"; },
       "higher_hf_tick_rate"},
      {"menu_repeat_delay", "patches", "Menu Hold Scroll Delay", false,
       {{"0.25", "0.25 s"}, {"0.18", "0.18 s"}, {"0.12", "0.12 s"}},
       [] { return FormatFloat(Get().menu_repeat_delay); },
       [](Values& v, const std::string& s) { v.menu_repeat_delay = std::stod(s); }},
      {"menu_animation_speed", "patches", "Menu Animation Speed", false,
       {{"1", "Original"}, {"1.5", "1.5x"}, {"2.5", "2.5x"}, {"4", "4x"}},
       [] { return FormatFloat(Get().menu_animation_speed); },
       [](Values& v, const std::string& s) { v.menu_animation_speed = std::stod(s); }},
      {"menu_instant_response", "patches", "Instant Menu Response", false,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().menu_instant_response)); },
       [](Values& v, const std::string& s) { v.menu_instant_response = s == "true"; }},
      {"menu_scroll_acceleration", "patches", "Menu Scroll Acceleration", false,
       {{"1", "Off"}, {"1.5", "1.5x"}, {"2", "2x"}, {"3", "3x"}},
       [] { return FormatFloat(Get().menu_scroll_acceleration); },
       [](Values& v, const std::string& s) { v.menu_scroll_acceleration = std::stod(s); }},
      {"skip_intro_videos", "patches", "Skip Intro Videos", true,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().skip_intro_videos)); },
       [](Values& v, const std::string& s) { v.skip_intro_videos = s == "true"; }},
      {"disable_motion_blur", "patches", "Motion Blur", false,
       {{"false", "On"}, {"true", "Off"}},
       [] { return std::string(BoolText(Get().disable_motion_blur)); },
       [](Values& v, const std::string& s) { v.disable_motion_blur = s == "true"; }},
      {"deck_button_prompts", "input", "Button Icons", true,
       {{"dark", "Steam Deck (Dark)"}, {"light", "Steam Deck (Light)"}, {"off", "Xbox 360"}},
       [] { return Get().deck_button_prompts; },
       [](Values& v, const std::string& s) { v.deck_button_prompts = s; }, nullptr, true},
      {"fps_limit", "perf", "Frame Rate Cap", false,
       {{"30", "30 fps"}, {"40", "40 fps"}, {"45", "45 fps"}, {"60", "60 fps"}, {"0", "Unlimited"}},
       [] { return std::to_string(Get().fps_limit); },
       [](Values& v, const std::string& s) { v.fps_limit = std::stoi(s); }, nullptr, false,
       [](const std::string& s) {
         // As at start-up (fable_2_app.h), but applied now; 30 = the game's
         // own vblank lock.
         if (s == "30") {
           SetCvar("guest_vblank_pacing", "true");
           SetCvar("frame_limit", "60");
         } else {
           SetCvar("frame_limit", s);
           SetCvar("guest_vblank_pacing", "false");
         }
       }},
      {"async_pipelines", "patches", "Async Shader Compilation", false,
       {{"false", "Off"}, {"true", "On"}},
       [] { return std::string(BoolText(Get().async_pipelines)); },
       [](Values& v, const std::string& s) { v.async_pipelines = s == "true"; }, nullptr, false,
       [](const std::string& s) {
         if (s == "true") SetCvar("async_shader_compilation", "true");
         SetCvar("vulkan_async_skip_draws", s);
       }},
  };
  return settings;
}

// The explanation shown in the pause menu's right panel while the setting is
// highlighted (wrapped there to the panel's width).
inline const char* Explanation(const std::string& key) {
  static const std::pair<const char*, const char*> kTexts[] = {
      {"high_tick_rate", "Runs the game's updates twice as often: smoother menus and HUD, less input delay. Experimental: it may cause issues."},
      {"menu_repeat_delay", "How long a held direction waits before the menu starts scrolling. The original game waits 0.25 s."},
      {"menu_animation_speed", "How fast the menus slide and fade. Faster shows the result of a press sooner."},
      {"menu_instant_response", "Menus react in the same update as the press, about 0.1 s sooner than the original game."},
      {"menu_scroll_acceleration", "Holding a direction speeds up scrolling over 1.5 s, up to this many times the normal rate."},
      {"skip_intro_videos", "Skips the Microsoft and Lionhead logo videos when the game starts."},
      {"disable_motion_blur", "The camera's full-screen blur when it moves."},
      {"deck_button_prompts", "Button prompts as Steam Deck buttons in dark or light, or the original Xbox 360 buttons."},
      {"fps_limit", "The highest frame rate. 30 fps is the original game's lock. Unlimited removes the cap."},
      {"async_pipelines", "New shaders are compiled in the background instead of pausing the game. This removes the stutters, but whatever needs a new shader (part of an object, or an effect) is left out for a moment the first time it appears."},
  };
  for (const auto& [k, text] : kTexts) {
    if (key == k) return text;
  }
  return "";
}

// The value each setting had when the game started, for "(after restart)".
inline std::vector<std::string>& StartupValues() {
  static std::vector<std::string> values = [] {
    std::vector<std::string> v;
    for (const Setting& s : Settings()) v.push_back(s.current());
    return v;
  }();
  return values;
}

inline std::string LuaQuote(const char* s) {
  std::string out = "\"";
  for (const char* p = s; *p; ++p) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
  return out + "\"";
}

// Lua table of the settings with their current values: f2deckItems = { { k =
// key, l = label, e = explanation, r = restart, v = { values }, d = { labels }, c = current,
// c0 = at startup }, ... } (indices 1-based).
inline std::string ItemsLua() {
  const std::vector<std::string>& startup = StartupValues();
  std::string lua = "local f2deckItems = {";
  const std::vector<Setting>& settings = Settings();
  for (size_t i = 0; i < settings.size(); ++i) {
    const Setting& s = settings[i];
    const std::string now = s.current();
    size_t current = 1, at_start = 1;
    std::string values, labels;
    for (size_t c = 0; c < s.choices.size(); ++c) {
      values += (c ? "," : "") + LuaQuote(s.choices[c].value);
      labels += (c ? "," : "") + LuaQuote(s.choices[c].label);
      if (now == s.choices[c].value) current = c + 1;
      if (startup[i] == s.choices[c].value) at_start = c + 1;
    }
    char tail[64];
    std::snprintf(tail, sizeof(tail), "}, c = %zu, c0 = %zu},", current, at_start);
    lua += "{k = " + LuaQuote(s.key) + ", l = " + LuaQuote(s.label) +
           ", e = " + LuaQuote(Explanation(s.key)) +
           ", r = " + (s.restart ? "true" : "false") + ", v = {" + values + "}, d = {" + labels +
           tail;
  }
  return lua + "}\n";
}

inline constexpr const char kScript[] = R"lua(
-- Deck Recomp settings in the pause menu (src/core/fable2_deck_menu.h).
pcall(function()
  if rawget(_G, "__f2deck") then return end
  rawset(_G, "__f2deck", true)
  local items, byTag = f2deckItems, {}
  local FOLDER, ICON = "F2DECK", "Art\\GUI\\WheelIcons\\icon_options.tga"
  for i = 1, #items do byTag[FOLDER .. ":" .. items[i].k] = items[i] end
  local function report(s) pcall(output, "[f2deck] " .. s) end
  local function label(it)
    local s = it.l .. ": " .. it.d[it.c]
    if it.r and it.c ~= it.c0 then s = s .. " (after restart)" end
    return s
  end
  local function pauseMenu()
    local menu = rawget(_G, "g_Menu")
    if type(menu) ~= "table" then return end
    local ok, which = pcall(function()
      return getAttribute(getElement("parent", menu.element), "MenuPopulationScript")
    end)
    if ok and which == "PauseMenu" then return menu end
  end
  -- 1. The pause menu's entries are spawned by the engine (SpawnButton:
  --    CreateMenuItem, AddChild, then table.insert(g_Menu.SpawnedItems,
  --    name)). The folder and its items go right after "Options"; the folder
  --    is listed as spawned too, so it goes when the engine clears the menu.
  local function addOurs(menu)
    local ec = menu.EventConst
    menu:AddChild(menu:CreateMenuItem(FOLDER, FOLDER, ICON, ec.EVENT_TYPE_SUBMENU))
    for i = 1, #items do
      local tag = FOLDER .. ":" .. items[i].k
      menu:AddChild(menu:CreateMenuItem(tag, tag, ICON, ec.EVENT_TYPE_OPTIONS_GAME_ITEM), FOLDER)
    end
  end
  local realInsert = table.insert
  table.insert = function(t, ...)
    realInsert(t, ...)
    if select("#", ...) ~= 1 or (...) ~= "Options" then return end
    pcall(function()
      local menu = pauseMenu()
      if not menu or menu.SpawnedItems ~= t or menu:GetMenuItem(FOLDER) then return end
      addOurs(menu)
      realInsert(t, FOLDER)
    end)
  end
  -- Spawned before this script ran: put the entries after Options aside, add
  -- ours, then put them back.
  pcall(function()
    local menu = pauseMenu()
    if not menu or not menu:GetMenuItem("Options") or menu:GetMenuItem(FOLDER) then return end
    local root, after, seen = menu.MenuRoot.ChildItems, {}, false
    local function copy(it)
      local c = {it = it, kids = {}}
      for _, k in ipairs(it.ChildItems or {}) do c.kids[#c.kids + 1] = copy(k) end
      return c
    end
    for _, it in ipairs(root) do
      if seen then after[#after + 1] = copy(it) end
      if it.Name == "Options" then seen = true end
    end
    for _, c in ipairs(after) do menu:RemoveChild(c.it.Name) end
    addOurs(menu)
    local function readd(c, parent)
      local o = c.it
      local n = menu:CreateMenuItem(o.Name, o.TextTag, o.IconTexture, o.EventType, o.IDLow, o.IDHigh)
      n.Ghosted, n.Emphasis = o.Ghosted, o.Emphasis
      menu:AddChild(n, parent)
      for _, k in ipairs(c.kids) do readd(k, o.Name) end
    end
    for _, c in ipairs(after) do readd(c) end
    if type(menu.SpawnedItems) == "table" then realInsert(menu.SpawnedItems, FOLDER) end
  end)
  -- 2. Labels: UpdateTextTag sets the title's TextID to the item's TextTag;
  --    ours get literal text (a TextID the game has no string for is shown
  --    as written), and each title showing one of ours is noted (rows are
  --    reused while scrolling, so any other TextID clears it).
  local realSetAttribute = setAttribute
  local shownBy = {}
  setAttribute = function(e, name, value, ...)
    if name == "TextID" then
      if type(value) == "string" and value:sub(1, #FOLDER) == FOLDER then
        local it = byTag[value]
        local text = value == FOLDER and "Deck Recomp" or (it and label(it))
        if text then
          shownBy[e] = value
          return realSetAttribute(e, "TextID", text)
        end
      else
        shownBy[e] = nil
      end
    end
    return realSetAttribute(e, name, value, ...)
  end
  local function applyLive(k, v, raw)
    -- The menu timing applies to the open menu at once (menuDelay, menuAccel:
    -- the locals of fitwindow's script, the same chunk; original times 0.15 s
    -- and 0.225 s).
    if k == "menu_repeat_delay" and type(GUI) == "table" and type(GUI.Timing) == "table" then
      GUI.Timing.FIRST_NAVIGATE_INTERVAL = v
    elseif k == "menu_scroll_acceleration" then
      menuAccel = v
    elseif k == "menu_instant_response" then
      menuQuick = raw == "true"
    elseif k == "menu_animation_speed" then
      rawset(_G, "ANIMATE_TIME", 0.15 / v)
      local mt = type(g_Menu) == "table" and g_Menu.Timing
      if type(mt) == "table" then
        mt.ANIMATE_TIME = 0.225 / v
        mt.OPACITY_FADE_SPEED = 1 / mt.ANIMATE_TIME
      end
    end
  end
  local function step(name, dir)
    local it = byTag[name]
    it.c = (it.c - 1 + dir) % #it.v + 1
    pcall(output, "[f2set] " .. it.k .. "=" .. it.v[it.c])
    pcall(applyLive, it.k, tonumber(it.v[it.c]), it.v[it.c])
    local text = label(it)
    for e, tag in pairs(shownBy) do
      if tag == name then pcall(realSetAttribute, e, "TextID", text) end
    end
  end
  -- 4. The right-hand panel. Its own texts (place name, objective, legend)
  --    are drawn by the game's map code and ignore script changes, but the
  --    menu's button labels take any text: the menu keeps more button
  --    components than it ever shows (NUM_VIS_SLOTS), so the last ones are
  --    borrowed as text lines while the folder or one of its settings is
  --    highlighted, with their button graphics hidden, and the map hidden.
  --    A label's "truncate" width (normally the button's) is set to the
  --    panel's, and the explanation is wrapped here by estimated width.
  local WIDTH, LINE_X, TOP_Y, LINE_H = 345, 388, -60, 30
  local INK = {70, 45, 28}
  local panel
  local function attr(e, a)
    local ok, v = pcall(getAttribute, e, a)
    if ok then return v end
  end
  local function kids(e)
    local ok, t = pcall(getChildren, e)
    return ok and type(t) == "table" and t or {}
  end
  local function findPanel(menu)
    if panel ~= nil then return panel end
    panel = false
    local okp, parent = pcall(getElement, "parent", menu.element)
    local okb, buttons = pcall(getElement, "Buttons", parent)
    local all = okp and okb and buttons and kids(buttons) or {}
    local first = (tonumber(rawget(_G, "NUM_VIS_SLOTS")) or 12) + 3
    if #all < first + 6 then return panel end
    local root = menu.element
    for _ = 1, 40 do
      local ok, p = pcall(getElement, "parent", root)
      if not ok or not p or p == root then break end
      root = p
    end
    local found = {}
    local function walk(e, depth)
      if depth > 12 then return end
      local name = attr(e, "name")
      if name == "MiniMap" or name == "CurrentQuest" or name == "Scrollbar" then
        found[name] = found[name] or e
      end
      for _, k in ipairs(kids(e)) do walk(k, depth + 1) end
    end
    walk(root, 0)
    local lines = {}
    for i = first, #all do
      local c = all[i]
      local ok1, title = pcall(getElement, "Icon.Title", c)
      local ok2, bg = pcall(getElement, "Icon.Background", c)
      local ok3, tab = pcall(getElement, "Tab", c)
      if ok1 and title then
        lines[#lines + 1] = {c = c, title = title, deco = {ok2 and bg or nil, ok3 and tab or nil}}
      end
    end
    panel = {lines = lines, hide = {found.MiniMap, found.CurrentQuest, found.Scrollbar},
             shown = false}
    return panel
  end
  -- Widths of the menu font at size 18, in canvas units, on the generous
  -- side (a line the game finds wider than "truncate" ends in "...").
  local function textWidth(s)
    local w = 0
    for ch in s:gmatch(".") do
      if ch == " " or ch:find("[ijl%.,:;'!|]") then w = w + 5.75
      elseif ch:find("[ftrI%(%)%-]") then w = w + 7.5
      elseif ch:find("[mwMW]") then w = w + 16
      elseif ch:find("%u") then w = w + 13.8
      else w = w + 10.9 end
    end
    return w
  end
  local function wrap(text)
    local out, cur = {}, ""
    for word in text:gmatch("%S+") do
      if cur == "" then cur = word
      elseif textWidth(cur .. " " .. word) <= WIDTH then cur = cur .. " " .. word
      else out[#out + 1] = cur; cur = word end
    end
    if cur ~= "" then out[#out + 1] = cur end
    return out
  end
  local function setLine(l, y, text, size)
    pcall(realSetAttribute, l.c, "opacity", 100)
    pcall(realSetAttribute, l.c, "position.x", LINE_X)
    pcall(realSetAttribute, l.c, "position.y", y)
    for _, d in pairs(l.deco) do pcall(realSetAttribute, d, "opacity", 0) end
    pcall(realSetAttribute, l.title, "horzalign", 0)
    pcall(realSetAttribute, l.title, "truncate", WIDTH + 35)
    pcall(realSetAttribute, l.title, "size", size)
    pcall(realSetAttribute, l.title, "textcolor.r", INK[1])
    pcall(realSetAttribute, l.title, "textcolor.g", INK[2])
    pcall(realSetAttribute, l.title, "textcolor.b", INK[3])
    pcall(realSetAttribute, l.title, "TextID", text)
  end
  local function showPanel(p, heading, body, note)
    if not p.shown then
      p.shown = true
      p.saved = {}
      for _, l in ipairs(p.lines) do
        local deco = {}
        for k, d in pairs(l.deco) do deco[k] = attr(d, "opacity") end
        p.saved[l] = {op = attr(l.c, "opacity"), x = attr(l.c, "position.x"),
                      y = attr(l.c, "position.y"), size = attr(l.title, "size"),
                      r = attr(l.title, "textcolor.r"), g = attr(l.title, "textcolor.g"),
                      b = attr(l.title, "textcolor.b"), align = attr(l.title, "horzalign"),
                      trunc = attr(l.title, "truncate"),
                      id = attr(l.title, "TextID"), deco = deco}
      end
      p.hideOp = {}
      for k, e in pairs(p.hide) do
        p.hideOp[k] = attr(e, "opacity")
        pcall(realSetAttribute, e, "opacity", 0)
      end
    end
    local text = wrap(body)
    if note then
      text[#text + 1] = ""
      for _, w in ipairs(wrap(note)) do text[#text + 1] = w end
    end
    local y = TOP_Y
    for i, l in ipairs(p.lines) do
      local t = text[i - 1]
      if i == 1 then
        setLine(l, y, heading, 22)
        y = y - LINE_H - 12
      elseif t and t ~= "" then
        setLine(l, y, t, 18)
        y = y - LINE_H
      else
        pcall(realSetAttribute, l.c, "opacity", 0)
        if t then y = y - LINE_H / 2 end
      end
    end
  end
  local function hidePanel(p)
    if not p.shown then return end
    p.shown = false
    for _, l in ipairs(p.lines) do
      local s = p.saved[l]
      pcall(realSetAttribute, l.c, "opacity", s.op or 0)
      if s.x then pcall(realSetAttribute, l.c, "position.x", s.x) end
      if s.y then pcall(realSetAttribute, l.c, "position.y", s.y) end
      if s.size then pcall(realSetAttribute, l.title, "size", s.size) end
      if s.r then pcall(realSetAttribute, l.title, "textcolor.r", s.r) end
      if s.g then pcall(realSetAttribute, l.title, "textcolor.g", s.g) end
      if s.b then pcall(realSetAttribute, l.title, "textcolor.b", s.b) end
      if s.align then pcall(realSetAttribute, l.title, "horzalign", s.align) end
      if s.trunc then pcall(realSetAttribute, l.title, "truncate", s.trunc) end
      if s.id then pcall(realSetAttribute, l.title, "TextID", s.id) end
      for k, d in pairs(l.deco) do pcall(realSetAttribute, d, "opacity", s.deco[k] or 100) end
    end
    for k, e in pairs(p.hide) do pcall(realSetAttribute, e, "opacity", p.hideOp[k] or 100) end
  end
  local function updatePanel()
    local menu = pauseMenu()
    if not menu then return end
    local vi = menu.VisibleInfo
    local it = type(vi) == "table" and type(vi.VisibleElements) == "table" and
               vi.VisibleElements[menu.CurrentHighlightIdx]
    local name = type(it) == "table" and it.Name
    local p = findPanel(menu)
    if not p then return end
    if name == FOLDER then
      showPanel(p, "Deck Recomp", "Settings for the Steam Deck build. Press A on a setting to " ..
                "change it. Changes are saved at once.")
    elseif byTag[name] then
      local s = byTag[name]
      showPanel(p, s.l, s.e, s.r and "Takes effect after a restart." or nil)
    else
      hidePanel(p)
    end
  end
  if type(fireEvent) == "function" then
    local realFire = fireEvent
    fireEvent = function(name, ...)
      if name == "onDirtyHighlight" then pcall(updatePanel) end
      return realFire(name, ...)
    end
  end
  -- 3. A: SelectMenuItem sends GUI:PushButtonEvent(item.Name, ...) to the
  --    engine, then opens or closes items with children. The folder sends
  --    nothing (SelectMenuItem opens it); a setting steps to its next value.
  if type(GUI) == "table" and type(GUI.PushButtonEvent) == "function" then
    local realPush = GUI.PushButtonEvent
    GUI.PushButtonEvent = function(gui, name, ...)
      if name == FOLDER then return end
      if type(name) == "string" and byTag[name] then
        pcall(step, name, 1)
        return
      end
      return realPush(gui, name, ...)
    end
  end
end)
)lua";

inline std::string BuildScript() { return ItemsLua() + kScript; }

// "[f2set] key=value" from the menu script: change the setting in memory and
// in fable2_config.toml. Returns true if the message was one of these.
inline bool OnMessage(const std::string& message) {
  static constexpr char kPrefix[] = "[f2set] ";
  if (message.rfind(kPrefix, 0) != 0) return false;
  std::string body = message.substr(sizeof(kPrefix) - 1);
  while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
  const size_t eq = body.find('=');
  if (eq == std::string::npos) return true;
  const std::string key = body.substr(0, eq), value = body.substr(eq + 1);
  for (const Setting& s : Settings()) {
    if (key != s.key) continue;
    for (const Choice& c : s.choices) {
      if (value != c.value) continue;
      s.set(fable2::config::Mutable(), value);
      const std::string toml = s.quote ? "\"" + value + "\"" : value;
      bool saved = fable2::config::SaveSetting(s.section, s.key, toml);
      if (s.extra_key) saved = fable2::config::SaveSetting(s.section, s.extra_key, toml) && saved;
      if (s.live) s.live(value);
      REXSYS_INFO("[deck-menu] {} = {} ({}){}", s.key, value, saved ? "saved" : "NOT saved",
                  s.restart ? ", takes effect at the next start" : "");
      return true;
    }
  }
  REXSYS_WARN("[deck-menu] unknown setting or value: {}", body);
  return true;
}

}  // namespace fable2::deckmenu
