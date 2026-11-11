-- mouse_diag.lua: camera trace for mouse-look work (see src/input/mouse_diag.h).
--
-- Run by the console command `mousetrace on|off` (console_cmds.lua), which
-- `f2 mouse trace on|off` sends. It wraps SACCamera.ProcessGameAction and
-- SACCamera.FrameUpdate (the main gameplay camera) and prints one line per
-- camera update through cprint, tagged "[f2diag] U". The host's cprint
-- override writes those lines to fable2_mouse.log. The camera is unchanged:
-- the originals run with the same arguments and their results are returned.
--
-- Fields of a U line:
--   f     Timing.GetProgramFrame()
--   tick  Timing.GetTickRate()
--   act   rotate (2) and zoom (4) actions since the last update, as
--         action:control_x/control_y, ";"-separated ("-" if none)
--   rr    RotateRight going into the update (the yaw it will apply)
--   srr   SmoothRotateRight after the update (the yaw it did apply)
--   mho   ManualHeightOffset going in > after (camera height, clamped +-0.7)
--   cmho  CurrentManualHeightOffset after (the smoothed height)
--   man   PlayerIsManuallyRotatingCamera going in (1/0)

local mode = ...

local st = rawget(_G, "__f2_mousetrace")
if not st then
  st = { pending = {}, installed = false }
  rawset(_G, "__f2_mousetrace", st)
end

local function say(s)
  pcall(cprint, "[console] mousetrace: " .. s)
end

local function num(v)
  if type(v) == "number" then return string.format("%.4f", v) end
  return tostring(v)
end

local function pack(...)
  return { n = select("#", ...), ... }
end

local cls = rawget(_G, "SACCamera")
if type(cls) ~= "table" then
  say("SACCamera is not loaded (load a save first)")
  return
end

local function install()
  if st.installed and cls.FrameUpdate == st.wrap_fu then return say("already on") end
  if type(cls.ProcessGameAction) ~= "function" or type(cls.FrameUpdate) ~= "function" then
    return say("SACCamera.ProcessGameAction / FrameUpdate not found")
  end
  st.orig_pga = cls.ProcessGameAction
  st.orig_fu = cls.FrameUpdate
  st.pending = {}

  st.wrap_pga = function(self, action, x, y, ...)
    if action == 2 or action == 4 then
      local p = st.pending
      if #p < 16 then
        p[#p + 1] = string.format("%d:%.3f/%.3f", action, x or 0, y or 0)
      end
    end
    return st.orig_pga(self, action, x, y, ...)
  end

  st.wrap_fu = function(self, ...)
    local rr, mho = self.RotateRight, self.ManualHeightOffset
    local man = self.PlayerIsManuallyRotatingCamera
    local r = pack(st.orig_fu(self, ...))
    local okf, f = pcall(Timing.GetProgramFrame)
    local okt, tick = pcall(Timing.GetTickRate)
    local act = (#st.pending > 0) and table.concat(st.pending, ";") or "-"
    st.pending = {}
    pcall(cprint, string.format(
      "[f2diag] U f=%s tick=%s act=%s rr=%s srr=%s mho=%s>%s cmho=%s man=%d",
      okf and tostring(f) or "?", okt and tostring(tick) or "?", act,
      num(rr), num(self.SmoothRotateRight), num(mho), num(self.ManualHeightOffset),
      num(self.CurrentManualHeightOffset), man and 1 or 0))
    return unpack(r, 1, r.n)
  end

  cls.ProcessGameAction = st.wrap_pga
  cls.FrameUpdate = st.wrap_fu
  st.installed = true
  say("on (no U lines in fable2_mouse.log means the live camera does not use SACCamera's methods)")
end

local function uninstall()
  if not st.installed then return say("already off") end
  -- Only restore if the class still holds our wrappers; after a camera script
  -- reload the class has fresh functions and the wrappers are already gone.
  if cls.FrameUpdate == st.wrap_fu then cls.FrameUpdate = st.orig_fu end
  if cls.ProcessGameAction == st.wrap_pga then cls.ProcessGameAction = st.orig_pga end
  st.installed = false
  st.pending = {}
  say("off")
end

if mode == "on" then
  install()
elseif mode == "off" then
  uninstall()
else
  local live = st.installed and cls.FrameUpdate == st.wrap_fu
  say(live and "on" or (st.installed and "on, but SACCamera was reloaded: run mousetrace on again" or "off"))
end
