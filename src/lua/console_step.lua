-- console_step.lua: runs one developer-console command. The host writes
-- console_run.lua (`return <seq>, [==[<command line>]==]`) and then runs this
-- file with RunScript, on the world thread at the start of a world update. It
-- runs console_cmds.lua with that line and reports "__f2 done <seq>", which the
-- host's cprint hook reads. Nothing is left registered with the game (see
-- console_pump.lua for why).

local RUN = "scripts/recomp/console_run.lua"
local CMDS = "scripts/recomp/console_cmds.lua"

local f = loadfile(RUN)
if not f then
  pcall(cprint, "[console] console_run.lua not found")
  return
end
local ok, seq, line = pcall(f)
if not ok or type(seq) ~= "number" then return end
-- Never run the same command twice (e.g. if this file is run again).
if rawget(_G, "__f2_last") == seq then return end
rawset(_G, "__f2_last", seq)

local cmds = loadfile(CMDS)
if cmds then
  local ok2, err = pcall(cmds, line)
  if not ok2 then pcall(cprint, "[console] error: " .. tostring(err)) end
else
  pcall(cprint, "[console] console_cmds.lua not found")
end
pcall(cprint, "[console] __f2 done " .. seq)
