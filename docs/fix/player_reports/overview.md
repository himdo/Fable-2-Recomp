# Open player reports: overview

**Status:** no commit yet (research only) · **Commits:** none · **Last checked against:** c2a9d12

## What players saw

- **A crash on the loading screen with an older Intel processor** (an i7-3770, from 2012).
- **Slow inventory menus.**
- **Texture problems on Android.**
- **Crashes with an old AMD graphics driver.**
- **A black screen when a "free game" notification popped up.**

## What caused it

None of these is fixed yet, so some of this is still a guess. The details are in [technical.md](technical.md).

- **Older Intel processor:** the first guess was that the game needs a processor feature this chip doesn't have, because other PC ports crashed on the same processor for that reason. But the current version of the game isn't built to need that feature, so the cause is open again. The crash report the game writes would show what really went wrong.
- **Slow menus:** the menus were slow in the original 2008 game too. Nobody has measured where the time goes yet. The project already has tools to measure it.
- **Android:** the Android version is made by a separate project and draws the game differently, so the problem is most likely on that side.
- **Old AMD driver:** the game's log names your graphics card on both graphics modes, but it only records the driver version on Vulkan. That makes it hard to tell which driver someone was using. A different AMD freeze in Wraithmarsh has already been fixed.
- **Notification black screen:** the game listens for system messages, such as "a system menu has opened", and dims itself while one is open. If it hears "opened" but never "closed", it could stay dark. It isn't known yet whether the notification comes from the game or from Windows.

## What's different now

Nothing yet. Planned next steps:

- Read the crash report from the older-processor crash, and show a clear message instead of a crash if the processor really is too old.
- Measure the slow menus with a full inventory, then fix what that finds.
- Write the graphics driver version into the game log, and add a bug report form that asks for it.
- Find out where the black-screen notification comes from.

## What players need to do

When you report one of these, attach these files from the game folder:

- the **logs** folder;
- **fable2_crash.log**, if there is one (it's written when the game crashes);
- **fable2_stall_dump.log**, if there is one (it's written when the picture freezes for more than a few seconds).

Also say which graphics card and driver version you have, which processor you have, and whether you play with the default graphics mode or Vulkan.

- **Older Intel processor:** there's no workaround yet. The crash report is the most useful thing you can send.
- **AMD crash:** update your graphics driver.
- **Android:** report it to the Android project.
