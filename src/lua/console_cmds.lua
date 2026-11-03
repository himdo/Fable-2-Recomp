-- console_cmds.lua (console-v5): interprets one command line typed in f2-console.cmd.
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
local function add(name, help, fn)
  CMD[name] = fn
  HELP[#HELP + 1] = { name, help }
end

add("help", "help [command]  list commands", function(W)
  if W[2] then
    for _, h in ipairs(HELP) do
      if h[1] == W[2]:lower() then out(h[2]) return end
    end
    out("no such command")
    return
  end
  local names = {}
  for _, h in ipairs(HELP) do names[#names + 1] = h[1] end
  out("commands: " .. table.concat(names, " "))
  out("help <command> shows usage. Prefix a line with ! to also get a message box.")
  out("Anything else is run as Lua.")
end)

-- Info ----------------------------------------------------------------------
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
add("reloadai", "reloadai  reload AI scripts", function() try("ReloadAI", dbgf("ReloadAI")) end)
add("reloadcam", "reloadcam  reload camera scripts", function() try("ReloadCameras", dbgf("ReloadCameras")) end)
add("quitmenu", "quitmenu confirm  quit to the front end (loses unsaved progress)", function(W)
  if (W[2] or "") ~= "confirm" then return out("this quits to the front end; run: quitmenu confirm") end
  try("quit to front end", dbgf("QuitToFrontEnd"))
end)

-- Discovery -----------------------------------------------------------------
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

-- Aliases / repeat ----------------------------------------------------------
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
