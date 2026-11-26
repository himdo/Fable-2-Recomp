# Black menu with two controllers: overview

**Status:** merged · **Commits:** a4b0274 · **Last checked against:** 7863041

## What players saw

With two controllers plugged in at launch, pressing **A on the second controller** at the title screen or main menu turned the menu black. With one controller, or when the second controller only moved around the menu, everything worked.

## What caused it

The game treats each controller as a separate player. On a real console, a second controller that isn't signed in still counts as a guest player. The recomp only has one signed-in profile, so when the second controller pressed A, the menu tried to start the game for a player who, as far as it could tell, didn't exist, and got stuck on a black screen.

## What's different now

Whoever presses a button becomes player one. When a controller presses a button, the recomp quietly swaps it into the first player's place, so the game always sees the press coming from the signed-in player. The other controller is still visible to the game, just in the second place. Holding buttons on both controllers doesn't make them keep swapping.

## What players need to do

Nothing. For testing, `FABLE2_PRIMARY_SLOT_REMAP=0` switches the swap off and brings the old behaviour back.
