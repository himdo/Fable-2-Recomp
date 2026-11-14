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
-- There is no per-frame script: GeneralScriptManager writes its scripts into
-- save games (GetSaveTable / LoadFromSave). The host runs this file again when
-- the mouse moves without reaching the camera (after a camera script reload,
-- for example). Running it again is harmless.
--
-- Saves made with an early version of this branch carry that version's
-- per-frame keeper, and loading one brings it back. It re-wraps the camera
-- every 2 s and takes whatever it finds for the original, so wrapping on top
-- of it makes a loop (the camera update spins until the game runs out of
-- memory, 2026-11-12). Functions restored from a save can't be inspected here
-- (no debug information), so such a keeper can't be found or stopped. Only
-- that keeper sends heartbeats, so the host knows when one is alive: then this
-- file leaves the camera alone, and the old keeper's own wrappers, which use
-- the same host channel, drive the direct mode.
--
-- Recognising our own wrappers. Every wrapper is recorded, with the game's
-- function it calls, in a weak table in the Lua registry, which outlives our
-- globals within a game session. Before wrapping, a method is followed back
-- through our wrappers to the game's function, so a wrapper always calls the
-- game's function and never another wrapper, and running this file again never
-- stacks wrappers. Wrappers restored from a save game can't be recognised at
-- all (see above), which is why the file stays away while an old keeper is
-- alive.

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

-- The weak table of our wrappers. Kept in the Lua registry when the debug
-- library offers it, since that survives a save load clearing the globals.
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

-- The label of one of our wrappers (any copy of this file), or nil.
local function wrapper_info(f)
  if type(f) ~= "function" then return nil end
  return known_table()[f]
end

-- Labels a new wrapper with the game's original.
local function label(wrapper, name, original)
  known_table()[wrapper] = { name = name, orig = original }
  return wrapper
end

-- Loop guard, without pcall (wrapping the game's camera functions in pcall
-- is the main change in the hot path since the runs that worked, and a clean
-- new game then crashed, 2026-11-12). enter() returns false when our wrapper
-- for `name` is entered again while it is still running, i.e. something has
-- put it in a loop: the wrapper then returns a neutral value instead of
-- calling further, and direct mode switches off. The mark is the program
-- frame, so a mark left behind by an error in the game's function is stale by
-- the next frame.
local ACTIVE = {}
local function frame()
  local ok, f = pcall(Timing.GetProgramFrame)
  return ok and f or 0
end
local function enter(name)
  local f = frame()
  if ACTIVE[name] == f then
    if not rawget(_G, "__f2_mousedirect_disabled") then
      rawset(_G, "__f2_mousedirect_disabled", true)
      host("note wrapper loop in " .. name .. ": direct mode switched off")
    end
    return false
  end
  ACTIVE[name] = f
  return true
end
local function leave(name)
  ACTIVE[name] = nil
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
    if not enter("CheckForTeleport") then return end
    local r = pack(original(self, ...))
    leave("CheckForTeleport")
    return unpack(r, 1, r.n)
  end
end

MAKERS.ApplyRotateVector = function(original)
  return function(self, cam_pos, ...)
    if not enter("ApplyRotateVector") then return cam_pos end
    local r = pack(original(self, cam_pos, ...))
    leave("ApplyRotateVector")
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
    if not enter("GetHeight") then return self.__f2_last_height end
    local r = pack(original(self, ...))
    leave("GetHeight")
    if type(r[1]) == "number" then self.__f2_last_height = r[1] end
    return unpack(r, 1, r.n)
  end
end

local NAMES = { "CheckForTeleport", "ApplyRotateVector", "GetHeight" }
-- Follows our labelled wrappers back to the game's own function; nil if the
-- chain doesn't end within 16 steps.
local function unwrap(f)
  for _ = 1, 16 do
    local info = wrapper_info(f)
    if not info then return f end
    f = info.orig
  end
  return nil
end

-- Wraps each method around the game's own function, unless it already holds
-- our current wrapper for exactly that function. The trace (mouse_diag.lua)
-- wraps other methods, so nothing else sits on top of these. Notes every wrap
-- in fable2_mouse.log.
local function ensure()
  if rawget(_G, "__f2_mousedirect_disabled") then return false end
  local cls = find_class()
  if not cls then return false end
  local changed = false
  for _, name in ipairs(NAMES) do
    local cur = cls[name]
    local info = wrapper_info(cur)
    local game = unwrap(cur)
    if not game then
      rawset(_G, "__f2_mousedirect_disabled", true)
      host("note direct mode disabled: can't find the game's " .. name .. " under " .. tostring(cur))
      return changed
    end
    if not (info and info.orig == game) then
      cls[name] = label(MAKERS[name](game), name, game)
      changed = true
      local held = wrapper_info(cls[name]) ~= nil
      host("note wrapped " .. name .. " on " .. tostring(cls) .. " (was " .. tostring(cur)
           .. ", game function " .. tostring(game) .. "; reads back as ours: " .. tostring(held) .. ")")
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

host("note debug.getregistry " .. ((type(debug) == "table" and type(debug.getregistry)) or "nil"))
local ok_old, old = host("old")
if ok_old and old == 1 then
  host("note an older version's keeper is running (from a save game): leaving the camera to it")
else
  ensure()
end
host("boot")
