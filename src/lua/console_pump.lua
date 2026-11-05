-- console_pump.lua (console-v9): runs f2 Lua commands inside the game's own
-- script update, at a safe point on the script thread.
--
-- Calling CScriptManager::RunScript from the host's MainRenderLoop hook for
-- every command raced the game's own Lua and corrupted the VM (crashes within
-- seconds of long or repeated commands). Instead the host runs this file once;
-- it registers a per-frame script with the GeneralScriptManager (the same way
-- F5.lua registers its menu). Every few frames it loads console_run.lua, which
-- the host rewrites as `return <seq>, [==[<command line>]==]`, and when <seq> is
-- new it runs console_cmds.lua with that line and reports "__f2 done <seq>".
-- The host's cprint hook reads the "__f2 ..." markers.

local RUN = "scripts/recomp/console_run.lua"
local CMDS = "scripts/recomp/console_cmds.lua"

local function current()
  local f = loadfile(RUN)
  if not f then return nil end
  local ok, seq, line = pcall(f)
  if ok and type(seq) == "number" then return seq, line end
  return nil
end

local st = rawget(_G, "__f2_pump")
if st then
  pcall(cprint, "[console] __f2 ready")
  return
end
st = { last = current() or -1 }
rawset(_G, "__f2_pump", st)

local function step()
  local seq, line = current()
  if seq == nil or seq == st.last then return end
  st.last = seq
  local cmds = loadfile(CMDS)
  if cmds then
    local ok, err = pcall(cmds, line)
    if not ok then pcall(cprint, "[console] error: " .. tostring(err)) end
  else
    pcall(cprint, "[console] console_cmds.lua not found")
  end
  pcall(cprint, "[console] __f2 done " .. seq)
end

-- Persistent per-frame body, driven as a coroutine by the GeneralScriptManager.
-- Nothing in console_cmds.lua yields, so each command runs inside pcall.
local function update()
  local n = 0
  while true do
    n = n + 1
    if n % 2 == 0 then pcall(step) end
    coroutine.yield()
  end
end

local pump = {}
setmetatable(pump, pump)
pump.__index = _G
pump._G = _G
pump.Update = update
GeneralScriptManager.AddScript(pump)
pcall(cprint, "[console] __f2 ready")
