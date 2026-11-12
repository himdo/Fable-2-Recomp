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

local st = rawget(_G, "__f2_mousedirect")
if not st then
  st = { gen = 0 }
  rawset(_G, "__f2_mousedirect", st)
end

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
  if not okc and not st.warned_gate then
    st.warned_gate = true
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

st.w_cft = st.w_cft or function(self, ...)
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
  return st.o_cft(self, ...)
end

st.w_arv = st.w_arv or function(self, cam_pos, ...)
  local r = pack(st.o_arv(self, cam_pos, ...))
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
    elseif not st.warned_arv then
      st.warned_arv = true
      host("note yaw failed: " .. tostring(out))
    end
  end
  return unpack(r, 1, r.n)
end

st.w_gh = st.w_gh or function(self, ...)
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
  return st.o_gh(self, ...)
end

-- Wraps the class, keeping the current methods as the originals. A method that
-- is already our wrapper is left alone, so running this again never wraps a
-- wrapper. Returns true when something was (re)wrapped.
local function ensure()
  local cls = find_class()
  if not cls then return false end
  local changed = false
  if cls.CheckForTeleport ~= st.w_cft then
    st.o_cft, cls.CheckForTeleport, changed = cls.CheckForTeleport, st.w_cft, true
  end
  if cls.ApplyRotateVector ~= st.w_arv then
    st.o_arv, cls.ApplyRotateVector, changed = cls.ApplyRotateVector, st.w_arv, true
  end
  if cls.GetHeight ~= st.w_gh then
    st.o_gh, cls.GetHeight, changed = cls.GetHeight, st.w_gh, true
  end
  return changed
end

-- Per-frame keeper. A newer copy (after this file runs again) replaces an
-- older one through st.gen.
local function frame_now()
  local ok, f = pcall(Timing.GetProgramFrame)
  return ok and type(f) == "number" and f or nil
end

local function start_keeper()
  st.gen = st.gen + 1
  local gen = st.gen
  local function update()
    local n = 0
    while st.gen == gen do
      n = n + 1
      st.keeper_frame = frame_now()
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

-- Start a keeper unless one ran in the last few seconds of game frames.
local now = frame_now()
local fresh = st.keeper_frame and now and (now - st.keeper_frame) < 300
if not fresh then
  local ok, err = pcall(start_keeper)
  if not ok then host("note keeper failed: " .. tostring(err)) end
end
if ensure() then host("note camera wrapped") end
host("boot")
