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

-- The wrappers are made once and look at st.on, so a copy left behind after a
-- reload of this file just passes through.
st.wrap_pga = st.wrap_pga or function(self, action, x, y, ...)
  if st.on and (action == 2 or action == 4) then
    local p = st.pending
    if #p < 16 then
      p[#p + 1] = string.format("%d:%.3f/%.3f", action, x or 0, y or 0)
    end
  end
  return st.orig_pga(self, action, x, y, ...)
end

st.wrap_sff = st.wrap_sff or function(self, ...)
  if st.on then
    self.__f2_tr_rr = self.RotateRight
    self.__f2_tr_mho = self.ManualHeightOffset
    self.__f2_tr_man = self.PlayerIsManuallyRotatingCamera
  end
  return st.orig_sff(self, ...)
end

st.wrap_ct = st.wrap_ct or function(self, ...)
  if st.on then
    local my, mdh = self.__f2_my_last or 0, self.__f2_mdh_last or 0
    self.__f2_my_last, self.__f2_mdh_last = 0, 0
    local okf, f = pcall(Timing.GetProgramFrame)
    local okt, tick = pcall(Timing.GetTickRate)
    local act = (#st.pending > 0) and table.concat(st.pending, ";") or "-"
    st.pending = {}
    pcall(cprint, string.format(
      "[f2diag] U f=%s tick=%s act=%s rr=%s srr=%s mho=%s>%s cmho=%s man=%d my=%s mdh=%s",
      okf and tostring(f) or "?", okt and tostring(tick) or "?", act,
      num(self.__f2_tr_rr), num(self.SmoothRotateRight), num(self.__f2_tr_mho),
      num(self.ManualHeightOffset), num(self.CurrentManualHeightOffset),
      self.__f2_tr_man and 1 or 0, num(my), num(mdh)))
  end
  return st.orig_ct(self, ...)
end

local function wrap(name, orig_key, wrapper_key)
  if cls[name] ~= st[wrapper_key] then
    st[orig_key] = cls[name]
    cls[name] = st[wrapper_key]
  end
end

local function install()
  if st.on and cls.ClearTime == st.wrap_ct then return say("already on") end
  st.pending = {}
  wrap("ProcessGameAction", "orig_pga", "wrap_pga")
  wrap("SearchForFoesIfNeeded", "orig_sff", "wrap_sff")
  wrap("ClearTime", "orig_ct", "wrap_ct")
  st.on = true
  st.installed = true
  say("on, via " .. found_as .. " (starts with the next camera update)")
end

local function uninstall()
  if not st.on then return say("already off") end
  st.on = false
  st.installed = false
  st.pending = {}
  -- Restore only where the class still holds our wrapper (after a camera
  -- script reload it has fresh functions and the wrappers are already gone).
  if cls.ProcessGameAction == st.wrap_pga then cls.ProcessGameAction = st.orig_pga end
  if cls.SearchForFoesIfNeeded == st.wrap_sff then cls.SearchForFoesIfNeeded = st.orig_sff end
  if cls.ClearTime == st.wrap_ct then cls.ClearTime = st.orig_ct end
  say("off")
end

if mode == "on" then
  install()
elseif mode == "off" then
  uninstall()
else
  local live = st.on and cls.ClearTime == st.wrap_ct
  say(live and "on" or (st.on and "on, but SACCamera was reloaded: run mousetrace on again" or "off"))
end
