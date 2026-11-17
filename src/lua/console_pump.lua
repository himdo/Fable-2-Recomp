-- console_pump.lua: readies the developer console's Lua commands. The host runs
-- it with RunScript, on the world thread, before the first command (and again
-- after a save is loaded, since a load clears the Lua globals).
--
-- It used to register a per-frame script with the GeneralScriptManager that
-- polled for commands. Don't do that again: the GeneralScriptManager writes its
-- running scripts into save games (GetSaveTable), and that script carried
-- `_G = _G` and `__index = _G`, so every save written while it ran held the
-- whole global table. Loading such a save restored the script beside a newly
-- installed one (each command then ran twice), and the game crashed inside its
-- script engine soon after: null reads, aborts in the error handler, a stack
-- overflow walking the tables (2026-11-14/15). Now each command is a
-- RunScript of console_step.lua, and nothing stays in the script list.
--
-- Clean up those scripts when an older save brings them back: this console's
-- pump and the F5 menu (F5.lua) are the only scripts that carry an `_G` field.
-- Ending one means giving it an update that finishes; the GeneralScriptManager
-- then drops it from the list (and from the next save).

local function retire_old_scripts()
  -- Not rawget: the game provides GeneralScriptManager through _G's
  -- metatable, so a raw lookup finds nothing.
  local gsm = GeneralScriptManager
  if type(gsm) ~= "table" then return 0 end
  local removed = 0
  local node = rawget(gsm, "CurrentlyRunningScripts")
  local guard = 0
  while type(node) == "table" and node.value ~= nil and guard < 1000 do
    guard = guard + 1
    local s = node.value
    if type(s) == "table" and rawget(s, "_G") ~= nil and rawget(s, "_Name") == nil and
        rawget(s, "QuestName") == nil then
      rawset(s, "Update", function() end)
      rawset(s, "co_update", coroutine.create(function() end))
      rawset(s, "_G", nil)
      rawset(s, "__index", nil)
      removed = removed + 1
    end
    node = node.next
  end
  return removed
end

local ok, n = pcall(retire_old_scripts)
if ok and n > 0 then
  pcall(cprint, "[console] retired " .. n .. " old console/F5 script(s) restored from the save")
elseif not ok then
  pcall(cprint, "[console] could not check for old console scripts: " .. tostring(n))
end
-- console_step.lua skips a sequence number it has already run; start fresh.
rawset(_G, "__f2_last", nil)
pcall(cprint, "[console] __f2 ready")
