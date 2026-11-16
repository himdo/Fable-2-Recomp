-- mouse_diag.lua: camera trace for mouse-look work (see src/input/mouse_diag.h).
--
-- Run by the console command `mousetrace on|off` (console_cmds.lua), which
-- `f2 mouse trace on|off` sends. It wraps three methods of SACCamera, the main
-- gameplay camera, and prints one line per camera update through cprint,
-- tagged "[f2diag] U". The host's cprint override writes those lines to
-- fable2_mouse.log. The camera is unchanged: the originals run with the same
-- arguments and their results are returned.
--
--   ProcessGameAction  records the stick actions the engine sends
--   SearchForFoesIfNeeded  the second call in SACCamera.FrameUpdate (right after
--                      CheckForTeleport, which mouse_direct.lua wraps): records
--                      the values going into the update
--   ClearTime          the last call in SACCamera.FrameUpdate: prints the line
--
-- FrameUpdate calls both by name through self, so the trace starts on the next
-- camera update. (Wrapping FrameUpdate itself only took effect after the
-- camera was re-activated, because the engine keeps the function it got at
-- activation.) The trace and mouse_direct.lua never wrap the same method, so
-- neither can end up wrapping the other's wrapper. Because the direct mode's
-- wrapper runs first, man=1 also shows updates the mouse turned.
--
-- Each wrapper carries its own label (a marker in its function environment,
-- as in mouse_direct.lua), so running this again never wraps one of our
-- wrappers, and each wrapper calls the game's real function. The on/off switch
-- and the pending actions are globals: if a save load clears them the trace is
-- simply off. Wrappers stay in place when off and just pass through.
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
--   my    yaw the direct mouse camera added this update, degrees, same sign
--         as rr (mouse_direct.lua; 0 in stick mode)
--   mdh   height the direct mouse camera added this update

local mode = ...

local function say(s)
  pcall(cprint, "[console] mousetrace: " .. s)
end

local function num(v)
  if type(v) == "number" then return string.format("%.4f", v) end
  return tostring(v)
end

-- Find the SACCamera class. Plain indexing, not rawget: game globals can sit
-- behind a metatable on _G (the console's own resolve() indexes the same way).
-- Fallback: the live camera's class, through its metatable.
local function has_methods(t)
  return type(t) == "table" and type(t.ProcessGameAction) == "function"
     and type(t.SearchForFoesIfNeeded) == "function" and type(t.ClearTime) == "function"
end

local function find_class()
  local ok, c = pcall(function() return SACCamera end)
  if ok and has_methods(c) then return c, "SACCamera" end
  local okm, cm = pcall(function() return CameraManager end)
  if okm and type(cm) == "table" and type(cm.GetCurrentCamera) == "function" then
    for _, call in ipairs({ function() return cm:GetCurrentCamera() end,
                            function() return cm.GetCurrentCamera() end }) do
      local okc, cam = pcall(call)
      if okc and type(cam) == "table" then
        local mt = getmetatable(cam)
        local idx = mt and rawget(mt, "__index")
        if has_methods(idx) then return idx, "current camera's class" end
        if has_methods(mt) then return mt, "current camera's metatable" end
      end
    end
  end
  return nil
end

local cls, found_as = find_class()
if not cls then
  say("SACCamera not found (load a save first; f2 lua type(SACCamera) shows what the console sees)")
  return
end

local MARK = "__f2_mousetrace_wrapper"

local function is_ours(f)
  if type(f) ~= "function" or type(getfenv) ~= "function" then return false end
  local ok, env = pcall(getfenv, f)
  return ok and type(env) == "table" and rawget(env, MARK) ~= nil
end

local function label(wrapper)
  if type(setfenv) == "function" and type(getfenv) == "function" then
    pcall(setfenv, wrapper, setmetatable({ [MARK] = true }, { __index = getfenv(1) }))
  end
  return wrapper
end

local function on()
  return rawget(_G, "__f2_mousetrace_on") == true
end

local function pending()
  local p = rawget(_G, "__f2_mousetrace_pending")
  if type(p) ~= "table" then
    p = {}
    rawset(_G, "__f2_mousetrace_pending", p)
  end
  return p
end

local function pack(...)
  return { n = select("#", ...), ... }
end

local MAKERS = {}

MAKERS.ProcessGameAction = function(original)
  return function(self, action, x, y, ...)
    if on() and (action == 2 or action == 4) then
      local p = pending()
      if #p < 16 then
        p[#p + 1] = string.format("%d:%.3f/%.3f", action, x or 0, y or 0)
      end
    end
    local r = pack(original(self, action, x, y, ...))
    return unpack(r, 1, r.n)
  end
end

MAKERS.SearchForFoesIfNeeded = function(original)
  return function(self, ...)
    if on() then
      self.__f2_tr_rr = self.RotateRight
      self.__f2_tr_mho = self.ManualHeightOffset
      self.__f2_tr_man = self.PlayerIsManuallyRotatingCamera
    end
    local r = pack(original(self, ...))
    return unpack(r, 1, r.n)
  end
end

MAKERS.ClearTime = function(original)
  return function(self, ...)
    if on() then
      local my, mdh = self.__f2_my_last or 0, self.__f2_mdh_last or 0
      self.__f2_my_last, self.__f2_mdh_last = 0, 0
      local okf, f = pcall(Timing.GetProgramFrame)
      local okt, tick = pcall(Timing.GetTickRate)
      local p = pending()
      local act = (#p > 0) and table.concat(p, ";") or "-"
      rawset(_G, "__f2_mousetrace_pending", {})
      pcall(cprint, string.format(
        "[f2diag] U f=%s tick=%s act=%s rr=%s srr=%s mho=%s>%s cmho=%s man=%d my=%s mdh=%s",
        okf and tostring(f) or "?", okt and tostring(tick) or "?", act,
        num(self.__f2_tr_rr), num(self.SmoothRotateRight), num(self.__f2_tr_mho),
        num(self.ManualHeightOffset), num(self.CurrentManualHeightOffset),
        self.__f2_tr_man and 1 or 0, num(my), num(mdh)))
    end
    local r = pack(original(self, ...))
    return unpack(r, 1, r.n)
  end
end

local NAMES = { "ProcessGameAction", "SearchForFoesIfNeeded", "ClearTime" }

-- Wrap each method that isn't already one of our wrappers.
local function install()
  for _, name in ipairs(NAMES) do
    local cur = cls[name]
    if type(cur) == "function" and not is_ours(cur) then
      cls[name] = label(MAKERS[name](cur))
    end
  end
  if on() then return say("already on") end
  rawset(_G, "__f2_mousetrace_pending", {})
  rawset(_G, "__f2_mousetrace_on", true)
  if type(getfenv) ~= "function" then
    say("warning: getfenv is missing, so running this again would stack wrappers")
  end
  say("on, via " .. found_as .. " (starts with the next camera update)")
end

local function uninstall()
  if not on() then return say("already off") end
  rawset(_G, "__f2_mousetrace_on", false)
  rawset(_G, "__f2_mousetrace_pending", {})
  say("off")
end

if mode == "on" then
  install()
elseif mode == "off" then
  uninstall()
else
  local live = on() and is_ours(cls.ClearTime)
  say(live and "on" or (on() and "on, but SACCamera was reloaded: run mousetrace on again" or "off"))
end
