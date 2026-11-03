-- console_cmds.lua (console-v6): interprets one `f2 <command>` line from the ~ console.
-- Called as console_cmds.lua(line) by the generated console_run.lua.
-- Output: cprint (game log). A line starting with "!" also shows a message box.
--
-- Most commands wrap the engine's Debug.* / Timing.* functions listed in
-- FABLE2_LUA_API.md. Their argument shapes are not all confirmed, so every call
-- is pcall'd and the error text is printed. Use  find  /  dump  /  call  to
-- discover and call anything the game exposes.

local line = ...
line = tostring(line or "")

local C = rawget(_G, "__f2_console")
if not C then
  C = { places = {}, aliases = {} }
  rawset(_G, "__f2_console", C)
end

local box = false
if line:sub(1, 1) == "!" then
  box = true
  line = line:sub(2)
end
line = line:gsub("^%s+", ""):gsub("%s+$", "")

local box_buf = {}
local function out(s)
  s = tostring(s)
  pcall(cprint, "[console] " .. s)
  box_buf[#box_buf + 1] = s
end

local function words(s)
  local t = {}
  for w in s:gmatch("%S+") do t[#t + 1] = w end
  return t
end

-- Convert a typed word to a Lua value: numbers, true/false/nil, else string.
local function val(w)
  if w == nil then return nil end
  local n = tonumber(w)
  if n ~= nil then return n end
  local l = w:lower()
  if l == "true" or l == "on" or l == "yes" then return true end
  if l == "false" or l == "off" or l == "no" then return false end
  if l == "nil" then return nil end
  return w
end

local function vals(W, from)
  local t, n = {}, 0
  for i = from, #W do
    n = n + 1
    t[n] = val(W[i])
  end
  return t, n
end

local function resolve(path)
  local cur = _G
  for part in tostring(path):gmatch("[^%.]+") do
    if type(cur) ~= "table" and type(cur) ~= "userdata" then return nil end
    local ok, nxt = pcall(function() return cur[part] end)
    if not ok then return nil end
    cur = nxt
  end
  return cur
end

local function fmt(v)
  local ts = tostring(v)
  if type(v) == "string" then return '"' .. v .. '"' end
  return ts
end

local function describe(...)
  local n = select("#", ...)
  if n == 0 then return "ok" end
  local parts = {}
  for i = 1, n do parts[i] = fmt((select(i, ...))) end
  return "ok -> " .. table.concat(parts, ", ")
end

local function try(label, f, ...)
  if type(f) ~= "function" then
    out(label .. ": not available in this game build")
    return false
  end
  local r = { pcall(f, ...) }
  if r[1] then
    out(label .. ": " .. describe(unpack(r, 2, #r)))
    return true
  end
  out(label .. " failed: " .. tostring(r[2]))
  return false
end

local function get_hero()
  local hero = nil
  if type(QuestManager) == "table" then hero = QuestManager.HeroEntity end
  if not hero and type(Debug) == "table" and type(Debug.GetHero) == "function" then
    local ok, h = pcall(Debug.GetHero)
    if ok then hero = h end
  end
  if not hero then
    local ok, h = pcall(GetPlayerHero)
    if ok then hero = h end
  end
  if type(hero) ~= "userdata" then return nil end
  return hero
end

local function parse_vec(v)
  local x, y, z = tostring(v):match(
      "CVector3%((%-?%d+%.?%d*),%s*(%-?%d+%.?%d*),%s*(%-?%d+%.?%d*)")
  if not x then return nil end
  return tonumber(x), tonumber(y), tonumber(z)
end

local function entity_pos(ent)
  local ok, pos = pcall(function() return ent:GetPosition() end)
  if not ok or type(pos) ~= "userdata" then return nil end
  return parse_vec(pos)
end

local function hero_pos()
  local hero = get_hero()
  if not hero then return nil end
  return entity_pos(hero)
end

local function need_hero()
  local h = get_hero()
  if not h then out("no hero (load a save first)") end
  return h
end

local function warp_to(x, y, z)
  local hero = need_hero()
  if not hero then return end
  local ok, err = pcall(function() hero:SetPosition(CVector3(x, y, z)) end)
  if ok then out(string.format("warped to %.2f %.2f %.2f", x, y, z))
  else out("warp failed: " .. tostring(err)) end
end

local D = rawget(_G, "Debug")

-- On/off toggle helper: "name [on|off]"; with no argument flips using the getter.
local function toggle(label, setter, getter, arg)
  local want = val(arg)
  if type(want) ~= "boolean" then
    if getter and getter() ~= nil then want = not getter() else want = true end
  end
  try(label .. " " .. (want and "on" or "off"), setter, want)
end

local function dbgf(name)
  if type(D) ~= "table" then return nil end
  local ok, f = pcall(function() return D[name] end)
  if ok then return f end
  return nil
end

---------------------------------------------------------------------------
-- Command table. Each entry: function(W) where W[1] is the command name.
---------------------------------------------------------------------------
local CMD = {}
local HELP = {}
local GROUPS, GROUP_ORDER, cur_group = {}, {}, "general"
local function section(name)
  cur_group = name
  if not GROUPS[name] then
    GROUPS[name] = {}
    GROUP_ORDER[#GROUP_ORDER + 1] = name
  end
end
section("general")
local function add(name, help, fn)
  CMD[name] = fn
  HELP[#HELP + 1] = { name, help }
  local g = GROUPS[cur_group]
  g[#g + 1] = name
end

add("help", "help [command|group]  list commands by group, one group's usage, or one command's usage", function(W)
  local q = W[2] and W[2]:lower()
  if q then
    for _, h in ipairs(HELP) do
      if h[1] == q then out(h[2]) return end
    end
    if GROUPS[q] then
      for _, n in ipairs(GROUPS[q]) do
        for _, h in ipairs(HELP) do
          if h[1] == n then out(h[2]) end
        end
      end
      return
    end
    out("no such command or group")
    return
  end
  for _, g in ipairs(GROUP_ORDER) do
    out(g .. ": " .. table.concat(GROUPS[g], " "))
  end
  out("help <group> lists usage for a group, help <command> for one command.")
  out("Prefix a line with ! to also get a message box. Anything else is run as Lua.")
end)

-- Shape helpers (console-v6) -------------------------------------------------
-- Many engine functions take the entity first (Money.Modify(hero, n)), others
-- do not (Debug.SetHeroLevel(n)). Where the shape is not confirmed, try_shapes
-- tries each argument list in turn and reports which one worked, so the log
-- tells us the real shape.
local function pack(...) return { n = select("#", ...), ... } end

local function try_shapes(label, f, shapes)
  if type(f) ~= "function" then
    out(label .. ": not available in this game build")
    return false
  end
  local last
  for i, a in ipairs(shapes) do
    local r = { pcall(f, unpack(a, 1, a.n)) }
    if r[1] then
      out(label .. ": " .. describe(unpack(r, 2, #r)) .. (#shapes > 1 and ("  [shape " .. i .. "]") or ""))
      return true, r[2]
    end
    last = r[2]
  end
  out(label .. " failed: " .. tostring(last))
  return false
end

-- Same, but silent: returns the first successful call's first result (or nil).
local function get_shapes(f, shapes)
  if type(f) ~= "function" then return nil end
  for _, a in ipairs(shapes) do
    local ok, v = pcall(f, unpack(a, 1, a.n))
    if ok then return v, true end
  end
  return nil, false
end

-- (ent, ...) first, then (...).
local function ent_shapes(ent, ...)
  return { pack(ent, ...), pack(...) }
end

-- Member of a global table, or nil.
local function member(tname, fname)
  local t = rawget(_G, tname)
  if type(t) ~= "table" then return nil end
  local ok, f = pcall(function() return t[fname] end)
  if ok then return f end
  return nil
end

-- Info ----------------------------------------------------------------------
section("info")
add("pos", "pos  print hero position", function()
  local x, y, z = hero_pos()
  if x then out(string.format("pos %.3f %.3f %.3f", x, y, z)) else out("no position") end
end)

add("level", "level  print the loaded level name", function()
  try("level", GetLevelLoaded)
end)

add("info", "info  hero position, level, time of day, game time, target", function()
  local x, y, z = hero_pos()
  out(x and string.format("pos %.3f %.3f %.3f", x, y, z) or "pos: n/a")
  try("level", GetLevelLoaded)
  if type(Timing) == "table" then try("timeofday", Timing.GetTimeOfDay) end
  try("gametime", dbgf("GetTime"))
  try("target", dbgf("GetHeroTarget"))
end)

add("target", "target  print the entity the hero is targeting and its position", function()
  local f = dbgf("GetHeroTarget")
  if not f then return out("Debug.GetHeroTarget not available") end
  local ok, t = pcall(f)
  if not ok then return out("failed: " .. tostring(t)) end
  if t == nil then return out("no target") end
  out("target " .. tostring(t))
  local x, y, z = entity_pos(t)
  if x then out(string.format("target pos %.3f %.3f %.3f", x, y, z)) end
end)

add("ent", "ent <name>  find an entity by name and print its position", function(W)
  if not W[2] then return out("usage: ent <name>") end
  local f = dbgf("GetEntityWithName")
  if not f then return out("Debug.GetEntityWithName not available") end
  local ok, e = pcall(f, W[2])
  if not ok then return out("failed: " .. tostring(e)) end
  if e == nil then return out("not found") end
  out("entity " .. tostring(e))
  local x, y, z = entity_pos(e)
  if x then out(string.format("pos %.3f %.3f %.3f", x, y, z)) end
end)

add("gotoent", "gotoent <name>  teleport the hero to a named entity", function(W)
  if not W[2] then return out("usage: gotoent <name>") end
  local f = dbgf("GetEntityWithName")
  if not f then return out("Debug.GetEntityWithName not available") end
  local ok, e = pcall(f, W[2])
  if not ok or e == nil then return out("entity not found: " .. W[2]) end
  local x, y, z = entity_pos(e)
  if x then warp_to(x, y, z) else out("entity has no position") end
end)

-- Warp ----------------------------------------------------------------------
section("warp")
add("warp", "warp <x> <y> <z> | save <name> | <name> | list | level <levelname> | rel <dx> <dy> <dz>", function(W)
  local a = (W[2] or ""):lower()
  if a == "save" and W[3] then
    local x, y, z = hero_pos()
    if x then
      C.places[W[3]:lower()] = { x, y, z }
      out("saved place " .. W[3])
    else out("no position to save") end
  elseif a == "list" then
    local names = {}
    for k in pairs(C.places) do names[#names + 1] = k end
    table.sort(names)
    out("places: " .. (#names > 0 and table.concat(names, ", ") or "(none)"))
  elseif a == "level" and W[3] then
    try("LoadLevel " .. W[3], dbgf("LoadLevel"), W[3])
  elseif a == "rel" and tonumber(W[3]) and tonumber(W[4]) and tonumber(W[5]) then
    local x, y, z = hero_pos()
    if not x then return out("no position") end
    warp_to(x + tonumber(W[3]), y + tonumber(W[4]), z + tonumber(W[5]))
  elseif tonumber(W[2]) and tonumber(W[3]) and tonumber(W[4]) then
    warp_to(tonumber(W[2]), tonumber(W[3]), tonumber(W[4]))
  elseif C.places[a] then
    local p = C.places[a]
    warp_to(p[1], p[2], p[3])
  else
    out("usage: warp <x> <y> <z> | warp save <name> | warp <name> | warp list | warp level <name> | warp rel <dx> <dy> <dz>")
  end
end)

add("up", "up [metres]  move the hero straight up (default 5), handy when stuck in geometry", function(W)
  local x, y, z = hero_pos()
  if not x then return out("no position") end
  warp_to(x, y, z + (tonumber(W[2]) or 5))
end)

-- Hero toggles --------------------------------------------------------------
section("toggles")
add("god", "god [on|off]  hero god mode (and henchman with 'god on all')", function(W)
  try("hero god", dbgf("SetHeroGodMode"), val(W[2]) ~= false)
  if (W[3] or ""):lower() == "all" then
    try("henchman god", dbgf("SetHenchmanGodMode"), val(W[2]) ~= false)
  end
end)

add("freecam", "freecam [on|off]  free camera", function(W)
  toggle("freecam", dbgf("SetUseFreeCamera"), function()
    local g = dbgf("GetUseFreeCamera")
    if not g then return nil end
    local ok, v = pcall(g)
    return ok and v or nil
  end, W[2])
end)

add("gui", "gui [on|off]  draw the HUD", function(W)
  try("gui", dbgf("SetDrawGUI"), val(W[2]) ~= false)
end)

add("names", "names [on|off] [filter]  draw entity names over entities", function(W)
  local on = val(W[2]) ~= false
  try("entity names", dbgf("SetDrawEntityNames"), on)
  if W[3] then try("names filter", dbgf("SetDrawEntityNamesFilter"), W[3]) end
end)

add("navgraph", "navgraph [on|off]  draw the navigation graph", function(W)
  try("navgraph", dbgf("SetDrawNavGraph"), val(W[2]) ~= false)
end)

add("debugpage", "debugpage <n>  set the on-screen debug page", function(W)
  try("debug page", dbgf("SetDrawDebugPage"), val(W[2]) or 0)
end)

add("combatdebug", "combatdebug [on|off]  draw combat group debug / behaviour names", function(W)
  local on = val(W[2]) ~= false
  try("behaviour names", dbgf("SetDrawBehaviourNames"), on)
  try("combat multiplier", dbgf("SetDrawCombatMultiplier"), on and 1 or 0)
end)

add("magic", "magic [on|off]  global magic enabled", function(W)
  try("magic", dbgf("SetGlobalMagicEnabled"), val(W[2]) ~= false)
end)

add("orbs", "orbs [on|off]  experience orbs", function(W)
  try("orbs", dbgf("SetExperienceOrbsEnabled"), val(W[2]) ~= false)
end)

add("tutorials", "tutorials [on|off]", function(W)
  try("tutorials", dbgf("SetTutorialsEnabled"), val(W[2]) ~= false)
end)

add("lod", "lod low|full|off  force low LOD, or full LOD for all creatures", function(W)
  local m = (W[2] or ""):lower()
  if m == "low" then try("force low lod", dbgf("ForceLowLoDLevel"), true)
  elseif m == "full" then try("full lod creatures", dbgf("SetFullLODForAllCreatures"), true)
  elseif m == "off" then
    try("force low lod", dbgf("ForceLowLoDLevel"), false)
    try("full lod creatures", dbgf("SetFullLODForAllCreatures"), false)
  else out("usage: lod low|full|off") end
end)

add("fast", "fast [on|off]  Debug.MakeGameGoFast", function(W)
  try("fast", dbgf("MakeGameGoFast"), val(W[2]) ~= false)
end)

-- Hero stats / items --------------------------------------------------------
section("stats")
local function stat(name, fname, hint)
  add(name, name .. " <n>  " .. hint, function(W)
    local n = tonumber(W[2])
    if not n then return out("usage: " .. name .. " <number>") end
    try(name, dbgf(fname), n)
  end)
end
stat("herolevel", "SetHeroLevel", "set hero level")
stat("will", "SetHeroWill", "set hero Will")
stat("strength", "SetHeroStrength", "set hero Strength")
stat("morality", "SetHeroMorality", "1000 good, -1000 evil, 0 neutral")
stat("purity", "SetHeroPurity", "1000 pure, -1000 corrupt, 0 neutral")
stat("age", "SetHeroAge", "set hero age")
stat("fat", "SetHeroFat", "set hero fat")
stat("tan", "SetHeroTan", "set hero tan")

add("skill", "skill <name> <n>  Debug.SetHeroSkill(name, n) (arguments unconfirmed)", function(W)
  if not W[2] then return out("usage: skill <name> <n>") end
  try("skill", dbgf("SetHeroSkill"), val(W[2]), val(W[3]) or 1)
end)

add("gold", "gold <n>  add (or subtract) gold", function(W)
  local n = tonumber(W[2])
  local hero = get_hero()
  if not n then return out("usage: gold <amount>") end
  if not hero then return out("no hero (load a save first)") end
  try("gold " .. n, Money and Money.Modify, hero, n)
end)

local ALL = {
  weapons = "AddAllWeaponsWithSlots", melee = "GiveAllMeleeWeapons",
  ranged = "GiveAllRangedWeapons", legendary = "AddAllLegendaryWeapons",
  heavy = "AddAllHeavyWeapons", light = "AddAllLightWeapons",
  crossbows = "AddAllCrossbows", firearms = "AddAllFirearms",
  clothing = "AddAllClothing", accessories = "AddAllClothingAccessories",
  wear = "SetHeroWearingAllClothing", dyes = "AddAllDyes",
  hairstyles = "AddAllHairstyles", makeup = "AddAllMakeUp", tattoos = "AddAllTattoos",
  potions = "AddAllPotions", food = "AddAllFoodAndDrink", furniture = "AddAllFurniture",
  tools = "AddAllTools", gifts = "AddAllGifts", trophies = "AddAllTrophies",
  cards = "AddAllCards", books = "AddAllGeneralBooks", diary = "AddAllLuciensDiaryPages",
  expressions = "AddAllExpressionBooks", dogbooks = "AddAllDogBooks",
  collars = "AddAllDogCollars", augmentations = "AddAllAugmentations",
  prizes = "AddAllCarbonatedPrizes", scriptitems = "AddAllScriptItems",
  items = "AddAllInventoryItems",
}
add("giveall", "giveall <category>  add every item of a kind (giveall list shows categories)", function(W)
  local c = (W[2] or ""):lower()
  if c == "list" or c == "" then
    local names = {}
    for k in pairs(ALL) do names[#names + 1] = k end
    table.sort(names)
    return out("categories: " .. table.concat(names, " "))
  end
  local fname = ALL[c]
  if not fname then return out("unknown category (giveall list)") end
  local f = dbgf(fname)
  if c == "wear" then try(c, f, true) else try(c, f) end
end)

add("weapon", "weapon <name>  give the hero a weapon by name (Debug.GiveHeroWeapon)", function(W)
  if not W[2] then return out("usage: weapon <name>") end
  try("weapon", dbgf("GiveHeroWeapon"), W[2])
end)

-- Time ----------------------------------------------------------------------
section("time")
add("time", "time [hour]  print or set time of day (Timing.SetTimeOfDay, hour 0-24 assumed)", function(W)
  if type(Timing) ~= "table" then return out("Timing table not available") end
  if not W[2] then return try("timeofday", Timing.GetTimeOfDay) end
  local h = tonumber(W[2])
  if not h then return out("usage: time <hour>") end
  try("time " .. h, Timing.SetTimeOfDay, h)
end)

add("ffwd", "ffwd <hour>  fast-forward time of day to an hour", function(W)
  if type(Timing) ~= "table" then return out("Timing table not available") end
  local h = tonumber(W[2])
  if not h then return out("usage: ffwd <hour>") end
  try("ffwd " .. h, Timing.FastForwardTimeOfDayTo, h)
end)

-- Spawning / cleanup --------------------------------------------------------
section("spawn")
add("spawn", "spawn <name> [count]  create an entity next to the hero (Debug.CreateEntityByHero)", function(W)
  if not W[2] then return out("usage: spawn <name> [count]") end
  local n = tonumber(W[3]) or 1
  for _ = 1, n do
    if not try("spawn " .. W[2], dbgf("CreateEntityByHero"), W[2]) then return end
  end
end)

add("spawnat", "spawnat <name> <x> <y> <z>  create an entity at a position", function(W)
  if not (W[2] and tonumber(W[3]) and tonumber(W[4]) and tonumber(W[5])) then
    return out("usage: spawnat <name> <x> <y> <z>")
  end
  try("spawnat", dbgf("CreateEntityAtPosition"), W[2],
      CVector3(tonumber(W[3]), tonumber(W[4]), tonumber(W[5])))
end)

add("creatures", "creatures <family> [count]  spawn a creature family (Debug.SpawnCreatureFamily)", function(W)
  if not W[2] then return out("usage: creatures <family> [count]") end
  try("creatures", dbgf("SpawnCreatureFamily"), val(W[2]), val(W[3]) or 1)
end)

add("combatants", "combatants [n]  create n combatants around the hero", function(W)
  try("combatants", dbgf("CreateCombatantsAroundHero"), tonumber(W[2]) or 3)
end)

add("killall", "killall  destroy all creature entities", function()
  try("killall", dbgf("DestroyAllCreatureEntities"))
end)

add("destroytype", "destroytype <type>  Debug.DestroyAllEntitiesOfType(type)", function(W)
  if not W[2] then return out("usage: destroytype <type>") end
  try("destroytype", dbgf("DestroyAllEntitiesOfType"), val(W[2]))
end)

add("resetcreatures", "resetcreatures  reset spawned creatures", function()
  try("reset", dbgf("ResetSpawnedCreatures"))
end)

-- Reload / session ----------------------------------------------------------
section("session")
add("reloadai", "reloadai  reload AI scripts", function() try("ReloadAI", dbgf("ReloadAI")) end)
add("reloadcam", "reloadcam  reload camera scripts", function() try("ReloadCameras", dbgf("ReloadCameras")) end)
add("quitmenu", "quitmenu confirm  quit to the front end (loses unsaved progress)", function(W)
  if (W[2] or "") ~= "confirm" then return out("this quits to the front end; run: quitmenu confirm") end
  try("quit to front end", dbgf("QuitToFrontEnd"))
end)

-- Discovery -----------------------------------------------------------------
section("discovery")
local function keys_of(t)
  local names = {}
  local ok = pcall(function()
    for k in pairs(t) do names[#names + 1] = tostring(k) end
  end)
  if not ok then return nil end
  table.sort(names)
  return names
end

-- Recursive walker used by dump / dumpall / methods. emit(line) gets one line per member.
local function member_names(t)
  local names = {}
  local ok = pcall(function()
    for k in pairs(t) do names[#names + 1] = k end
  end)
  if not ok then return nil end
  table.sort(names, function(x, y) return tostring(x) < tostring(y) end)
  return names
end

local function meta_index(v)
  local ok, mt = pcall(getmetatable, v)
  if not ok or type(mt) ~= "table" then return nil end
  local idx = mt.__index
  if type(idx) == "table" then return idx end
  return nil
end

local function walk(v, path, depth, seen, emit, filter)
  local ty = type(v)
  if ty == "function" then
    if not filter or path:lower():find(filter, 1, true) then emit(path .. "()") end
    return
  end
  if ty ~= "table" and ty ~= "userdata" then
    if not filter or path:lower():find(filter, 1, true) then
      emit(path .. " = " .. fmt(v) .. "  (" .. ty .. ")")
    end
    return
  end
  if seen[v] then
    emit(path .. " -> (already listed)")
    return
  end
  seen[v] = true
  local names = (ty == "table") and member_names(v) or nil
  local methods = nil
  if ty == "userdata" then
    local idx = meta_index(v)
    if idx then methods = member_names(idx) end
  end
  if not filter or path:lower():find(filter, 1, true) then
    emit(path .. " {" .. ty .. ", " .. tostring(names and #names or (methods and #methods) or "?") .. " members}")
  end
  if depth <= 0 then return end
  if names then
    for _, k in ipairs(names) do
      local ok, child = pcall(function() return v[k] end)
      if ok then walk(child, path .. "." .. tostring(k), depth - 1, seen, emit, filter) end
    end
  elseif methods then
    local idx = meta_index(v)
    for _, k in ipairs(methods) do
      emit(path .. ":" .. tostring(k) .. "()")
    end
  end
end

-- Emit lines in chunks so a very long dump is a few log writes, not thousands.
local function chunked_emit(limit)
  local buf, count = {}, 0
  local function flush()
    if #buf > 0 then pcall(cprint, "[console] " .. table.concat(buf, "\n")) buf = {} end
  end
  local function emit(line)
    if count >= limit then return end
    count = count + 1
    buf[#buf + 1] = line
    if #buf >= 25 then flush() end
    if count == limit then
      buf[#buf + 1] = "... output limit " .. limit .. " reached; narrow with a path or filter"
      flush()
    end
  end
  return emit, flush, function() return count end
end

add("dump", "dump <path> [depth]  list a table/object with values (default depth 1). dump _G, dump Debug, dump GUI 2", function(W)
  local v = resolve(W[2] or "_G")
  if v == nil then return out("not found: " .. tostring(W[2])) end
  local emit, flush, n = chunked_emit(3000)
  walk(v, W[2] or "_G", tonumber(W[3]) or 1, {}, emit, nil)
  flush()
  out(n() .. " lines (see the log: [console] lines)")
end)

add("dumpall", "dumpall [depth] [filter]  dump EVERY global table/function/value (default depth 3; filter = substring of the path)", function(W)
  local depth = tonumber(W[2]) or 3
  local filter = W[3] and W[3]:lower() or nil
  local emit, flush, n = chunked_emit(20000)
  local seen = {}
  local names = member_names(_G) or {}
  for _, k in ipairs(names) do
    local ok, child = pcall(function() return _G[k] end)
    if ok and child ~= _G then walk(child, tostring(k), depth, seen, emit, filter) end
  end
  flush()
  out(n() .. " lines written to the log (search it for [console])")
end)

add("methods", "methods <path>  list the methods of an object or class, e.g. methods hero / methods Debug", function(W)
  local path = W[2] or "hero"
  local v
  if path == "hero" then v = get_hero() else v = resolve(path) end
  if v == nil then return out("not found: " .. path) end
  local idx = meta_index(v)
  local names = idx and member_names(idx) or (type(v) == "table" and member_names(v)) or nil
  if not names then return out("no enumerable methods on " .. path .. " (" .. type(v) .. ")") end
  local emit, flush, n = chunked_emit(3000)
  for _, k in ipairs(names) do emit(path .. ":" .. tostring(k) .. "()") end
  flush()
  out(n() .. " methods")
end)

add("find", "find <text>  search global and Debug/Timing/Money/GUI names for text (case-insensitive)", function(W)
  local needle = (W[2] or ""):lower()
  if needle == "" then return out("usage: find <text>") end
  local hits = {}
  local roots = { { "", _G }, { "Debug.", rawget(_G, "Debug") }, { "Timing.", rawget(_G, "Timing") },
                  { "Money.", rawget(_G, "Money") }, { "GUI.", rawget(_G, "GUI") },
                  { "Player.", rawget(_G, "Player") }, { "QuestManager.", rawget(_G, "QuestManager") } }
  for _, r in ipairs(roots) do
    if r[2] ~= nil then
      local names = keys_of(r[2])
      if names then
        for _, k in ipairs(names) do
          if k:lower():find(needle, 1, true) then hits[#hits + 1] = r[1] .. k end
        end
      end
    end
  end
  out(#hits .. " matches")
  for i = 1, math.min(#hits, 60) do out(hits[i]) end
  if #hits > 60 then out("... narrow the search") end
end)

add("call", "call <path> [args...]  call any function, e.g. call Debug.SetHeroLevel 20", function(W)
  if not W[2] then return out("usage: call <path> [args]") end
  local f = resolve(W[2])
  local a, n = vals(W, 3)
  try(W[2], f, unpack(a, 1, n))
end)

add("get", "get <path>  print a global or member value", function(W)
  if not W[2] then return out("usage: get <path>") end
  out(W[2] .. " = " .. fmt(resolve(W[2])))
end)

add("set", "set <path> <value>  set a global or table member", function(W)
  if not (W[2] and W[3]) then return out("usage: set <path> <value>") end
  local parent, key = W[2]:match("^(.*)%.([^%.]+)$")
  local t = parent and resolve(parent) or _G
  key = key or W[2]
  if t == nil then return out("parent not found") end
  local ok, err = pcall(function() t[key] = val(W[3]) end)
  out(ok and (W[2] .. " = " .. fmt(val(W[3]))) or ("set failed: " .. tostring(err)))
end)

add("lua", "lua <code>  run Lua (also: type Lua directly)", function()
  local code = line:sub(4)
  local f = loadstring and (loadstring("return " .. code) or loadstring(code))
  if not f then return out("cannot compile (or loadstring unavailable): " .. code) end
  local r = { pcall(f) }
  if r[1] then out("= " .. describe(unpack(r, 2, #r)):gsub("^ok %-> ", ""):gsub("^ok$", "(nothing)"))
  else out("error: " .. tostring(r[2])) end
end)

---------------------------------------------------------------------------
-- console-v6 commands. Shapes marked "unconfirmed" use try_shapes, which
-- prints "[shape N]" for the argument list that worked; please send those.
---------------------------------------------------------------------------

-- Entity lookup: hero/me, target, dog, henchman, or an entity name.
local function find_ent(word)
  local w = (word or "hero"):lower()
  if w == "hero" or w == "me" then return get_hero(), "hero" end
  if w == "target" then
    local f = dbgf("GetHeroTarget")
    if f then
      local ok, t = pcall(f)
      if ok and type(t) == "userdata" then return t, "target" end
    end
    return nil, "target"
  end
  if w == "dog" then
    local d = get_shapes(rawget(_G, "GetDog"), { pack(), pack(get_hero()) })
    if type(d) == "userdata" then return d, "dog" end
    return nil, "dog"
  end
  if w == "henchman" then
    local h = get_shapes(rawget(_G, "GetPlayerHenchman"), { pack(), pack(get_hero()) })
    if type(h) == "userdata" then return h, "henchman" end
    return nil, "henchman"
  end
  local f = dbgf("GetEntityWithName")
  if f then
    local ok, e = pcall(f, word)
    if ok and type(e) == "userdata" then return e, word end
  end
  return nil, word
end

local function need_ent(word)
  local e, label = find_ent(word)
  if not e then out("no entity: " .. tostring(label)) end
  return e, label
end

local function method(ent, name, ...)
  local args = pack(...)
  local ok, v = pcall(function() return ent[name](ent, unpack(args, 1, args.n)) end)
  if ok then return v end
  return nil
end

local function onoff(arg, default)
  local v = val(arg)
  if type(v) == "boolean" then return v end
  return default
end

-- get/set pair: "name" prints the getter, "name <n>" calls the setter.
local function getset(label, getter, setter, arg, shapes_for)
  if arg == nil then
    return try_shapes(label, getter, shapes_for and shapes_for() or { pack() })
  end
  local v = val(arg)
  return try_shapes(label .. " " .. tostring(arg), setter,
                    shapes_for and shapes_for(v) or { pack(v) })
end

-- Health --------------------------------------------------------------------
section("health")

local function hp_of(ent)
  local cur = get_shapes(member("Health", "Get"), { pack(ent) })
  local mx = get_shapes(member("Health", "GetMax"), { pack(ent) })
  return cur, mx
end

add("hp", "hp [n|full] [who]  print or set health (who: hero, target, dog, henchman, name)", function(W)
  local arg, who = W[2], W[3]
  if arg and not tonumber(arg) and arg:lower() ~= "full" then who, arg = arg, nil end
  local e, label = need_ent(who)
  if not e then return end
  local cur, mx = hp_of(e)
  if arg == nil then return out(label .. " health " .. tostring(cur) .. " / " .. tostring(mx)) end
  local want = (arg:lower() == "full") and mx or tonumber(arg)
  if not (want and cur) then return out("health not readable for " .. label) end
  try_shapes(label .. " health -> " .. want, member("Health", "Modify"), { pack(e, want - cur) })
end)

add("heal", "heal [who]  restore full health", function(W)
  local e, label = need_ent(W[2])
  if not e then return end
  local cur, mx = hp_of(e)
  if not (cur and mx) then return out("health not readable for " .. label) end
  try_shapes("heal " .. label, member("Health", "Modify"), { pack(e, mx - cur) })
end)

add("maxhp", "maxhp <n> [who]  set maximum health (Health.SetMax, args unconfirmed)", function(W)
  local n = tonumber(W[2])
  if not n then return out("usage: maxhp <n> [who]") end
  local e, label = need_ent(W[3])
  if e then try_shapes("maxhp " .. label, member("Health", "SetMax"), { pack(e, n) }) end
end)

add("invuln", "invuln [on|off] [who]  Health.SetAsInvulnerable (works on any entity)", function(W)
  local e, label = need_ent(W[3])
  if e then try_shapes("invulnerable " .. label, member("Health", "SetAsInvulnerable"), { pack(e, onoff(W[2], true)) }) end
end)

add("cantdie", "cantdie [on|off] [who]  Health.SetCantDie: takes damage but never dies", function(W)
  local e, label = need_ent(W[3])
  if e then try_shapes("cant die " .. label, member("Health", "SetCantDie"), { pack(e, onoff(W[2], true)) }) end
end)

add("regen", "regen <rate> [who]  Health.SetRegeneration (args unconfirmed)", function(W)
  local n = tonumber(W[2])
  if not n then return out("usage: regen <rate> [who]") end
  local e, label = need_ent(W[3])
  if e then try_shapes("regen " .. label, member("Health", "SetRegeneration"), { pack(e, n) }) end
end)

add("kill", "kill [who]  reduce health to 0 (default: target)", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  local cur = hp_of(e)
  try_shapes("kill " .. label, member("Health", "Modify"), { pack(e, -((cur or 100000) + 1)) })
end)

add("deplete", "deplete  Debug.DepleteHealth (script debug helper)", function()
  try("deplete", dbgf("DepleteHealth"))
end)

-- Hero extras ---------------------------------------------------------------
section("hero")

add("mana", "mana [on|off]  infinite Will energy (Debug.SetHeroManaInfinite, as in the GDC/E3 startup scripts)", function(W)
  try("infinite mana", dbgf("SetHeroManaInfinite"), onoff(W[2], true))
end)

add("sprint", "sprint [on|off]  allow sprinting (Debug.SetHeroSprintingEnabled)", function(W)
  try("sprint", dbgf("SetHeroSprintingEnabled"), onoff(W[2], true))
end)

add("speed", "speed <multiplier>|off  override hero movement speed (Player.*, args unconfirmed)", function(W)
  local hero = need_hero()
  if not hero then return end
  local a = (W[2] or ""):lower()
  if a == "off" then
    return try_shapes("speed override off", member("Player", "SetPlayerMovementSpeedOverridden"), ent_shapes(hero, false))
  end
  local m = tonumber(a)
  if not m then return out("usage: speed <multiplier>|off") end
  try_shapes("speed override on", member("Player", "SetPlayerMovementSpeedOverridden"), ent_shapes(hero, true))
  try_shapes("speed x" .. m, member("Player", "SetOverriddenMovementSpeedMultiplier"), ent_shapes(hero, m))
end)

add("footlock", "footlock [on|off]  hero foot IK locking (Debug.SetHeroFootLockingEnabled)", function(W)
  try("foot locking", dbgf("SetHeroFootLockingEnabled"), onoff(W[2], true))
end)

add("safety", "safety [on|off]  Player.SetSafetyMode (args unconfirmed)", function(W)
  local hero = need_hero()
  if hero then try_shapes("safety mode", member("Player", "SetSafetyMode"), ent_shapes(hero, onoff(W[2], true))) end
end)

add("orbpickup", "orbpickup [on|off]  Player.SetAbleToCollectExperienceOrbs (args unconfirmed)", function(W)
  local hero = need_hero()
  if hero then
    try_shapes("collect orbs", member("Player", "SetAbleToCollectExperienceOrbs"), ent_shapes(hero, onoff(W[2], true)))
  end
end)

add("orbspawn", "orbspawn  Debug.SpawnExperienceOrbs (script debug helper)", function()
  try("spawn orbs", dbgf("SpawnExperienceOrbs"))
end)

add("scar", "scar [face|random]  add a scar to the hero", function(W)
  if (W[2] or "random"):lower() == "face" then try("face scar", dbgf("AddHeroFaceScar"))
  else try("random scar", dbgf("AddHeroRandomScar")) end
end)

local OUTFITS = {
  barbarian = "SetHeroWearingBarbarianOutfit", brawler = "SetHeroWearingBrawlerOutfit",
  crossdresser = "SetHeroWearingCrossDresserOutfit", goth = "SetHeroWearingGothChickOutfit",
  martialarts = "SetHeroWearingMartialArtsOutfit", pirate = "SetHeroWearingPirateOutfit",
  pureevil = "SetHeroWearingPureEvilOutfit", swashbuckler = "SetHeroWearingSwashBucklerOutfit",
  year = "SetHeroWearingYearOutfit",
}
add("outfit", "outfit <name>|list  dress the hero in a debug outfit", function(W)
  local o = (W[2] or "list"):lower()
  if o == "list" then
    local names = {}
    for k in pairs(OUTFITS) do names[#names + 1] = k end
    table.sort(names)
    return out("outfits: " .. table.concat(names, " "))
  end
  if not OUTFITS[o] then return out("unknown outfit (outfit list)") end
  try("outfit " .. o, dbgf(OUTFITS[o]))
end)

add("dye", "dye <slot> <dye>  Debug.SetHeroWornItemDye (args unconfirmed)", function(W)
  if not (W[2] and W[3]) then return out("usage: dye <slot> <dye>") end
  try("dye", dbgf("SetHeroWornItemDye"), val(W[2]), val(W[3]))
end)

add("expressions", "expressions all|extend|succeed [on|off]  unlock expressions / force extended ones to succeed", function(W)
  local m = (W[2] or ""):lower()
  if m == "all" then try("all expressions", dbgf("SetAllExpressionsAvailable"), onoff(W[3], true))
  elseif m == "extend" then try("all extendable", dbgf("SetAllExpressionsExtendable"), onoff(W[3], true))
  elseif m == "succeed" then try("extended succeed", dbgf("SetForceExtendedExpressionsToSucceed"), onoff(W[3], true))
  else out("usage: expressions all|extend|succeed [on|off]") end
end)

add("entitytype", "entitytype <type>  Debug.ChangePlayerEntityType (play as another creature; args unconfirmed)", function(W)
  if not W[2] then return out("usage: entitytype <type>") end
  try("change player entity type", dbgf("ChangePlayerEntityType"), val(W[2]))
end)

-- Money and stats -----------------------------------------------------------
section("money")

add("money", "money [set <n>]  print gold, or set it exactly", function(W)
  local hero = need_hero()
  if not hero then return end
  if (W[2] or ""):lower() == "set" and tonumber(W[3]) then
    return try_shapes("money set " .. W[3], member("Money", "Set"), { pack(hero, tonumber(W[3])) })
  end
  try_shapes("money", member("Money", "Get"), { pack(hero) })
end)

local STAT_GETTERS = {
  { "morality", "GetMorality" }, { "purity", "GetPurity" }, { "renown", "GetRenown" },
  { "strength", "GetStrength" }, { "will", "GetWill" }, { "skill", "GetSkill" },
  { "fat", "GetFat" }, { "tan", "GetTan" }, { "height", "GetHeight" }, { "title", "GetTitle" },
  { "chapter", "GetChapterProgress" }, { "seconds played", "GetSecondsPlayed" },
  { "metres travelled", "GetMetresTravelled" }, { "combat multiplier", "GetCombatMultiplier" },
  { "affordable abilities", "GetNumAffordableHeroAbilities" },
}
add("stats", "stats  print every hero stat the Stats table exposes", function()
  local hero = need_hero()
  if not hero then return end
  for _, s in ipairs(STAT_GETTERS) do
    local v, ok = get_shapes(member("Stats", s[2]), ent_shapes(hero))
    out(s[1] .. ": " .. (ok and fmt(v) or "n/a"))
  end
  local m = get_shapes(member("Money", "Get"), { pack(hero) })
  out("gold: " .. tostring(m))
  local d = get_shapes(member("Player", "GetDifficultyLevel"), { pack(), pack(hero) })
  out("difficulty: " .. tostring(d))
end)

local function stat_mod(name, fname, hint)
  add(name, name .. " <n>  " .. hint .. " (Stats." .. fname .. ", args unconfirmed)", function(W)
    local n = tonumber(W[2])
    if not n then return out("usage: " .. name .. " <n>") end
    local hero = need_hero()
    if hero then try_shapes(name .. " " .. n, member("Stats", fname), ent_shapes(hero, n)) end
  end)
end
stat_mod("renown", "ModifyRenown", "add renown (negative subtracts)")
stat_mod("addmorality", "ModifyMorality", "add to morality")
stat_mod("addpurity", "ModifyPurity", "add to purity")
stat_mod("addstrength", "ModifyStrength", "add to strength")
stat_mod("addwill", "ModifyWill", "add to will")
stat_mod("addfat", "ModifyFat", "add to fat")
stat_mod("addtan", "ModifyTan", "add to tan")
stat_mod("height", "ModifyHeight", "add to height")

add("ability", "ability <name> <level>  Stats.SetHeroAbilityLevel (args unconfirmed)", function(W)
  if not (W[2] and tonumber(W[3])) then return out("usage: ability <name> <level>") end
  local hero = need_hero()
  if hero then
    try_shapes("ability " .. W[2], member("Stats", "SetHeroAbilityLevel"), ent_shapes(hero, val(W[2]), tonumber(W[3])))
  end
end)

add("title", "title <name>  unlock a title (Stats.AddUnlockedTitle, args unconfirmed); title alone prints it", function(W)
  local hero = need_hero()
  if not hero then return end
  if not W[2] then return try_shapes("title", member("Stats", "GetTitle"), ent_shapes(hero)) end
  try_shapes("unlock title " .. W[2], member("Stats", "AddUnlockedTitle"), ent_shapes(hero, val(W[2])))
end)

add("chapter", "chapter [n]  print or set chapter progress (args unconfirmed)", function(W)
  local hero = need_hero()
  if hero then
    getset("chapter", member("Stats", "GetChapterProgress"), member("Stats", "SetChapterProgress"), W[2],
           function(v) if v == nil then return ent_shapes(hero) end return ent_shapes(hero, v) end)
  end
end)

add("trackedstat", "trackedstat <name> [n]  print or set a tracked stat (args unconfirmed)", function(W)
  if not W[2] then return out("usage: trackedstat <name> [n]") end
  local hero = need_hero()
  if not hero then return end
  if W[3] then
    return try_shapes("set stat " .. W[2], member("Stats", "SetTrackedStat"), ent_shapes(hero, val(W[2]), tonumber(W[3])))
  end
  try_shapes("stat " .. W[2], member("Stats", "GetTrackedStat"), ent_shapes(hero, val(W[2])))
end)

-- Inventory -----------------------------------------------------------------
section("items")

add("give", "give <itemtype> [count]  Inventory.AddItemOfType (item type = object record name)", function(W)
  if not W[2] then return out("usage: give <itemtype> [count]") end
  local hero = need_hero()
  if not hero then return end
  local n = math.max(1, math.min(tonumber(W[3]) or 1, 999))
  local f = member("Inventory", "AddItemOfType")
  if n == 1 then return try_shapes("give " .. W[2], f, { pack(hero, W[2]) }) end
  if type(f) ~= "function" then return out("Inventory.AddItemOfType not available") end
  for i = 1, n do
    local ok, err = pcall(f, hero, W[2])
    if not ok then return out("give " .. W[2] .. " failed at " .. i .. ": " .. tostring(err)) end
  end
  out("give " .. W[2] .. " x" .. n .. ": ok")
end)

add("have", "have <itemtype>  count items of a type in the hero's inventory", function(W)
  if not W[2] then return out("usage: have <itemtype>") end
  local hero = need_hero()
  if hero then try_shapes("have " .. W[2], member("Inventory", "GetNumberOfItemsOfType"), { pack(hero, W[2]) }) end
end)

add("take", "take <itemtype>  remove every item of a type from the hero", function(W)
  if not W[2] then return out("usage: take <itemtype>") end
  local hero = need_hero()
  if hero then try_shapes("take " .. W[2], member("Inventory", "RemoveAllItemsOfType"), { pack(hero, W[2]) }) end
end)

add("invcount", "invcount  number of items the hero carries", function()
  local hero = need_hero()
  if hero then try_shapes("items", member("Inventory", "GetNumberOfItems"), { pack(hero) }) end
end)

local TIERS = {
  rusty = "GiveRustyWeapons", iron = "GiveIronWeapons", steel = "GiveSteelWeapons",
  masterwork = "GiveMasterworkWeapons", legendary = "GiveLegendaryWeapons",
  gun = "GiveHeroGun", pistol = "GiveHeroPistol", all = "GiveAllWeapons",
}
add("weapons", "weapons rusty|iron|steel|masterwork|legendary|gun|pistol|all  debug weapon sets", function(W)
  local t = (W[2] or ""):lower()
  if not TIERS[t] then return out("usage: weapons rusty|iron|steel|masterwork|legendary|gun|pistol|all") end
  try("weapons " .. t, dbgf(TIERS[t]))
end)

add("henchweapon", "henchweapon <name>  give the henchman a weapon (Debug.GiveHenchmanWeapon)", function(W)
  if not W[2] then return out("usage: henchweapon <name>") end
  try("henchman weapon", dbgf("GiveHenchmanWeapon"), W[2])
end)

-- Extra giveall categories found in the Debug table.
ALL.dlc1 = "AddAllDLC1Items"
ALL.dlc1weapons = "AddAllDLC1Weapons"
ALL.dlc1clothing = "AddAllDLC1Clothing"
ALL.dlc1potions = "AddAllDLC1Potions"
ALL.dlc1misc = "AddAllDLC1Misc"
ALL.dlc1augs = "AddAllDLC1Augmentations"
ALL.dlc1augweapons = "AddAllDLC1AugmentableWeapons"
ALL.dlc2 = "AddAllDLC2Items"
ALL.augweapons = "AddAllAugmentableWeapons"
ALL.suits = "AddAllClothingSuits"
ALL.dogitems = "AddAllDogItems"
ALL.randomaug = "AddRandomAugmentation"
ALL.laser = "AddLaserSight"

-- Time and weather -----------------------------------------------------------
section("world")

add("dayspeed", "dayspeed <n>  Timing.SetDaySpeed (as in the GDC/E3 startup scripts)", function(W)
  local n = tonumber(W[2])
  if not n then return try("seconds per second", member("Timing", "GetWorldSecondsPerSecond")) end
  try("day speed " .. n, member("Timing", "SetDaySpeed"), n)
end)

add("stoptime", "stoptime [on|off]  Timing.SetTimeAsStopped", function(W)
  try("time stopped", member("Timing", "SetTimeAsStopped"), onoff(W[2], true))
end)

add("midnight", "midnight  stop time at midnight (Debug.SetTimeStoppedAndMidnight)", function()
  try("midnight", dbgf("SetTimeStoppedAndMidnight"))
end)

add("day", "day [n]  print or set the day count", function(W)
  getset("day", member("Timing", "GetDayCount"), member("Timing", "SetDayCount"), W[2])
end)

add("nextday", "nextday  Timing.AdvanceDayCount", function()
  try("advance day", member("Timing", "AdvanceDayCount"))
end)

add("moon", "moon <phase>  Timing.SetMoonPhase (args unconfirmed)", function(W)
  if not tonumber(W[2]) then return out("usage: moon <phase>") end
  try("moon phase", member("Timing", "SetMoonPhase"), tonumber(W[2]))
end)

add("clock", "clock  frames, tick rate, day, time of day and real-world time", function()
  try("world frame", member("Timing", "GetWorldFrame"))
  try("program frame", member("Timing", "GetProgramFrame"))
  try("tick rate", member("Timing", "GetTickRate"))
  try("time step", member("Timing", "GetTimeStep"))
  try("day", member("Timing", "GetDayCount"))
  try("time of day", member("Timing", "GetTimeOfDay"))
  try("world secs/sec", member("Timing", "GetWorldSecondsPerSecond"))
  try("real hour", member("Timing", "GetRealWorldHour"))
  try("real minute", member("Timing", "GetRealWorldMinute"))
end)

add("season", "season winter|summer  swap the world season (Debug.SwapToWinter / SwapToSummer)", function(W)
  local s = (W[2] or ""):lower()
  if s == "winter" then try("winter", dbgf("SwapToWinter"))
  elseif s == "summer" then try("summer", dbgf("SwapToSummer"))
  else out("usage: season winter|summer") end
end)

add("ground", "ground [x y z]  landscape height and water depth at a point (default: hero; args unconfirmed)", function(W)
  local x, y, z = tonumber(W[2]), tonumber(W[3]), tonumber(W[4])
  if not x then x, y, z = hero_pos() end
  if not x then return out("no position") end
  local v = CVector3(x, y, z or 0)
  try_shapes("landscape height", rawget(_G, "GetLandscapeHeightAt"), { pack(v), pack(x, y) })
  try_shapes("water depth", rawget(_G, "GetWaterDepthAt"), { pack(v), pack(x, y) })
end)

add("snap", "snap  drop the hero onto the landscape surface below", function()
  local x, y, z = hero_pos()
  if not x then return out("no position") end
  local h = get_shapes(rawget(_G, "GetLandscapeHeightAt"), { pack(CVector3(x, y, z)), pack(x, y) })
  if type(h) ~= "number" then return out("landscape height not available here") end
  warp_to(x, y, h + 0.1)
end)

add("autosave", "autosave  AutoSaveGame()", function()
  try("autosave", rawget(_G, "AutoSaveGame"))
end)

add("sfx", "sfx <event>  play a sound event (Sound.PlayEvent, args unconfirmed)", function(W)
  if not W[2] then return out("usage: sfx <event>") end
  local hero = get_hero()
  try_shapes("sound " .. W[2], member("Sound", "PlayEvent"), { pack(W[2]), pack(hero, W[2]) })
end)

add("havok", "havok friction|restitution|angulardamping|maxlinear|maxangular|minmass <value>  physics tuning (E3 startup script)", function(W)
  local P = {
    friction = "SetHavokFriction", restitution = "SetHavokRestitution",
    angulardamping = "SetHavokAngularDamping", maxlinear = "SetHavokMaxLinearVelocity",
    maxangular = "SetHavokMaxAngularVelocity", minmass = "SetHavokMinMass",
  }
  local k = (W[2] or ""):lower()
  if not (P[k] and tonumber(W[3])) then
    return out("usage: havok friction|restitution|angulardamping|maxlinear|maxangular|minmass <value>")
  end
  try("havok " .. k, dbgf(P[k]), tonumber(W[3]))
end)

-- Camera --------------------------------------------------------------------
section("camera")

local function cam(name) return member("Camera", name) end

add("fov", "fov [degrees]  print or set the camera field of view (args unconfirmed)", function(W)
  getset("fov", cam("GetFOV"), cam("SetFOV"), W[2])
end)
add("zoom", "zoom [level]  print or set the camera zoom level (args unconfirmed)", function(W)
  getset("zoom", cam("GetZoomLevel"), cam("SetZoomLevel"), W[2])
end)
add("roll", "roll [angle]  print or set camera roll (args unconfirmed)", function(W)
  getset("roll", cam("GetRoll"), cam("SetRoll"), W[2])
end)
add("blur", "blur [amount]  print or set camera blur (args unconfirmed)", function(W)
  getset("blur", cam("GetBlur"), cam("SetBlur"), W[2])
end)
add("dof", "dof [value]  print or set depth of field (args unconfirmed)", function(W)
  getset("dof", cam("GetDOF"), cam("SetDOF"), W[2])
end)

add("shake", "shake [amount]  add camera shake (args unconfirmed)", function(W)
  local n = tonumber(W[2]) or 1
  local hero = get_hero()
  if not try_shapes("camera shake", cam("AddCameraShake"), { pack(n), pack(n, 1) }) then
    try_shapes("screen shake", member("Player", "AddScreenShake"), { pack(hero, n), pack(n) })
  end
end)

add("campos", "campos  camera position, angles and forward vector", function()
  local hero = get_hero()
  try_shapes("camera position", member("Player", "GetPositionOfCameraAssociatedWithPlayer"), { pack(), pack(hero) })
  try_shapes("camera forward", member("Player", "GetForwardOfCameraAssociatedWithPlayer"), { pack(), pack(hero) })
  try("camera angles", cam("GetAngles"))
end)

add("camto", "camto <x> <y> <z>  move the camera to a position (Debug.MoveCameraToPosition)", function(W)
  if not (tonumber(W[2]) and tonumber(W[3]) and tonumber(W[4])) then return out("usage: camto <x> <y> <z>") end
  try("camera to", dbgf("MoveCameraToPosition"), CVector3(tonumber(W[2]), tonumber(W[3]), tonumber(W[4])))
end)

add("camlook", "camlook <x> <y> <z>  point the camera focus at a position (Debug.MoveCameraFocusToPosition)", function(W)
  if not (tonumber(W[2]) and tonumber(W[3]) and tonumber(W[4])) then return out("usage: camlook <x> <y> <z>") end
  try("camera focus", dbgf("MoveCameraFocusToPosition"), CVector3(tonumber(W[2]), tonumber(W[3]), tonumber(W[4])))
end)

add("canopy", "canopy [on|off]  tree canopy fading (Camera.EnableTreeCanopyFading)", function(W)
  try("canopy fading", cam("EnableTreeCanopyFading"), onoff(W[2], true))
end)

add("trackcam", "trackcam  toggle the target-tracking camera (Debug.ToggleTargetTrackingCamera)", function()
  try("tracking camera", dbgf("ToggleTargetTrackingCamera"))
end)

-- UI ------------------------------------------------------------------------
section("ui")

local function gui(name) return member("GUI", name) end

local UI = {}
UI.hud = { "ui hud on|off  draw the whole HUD", function(W) try("hud", dbgf("SetDrawGUI"), onoff(W[3], true)) end }
UI.msg = { "ui msg <text>  show a message box", function(W)
  try("message box", gui("DisplayMessageBox"), line:match("^%S+%s+%S+%s+(.*)$") or "")
end }
UI.info = { "ui info <text>  show an info box (args unconfirmed)", function(W)
  try("info box", gui("DisplayInfoBox"), line:match("^%S+%s+%S+%s+(.*)$") or "")
end }
UI.close = { "ui close  remove the current display box", function() try("remove box", gui("RemoveDisplayBox")) end }
UI.fadeout = { "ui fadeout [seconds]  fade the screen to black", function(W)
  try_shapes("fade out", gui("FadeScreenOut"), { pack(tonumber(W[3]) or 1), pack() })
end }
UI.fadein = { "ui fadein [seconds]  fade the screen back in", function(W)
  try_shapes("fade in", gui("FadeScreenIn"), { pack(tonumber(W[3]) or 1), pack() })
end }
UI.toggle = { "ui toggle <element> [on|off]  GUI.ToggleElement (element names unconfirmed)", function(W)
  if not W[3] then return out("usage: ui toggle <element> [on|off]") end
  try_shapes("toggle " .. W[3], gui("ToggleElement"), { pack(val(W[3]), onoff(W[4], true)), pack(val(W[3])) })
end }
UI.icons = { "ui icons on|off  player icons and world icons", function(W)
  local on = onoff(W[3], true)
  try("player icons", gui("SetShouldDrawPlayerIcons"), on)
  try("world icons", dbgf("SetShouldDrawWorldIcons"), on)
end }
UI.vignette = { "ui vignette on|off  cutscene vignette", function(W)
  try("vignette", gui("SetCutsceneVignette"), onoff(W[3], true))
end }
UI.dpad = { "ui dpad on|off  d-pad HUD", function(W) try("dpad", gui("SetDPadEnabled"), onoff(W[3], true)) end }
UI.highlight = { "ui highlight on|off  targeting highlight", function(W)
  try("targeting highlight", dbgf("SetDrawTargetingHighlight"), onoff(W[3], true))
end }
UI.version = { "ui version on|off  milestone version text", function(W)
  try("version text", dbgf("SetDrawMilestoneVersionText"), onoff(W[3], true))
end }
UI.pausedtext = { "ui pausedtext on|off  'paused' text while the GUI is off", function(W)
  try("paused text", dbgf("SetDrawPausedTextWhenGUIDisabled"), onoff(W[3], true))
end }
UI.interesting = { "ui interesting on|off  highlighting of interesting objects (args unconfirmed)", function(W)
  try_shapes("interesting highlight", member("Player", "SetInterestingHighlightingAsEnabled"),
             ent_shapes(get_hero(), onoff(W[3], true)))
end }
UI.suggestions = { "ui suggestions on|off  automatic HUD suggestions (args unconfirmed)", function(W)
  try_shapes("hud suggestions", member("Player", "SetAutomaticHUDSuggestionsAsEnabled"),
             ent_shapes(get_hero(), onoff(W[3], true)))
end }
UI.counter = { "ui counter <n>|off  on-screen counter (args unconfirmed)", function(W)
  if (W[3] or ""):lower() == "off" then return try("remove counter", gui("RemoveCounter")) end
  try_shapes("counter", gui("SetCounter"), { pack(tonumber(W[3]) or 0), pack("", tonumber(W[3]) or 0) })
end }
UI.timer = { "ui timer <seconds>|off  on-screen timer (args unconfirmed)", function(W)
  if (W[3] or ""):lower() == "off" then return try("remove timer", gui("RemoveTimer")) end
  try("timer", gui("SetTimer"), tonumber(W[3]) or 60)
end }
UI.state = { "ui state  is a UI/menu/box/movie active", function()
  try("ui active", gui("IsUIActive"))
  try("quick menu open", gui("IsQuickMenuOpen"))
  try("display box active", gui("IsDisplayBoxActive"))
  try("movie playing", gui("IsPlayingMovie"))
  try("screen faded", gui("IsScreenFullyFaded"))
end }
UI.credits = { "ui credits  show the credits screen", function() try("credits", gui("ShowCreditsScreen")) end }
UI.consolescale = { "ui consolescale <n>  engine debug console scale (Debug.SetConsoleScale)", function(W)
  try("console scale", dbgf("SetConsoleScale"), tonumber(W[3]) or 1)
end }
UI.expmeter = { "ui expmeter on|off  draw the expression meter", function(W)
  try("expression meter", dbgf("SetDrawExpressionMeter"), onoff(W[3], true))
end }

add("ui", "ui <sub>  HUD and screen commands; ui list shows them", function(W)
  local s = (W[2] or "list"):lower()
  if s == "list" or not UI[s] then
    local names = {}
    for k in pairs(UI) do names[#names + 1] = k end
    table.sort(names)
    if s ~= "list" then out("unknown ui command: " .. s) end
    for _, k in ipairs(names) do out(UI[k][1]) end
    return
  end
  UI[s][2](W)
end)

add("msg", "msg <text>  show a message box (same as ui msg)", function()
  try("message box", gui("DisplayMessageBox"), line:match("^%S+%s+(.*)$") or "")
end)

-- Rendering -----------------------------------------------------------------
section("render")

local function engine_cfg()
  if type(D) ~= "table" then return nil end
  local ok, e = pcall(function() return D.EngineConfig end)
  if ok and (type(e) == "table" or type(e) == "userdata") then return e end
  return nil
end

add("engine", "engine [field] [value]  list, read or set Debug.EngineConfig fields", function(W)
  local e = engine_cfg()
  if not e then return out("Debug.EngineConfig not available") end
  if not W[2] then
    local emit, flush, n = chunked_emit(500)
    walk(e, "EngineConfig", 1, {}, emit, nil)
    flush()
    return out(n() .. " lines (see the log)")
  end
  if W[3] == nil then
    local ok, v = pcall(function() return e[W[2]] end)
    return out("EngineConfig." .. W[2] .. " = " .. (ok and fmt(v) or ("error: " .. tostring(v))))
  end
  local v = val(W[3])
  local ok, err = pcall(function() e[W[2]] = v end)
  out(ok and ("EngineConfig." .. W[2] .. " = " .. fmt(v)) or ("set failed: " .. tostring(err)))
end)

-- EngineConfig switches seen in the startup scripts.
local function engine_switch(name, field)
  add(name, name .. " [on|off]  Debug.EngineConfig." .. field, function(W)
    local e = engine_cfg()
    if not e then return out("Debug.EngineConfig not available") end
    local want = val(W[2])
    if type(want) ~= "boolean" then
      local ok, cur = pcall(function() return e[field] end)
      want = not (ok and cur)
    end
    local ok, err = pcall(function() e[field] = want end)
    out(ok and (field .. " = " .. tostring(want)) or ("set failed: " .. tostring(err)))
  end)
end
engine_switch("shadows", "EnableShadows")
engine_switch("tonemap", "EnableToneMapping")
engine_switch("lightmaps", "EnableLightmaps")
engine_switch("prt", "EnablePRT")
engine_switch("pointlights", "EnablePointLights")
engine_switch("spotlights", "EnableSpotLights")
engine_switch("whitetex", "UseWhiteTexture")
engine_switch("trees", "EnableTrees")
engine_switch("mist", "EnableMist")
engine_switch("repeatedmeshes", "EnableRepeatedMeshes")

local VIS = {
  lightcache = "EnableLightCacheVis", lightcount = "EnableLightCountVis",
  lightpass = "EnableLightPassVis", lightzone = "EnableLightZoneVis",
  lod = "EnableLoDLevelVis", sortprim = "EnableSortPrimVis",
  vertexcost = "EnableVertexCostVis", shadowcasters = "EnableDirectionalShadowCastersVis",
}
add("vis", "vis <mode>|off|list  engine visualisation modes (lightcache lightcount lightpass lightzone lod sortprim vertexcost shadowcasters)", function(W)
  local m = (W[2] or "list"):lower()
  if m == "off" then return try("clear vis modes", dbgf("ClearEngineVisModes")) end
  if not VIS[m] then
    local names = {}
    for k in pairs(VIS) do names[#names + 1] = k end
    table.sort(names)
    return out("vis modes: " .. table.concat(names, " ") .. " | off")
  end
  try("vis " .. m, dbgf(VIS[m]))
end)

add("lightdebug", "lightdebug off|nomain|nolightmaps|noambient|nolocal|missing  lighting debug from StartupConsoleScript", function(W)
  local M = {
    off = "RestoreLightingDebug", nomain = "DisableMainLight", nolightmaps = "DisableLightmaps",
    noambient = "DisableAmbient", nolocal = "DisableLocalLights", missing = "ShowMissingLightmaps",
  }
  local m = (W[2] or ""):lower()
  if not M[m] then return out("usage: lightdebug off|nomain|nolightmaps|noambient|nolocal|missing") end
  local f = rawget(_G, M[m]) or dbgf(M[m])
  try("lighting " .. m, f)
end)

add("particles", "particles [on|off]  particle updates (Debug.SetEnableParticleUpdate)", function(W)
  try("particles", dbgf("SetEnableParticleUpdate"), onoff(W[2], true))
end)

add("entdebug", "entdebug [on|off]  per-entity debug info (Debug.SetEnableEntityDrawDebugInfo)", function(W)
  try("entity debug info", dbgf("SetEnableEntityDrawDebugInfo"), onoff(W[2], true))
end)

add("facing", "facing  draw entity facing directions (Debug.DrawEntityFacing)", function()
  try("entity facing", dbgf("DrawEntityFacing"))
end)

add("generators", "generators [on|off]  draw creature generators", function(W)
  try("creature generators", dbgf("SetDrawCreatureGenerators"), onoff(W[2], true))
end)

add("combatdraw", "combatdraw [on|off]  Debug.SetDrawCombatDebug", function(W)
  try("combat debug", dbgf("SetDrawCombatDebug"), onoff(W[2], true))
end)

add("memstats", "memstats  Debug.PrintMemoryStats", function() try("memory stats", dbgf("PrintMemoryStats")) end)

-- Entities ------------------------------------------------------------------
section("entities")

add("inspect", "inspect [who]  name, id, position, distance, health, gold of an entity (who: hero, target, dog, henchman, name)", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  out("inspect " .. label .. ": " .. tostring(e))
  out("name: " .. tostring(method(e, "GetName")) .. "  id: " .. tostring(method(e, "GetID")))
  local x, y, z = entity_pos(e)
  if x then
    out(string.format("pos %.3f %.3f %.3f", x, y, z))
    local hx, hy, hz = hero_pos()
    if hx then
      out(string.format("distance to hero %.2f", math.sqrt((x - hx) ^ 2 + (y - hy) ^ 2 + (z - hz) ^ 2)))
    end
  end
  local cur, mx = hp_of(e)
  out("health " .. tostring(cur) .. " / " .. tostring(mx))
  local inv = get_shapes(member("Health", "IsInvulnerable"), { pack(e) })
  if inv ~= nil then out("invulnerable " .. tostring(inv)) end
  local g = get_shapes(member("Money", "Get"), { pack(e) })
  if g ~= nil then out("gold " .. tostring(g)) end
  local cf = get_shapes(member("Combat", "GetCanFight"), { pack(e) })
  if cf ~= nil then out("can fight " .. tostring(cf)) end
end)

add("tp", "tp <who>  teleport the hero to an entity (target, dog, henchman, name)", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  local x, y, z = entity_pos(e)
  if x then warp_to(x + 1, y, z) else out(label .. " has no position") end
end)

add("bring", "bring <who> [distance]  move an entity next to the hero", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  local x, y, z = hero_pos()
  if not x then return out("no hero position") end
  local d = tonumber(W[3]) or 2
  local ok, err = pcall(function() e:SetPosition(CVector3(x + d, y, z)) end)
  out(ok and ("brought " .. label) or ("bring failed: " .. tostring(err)))
end)

add("near", "near [radius]  list entities around the hero (Debug.GetAllEntitiesAroundHero, args unconfirmed)", function(W)
  local r = tonumber(W[2]) or 10
  local f = dbgf("GetAllEntitiesAroundHero")
  if not f then return out("Debug.GetAllEntitiesAroundHero not available") end
  local list, ok = get_shapes(f, { pack(r), pack() })
  if not ok then return try("near", f, r) end
  if type(list) ~= "table" then return out("near: " .. fmt(list)) end
  local n = 0
  for _, e in pairs(list) do
    n = n + 1
    if n <= 60 then
      local x, y, z = entity_pos(e)
      out(tostring(method(e, "GetName") or e) .. (x and string.format("  %.1f %.1f %.1f", x, y, z) or ""))
    end
  end
  out(n .. " entities")
end)

add("follow", "follow  make the targeted entity follow the hero (Debug.TargetedEntityFollowHero)", function()
  try("follow hero", dbgf("TargetedEntityFollowHero"))
end)

add("combat", "combat <who> fight|flee|attackable on|off  Combat.SetCanFight / SetCanFlee / SetCanBeAttacked", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  local F = { fight = "SetCanFight", flee = "SetCanFlee", attackable = "SetCanBeAttacked" }
  local k = (W[3] or ""):lower()
  if not F[k] then return out("usage: combat <who> fight|flee|attackable on|off") end
  try_shapes(label .. " " .. k, member("Combat", F[k]), { pack(e, onoff(W[4], true)) })
end)

add("damage", "damage <who> <multiplier>  Combat.SetDamageMultiplier (args unconfirmed)", function(W)
  local e, label = need_ent(W[2] or "hero")
  if not (e and tonumber(W[3])) then return out("usage: damage <who> <multiplier>") end
  try_shapes(label .. " damage x" .. W[3], member("Combat", "SetDamageMultiplier"), { pack(e, tonumber(W[3])) })
end)

add("dog", "dog here|pos|bark|growl|whimper|worms on|off|god on|off|inspect  hero's dog", function(W)
  local s = (W[2] or "pos"):lower()
  if s == "here" then
    if dbgf("TeleportDogToHero") then return try("dog to hero", dbgf("TeleportDogToHero")) end
    return CMD.bring({ "bring", "dog" })
  end
  local d, label = need_ent("dog")
  if not d then return end
  if s == "pos" then
    local x, y, z = entity_pos(d)
    out(x and string.format("dog pos %.3f %.3f %.3f", x, y, z) or "dog has no position")
  elseif s == "bark" or s == "growl" or s == "whimper" then
    local f = ({ bark = "Bark", growl = "Growl", whimper = "Whimper" })[s]
    try_shapes("dog " .. s, member("Dog", f), { pack(d), pack() })
  elseif s == "worms" then
    try_shapes("dog worms", member("Dog", "SetHasWorms"), { pack(d, onoff(W[3], true)) })
  elseif s == "god" then
    try_shapes("dog invulnerable", member("Health", "SetAsInvulnerable"), { pack(d, onoff(W[3], true)) })
  elseif s == "inspect" then
    CMD.inspect({ "inspect", "dog" })
  else
    out("usage: dog here|pos|bark|growl|whimper|worms on|off|god on|off|inspect")
  end
end)

-- Script debug helpers (the game's own Lua-defined Debug.* functions) -------
section("debugfns")

add("dbg", "dbg list [filter] | dbg <Name> [args]  list or call any Debug.* function", function(W)
  if type(D) ~= "table" then return out("Debug table not available") end
  local a = W[2]
  if not a or a:lower() == "list" then
    local filter = W[3] and W[3]:lower()
    local names = {}
    for _, k in ipairs(member_names(D) or {}) do
      local ok, v = pcall(function() return D[k] end)
      if ok and type(v) == "function" and (not filter or tostring(k):lower():find(filter, 1, true)) then
        names[#names + 1] = tostring(k)
      end
    end
    local emit, flush = chunked_emit(2000)
    for i = 1, #names, 8 do emit(table.concat(names, " ", i, math.min(i + 7, #names))) end
    flush()
    return out(#names .. " Debug functions" .. (filter and (" matching " .. filter) or ""))
  end
  local f = dbgf(a)
  if not f then
    -- case-insensitive match
    for _, k in ipairs(member_names(D) or {}) do
      if tostring(k):lower() == a:lower() then f = dbgf(k) a = tostring(k) break end
    end
  end
  local args, n = vals(W, 3)
  try("Debug." .. a, f, unpack(args, 1, n))
end)

add("probe", "probe <path> [args]  call a function with (hero, args) then (args) and report which shape works", function(W)
  if not W[2] then return out("usage: probe <path> [args]") end
  local f = resolve(W[2])
  local args, n = vals(W, 3)
  local hero = get_hero()
  try_shapes(W[2], f, { pack(hero, unpack(args, 1, n)), pack(unpack(args, 1, n)) })
end)

local QUICK = {
  winter = "DLC_ActivateWinter", summer = "DLC_ActivateSummer",
  openshops = "DLCToggleOpenAllShops", buyall = "BuyEverythingInLevel",
  doorsopen = "OpenDoor", doorclose = "CloseTargetDoor", resetopinions = "ResetOpinions",
  resetambush = "ResetAllAmbushes", villagers = "CreateAdditionalVillagers",
  crowd = "CreateLotsOfPeople", bandits = "CreateBanditGroupAroundHero",
  trolls = "CreateTheElevenStagesOfTrolls", hobbes = "CreateHobbes", zombies = "Zombiemageddon",
  superzombies = "SuperZombiemageddon", wave = "MexicanWave", commotion = "StartCommotion",
  nocolosseumtimer = "SkipColosseumTimer", lastcreature = "SkipToLastCreature",
  meetquestreq = "MeetQuestReq", questreward = "GiveQuestReward", title = "GiveTitle",
  tracktarget = "TrackingHeroTarget", traits = "GetTargetsDescriptiveTraits",
  personality = "TraceTargetPersonality", trackconvo = "TrackNextConversation",
  breadcrumbdig = "SetBreadcrumbTrailToNearestDiggingSpot", tobreadcrumb = "TeleportToBreadcrumbTargetPosition",
  tovillage = "TeleportToVillageMarker", beamup = "BeamHeroUpScotty", family = "CreateInstantFamily",
  spouse = "CreateSpouse", marryall = "MarryEveryone", gifts = "GiveEveryoneGifts",
  hateme = "HateMe", flee = "MakeFlee", drunk = "DrunkTarget", growup = "MakeChildGrowUpThroughTime",
  mimic = "ToggleMimic", garthmorph = "ToggleGarthMorph", pregnancy = "TogglePregnancyCertainty",
  bigamist = "ToggleCanCatchBigamist", controlcreature = "ControlCreature", telescope = "TurnFirstChestIntoATelescope",
  unlocktarget = "UnlockTargetQuestAll", discount = "GiveTargetDiscount", buildings = "CountBuildingsOwned",
}
add("quick", "quick <name>|list  one-word shortcuts for the game's own debug helpers (quick list)", function(W)
  local q = (W[2] or "list"):lower()
  if q == "list" or not QUICK[q] then
    local names = {}
    for k, v in pairs(QUICK) do names[#names + 1] = k .. "=" .. v end
    table.sort(names)
    if q ~= "list" then out("unknown quick name: " .. q) end
    for i = 1, #names, 6 do out(table.concat(names, "  ", i, math.min(i + 5, #names))) end
    return
  end
  local args, n = vals(W, 3)
  try("Debug." .. QUICK[q], dbgf(QUICK[q]), unpack(args, 1, n))
end)

-- Places ---------------------------------------------------------------------
section("places")

add("place", "place set <name> <x> <y> <z> | place del <name> | place export  manage warp places", function(W)
  local s = (W[2] or ""):lower()
  if s == "set" and W[3] and tonumber(W[4]) and tonumber(W[5]) and tonumber(W[6]) then
    C.places[W[3]:lower()] = { tonumber(W[4]), tonumber(W[5]), tonumber(W[6]) }
    return out("place " .. W[3] .. " set")
  end
  if s == "del" and W[3] then
    C.places[W[3]:lower()] = nil
    return out("place " .. W[3] .. " removed")
  end
  if s == "export" then
    local names = {}
    for k in pairs(C.places) do names[#names + 1] = k end
    table.sort(names)
    for _, k in ipairs(names) do
      local p = C.places[k]
      out(string.format("place set %s %.3f %.3f %.3f", k, p[1], p[2], p[3]))
    end
    return out(#names .. " places. Copy these lines into console\\autoexec.txt to keep them.")
  end
  out("usage: place set <name> <x> <y> <z> | place del <name> | place export")
end)

add("near_place", "near_place  the saved place closest to the hero", function()
  local x, y, z = hero_pos()
  if not x then return out("no position") end
  local best, bd
  for k, p in pairs(C.places) do
    local d = math.sqrt((p[1] - x) ^ 2 + (p[2] - y) ^ 2 + (p[3] - z) ^ 2)
    if not bd or d < bd then best, bd = k, d end
  end
  out(best and string.format("closest place %s (%.1f m)", best, bd) or "no places saved")
end)

-- Aliases / repeat ----------------------------------------------------------
section("aliases")
add("alias", "alias <name> <command...>  save a shortcut (lasts until the game closes)", function(W)
  if not (W[2] and W[3]) then return out("usage: alias <name> <command...>") end
  local body = line:match("^%S+%s+%S+%s+(.*)$")
  C.aliases[W[2]:lower()] = body
  out("alias " .. W[2] .. " = " .. body)
end)

---------------------------------------------------------------------------
-- Dispatch
---------------------------------------------------------------------------
local function run_line(l, depth)
  local W = words(l)
  local name = (W[1] or ""):lower()
  if name == "" then return end
  if C.aliases[name] and depth < 5 then
    return run_line(C.aliases[name], depth + 1)
  end
  local fn = CMD[name]
  if fn then
    local ok, err = pcall(fn, W)
    if not ok then out("error: " .. tostring(err)) end
    return
  end
  -- Not a known command: treat as Lua.
  local f = loadstring and (loadstring("return " .. l) or loadstring(l))
  if not f then return out("unknown command (and not valid Lua): " .. name .. "  -- try help") end
  local r = { pcall(f) }
  if r[1] then out("= " .. (#r > 1 and describe(unpack(r, 2, #r)):gsub("^ok %-> ", "") or "(nothing)"))
  else out("error: " .. tostring(r[2])) end
end

run_line(line, 0)

if box and #box_buf > 0 then
  pcall(GUI.DisplayMessageBox, table.concat(box_buf, "\n"))
end
