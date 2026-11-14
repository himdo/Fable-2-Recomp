-- console_cmds.lua (console-v9): interprets one `f2 <command>` line from the ~ console.
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
  -- Keep each cprint short (fixed-size buffer in the release cprint).
  for i = 1, #s, 380 do pcall(cprint, "[console] " .. s:sub(i, i + 379)) end
  if #s == 0 then pcall(cprint, "[console] ") end
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
add("warp", "warp <x> <y> <z> | save <name> | <name> | list | level <region> [marker] [world] | rel <dx> <dy> <dz>", function(W)
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
    -- Debug.LoadLevel(world, region, marker): every script call passes "Albion" (or
    -- "TestWorld"), a region path and a start marker ("" = default). See travel list.
    try("LoadLevel " .. W[3], dbgf("LoadLevel"), W[5] or "Albion", W[3], W[4] or "")
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

-- godmode: god mode alone still lets hits through, so also make the hero
-- invulnerable (no damage) and unable to die.
add("godmode", "godmode [on|off]  no damage: god mode + Health.SetAsInvulnerable + Health.SetCantDie on the hero", function(W)
  local hero = need_hero()
  if not hero then return end
  local on = val(W[2]) ~= false
  try("hero god", dbgf("SetHeroGodMode"), on)
  try_shapes("hero invulnerable", member("Health", "SetAsInvulnerable"), { pack(hero, on) })
  try_shapes("hero cant die", member("Health", "SetCantDie"), { pack(hero, on) })
end)

-- noclip: the engine has no working hero collision switch, so fly the free
-- camera through walls and drop the hero where it ends up. Only the call shapes
-- the game's scripts use: the wrong shape of these natives crashes the game.
local function free_cam_on()
  local g = dbgf("GetUseFreeCamera")
  local ok, v = pcall(g or error)
  return ok and v == true
end
add("noclip", "noclip [on|off]  on: fly the free camera through walls; off: put the hero on the ground below the camera", function(W)
  local want = val(W[2])
  if type(want) ~= "boolean" then want = not free_cam_on() end
  if want then
    try("noclip on (fly with the sticks, then noclip off)", dbgf("SetUseFreeCamera"), true)
    return
  end
  local hero = need_hero()
  if not hero then return end
  local ok, p = pcall(function() return CameraManager.CameraPosition end)
  if free_cam_on() and ok and p then
    local x, y, z = p:GetX(), p:GetY(), p:GetZ()
    -- Camera.GetGroundPositionAtPosition(camera, pos), as CameraFunctions calls it.
    local gok, g = pcall(function()
      return Camera.GetGroundPositionAtPosition(CameraManager.GetCurrentCamera().Camera, CVector3(x, y, z + 50))
    end)
    if gok and type(g) == "userdata" then z = g:GetZ() end
    try("noclip off", dbgf("SetUseFreeCamera"), false)
    warp_to(x, y, z)
  else
    try("noclip off", dbgf("SetUseFreeCamera"), false)
  end
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
  try("debug page", dbgf("SetDrawDebugPage"), W[2] or "player")
end)

add("combatdebug", "combatdebug [on|off]  draw combat group debug / behaviour names", function(W)
  local on = val(W[2]) ~= false
  try("behaviour names", dbgf("SetDrawBehaviourNames"), on)
  try("combat multiplier", dbgf("SetDrawCombatMultiplier"), on)
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
stat("herolevel", "SetHeroLevel", "set hero level (scripts use 1-5)")
stat("will", "SetHeroWill", "set hero Will, 0 to 1")
stat("strength", "SetHeroStrength", "set hero Strength, 0 to 1")
stat("morality", "SetHeroMorality", "1000 good, -1000 evil, 0 neutral")
stat("purity", "SetHeroPurity", "1000 pure, -1000 corrupt, 0 neutral")
stat("age", "SetHeroAge", "set hero age (scripts use 20-34)")
stat("fat", "SetHeroFat", "set hero fat, 0 to 1")
stat("tan", "SetHeroTan", "set hero tan, 0 to 1")

add("skill", "skill <0-1>  Debug.SetHeroSkill (the game's scripts pass 0 to 1, e.g. 0.5)", function(W)
  local n = tonumber(W[2])
  if not n then return out("usage: skill <0-1>") end
  try("skill", dbgf("SetHeroSkill"), n)
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
  -- Debug.AddAll*(entity) take the entity to fill; GiveAll*Weapons take nothing;
  -- AddAllWeaponsWithSlots(entity, slots); SetHeroWearingAllClothing(bool).
  local hero = get_hero()
  if c == "wear" then try(c, f, true)
  elseif fname == "AddAllWeaponsWithSlots" then
    if hero then try(c, f, hero, tonumber(W[3]) or 4) else out("no hero (load a save first)") end
  elseif fname:sub(1, 4) == "Give" then try(c, f)
  elseif hero then try(c, f, hero)
  else out("no hero (load a save first)") end
end)

add("weapon", "weapon <name>  give the hero a weapon by name (Debug.GiveHeroWeapon)", function(W)
  if not W[2] then return out("usage: weapon <name>") end
  -- Debug.GiveHeroWeapon(weapon_name, start_sheathed), e.g. ObjectInventory_L_LongSword_Iron
  try("weapon", dbgf("GiveHeroWeapon"), W[2], true)
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
  try("ffwd " .. h, Timing.FastForwardTimeOfDayTo, h, 0)
end)

-- Spawning / cleanup --------------------------------------------------------
section("spawn")
-- Debug.CreateEntityByHero(record_name, entity_name, extra_params)
add("spawn", "spawn <record> [count] [entityname]  create an entity next to the hero, e.g. spawn CreatureFaunaChicken 3", function(W)
  if not W[2] then return out("usage: spawn <record> [count] [entityname]") end
  local n = math.max(1, math.min(tonumber(W[3]) or 1, 50))
  for i = 1, n do
    local name = W[4] and (n > 1 and (W[4] .. i) or W[4]) or nil
    if not try("spawn " .. W[2], dbgf("CreateEntityByHero"), W[2], name) then return end
  end
end)

-- Debug.CreateEntityAtPosition(record_name, entity_name, position)
add("spawnat", "spawnat <record> <x> <y> <z> [entityname]  create an entity at a position", function(W)
  if not (W[2] and tonumber(W[3]) and tonumber(W[4]) and tonumber(W[5])) then
    return out("usage: spawnat <record> <x> <y> <z> [entityname]")
  end
  try("spawnat", dbgf("CreateEntityAtPosition"), W[2], W[6] or ("f2_" .. W[2]),
      CVector3(tonumber(W[3]), tonumber(W[4]), tonumber(W[5])))
end)

local FAMILIES = {
  "CreatureFamilyBalverineEasy03", "CreatureFamilyBanditMedium02a", "CreatureFamilyBeetleHard04",
  "CreatureFamilyHighwaymanEasy03", "CreatureFamilyHobbeMedium03a", "CreatureFamilyHollowmanEasy04",
  "CreatureFamilyLuciansGuardsHard", "CreatureFamilyLucienArmyHard03",
}
add("creatures", "creatures <family>|list  spawn a creature family (Debug.SpawnCreatureFamily)", function(W)
  if not W[2] or W[2]:lower() == "list" then
    return out("families used by the game's scripts: " .. table.concat(FAMILIES, " "))
  end
  try("creatures", dbgf("SpawnCreatureFamily"), W[2])
end)

-- Debug.CreateCombatantsAroundHero(type, num, preserve_faction, radius)
add("combatants", "combatants [record] [n]  create combatants around the hero (default CreatureHobbe 3)", function(W)
  local rec, n = W[2], tonumber(W[3])
  if tonumber(rec) then rec, n = nil, tonumber(rec) end
  try("combatants", dbgf("CreateCombatantsAroundHero"), rec or "CreatureHobbe", n or 3)
end)

add("killall", "killall  destroy all creature entities", function()
  try("killall", dbgf("DestroyAllCreatureEntities"), false)
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
-- Chunks stay under ~400 characters: the release cprint copies into a fixed-size
-- buffer and longer strings overran it (the host hook also takes [console]
-- lines away from it, console-v9).
local function chunked_emit(limit)
  local buf, count, chars = {}, 0, 0
  local function flush()
    if #buf > 0 then pcall(cprint, "[console] " .. table.concat(buf, "\n")) buf = {} chars = 0 end
  end
  local function emit(line)
    if count >= limit then return end
    count = count + 1
    line = tostring(line)
    if #line > 380 then line = line:sub(1, 377) .. "..." end
    if chars + #line > 380 then flush() end
    buf[#buf + 1] = line
    chars = chars + #line + 1
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
-- console-v6 commands (v7: argument shapes matched to the game's scripts). Where a shape is still a guess, try_shapes
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
  try_shapes(label .. " health -> " .. want, member("Health", "Modify"), { pack(e, want - cur, get_hero()) })
end)

add("heal", "heal [who]  restore full health", function(W)
  local e, label = need_ent(W[2])
  if not e then return end
  local cur, mx = hp_of(e)
  if not (cur and mx) then return out("health not readable for " .. label) end
  try_shapes("heal " .. label, member("Health", "Modify"), { pack(e, mx - cur, get_hero()) })
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

add("regen", "regen <rate> [who] [third]  Health.SetRegeneration(entity, rate, 100); the one script call passes 100 last", function(W)
  local n = tonumber(W[2])
  if not n then return out("usage: regen <rate> [who] [third]") end
  local e, label = need_ent(W[3])
  if e then try("regen " .. label, member("Health", "SetRegeneration"), e, n, tonumber(W[4]) or 100) end
end)

add("kill", "kill [who]  reduce health to 0 (default: target)", function(W)
  local e, label = need_ent(W[2] or "target")
  if not e then return end
  local cur = hp_of(e)
  try_shapes("kill " .. label, member("Health", "Modify"), { pack(e, -((cur or 100000) + 1), get_hero()) })
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

add("orbspawn", "orbspawn [count]  spawn experience orbs (Debug.SpawnExperienceOrbs(count), default 10)", function(W)
  try("spawn orbs", dbgf("SpawnExperienceOrbs"), tonumber(W[2]) or 10)
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

-- The hero performs one expression (photo mode's pose cards use this).
-- ExpressionPerformer.PerformExpression(hero, GDB.GetRecord(name)), as the game's
-- scripts call it. An expression the hero has not learned yet is made available
-- first (ExpressionPerformer.SetExpressionAsAvailable(hero, name, true)).
-- `pose relock` makes the expressions it had to make available unavailable
-- again (photo mode runs it when it closes), so nothing stays unlocked.
add("pose", "pose <expression>|relock  the hero performs an expression, e.g. pose ExpressionDance", function(W)
  if not W[2] then return out("usage: pose <expression>|relock  (e.g. pose ExpressionDance)") end
  local hero = need_hero()
  if not hero then return end
  local EPr = rawget(_G, "ExpressionPerformer")
  if W[2]:lower() == "relock" then
    local n = 0
    for name in pairs(C.pose_unlocked or {}) do
      if type(EPr) == "table" and pcall(EPr.SetExpressionAsAvailable, hero, name, false) then n = n + 1 end
    end
    C.pose_unlocked = nil
    return out("pose: locked " .. n .. " expressions again")
  end
  local name = W[2]
  if not name:find("^Expression") then name = "Expression" .. name end
  local EP = rawget(_G, "ExpressionPerformer")
  if type(EP) ~= "table" or type(EP.PerformExpression) ~= "function" then
    return out("pose: ExpressionPerformer is not available here")
  end
  local G = rawget(_G, "GDB")
  local okr, rec = pcall(function() return G.GetRecord(name) end)
  if not okr or rec == nil then return out("pose " .. name .. ": no such expression record") end
  if type(EP.IsExpressionAvailable) == "function" and type(EP.SetExpressionAsAvailable) == "function" then
    local oka, avail = pcall(EP.IsExpressionAvailable, hero, name)
    if oka and avail == false then
      pcall(EP.SetExpressionAsAvailable, hero, name, true)
      C.pose_unlocked = C.pose_unlocked or {}
      C.pose_unlocked[name] = true
      out("pose " .. name .. ": made available for now")
    end
  end
  try("pose " .. name, EP.PerformExpression, hero, rec)
end)

add("dye", "dye <clothing record> <dye record>  e.g. dye ObjectClothingCoatPoshM ObjectInventoryDyeClothingBasicBlue01", function(W)
  if not (W[2] and W[3]) then return out("usage: dye <clothing record> <dye record>") end
  -- Debug.SetHeroWornItemDye(clothing, true, dye), as the game's scripts call it
  try("dye", dbgf("SetHeroWornItemDye"), W[2], true, W[3])
end)

add("expressions", "expressions all|extend|succeed [on|off]  unlock expressions / force extended ones to succeed", function(W)
  local m = (W[2] or ""):lower()
  if m == "all" then try("all expressions", dbgf("SetAllExpressionsAvailable"), onoff(W[3], true))
  elseif m == "extend" then try("all extendable", dbgf("SetAllExpressionsExtendable"), onoff(W[3], true))
  elseif m == "succeed" then try("extended succeed", dbgf("SetForceExtendedExpressionsToSucceed"), onoff(W[3], true))
  else out("usage: expressions all|extend|succeed [on|off]") end
end)

add("entitytype", "entitytype <record>  Debug.ChangePlayerEntityType; scripts use CreatureHero and CreatureHeroFemale", function(W)
  if not W[2] then return out("usage: entitytype <record>  (scripts use CreatureHero, CreatureHeroFemale)") end
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
  -- Never call it with no arguments: Player.GetDifficultyLevel() crashes the game.
  local d = get_shapes(member("Player", "GetDifficultyLevel"), { pack(hero) })
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
stat_mod("addskill", "ModifySkill", "add to skill")

-- Enum lookup: "fireball" -> EHeroAbilityType.HERO_ABILITY_WILL_FIREBALL (unique substring match).
local function enum_lookup(tname, word)
  local t = rawget(_G, tname)
  if type(t) ~= "table" then return nil, tname .. " not available" end
  local w = word:upper()
  local ok, exact = pcall(function() return t[w] end)
  if ok and exact ~= nil then return exact, w end
  local hits = {}
  for _, k in ipairs(member_names(t) or {}) do
    if tostring(k):find(w, 1, true) then hits[#hits + 1] = tostring(k) end
  end
  if #hits == 1 then return t[hits[1]], hits[1] end
  if #hits == 0 then return nil, "no " .. tname .. " matches " .. word end
  return nil, "ambiguous: " .. table.concat(hits, " ", 1, math.min(#hits, 12))
end

local function enum_list(tname, filter)
  local t = rawget(_G, tname)
  if type(t) ~= "table" then return out(tname .. " not available") end
  local names = {}
  for _, k in ipairs(member_names(t) or {}) do
    if not filter or tostring(k):lower():find(filter:lower(), 1, true) then names[#names + 1] = tostring(k) end
  end
  local emit, flush = chunked_emit(1000)
  for i = 1, #names, 4 do emit(table.concat(names, " ", i, math.min(i + 3, #names))) end
  flush()
  out(#names .. " " .. tname .. " values")
end

add("ability", "ability <name> <level> | ability list  Stats.SetHeroAbilityLevel(hero, EHeroAbilityType.*, level), e.g. ability fireball 5", function(W)
  if (W[2] or ""):lower() == "list" then return enum_list("EHeroAbilityType", W[3]) end
  if not W[2] then return out("usage: ability <name> [level] | ability list") end
  local hero = need_hero()
  if not hero then return end
  local e, label = enum_lookup("EHeroAbilityType", W[2])
  if e == nil then return out(label) end
  if not tonumber(W[3]) then return try(label, member("Stats", "GetHeroAbilityLevel"), hero, e) end
  try("ability " .. label, member("Stats", "SetHeroAbilityLevel"), hero, e, tonumber(W[3]))
end)

local TITLES = {
  "Avenger", "Blade", "BountyHunter", "Brute", "Butcher", "ChickenChaser", "ChosenOne", "Conjurer",
  "Desperado", "DogLover", "Dumpling", "Executioner", "Fearmonger", "Gunslinger", "Joker", "Lionheart",
  "MasterChief", "Mayor", "Nobhead", "SaladDodger", "Shadowfiend", "Sparrow",
}
add("title", "title [<name>|list]  print the title, or unlock one, e.g. title ChickenChaser", function(W)
  local hero = need_hero()
  if not hero then return end
  local a = W[2]
  if not a then return try_shapes("title", member("Stats", "GetTitle"), ent_shapes(hero)) end
  if a:lower() == "list" then return out("titles: " .. table.concat(TITLES, " ")) end
  local rec = a:find("^ObjectInventory") and a or ("ObjectInventoryHeroTitle" .. a)
  try("unlock title " .. rec, member("Stats", "AddUnlockedTitle"), hero, rec)
end)

add("chapter", "chapter [n]  print or set chapter progress (args unconfirmed)", function(W)
  local hero = need_hero()
  if hero then
    getset("chapter", member("Stats", "GetChapterProgress"), member("Stats", "SetChapterProgress"), W[2],
           function(v) if v == nil then return ent_shapes(hero) end return ent_shapes(hero, v) end)
  end
end)

add("trackedstat", "trackedstat <name> [n] | trackedstat list [filter]  ETrackedStatType stats, e.g. trackedstat groin_shots", function(W)
  if not W[2] then return out("usage: trackedstat <name> [n] | trackedstat list [filter]") end
  if W[2]:lower() == "list" then return enum_list("ETrackedStatType", W[3]) end
  local hero = need_hero()
  if not hero then return end
  local e, label = enum_lookup("ETrackedStatType", W[2])
  if e == nil then return out(label) end
  if W[3] then
    return try("set " .. label, member("Stats", "SetTrackedStat"), hero, e, tonumber(W[3]))
  end
  try(label, member("Stats", "GetTrackedStat"), hero, e)
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
  try("henchman weapon", dbgf("GiveHenchmanWeapon"), W[2], true)
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

add("nextday", "nextday [n]  Timing.AdvanceDayCount(n), default 1", function(W)
  try("advance day", member("Timing", "AdvanceDayCount"), tonumber(W[2]) or 1)
end)

add("moon", "moon <phase>  Timing.SetMoonPhase (the scripts pass 4)", function(W)
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
  try("autosave", rawget(_G, "AutoSaveGame"), true, true)
end)

add("sfx", "sfx <event>  play a sound event on the hero, e.g. sfx SE_GOLD_PAYMENT (Sound.PlayEvent(entity, event, ''))", function(W)
  if not W[2] then return out("usage: sfx <event>") end
  local hero = get_hero()
  try_shapes("sound " .. W[2], member("Sound", "PlayEvent"), { pack(hero, W[2], "") })
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

-- Camera.* functions take the camera object first. The game's scripts get it as
-- CameraManager.GetCurrentCamera().Camera (e.g. Camera.SetFOV(cur.Camera, fov)).
local function cam_obj()
  local ok, c = pcall(function() return CameraManager.GetCurrentCamera().Camera end)
  if ok and c ~= nil then return c end
  out("no current camera (CameraManager.GetCurrentCamera().Camera)")
  return nil
end

local function cam_getset(label, gname, sname, arg)
  local c = cam_obj()
  if c == nil then return end
  if arg == nil then return try(label, cam(gname), c) end
  local v = tonumber(arg)
  if not v then return out("usage: " .. label .. " [number]") end
  try(label .. " " .. v, cam(sname), c, v)
end

add("fov", "fov [degrees]  print or set the camera field of view", function(W)
  if W[2] and tonumber(W[2]) and dbgf("SetFOV") then
    -- the game's own helper: Debug.SetFOV(fov)
    return try("fov " .. W[2], dbgf("SetFOV"), tonumber(W[2]))
  end
  cam_getset("fov", "GetFOV", "SetFOV", W[2])
end)
add("zoom", "zoom [level]  print or set the camera zoom level", function(W)
  cam_getset("zoom", "GetZoomLevel", "SetZoomLevel", W[2])
end)
add("roll", "roll [angle]  print or set camera roll", function(W)
  cam_getset("roll", "GetRoll", "SetRoll", W[2])
end)
add("blur", "blur [amount]  print or set camera blur", function(W)
  cam_getset("blur", "GetBlur", "SetBlur", W[2])
end)
add("dof", "dof  print depth of field (setting it takes 7 values; use call Camera.SetDOF)", function()
  local c = cam_obj()
  if c ~= nil then try("dof", cam("GetDOF"), c) end
end)

add("shake", "shake  screen shake on the hero (Player.AddScreenShake(hero, {}); table fields unknown)", function()
  local hero = need_hero()
  if hero then try("screen shake", member("Player", "AddScreenShake"), hero, {}) end
end)

add("campos", "campos  camera position, angles and forward vector", function()
  local hero = need_hero()
  if not hero then return end
  try("camera position", member("Player", "GetPositionOfCameraAssociatedWithPlayer"), hero)
  try("camera forward", member("Player", "GetForwardOfCameraAssociatedWithPlayer"), hero)
  local c = cam_obj()
  if c ~= nil then try("camera angles", cam("GetAngles"), c) end
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
UI.close = { "ui close <boxname>  remove a named display box (GUI.RemoveDisplayBox(name))", function(W)
  if not W[3] then return out("usage: ui close <boxname>") end
  try("remove box " .. W[3], gui("RemoveDisplayBox"), W[3])
end }
UI.fadeout = { "ui fadeout [seconds]  fade the screen to black", function(W)
  try("fade out", gui("FadeScreenOut"), tonumber(W[3]) or 1)
end }
UI.fadein = { "ui fadein [seconds]  fade the screen back in", function(W)
  try("fade in", gui("FadeScreenIn"), tonumber(W[3]) or 1)
end }
UI.toggle = { "ui toggle <element> [on|off]  GUI.ToggleElement(name, bool); scripts use DPadUp DPadDown DPadRight", function(W)
  if not W[3] then return out("usage: ui toggle <element> [on|off]  (scripts use DPadUp DPadDown DPadRight)") end
  try("toggle " .. W[3], gui("ToggleElement"), W[3], onoff(W[4], true))
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
UI.counter = { "ui counter <n> [max]|off  on-screen counter (GUI.SetCounter(name, textkey, value[, max]))", function(W)
  if (W[3] or ""):lower() == "off" then return try("remove counter", gui("RemoveCounter"), "f2counter") end
  local n, mx = tonumber(W[3]) or 0, tonumber(W[4])
  if mx then try("counter", gui("SetCounter"), "f2counter", "", n, mx)
  else try("counter", gui("SetCounter"), "f2counter", "", n) end
end }
UI.timer = { "ui timer <seconds>|off  on-screen countdown (GUI.SetTimer(name, seconds))", function(W)
  if (W[3] or ""):lower() == "off" then return try("remove timer", gui("RemoveTimer"), "f2timer") end
  try("timer", gui("SetTimer"), "f2timer", tonumber(W[3]) or 60)
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

-- HUD element editing (console-v8). The game-side GUI table reaches HUD elements
-- by name (names from art/gui/gameface/hud.bgf). Scripts only pass the names in
-- the comments, so other names are untested. Position/scale/opacity/tint live in
-- the GUI Lua state (setAttribute) and are not reachable from here yet.
local HUD_ELEMENTS = {
  "HUD", "Layer", "UpperElements", "PlayerHealth", "HealthAndMana", "Health", "PlayerHealthMax",
  "PlayerLowHealthPulser", "CoopHealth", "CoopHealthAndMana", "CoopHealthMax", "CoopLowHealthPulser",
  "Minimap", "WorldIcons", "Subtitles", "CenterToggle", "DPad", "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
  "Target", "JobMultiplier", "CrucibleMeter", "GenericMeter", "PriceWidget", "CornerBox", "Counter1", "Counter2",
  "Counter3", "Counter4", "TargetedQuery", "CutsceneVignette", "CutsceneVignetteBorder", "VignetteBorder",
  "ShardHealth", "ExpressionMeter", "ExpressionFill", "LeftTopButton", "RightTopButton", "TopTitle",
  "AButton", "YButton", "SpellAssigning", "SpellCharging", "PlayerSpellCharger", "HenchmanSpellCharger",
  "ExperianceDisplay", "AbilitiesBox", "ExperienceAmounts", "StrengthExperience", "SkillExperience",
  "WillExperience", "GeneralExperience", "ExperienceOrbs", "TargetedQueryOpinion", "TargetedGift",
  "TargetedDivorce", "TargetedMarry", "HighlightNPC", "Screenshot", "Theme",
}
UI.elements = { "ui elements [filter]  HUD element names from hud.bgf", function(W)
  local f = W[3] and W[3]:lower()
  local names = {}
  for _, n in ipairs(HUD_ELEMENTS) do
    if not f or n:lower():find(f, 1, true) then names[#names + 1] = n end
  end
  for i = 1, #names, 10 do out(table.concat(names, " ", i, math.min(i + 9, #names))) end
end }
UI.show = { "ui show <element>  GUI.ToggleElement(element, true) (scripts use DPadUp/Down/Right; others untested)", function(W)
  if not W[3] then return out("usage: ui show <element>  (ui elements lists names)") end
  try("show " .. W[3], gui("ToggleElement"), W[3], true)
end }
UI.hide = { "ui hide <element>  GUI.ToggleElement(element, false)", function(W)
  if not W[3] then return out("usage: ui hide <element>  (ui elements lists names)") end
  try("hide " .. W[3], gui("ToggleElement"), W[3], false)
end }
UI.fade = { "ui fade <element> in|out  GUI.FadeElementIn / FadeElementOutSlow (scripts use ShardHealth)", function(W)
  local d = (W[4] or ""):lower()
  if not W[3] or (d ~= "in" and d ~= "out") then return out("usage: ui fade <element> in|out") end
  if d == "in" then try("fade in " .. W[3], gui("FadeElementIn"), W[3])
  else try("fade out " .. W[3], gui("FadeElementOutSlow"), W[3]) end
end }
UI.text = { "ui text <element> <TEXT_ID>  GUI.SetGUIText(element, text id), e.g. ui text ShardHealth TEXT_CREATURE_NAME_TROLL", function(W)
  if not (W[3] and W[4]) then return out("usage: ui text <element> <TEXT_ID>  (a string-table id, not free text)") end
  try("text " .. W[3], gui("SetGUIText"), W[3], W[4])
end }
UI.meter = { "ui meter <element> <percent> [seconds]  GUI.SetMeterPercentOverTime (scripts use ShardHealth)", function(W)
  if not (W[3] and tonumber(W[4])) then return out("usage: ui meter <element> <percent> [seconds]") end
  try("meter " .. W[3], gui("SetMeterPercentOverTime"), W[3], tonumber(W[4]), tonumber(W[5]) or 0)
end }
UI.fill = { "ui fill low|med|high <r> <g> <b>  meter fill colours (0-255)", function(W)
  local F = { low = "SetLowFillColour", med = "SetMedFillColour", high = "SetHighFillColour" }
  local k = (W[3] or ""):lower()
  local r, g, b = tonumber(W[4]), tonumber(W[5]), tonumber(W[6])
  if not (F[k] and r and g and b) then return out("usage: ui fill low|med|high <r> <g> <b>") end
  try("fill " .. k, gui(F[k]), r, g, b)
end }
UI.fillpct = { "ui fillpct <low%> <med%>  where the meter switches fill colour (scripts use 18 28)", function(W)
  if not (tonumber(W[3]) and tonumber(W[4])) then return out("usage: ui fillpct <low%> <med%>") end
  try("fill thresholds", gui("SetFillColourPercentages"), tonumber(W[3]), tonumber(W[4]))
end }
UI.fadecolour = { "ui fadecolour <r> <g> <b> [seconds]  fade the screen to a colour (ui fadein to undo)", function(W)
  local r, g, b = tonumber(W[3]), tonumber(W[4]), tonumber(W[5])
  if not (r and g and b) then return out("usage: ui fadecolour <r> <g> <b> [seconds]") end
  try("fade to colour", gui("FadeScreenOutToColour"), r, g, b, tonumber(W[6]) or 1)
end }
UI.dpadcenter = { "ui dpadcenter on|off  GUI.SetDPadCenter", function(W)
  try("dpad center", gui("SetDPadCenter"), onoff(W[3], true))
end }
UI.abilities = { "ui abilities on|off  experience display shows abilities", function(W)
  try("show abilities", gui("SetExperienceShouldShowAbilities"), onoff(W[3], true))
end }
UI.gdc = { "ui gdc on|off  the GDC demo HUD (GUI.ShowGDCUI)", function(W)
  try("gdc ui", gui("ShowGDCUI"), onoff(W[3], true))
end }
UI.suggestmode = { "ui suggestmode normal|expressions  GUI.SetHUDSuggestionMode", function(W)
  local m = (W[3] or ""):lower()
  local key = m == "normal" and "SUGGESTION_MODE_NORMAL" or m == "expressions" and "SUGGESTION_MODE_EXPRESSION_CHOICES_ONLY"
  if not key then return out("usage: ui suggestmode normal|expressions") end
  local e = rawget(_G, "EHUDSuggestionMode")
  if type(e) ~= "table" then return out("EHUDSuggestionMode not available") end
  try("suggestion mode " .. m, gui("SetHUDSuggestionMode"), e[key])
end }
UI.icon = { "ui icon add|remove [who] [type]  world icon over an entity (GUI.AddWorldIcon(name, entity, text, EWorldIconType.*))", function(W)
  local a = (W[3] or ""):lower()
  local e, label = need_ent(W[4] or "target")
  if not e then return end
  if a == "remove" then return try("remove icon " .. label, gui("RemoveWorldIcon"), "f2icon_" .. label, e) end
  if a ~= "add" then return out("usage: ui icon add|remove [who] [type]") end
  local t = rawget(_G, "EWorldIconType")
  if type(t) ~= "table" then return out("EWorldIconType not available") end
  local ty = t[(W[5] or "WI_QUEST_GIVER"):upper()]
  if ty == nil then return out("unknown EWorldIconType " .. tostring(W[5]) .. " (dump EWorldIconType lists them)") end
  try("add icon " .. label, gui("AddWorldIcon"), "f2icon_" .. label, e, "", ty)
end }
UI.timertext = { "ui timertext <seconds> <TEXT_ID>  countdown with a label (GUI.SetTimerWithText)", function(W)
  if not (tonumber(W[3]) and W[4]) then return out("usage: ui timertext <seconds> <TEXT_ID>") end
  try("timer", gui("SetTimerWithText"), "f2timer", tonumber(W[3]), W[4])
end }

-- These run in the GameFace GUI Lua state through the host bridge
-- (fable2_gui_lua.h); the host answers them before this file sees them.
local function host_only() out("handled by the host bridge; if you see this, rebuild the game (console-v9)") end
for k, h in pairs({
  tree = "ui tree [element] [depth]  the element hierarchy (default Scene, depth 3)",
  get = "ui get <element> [attr...]  position, scale, rotation, opacity, active, or the named attributes",
  pos = "ui pos <element> <x> <y>  set position", move = "ui move <element> <dx> <dy>  move by an offset",
  scale = "ui scale <element> <sx> [sy]  set scale", rotate = "ui rotate <element> <degrees>  rotation.z",
  opacity = "ui opacity <element> <0-100>", visible = "ui visible <element> on|off  the 'active' attribute",
  tint = "ui tint <element> <r> <g> <b> [a]  diffuse colour", textcolor = "ui textcolor <element> <r> <g> <b>",
  label = "ui label <element> <free text>  textstring", attr = "ui attr <element> <attribute> <value>  any attribute",
  hudscale = "ui hudscale <s>  scale the whole HUD layer", hudopacity = "ui hudopacity <0-100>  the whole HUD layer",
  reset = "ui reset <element>|all  restore what ui changed", export = "ui export  print ui attr lines for autoexec.txt",
}) do UI[k] = { h, host_only } end

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

add("facing", "facing [who]  draw an entity's facing direction (Debug.DrawEntityFacing(entity))", function(W)
  local e, label = need_ent(W[2] or "target")
  if e then try("facing " .. label, dbgf("DrawEntityFacing"), e) end
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

add("near", "near [radius] [type]  list entities around the hero (Debug.GetAllEntitiesAroundHero(type, range))", function(W)
  local r = tonumber(W[2]) or 10
  local f = dbgf("GetAllEntitiesAroundHero")
  if not f then return out("Debug.GetAllEntitiesAroundHero not available") end
  local list, ok = get_shapes(f, { pack(W[3], r) })
  if not ok then return try("near", f, W[3], r) end
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

add("flee", "flee [who]  make a combatant flee (Debug.MakeFlee(combatant), default target)", function(W)
  local e, label = need_ent(W[2] or "target")
  if e then try("flee " .. label, dbgf("MakeFlee"), e) end
end)

add("control", "control [who]  take control of a creature (Debug.ControlCreature(dood, targ)); experimental", function(W)
  local e, label = need_ent(W[2] or "target")
  if e then try("control " .. label, dbgf("ControlCreature"), e, nil) end
end)

add("teleportent", "teleportent <entityname>  move a named entity to the player (Debug.TeleportEntityToPlayer)", function(W)
  if not W[2] then return out("usage: teleportent <entityname>") end
  try("teleport " .. W[2], dbgf("TeleportEntityToPlayer"), W[2])
end)

add("damage", "damage <who> <multiplier>  Combat.SetDamageMultiplier(entity, multiplier)", function(W)
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
    try_shapes("dog " .. s, member("Dog", f), { pack(d, true), pack(d) })
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

-- name -> { Debug function, parameter list read from the game's script banks }.
-- Typed arguments are passed through in order; most take none.
local QUICK = {
  winter = { "DLC_ActivateWinter", "" }, summer = { "DLC_ActivateSummer", "" },
  openshops = { "DLCToggleOpenAllShops", "open" }, buyall = { "BuyEverythingInLevel", "" },
  doorsopen = { "OpenDoor", "" }, doorclose = { "CloseTargetDoor", "" },
  resetopinions = { "ResetOpinions", "" }, resetambush = { "ResetAllAmbushes", "" },
  villagers = { "CreateAdditionalVillagers", "num" }, crowd = { "CreateLotsOfPeople", "num" },
  extravillagers = { "AddExtraVillagers", "village_name number" },
  enemyvillagers = { "CreateEnemyVillagers", "num" },
  bandits = { "CreateBanditGroupAroundHero", "number" }, trolls = { "CreateTheElevenStagesOfTrolls", "" },
  troll = { "CreateTroll", "" }, hobbes = { "CreateHobbes", "" }, thug = { "CreateThug", "" },
  reaver = { "CreateReaver", "" }, reaverenemy = { "CreateReaverEnemy", "" }, garth = { "CreateGarth", "" },
  garthenemy = { "CreateGarthEnemy", "" }, terror = { "CreateTerror", "" },
  hugger = { "CreateHuggerBeetles", "number force_variant" }, spitter = { "CreateSpitterBeetles", "number force_variant" },
  testcombatants = { "CreateTestCombatants", "number creature_type" },
  humanoids = { "CreateHumanoidCombatants", "" }, rumblepit = { "RumblePit", "add_allies" },
  daggers = { "HouseOfFlyingDaggers", "num weapon_name" }, gunshots = { "OverrideHeroGunCapacity", "num_shots" },
  hammertime = { "HammerTime", "" }, landofthedead = { "LandOfTheDead", "" },
  usualsuspects = { "TheUsualSuspects", "" }, killallthugs = { "KillAll", "" },
  zombies = { "Zombiemageddon", "entity_type" }, superzombies = { "SuperZombiemageddon", "disable_wisp" },
  wave = { "MexicanWave", "" }, commotion = { "StartCommotion", "" }, crowdstate = { "SetCrowdState", "num" },
  nocolosseumtimer = { "SkipColosseumTimer", "" }, lastcreature = { "SkipToLastCreature", "" },
  meetquestreq = { "MeetQuestReq", "" }, questreward = { "GiveQuestReward", "" }, title = { "GiveTitle", "" },
  traits = { "GetTargetsDescriptiveTraits", "" }, personality = { "TraceTargetPersonality", "" },
  trackconvo = { "TrackNextConversation", "" }, cleartracking = { "ClearTracking", "" },
  breadcrumbdig = { "SetBreadcrumbTrailToNearestDiggingSpot", "" },
  tobreadcrumb = { "TeleportToBreadcrumbTargetPosition", "" }, tovillage = { "TeleportToVillageMarker", "" },
  beamup = { "BeamHeroUpScotty", "" }, family = { "CreateInstantFamily", "" }, spouse = { "CreateSpouse", "" },
  marryall = { "MarryEveryone", "" }, gifts = { "GiveEveryoneGifts", "" }, giftreward = { "GiveTargetGiftReward", "reward_name" },
  hateme = { "HateMe", "" }, drunk = { "DrunkTarget", "" }, growup = { "MakeChildGrowUpThroughTime", "" },
  mimic = { "ToggleMimic", "" }, garthmorph = { "ToggleGarthMorph", "create" },
  pregnancy = { "TogglePregnancyCertainty", "" }, bigamist = { "ToggleCanCatchBigamist", "" },
  telescope = { "TurnFirstChestIntoATelescope", "" }, chests = { "ChestAbracadabramageddon", "" },
  unlocktarget = { "UnlockTargetQuestAll", "" }, discount = { "GiveTargetDiscount", "" },
  buildings = { "CountBuildingsOwned", "" }, buildingsforsale = { "CountBuildingsForSaleInLevel", "" },
  heromoney = { "ShowHeroMoney", "" }, statue = { "CreateHeroStatue", "" }, sermon = { "PerformTheSermonOnTheMount", "" },
  wheel = { "SpinSpinSpinTheWheelOfJustice", "" }, stopdayspeed = { "StopDaySpeed", "stop" },
  henchmanto = { "ChangeHenchmanTo", "recordname" }, follower = { "CreateFollower", "recordname entityname" },
  miscitems = { "SpawnMiscItems", "num item_name" }, testfx = { "TestFX", "fx_name speed" },
  moralitynomorph = { "SetHeroMoralityNoMorph", "number" }, scripthelp = { "ScriptDebugHelpOn", "" },
  scripthelpoff = { "ScriptDebugHelpOff", "" }, playerpage = { "PlayerDebugPage", "" },
  dogoff = { "DisableDogBehaviours", "" }, trackdog = { "TrackDog", "" },
}
add("quick", "quick <name> [args] | quick list [filter]  one-word shortcuts for the game's own debug helpers", function(W)
  local q = (W[2] or "list"):lower()
  if q == "list" or not QUICK[q] then
    local filter = q == "list" and W[3] and W[3]:lower() or nil
    local names = {}
    for k, v in pairs(QUICK) do
      if not filter or (k .. v[1]):lower():find(filter, 1, true) then
        names[#names + 1] = k .. "=" .. v[1] .. (v[2] ~= "" and ("(" .. v[2] .. ")") or "")
      end
    end
    table.sort(names)
    if q ~= "list" then out("unknown quick name: " .. q) end
    for i = 1, #names, 4 do out(table.concat(names, "  ", i, math.min(i + 3, #names))) end
    return
  end
  local args, n = vals(W, 3)
  try("Debug." .. QUICK[q][1], dbgf(QUICK[q][1]), unpack(args, 1, n))
end)

-- Region/start-marker pairs the game's own scripts pass to Debug.LoadLevel("Albion", region, marker).
local TRAVEL = {
  bowerstone = { "BWSMarket", "PlayerStart" }, colosseum = { "BWSMarket", "QO890_ColosseumHeroMarker" },
  slums = { "BWSSlums", "BSStartFromBMkt" }, bloodstone = { "Bloodstone", "BloodstoneStartFromTreasureIsland" },
  bowerlake = { "BowerLake", "QC060_PStart" }, thagscave = { "Caves\\BowerLake\\ThagsCave", "BLThagCaveStartFromBL_2" },
  brightwood = { "Brightwood", "Chapter3BigFarm" }, treasureisland = { "Caves\\Bloodstone\\TreasureIsland", "" },
  ritualcave = { "Caves\\Ravenscar\\RVSRitualCave", "PlayerStart" }, fairfax = { "FairfaxCastleGardens", "FCGStartFromBMkt" },
  mysteryisland = { "MysteryIsland", "" }, ravenscar = { "Ravenscar", "QC160_StartMarker" },
  spire = { "TatteredSpire", "QC140_OfficersQuartersMarker" }, temple = { "TempleOfEvil", "TOEStartFromDune" },
  nightmarehollow = { "Tombs\\BWSMarket\\Nightmare Hollow", "" }, westcliff = { "Westcliff", "WestcliffStartFromCrucible" },
  wraithmarsh = { "Wraithmarsh", "QC170_TempTeleportMarker" }, rivercave = { "caves\\deepwood\\rivercave", "PlayerStart" },
}
add("travel", "travel <place>|list  load a region at a start marker the game's scripts use (Debug.LoadLevel)", function(W)
  local p = (W[2] or "list"):lower()
  local t = TRAVEL[p]
  if not t then
    local names = {}
    for k, v in pairs(TRAVEL) do names[#names + 1] = k .. "=" .. v[1] end
    table.sort(names)
    if p ~= "list" then out("unknown place: " .. p .. " (or use: warp level <region> [marker])") end
    for i = 1, #names, 4 do out(table.concat(names, "  ", i, math.min(i + 3, #names))) end
    return
  end
  try("travel " .. p, dbgf("LoadLevel"), "Albion", t[1], t[2])
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
