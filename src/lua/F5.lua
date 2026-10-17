-- F5 selector menu.
--
-- Pressing F5 (host keyboard) re-runs this file. It opens a native menu box
-- listing available Lua scripts; pick one with up/down + A to run it.
--
-- Implementation model (matches the in-game Debug Menu / MultipageMenu):
-- a SINGLE persistent per-frame script is registered with the
-- GeneralScriptManager. The top-level below runs on every F5 press, but it
-- only registers the script the FIRST time; each subsequent press just sets a
-- "show" flag that the per-frame coroutine picks up on the next frame. This
-- avoids re-registering a new script (and re-entering the menu box) on every
-- press, which is what crashed the game on the second F5.
--
-- The menu box (GUI.DisplayMenuBox) is, in the words of the game's own
-- MultipageMenu.lua, "honestly terrible to use" and has a known bug where an
-- item that was hovered when a box closed stays highlighted if the box is
-- reopened too soon, and where passing a nil arg (vs. "nothing") crashes. To
-- stay in the safe zone we replicate the game's exact idiom:
--   * prepend a "Leave Menu" entry (the box always has >= 1 entry and the
--     processor's item array is never left with a shifted/short tail),
--   * pad the page up to the full 5 slots with "NONE" so the item array the
--     per-frame processor iterates is always complete (no trailing nils),
--   * wait for the teardown/animation to finish after close before allowing a
--     re-open (the game's 0.45 s slowmode, plus our message-ID re-anchor).
-- Cycling the box then no longer leaves a dangling item pointer that the
-- per-frame "process items" pass dereferences (which was the null read that
-- aborted the game).
--
-- To add a script, add { name = "...", path = "..." } to F5_MENU_ITEMS.
-- `path` is relative to the game VFS root (data/), forward slashes, e.g.
-- "scripts/recomp/myScript.lua". The file must also be a line in
-- data/dir.manifest (the build auto-maintains that for src/lua/*.lua).

local F5_MENU_ITEMS = {
  { name = "Get Player Position", path = "scripts/recomp/getPlayerPos.lua" },
  { name = "Modify Money", path = "scripts/recomp/modifyMoney.lua" },
  { name = "Morality / Purity", path = "scripts/recomp/modifyMoralityPurity.lua" },
  { name = "Toggle Freecam", path = "scripts/recomp/toggleFreecam.lua" },
}

-- Run a script from the game VFS. The game's custom loadfile resolves paths
-- relative to the data/ root (io is disabled), so there is no "data/" prefix.
-- Returns (chunk, nil) on success or (nil, msg) when not found / compile error.
local function run_script(path)
  local chunk, err = loadfile(path)
  if type(chunk) ~= "function" then
    local why = (err and err ~= "") and tostring(err)
        or "file not found in the game VFS (is it in data/dir.manifest?)"
    pcall(GUI.DisplayMessageBox, "F5: could not load " .. path .. "\n" .. why)
    return
  end
  -- Run the chunk directly, NOT inside pcall(): a script may coroutine.yield()
  -- to run across frames (e.g. modifyMoney.lua's sub-menu), and a yield cannot
  -- cross a C-call boundary — pcall is one. Errors in the chunk propagate to
  -- the update() coroutine (the GeneralScriptManager then logs + terminates it);
  -- the scripts guard their game API calls with pcall individually, so a normal
  -- script run should not raise.
  chunk()
end

-- Shared state that persists across F5 presses (the game uses one Lua state,
-- so a global table survives across separate runs of this file).
local st = rawget(_G, "__f5_menu_state")
if not st then
  st = { show = false, registered = false, busy = false }
  rawset(_G, "__f5_menu_state", st)
end
-- Ignore an F5 press while any native modal (the menu box OR a message box
-- from the chosen script) is up or mid-teardown: re-opening a box on top of
-- a live one (and re-running the script loader mid-modal) is what
-- hard-crashed the game. The press is simply dropped.
if not st.busy then
  st.show = true -- this F5 press requests the menu
end

if not st.registered then
  st.registered = true

  -- The menu box can show at most 5 entries per page (6 args incl. title).
  -- We build a full 5-slot page (never a short one) so the game's per-frame
  -- "process items" pass always sees a complete item array.
  local PAGE_SIZE = 5
  local LEAVE_LABEL = "Leave Menu"
  local PAD_LABEL = "NONE"

  -- Build the full 5-slot page of entry strings: slot 1 is the "Leave Menu"
  -- entry, slots 2..N are our scripts, remaining slots are "NONE". Always
  -- returns exactly PAGE_SIZE non-nil strings.
  local function build_page()
    local page = {}
    page[1] = LEAVE_LABEL
    for i = 2, PAGE_SIZE do
      local item = F5_MENU_ITEMS[i - 1]
      page[i] = (item and item.name) or PAD_LABEL
    end
    return page -- exactly PAGE_SIZE entries, none nil
  end

  -- Open the native menu box and wait for the selection. Returns the 1-based
  -- F5_MENU_ITEMS index chosen, or 0 if cancelled / "Leave Menu". Uses the
  -- game's idiom: capture the most recent message id BEFORE opening, then poll
  -- for the MENUBOX message; yield once more after the selection so the menu
  -- box fully closes first.
  --
  -- IMPORTANT: NOT wrapped in pcall by the caller. LuaPlus is Lua 5.1, where
  -- a coroutine.yield() cannot propagate through pcall (it becomes a
  -- "yield across C boundary" error), so the yield loop must live directly in
  -- the coroutine. Each individual (non-yielding) API call is pcall'd and
  -- errors degrade to a message box + return 0.
  local function menu_fail(what)
    pcall(GUI.DisplayMessageBox, "F5: menu error: " .. tostring(what))
    return 0
  end
  local function open_menu()
    st.busy = true
    local page = build_page()
    -- args = title + exactly PAGE_SIZE entries (no nils, full array).
    local args = { "F5 Menu" }
    for i = 1, PAGE_SIZE do args[i + 1] = page[i] end
    local ok, last_id = pcall(MessageEvents.GetMostRecentMessageID)
    if not ok or last_id == nil then return menu_fail(last_id) end
    local ok2, disperr = pcall(GUI.DisplayMenuBox, unpack(args, 1, #args))
    if not ok2 then return menu_fail(disperr) end
    while true do
      local ok3, posted, message = pcall(
          MessageEvents.IsMessagePosted, EMessageEventType.MESSAGE_EVENT_MENUBOX,
          last_id)
      if not ok3 then return menu_fail(posted) end
      if posted then
        local okc, choice = pcall(function()
          return message:GetExtraDataAsNumber()
        end)
        coroutine.yield() -- let the menu box finish closing.
        st.busy = false
        if not okc then return menu_fail(choice) end
        -- choice is the 1-based box entry index: 1 = "Leave Menu" (cancel),
        -- 2..PAGE_SIZE = F5_MENU_ITEMS[1..PAGE_SIZE-1]. 0 = B (cancel).
        if choice == 0 or choice == 1 then return 0 end
        local idx = choice - 1
        if idx >= 1 and idx <= #F5_MENU_ITEMS then return idx end
        return 0
      end
      coroutine.yield()
    end
  end

  -- Message-ID helpers. GetMostRecentMessageID() is a monotonic counter over
  -- the game's message pipeline; every box (menu box, message box) posts a
  -- message when it is dismissed, so "the ID advanced" == "a box closed".
  local function latest_id()
    local ok, id = pcall(MessageEvents.GetMostRecentMessageID)
    if ok and id ~= nil then return id end
    return nil
  end

  -- Persistent per-frame body, driven as a coroutine by the GeneralScriptManager.
  local function update()
    local cooldown = 0
    local busy_anchor = nil -- message ID captured when a modal went up
    local busy_frames = 0
    -- A modal counts as closed once a box-dismissal message has been posted
    -- (ID advanced past the anchor) AND a few frames have elapsed for the box
    -- to fully tear down. Hard cap so a stray/never-dismissed modal can't
    -- wedge F5 forever.
    local function modal_clear()
      if busy_anchor == nil then return true end
      local id = latest_id()
      local dismissed = id ~= nil and id > busy_anchor
      busy_frames = busy_frames + 1
      if (dismissed and busy_frames >= 10) or busy_frames >= 300 then
        busy_anchor = nil
        return true
      end
      return false
    end
    while true do
      if not modal_clear() then
        -- A native box is still up/tearing down: drop F5, wait.
      elseif cooldown > 0 then
        -- The game's menu box needs a few frames to fully tear down after a
        -- selection/cancel before it can be re-opened without an error. Hold
        -- off on re-opening until the cooldown elapses.
        cooldown = cooldown - 1
      elseif st.show then
        st.show = false
        st.busy = true
        busy_anchor = latest_id() or 0
        busy_frames = 0
        -- open_menu yields (must not be pcall'd; see its note). It returns 0
        -- on any internal error after showing an error box.
        local choice = open_menu() or 0
        if choice >= 1 and choice <= #F5_MENU_ITEMS then
          run_script(F5_MENU_ITEMS[choice].path) -- may yield (sub-menus); run
                                                 -- directly so yields stay in this coroutine
        end
        -- Re-anchor AFTER the whole sequence: the menu selection itself
        -- advanced the ID, and the script may have opened its own message box
        -- whose dismissal posts a later ID.
        local id = latest_id()
        if id then busy_anchor = id end
        st.busy = false
        -- Wait long enough for the box's teardown/animation to finish (the
        -- game's 0.45 s slowmode at 60 fps ~= 27 frames) before we're allowed
        -- to re-open. This is what keeps a re-opened box from reusing a stale
        -- hovered-item index from the previous box.
        cooldown = 40
      end
      coroutine.yield()
    end
  end

  local menutab = {}
  setmetatable(menutab, menutab)
  menutab.__index = _G
  menutab._G = _G
  menutab.Update = update
  GeneralScriptManager.AddScript(menutab)
end
