-- cullBehindHero.lua: directional culling.
--
-- Stops the renderer from drawing buildings that are BEHIND the hero's camera
-- view cone, so the world only renders in front of you. Turning the camera
-- around re-draws the buildings that come into the forward cone.
--
-- How it works (each frame, while enabled):
--   1. Read the camera eye position + forward vector.
--   2. Enumerate the level's buildings (the engine's own "building" search).
--   3. For each building within `radius`, test the angle between the forward
--      vector and the (building - camera) direction. If it is wider than
--      `coneDeg` (i.e. behind the view cone) mark it non-drawable.
--
-- It uses the engine's own drawable flag (GraphicAppearance.SetAsDrawable), so
-- the renderer genuinely skips those buildings (no vertex/shader work) rather
-- than hiding them with a material. State is only written when it actually
-- flips, so sweeping the camera only touches the buildings that cross the cone.
-- Building positions are static and are parsed once (cached) per building.
--
-- Run this script (or the F5 "Toggle Behind-Cull" entry) to flip it ON/OFF.
-- A small status HUD is drawn on screen while enabled (set __cull_state.showHud
-- = false to hide it).
--
-- The persistent per-frame coroutine is registered ONCE (a global guard), but
-- it always dispatches to the latest implementation stored in __cull_internals,
-- so re-running this script updates behavior on the next frame WITHOUT a game
-- restart.
--
-- Shared state / knobs:  __cull_state
--   enabled     bool  cull on/off
--   coneDeg     num   half-angle of the keep cone (180 = keep everything)
--   radius      num   only cull buildings within this distance of the camera
--   nearRadius  num   always keep buildings closer than this (no popping)
--   searchType  str   SearchTools category to cull (default "building")
--   refreshEvery int  re-run the spatial search every N frames (1 = every frame)
--   showHud     bool  draw the on-screen status while enabled

local st = rawget(_G, "__cull_state")
local internals = rawget(_G, "__cull_internals")
if not st then
  st = {
    enabled = false,      -- start OFF; flip by running this script (F5)
    coneDeg = 100,        -- half-angle of the fully-visible cone (degrees)
    fadeDeg = 25,         -- fade band: fully visible inside coneDeg-fadeDeg,
                          -- fully hidden at/behind coneDeg
    radius = 180,         -- only cull buildings within this distance
    nearRadius = 6,       -- always keep buildings closer than this
    searchType = "building",
    refreshEvery = 1,
    showHud = false,      -- Debug.DrawText is disabled in this build; keep off
  }
  rawset(_G, "__cull_state", st)
end
if not internals then
  internals = {
    poscache = {},  -- [entity] = {x, y, z}   (buildings are static)
    state = {},     -- [entity] = bool        (last drawable state we set)
    frame = 0,
    stats = { frames = 0, found = 0, kept = 0, culled = 0, err = nil },
  }
  rawset(_G, "__cull_internals", internals)
end
local stats = internals.stats

local hero = function()
  if type(QuestManager) == "table" then
    local h = QuestManager.HeroEntity
    if h and type(h) == "userdata" then return h end
  end
  if type(GetPlayerHero) == "function" then
    local ok, h = pcall(GetPlayerHero)
    if ok and type(h) == "userdata" then return h end
  end
  return nil
end

-- Camera eye (ex,ey,ez) + normalized forward (fx,fy,fz). nil if unavailable.
local camera_eye_forward = function(h)
  local ok1, pos = pcall(Player.GetPositionOfCameraAssociatedWithPlayer, h)
  local ok2, fwd = pcall(Player.GetForwardOfCameraAssociatedWithPlayer, h)
  if not (ok1 and ok2 and pos and fwd) then return nil end
  local ex, ey, ez = pos:GetX(), pos:GetY(), pos:GetZ()
  local fx, fy, fz = fwd:GetX(), fwd:GetY(), fwd:GetZ()
  local fl = math.sqrt(fx * fx + fy * fy + fz * fz)
  if fl < 1e-5 then return nil end
  return ex, ey, ez, fx / fl, fy / fl, fz / fl
end

-- One cull pass: fade each building's opacity by how far it is behind the
-- view cone. Uses SetCameraAlpha -- the engine's per-frame camera-fade channel
-- (FadeUpEntitiesIfNeeded re-asserts it every frame), so the render re-reads it
-- continuously and the effect is fully reversible. (SetAsDrawable hides but
-- does not re-show; SetAlpha is a one-shot material alpha.)
-- a = 1 (opaque) inside `coneDeg-fadeDeg`, a = 0 (invisible) at/behind
-- `coneDeg`, and linearly faded in between. Reasserted every frame so it
-- tracks the camera in real time and is trivially undone.
local function do_cull()
  stats.found, stats.hidden, stats.visible = 0, 0, 0

  local h = hero()
  if not h then stats.err = "no hero entity"; return end
  local ex, ey, ez, fx, fy, fz = camera_eye_forward(h)
  if not ex then stats.err = "no camera pos/forward"; return end
  stats.err = nil
  stats.eye, stats.fwd = { ex, ey, ez }, { fx, fy, fz }

  local ok, search = pcall(SearchTools.StartNewSearch, st.searchType)
  if not ok or not search then stats.err = "StartNewSearch: " .. tostring(search); return end
  local fok, fres = pcall(SearchTools.FilterWithinDistanceOfPos, search,
      CVector3(ex, ey, ez), st.radius)
  if not fok then stats.err = "FilterWithinDistanceOfPos: " .. tostring(fres); return end
  if type(fres) == "userdata" then search = fres end -- chains the handle when it does
  local rok, results = pcall(SearchTools.GetSearchResults, search)
  if not rok then stats.err = "GetSearchResults: " .. tostring(results); return end
  if type(results) ~= "table" then stats.err = "GetSearchResults not a table"; return end

  local fade = (st.fadeDeg and st.fadeDeg > 0) and st.fadeDeg or 0
  local outerCos = math.cos(math.rad(st.coneDeg))
  local innerCos = math.cos(math.rad(st.coneDeg - fade))
  local span = innerCos - outerCos -- > 0 when fade > 0
  local nr2 = st.nearRadius * st.nearRadius
  local n = 0
  local nearestD2, nearestDir = math.huge, nil
  for _, e in pairs(results) do
    n = n + 1
    local p = internals.poscache[e]
    if not p then
      local pok, pos = pcall(function() return e:GetPosition() end)
      if pok and pos then
        p = { pos:GetX(), pos:GetY(), pos:GetZ() }
        internals.poscache[e] = p
      end
    end
    if p then
      local dx, dy, dz = p[1] - ex, p[2] - ey, p[3] - ez
      local d2 = dx * dx + dy * dy + dz * dz
      local a
      if d2 < nr2 or d2 < 1e-8 then
        a = 1
      else
        local d = math.sqrt(d2)
        local cosang = (dx * fx + dy * fy + dz * fz) / d
        if cosang >= innerCos then
          a = 1
        elseif span <= 1e-6 or cosang <= outerCos then
          a = 0
        else
          a = (cosang - outerCos) / span
        end
      end
      pcall(GraphicAppearance.SetCameraAlpha, e, a)
      internals.state[e] = a
      if a < 0.5 then stats.hidden = stats.hidden + 1 else stats.visible = stats.visible + 1 end
      if d2 < nearestD2 then
        nearestD2 = d2
        local nd = math.sqrt(d2) or 1e-6
        nearestDir = { dx / nd, dy / nd, dz / nd }
      end
    end
  end
  stats.found = n
  stats.nearestDir = nearestDir
  stats.nearestDist = (nearestD2 < math.huge) and math.sqrt(nearestD2) or nil
end

-- Restore every building in the level to fully opaque (used on disable).
-- Uses a FRESH search so the handle is valid, and sets the whole level so
-- nothing we faded stays hidden.
local function restore_all()
  local ok, search = pcall(SearchTools.StartNewSearch, st.searchType)
  if ok and search then
    local ok2, results = pcall(SearchTools.GetSearchResults, search)
    if type(results) == "table" then
      for _, e in pairs(results) do
        pcall(GraphicAppearance.SetCameraAlpha, e, 1)
      end
    end
  end
  internals.state = {}
  internals.poscache = {}
  internals.frame = 0
end

-- On-screen status (Debug.DrawText is screen-space 2D text).
local function draw_hud()
  local lines = {
    "[cull] " .. (st.enabled and "ON" or "OFF") .. "  updFrames=" .. tostring(stats.frames),
    "found=" .. tostring(stats.found) .. "  visible=" .. tostring(stats.visible)
        .. "  hidden=" .. tostring(stats.hidden),
    "cone=" .. tostring(st.coneDeg) .. "  radius=" .. tostring(st.radius)
        .. "  search=" .. tostring(st.searchType),
  }
  if stats.fwd then
    lines[#lines + 1] = string.format("eye=(%.0f,%.0f,%.0f) fwd=(%.2f,%.2f,%.2f)",
        stats.eye[1], stats.eye[2], stats.eye[3],
        stats.fwd[1], stats.fwd[2], stats.fwd[3])
  end
  if stats.err then lines[#lines + 1] = "ERR: " .. tostring(stats.err) end
  local y = 8
  for _, s in ipairs(lines) do
    pcall(Debug.DrawText, s, CI32Vector2(8, y))
    y = y + 18
  end
end

-- Expose the latest implementations so the per-frame driver always calls the
-- newest code, even after this file re-runs without a restart.
internals.do_cull = do_cull
internals.restore_all = restore_all
internals.draw_hud = draw_hud

-- Per-frame body. IMPORTANT: this is driven by F5.lua's persistent per-frame
-- script (see the "behind-cull tick" hook there). Registering a SEPARATE
-- GeneralScriptManager script from inside F5's own coroutine proved NOT to be
-- picked up by the engine, so we piggyback on F5's known-running loop instead.
internals.prev_level = internals.prev_level or nil
local function tick()
  stats.frames = stats.frames + 1
  if st.enabled then
    stats.enabledFrames = (stats.enabledFrames or 0) + 1
    -- Reset caches when the loaded level changes (new building set).
    local ok, _w, level = pcall(GetLevelLoaded)
    if ok and level ~= internals.prev_level then
      internals.prev_level = level
      internals.poscache = {}
      internals.state = {}
    end
    internals.frame = internals.frame + 1
    local ref = (st.refreshEvery and st.refreshEvery > 0) and st.refreshEvery or 1
    if internals.frame % ref == 1 then
      -- pcall the whole pass so a one-off engine error can't kill the F5 loop.
      local cok, cerr = pcall(internals.do_cull)
      if not cok then stats.err = "do_cull: " .. tostring(cerr) end
    end
    if st.showHud then
      local hok, herr = pcall(internals.draw_hud)
      if not hok then stats.err = "hud: " .. tostring(herr) end
    end
  elseif next(internals.state) ~= nil then
    -- Just turned off: undo the cull once, then stop touching the caches.
    pcall(internals.restore_all)
    stats.err = nil
  end
end
internals.tick = tick

-- Each run of this script toggles the cull and reports diagnostics in a
-- message box (Debug.DrawText may be disabled in this build, so the box is
-- the reliable channel).
if st.enabled then
  -- Turning OFF: restore culled buildings immediately + report loop liveness.
  st.enabled = false
  pcall(restore_all)
  local ef = stats.enabledFrames or 0
  local verdict
  if ef > 60 then
    verdict = "LOOP RUNNING (enabledFrames=" .. tostring(ef) .. ")"
  else
    verdict = "LOOP NOT RUNNING (enabledFrames=" .. tostring(ef)
        .. ")\n-> restart the game so the NEW F5.lua registers the per-frame hook"
  end
  local diag = ""
  if stats.fwd and stats.nearestDir then
    local f, n = stats.fwd, stats.nearestDir
    local dot = f[1] * n[1] + f[2] * n[2] + f[3] * n[3]
    local deg = math.deg(math.acos(math.min(1, math.max(-1, dot))))
    diag = string.format("\nfwd=(%.2f,%.2f,%.2f)", f[1], f[2], f[3])
        .. string.format("\nnear=(%.2f,%.2f,%.2f) dist=%.0f", n[1], n[2], n[3], stats.nearestDist or 0)
        .. string.format("\nangle=%.0f  %s", deg, dot > 0 and "FORWARD toward building" or "FORWARD AWAY from building")
  end
  pcall(GUI.DisplayMessageBox,
      "Behind-cull: OFF\n" .. verdict
      .. "\nvisible=" .. tostring(stats.visible or 0)
      .. "  hidden=" .. tostring(stats.hidden or 0)
      .. diag)
else
  -- Turning ON: run one pass NOW for immediate feedback, then report counts.
  st.enabled = true
  stats.enabledFrames = 0
  local ok, err = pcall(do_cull)
  local msg = "Behind-cull: ON\ncone=" .. tostring(st.coneDeg) .. " fade=" .. tostring(st.fadeDeg)
      .. " radius=" .. tostring(st.radius)
  if ok then
    msg = msg .. "\nfound=" .. tostring(stats.found)
        .. " visible=" .. tostring(stats.visible)
        .. " hidden=" .. tostring(stats.hidden)
    if stats.err then msg = msg .. "\n" .. stats.err end
  else
    msg = msg .. "\nERR " .. tostring(err)
  end
  pcall(GUI.DisplayMessageBox, msg)
end
