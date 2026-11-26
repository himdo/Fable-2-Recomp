# AMD GPU hang in Wraithmarsh: overview

**Status:** merged · **Commits:** SDK 0fe6654 (himdo/rexglue-sdk#29), first picked up by the master pin in 2df8825 (#158), shipped in 1.6.0 · **Last checked against:** 7863041 (SDK pin 22ef700)

Issue: [#126](https://github.com/himdo/Fable-2-Recomp/issues/126).

## What players saw

On recent AMD graphics cards (RX 7000 and 9000 series), the game froze and closed during Wraithmarsh fights, usually around hollow men or banshees. Sometimes just running past them was enough. NVIDIA players got through the same places without trouble. On 1.5.0 no setting helped: lower resolution, 30 FPS, vsync, fullscreen modes and closing overlays were all tried.

## What caused it

- **It was a graphics card hang, not a normal crash.** The card got stuck on one job, Windows reset it after about two seconds, and the game lost its graphics device.
- **One visual effect caused it:** a trail or ribbon effect the game draws around hollow men and some other Wraithmarsh enemies.
- **The effect looks up some of its settings by position in a list, and now and then the position comes out negative.** Think of reading item number −142 of a list. On NVIDIA cards and on the original Xbox 360, that just gives a wrong value and nobody notices. Recent AMD cards treat the negative number as a huge positive one, try to read memory that doesn't exist, and stop dead.

## What's different now

The recomp's graphics layer now keeps that position inside the valid range (0 to 255) before the lookup. At worst, the odd point of a trail gets a slightly wrong colour or width instead of freezing the game.

- **Fixed in 1.6.0.**
- With the fix, a save that froze the game within a minute played through Wraithmarsh with no problem. With the fix switched off, it froze again within 20 seconds.

## What players need to do

Update to 1.6.0 or later. If an AMD card still freezes, post the game log and your graphics card on [#126](https://github.com/himdo/Fable-2-Recomp/issues/126): another effect could have a similar problem.

## Not the same problem

- **Laptops and integrated graphics** show the same "device hung" message, but for a different reason. See `docs/fix/integrated_graphics_crash/`.
- **Low frame rates on AMD** are a performance question, not this crash.
