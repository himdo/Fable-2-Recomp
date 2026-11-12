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
-- Recognising our own wrappers. Each wrapper carries its own label: its
-- function environment holds a marker with the name and the game's original,
-- so any copy of this file, in any state, can tell one of ours by looking at
-- the function itself, and every wrapper calls the game's real function, never
-- another wrapper.
--
-- Two earlier attempts kept this bookkeeping elsewhere and froze the game
-- (2026-11-12). In a global, a save load cleared it while an older keeper
-- survived: two copies wrapped each other's wrappers. On the class table, the
-- record didn't stick (SACCamera as seen through the globals is not a stable
-- table), so a new wrapper went on top of the last one every pass until the
-- chain pointed back at itself. Wrappers also no longer tail-call the
-- original: if a loop ever forms again, Lua runs out of stack and reports an
-- error instead of the camera update spinning forever.

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

local MARK = "__f2_mousedirect_wrapper"

-- A weak table of our wrappers, as a fallback where getfenv/setfenv are
-- missing. Kept in the Lua registry when the debug library offers it, since
-- that survives a save load clearing the globals.
local function known_table()
  local reg = nil
  if type(debug) == "table" and type(debug.getregistry) == "function" then
    local ok, r = pcall(debug.getregistry)
    if ok and type(r) == "table" then reg = r end
  end
  reg = reg or _G
  local k = rawget(reg, MARK)
  if type(k) ~= "table" then
    k = setmetatable({}, { __mode = "k" })
    rawset(reg, MARK, k)
  end
  return k
end

-- The label of one of our wrappers (any copy), or nil for anything else.
local function wrapper_info(f)
  if type(f) ~= "function" then return nil end
  if type(getfenv) == "function" then
    local ok, env = pcall(getfenv, f)
    if ok and type(env) == "table" then
      local info = rawget(env, MARK)
      if type(info) == "table" then return info end
    end
  end
  return known_table()[f]
end

-- Labels a new wrapper with the game's original.
local function label(wrapper, name, original)
  local info = { name = name, orig = original }
  if type(setfenv) == "function" and type(getfenv) == "function" then
    local env = setmetatable({ [MARK] = info }, { __index = getfenv(1) })
    pcall(setfenv, wrapper, env)
  end
  known_table()[wrapper] = info
  return wrapper
end

-- One wrapper per method, around the game's own function `original`.
local MAKERS = {}

MAKERS.CheckForTeleport = function(original)
  return function(self, ...)
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
    local r = pack(original(self, ...))
    return unpack(r, 1, r.n)
  end
end

MAKERS.ApplyRotateVector = function(original)
  return function(self, cam_pos, ...)
    local r = pack(original(self, cam_pos, ...))
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
      elseif not rawget(_G, "__f2_mousedirect_warned_arv") then
        rawset(_G, "__f2_mousedirect_warned_arv", true)
        host("note yaw failed: " .. tostring(out))
      end
    end
    return unpack(r, 1, r.n)
  end
end

MAKERS.GetHeight = function(original)
  return function(self, ...)
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
    local r = pack(original(self, ...))
    return unpack(r, 1, r.n)
  end
end

local NAMES = { "CheckForTeleport", "ApplyRotateVector", "GetHeight" }

-- Wraps each method that isn't already one of our wrappers (from any copy of
-- this file). The trace (mouse_diag.lua) wraps other methods, so nothing else
-- sits on top of these. Returns true when something was wrapped, and notes
-- the details in fable2_mouse.log.
local function ensure()
  if rawget(_G, "__f2_mousedirect_disabled") then return false end
  local cls = find_class()
  if not cls then return false end
  local changed = false
  for _, name in ipairs(NAMES) do
    local cur = cls[name]
    if type(cur) == "function" and not wrapper_info(cur) then
      cls[name] = label(MAKERS[name](cur), name, cur)
      changed = true
      local held = wrapper_info(cls[name]) ~= nil
      host("note wrapped " .. name .. " on " .. tostring(cls) .. " (was " .. tostring(cur)
           .. "; reads back as ours: " .. tostring(held) .. ")")
      if not held then
        -- Can't tell our wrappers apart here, so wrapping again later could
        -- stack them. Stop: without camera updates asking for movement, the
        -- host sends the mouse to the stick.
        rawset(_G, "__f2_mousedirect_disabled", true)
        host("note direct mode disabled: the wrapper doesn't read back as ours")
        return changed
      end
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
        ensure()
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
host("note getfenv " .. type(getfenv) .. ", setfenv " .. type(setfenv) .. ", debug.getregistry "
     .. ((type(debug) == "table" and type(debug.getregistry)) or "nil"))
ensure()
host("boot")
