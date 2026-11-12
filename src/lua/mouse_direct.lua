-- mouse_direct.lua: direct mouse camera (see src/input/mouse_direct.h).
--
-- The host runs this file once a save is loaded (mouse_look_mode = "direct").
-- It wraps three methods of SACCamera, the main gameplay camera. SACCamera's
-- FrameUpdate calls each of them by name through self, so the wrappers take
-- effect on the next camera update and also cover the cameras built on it:
--
--   CheckForTeleport   the first call in FrameUpdate. Asks the host for the
--                      mouse movement since the last update (cprint
--                      "[f2mouse] 1", or "0" when the camera can't be turned:
--                      target lock, camera rotation disabled). The host
--                      returns the yaw in degrees and the height change.
--                      Marks the update as a manual turn, as the stick does,
--                      so auto-alignment and follow don't pull against it.
--   ApplyRotateVector  after the game applies its own (smoothed) stick turn,
--                      turns the camera further about the same focal point by
--                      the mouse yaw. The game stores the result as the next
--                      update's starting position, so the turn just becomes
--                      where the camera is: nothing here remembers an angle.
--   GetHeight          adds the mouse height to the script's height offset
--                      and to its smoothed copy, so it applies at once, before
--                      the game clamps it to its limits (shared with the
--                      stick) and lets it drift back to the middle.
--
-- The stick is untouched: it still goes through ProcessGameAction, its top
-- speed and its smoothing, and the mouse adds to it.
--
-- A per-frame script (GeneralScriptManager, like console_pump.lua) re-wraps the
-- class if the camera scripts are reloaded and sends the host a heartbeat.
-- Running this file again is harmless.
--
-- Which functions are ours, and the originals they call, is kept on the class
-- table itself (cls.__f2_mousedirect), not in a global. Loading a save can
-- clear our globals while SACCamera and an older keeper survive; with the
-- bookkeeping in a global, a second copy took the first copy's wrappers for the
-- game's originals and the two wrapped each other in turns, so every camera
-- update recursed until Lua gave up and the game froze (2026-11-12). Now any
-- copy recognises the wrappers on the class, and only the newest keeper runs.

local function host(s)
  return pcall(cprint, "[f2mouse] " .. s)
end

local function pack(...)
  return { n = select("#", ...), ... }
end

local function find_class()
  local ok, c = pcall(function() return SACCamera end)
  if ok and type(c) == "table" and type(c.CheckForTeleport) == "function"
     and type(c.ApplyRotateVector) == "function" and type(c.GetHeight) == "function" then
    return c
  end
  return nil
end

-- The same conditions under which ProcessGameAction lets the stick turn the
-- camera. During target lock the stick switches targets instead, so the mouse
-- goes back to the stick there (the host routes it).
local function can_turn(self)
  local okl, locking = pcall(function() return self:IsTargetLocking() end)
  if okl and locking then return false end
  local okc, can = pcall(function() return GetCanRotateCamera() end)
  if not okc and not rawget(_G, "__f2_mousedirect_warned_gate") then
    rawset(_G, "__f2_mousedirect_warned_gate", true)
    host("note GetCanRotateCamera failed: " .. tostring(can))
  end
  local okp, photo = pcall(function() return CameraManager.GetCameraPhotoMode() end)
  if not ((okc and can) or (okp and photo)) then return false end
  local cam = self.Camera
  if not cam then return false end
  local oka, alive = pcall(function() return cam:IsAlive() end)
  return (oka and alive) and true or false
end

-- The in-game camera sensitivity scales the stick by (0.5 + sens); the mouse
-- follows the same slider.
local function sensitivity(self)
  local ok, s = pcall(function() return Player.GetCameraSensitivity(self.Player) end)
  if ok and type(s) == "number" then return 0.5 + s end
  return 1
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local NAMES = { "CheckForTeleport", "ApplyRotateVector", "GetHeight" }

-- The wrappers for one class, made once per class table and kept on it. Each
-- calls the original recorded next to it, so a wrapper never depends on which
-- copy of this file made it.
local function make_wrappers(w)
  local orig = w.orig
  w.wrap.CheckForTeleport = function(self, ...)
    local accept = can_turn(self)
    local ok, yaw, dh = host(accept and "1" or "0")
    if accept and ok and type(yaw) == "number" and type(dh) == "number" and (yaw ~= 0 or dh ~= 0) then
      local f = sensitivity(self)
      -- Added, not set: nothing is lost if this runs twice before it's applied.
      self.__f2_my = (self.__f2_my or 0) + yaw * f
      self.__f2_mdh = (self.__f2_mdh or 0) + dh * f
      -- As the stick does for a turn: auto-alignment and follow hold off.
      if yaw ~= 0 then self.PlayerIsManuallyRotatingCamera = true end
    end
    return orig.CheckForTeleport(self, ...)
  end

  w.wrap.ApplyRotateVector = function(self, cam_pos, ...)
    local r = pack(orig.ApplyRotateVector(self, cam_pos, ...))
    local yaw = self.__f2_my
    self.__f2_my = 0
    self.__f2_my_last = yaw or 0  -- for the camera trace (mouse_diag.lua)
    local focal = self.PreBoundFocalPos
    if yaw and yaw ~= 0 and r[1] and focal then
      local ok, out = pcall(function()
        local v = r[1] - focal
        v:RotateAroundDirectionByAngle(CVector3(0, 0, 1), yaw)
        return focal + v
      end)
      if ok and out then
        r[1] = out
      elseif not w.warned_arv then
        w.warned_arv = true
        host("note yaw failed: " .. tostring(out))
      end
    end
    return unpack(r, 1, r.n)
  end

  w.wrap.GetHeight = function(self, ...)
    local dh = self.__f2_mdh
    self.__f2_mdh = 0
    self.__f2_mdh_last = dh or 0  -- for the camera trace (mouse_diag.lua)
    if dh and dh ~= 0 then
      local hi = self.HeightOffsetUpperBound or 0.7
      local lo = -(self.HeightOffsetLowerBound or 0.7)
      self.ManualHeightOffset = (self.ManualHeightOffset or 0) + dh  -- clamped by the original
      if self.CurrentManualHeightOffset then
        self.CurrentManualHeightOffset = clamp(self.CurrentManualHeightOffset + dh, lo, hi)
      end
    end
    return orig.GetHeight(self, ...)
  end
end

-- Wraps the class. A method that holds our wrapper is left alone; anything
-- else is the game's function (first run, or the camera scripts were reloaded
-- into this table) and becomes the original. The trace (mouse_diag.lua) wraps
-- other methods, so nothing else sits on top of these. Returns true when
-- something was (re)wrapped.
local function ensure()
  local cls = find_class()
  if not cls then return false end
  local w = rawget(cls, "__f2_mousedirect")
  if type(w) ~= "table" then
    w = { orig = {}, wrap = {} }
    rawset(cls, "__f2_mousedirect", w)
  end
  if not w.wrap.CheckForTeleport then make_wrappers(w) end
  local changed = false
  for _, name in ipairs(NAMES) do
    local cur = cls[name]
    if cur ~= w.wrap[name] then
      w.orig[name] = cur
      cls[name] = w.wrap[name]
      changed = true
    end
  end
  return changed
end

-- Per-frame keeper. Only the newest one runs: each start replaces the token in
-- the global, and a keeper whose token is gone (replaced, or cleared with the
-- globals by a load) stops.
local function start_keeper()
  local token = {}
  rawset(_G, "__f2_mousedirect_keeper", token)
  local function update()
    local n = 0
    while rawget(_G, "__f2_mousedirect_keeper") == token do
      n = n + 1
      if n % 30 == 1 then
        if ensure() then host("note camera wrapped") end
        host("hb")
      end
      coroutine.yield()
    end
  end
  local keeper = {}
  setmetatable(keeper, keeper)
  keeper.__index = _G
  keeper._G = _G
  keeper.Update = update
  GeneralScriptManager.AddScript(keeper)
end

local ok, err = pcall(start_keeper)
if not ok then host("note keeper failed: " .. tostring(err)) end
if ensure() then host("note camera wrapped") end
host("boot")
