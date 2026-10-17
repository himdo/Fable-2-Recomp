-- toggleFreecam.lua: toggle the game's freecam on/off.
--
-- Run directly, or via the F5 selector menu (F5.lua).
--
-- Freecam is a camera mode (ECameraMode.CAMERA_FREE) the CameraManager
-- activates when the "use free camera" flag is set. That flag is driven by
-- the engine's C++ Debug functions:
--   * Debug.GetUseFreeCamera()  -> current state (true = on, false = off)
--   * Debug.SetUseFreeCamera(v) -> set the state
--
-- We call those directly instead of Debug.ToggleFreeCam, because ToggleFreeCam
-- is itself just a Lua wrapper (defined in scripts/camera/cameramanager.lua)
-- around SetUseFreeCamera(not GetUseFreeCamera()); using the engine functions
-- keeps this working even if that script hasn't run. Each press flips the
-- flag; the resulting state is echoed in a message box.

local ok, is_on = pcall(Debug.GetUseFreeCamera)
local new_state
if ok and is_on ~= nil then
  new_state = not is_on
else
  new_state = true -- unknown state: enable
end
pcall(Debug.SetUseFreeCamera, new_state)
pcall(GUI.DisplayMessageBox, (new_state and "Freecam: ON" or "Freecam: OFF"))
