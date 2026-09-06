# Fable 2 (ReXGlue) — Function Names & Reference

Every `[entrypoint.functions]` entry in `fable_2_manifest.toml` now carries a `name` field. This document explains each one: what it does, its name, and how it is triggered.

**Total functions named: 398** (no entrypoint was removed).

## How these were determined

The guest code is PowerPC (Xbox 360). Each function was analyzed from its disassembly (the PPC instruction stream preserved as comments in `generated/default/*.cpp`) plus the imports and other functions it calls. Signals used:

- **Import calls** (e.g. `NtAllocateVirtualMemory`, `RtlEnterCriticalSection`, `XamLoaderTerminateTitle`) identify the SDK subsystem.
- **Instruction shape** identifies thunk families: vtable dispatch, switch jump-table, const/global return, tail-call trampoline.
- **In-game annotations** you left in the manifest (e.g. "Swimming", "First Combat childhood fight") pin down the story beats.

**Memory map:** code `.text` = `0x82000000`–`0x83438000`; data ≥ `0x83438000`. The `0x82C00000`–`0x831FFFFF` range is the Xbox 360 **SDK runtime**; elsewhere is **game** code.

**Confidence legend:** **high** = shape is unambiguous · medium = subsystem clear, exact role inferred · low = best guess from limited signal.


## A. Story & Gameplay

_The functions you annotated with in-game moments. These are virtual-call thunks (and a couple of getters) that the engine fires at specific story beats. Highest confidence._

**19 functions.**

### `GetNewGameLoadingGlobal`

- **Address:** `0x822142D0` · **Size:** 3 insns · **Category:** global · **Region:** game · **Confidence:** medium

- **Role:** returns the new-game loading-screen global pointer

- **Does:** Const thunk: builds the data address 0x8331950C (lis r11,-31950; lwz r3,-27380(r11)) and returns the pointer stored there in r3. Head of the 'loading screen when starting a new game' group.

- **Trigger:** Loading screen when starting a new game (internal getter).

### `LoadingScreen_VectorOffset`

- **Address:** `0x822D6678` · **Size:** 6 insns · **Category:** tailcall · **Region:** game · **Confidence:** low

- **Role:** loading-screen vector offset add (tails to sub_822AADE0)

- **Does:** Loads a 128-bit vector constant from a data table (lis r11,-31926; addi r10,r11,-21152; lvx128), adds it to the in-flight float vectors v1/v2 (vaddfp), then tails into sub_822AADE0. Applies a fixed positional offset, part of the new-game loading-screen group.

- **Trigger:** Loading screen when starting a new game (internal helper).

- **Calls:** `sub_822AADE0`

### `Story_JobSignBlacksmith`

- **Address:** `0x823A1810` · **Size:** 4 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Role:** interacting with the blacksmith job sign

- **Does:** Loads a global object pointer (data at ~0x833xxxxx built from lis r11,-31927; lwz r3,27596(r11)), forwards the incoming r3 as r4, then tail-calls sub_82297F88. Handles the interaction with the job/quest sign for the blacksmith.

- **Trigger:** Interact with the job sign (blacksmith) in the town.

- **Calls:** `sub_82297F88`

### `Story_EnterWarehouseAsChild`

- **Address:** `0x8274B738` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** entering the warehouse as child (vtable slot 38)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 38 (offset 152) -> bctr. Triggers the scripted beat for entering the warehouse during the childhood prologue.

- **Trigger:** Walk into the warehouse as the child in the Albion prologue.

### `LoadingScreen_Virtual42`

- **Address:** `0x8274B748` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 42)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 42 (offset 168) -> bctr. Part of the loading-screen function group used when starting a new game.

- **Trigger:** Loading screen when starting a new game (internal dispatch).

### `LoadingScreen_Virtual39`

- **Address:** `0x8274B758` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 39)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 39 (offset 156) -> bctr. Part of the loading-screen function group used when starting a new game.

- **Trigger:** Loading screen when starting a new game (internal dispatch).

### `Story_TatterSpireGetMoving`

- **Address:** `0x8274B768` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** Tatter Spire 'Get Moving' beat (vtable slot 20)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 20 (offset 80) -> bctr. Fires when arriving at the Tatter Spire and the man says 'Get Moving'.

- **Trigger:** Arrive at the Tatter Spire when the man says 'Get Moving'.

### `LoadingScreen_Virtual28`

- **Address:** `0x8274B778` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 28)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 28 (offset 112) -> bctr. Part of the loading-screen function group used when starting a new game.

- **Trigger:** Loading screen when starting a new game (internal dispatch).

### `Story_SisterAfterFirstFight`

- **Address:** `0x8274B788` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** interacting with sister after first fight (vtable slot 37)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 37 (offset 148) -> bctr. Fires the interaction with the player's sister immediately after the first childhood fight.

- **Trigger:** Talk to your sister right after the first childhood battle.

### `Swimming`

- **Address:** `0x8297EB28` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** childhood swimming interaction (vtable slot 23)

- **Does:** Virtual-call thunk: loads the vtable pointer from the object in r3, reads slot 23 (byte offset 92), and tail-calls it. Reached when the child character swims.

- **Trigger:** Swim as the child. (Name already fixed by the user; preserved verbatim.)

### `Story_FirstChildCombat`

- **Address:** `0x8297EB48` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** first childhood fight (vtable slot 25)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 25 (offset 100) -> bctr. Triggers the first combat encounter of the childhood (Albion) prologue.

- **Trigger:** The first fight as the child in the opening Albion sequence.

### `Story_AfterNewGameVideo`

- **Address:** `0x82988EC8` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** after new-game video plays (vtable slot 19)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 19 (offset 76) -> bctr. Triggers the beat immediately after the new-game intro video finishes.

- **Trigger:** Right after the new-game intro video plays.

### `LoadingScreen_Virtual43`

- **Address:** `0x82988ED8` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 43)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 43 (offset 172) -> bctr. Part of the loading-screen function group used when starting a new game.

- **Trigger:** Loading screen when starting a new game (internal dispatch).

### `Skill_AimRangeWeapon`

- **Address:** `0x82988EE8` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** medium

- **Role:** aiming a range weapon (skill), vtable slot 23 on r4

- **Does:** Virtual-call thunk reading the object from r4 (not r3): vtable slot 23 (offset 92) -> bctr. Same slot as Swimming but dispatched on a different register, used when aiming a ranged weapon (skill).

- **Trigger:** Aim a ranged weapon / use the aiming skill.

### `LoadingScreen_Virtual48`

- **Address:** `0x82988F28` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 48)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 48 (offset 192) -> bctr. Part of the loading-screen function group used when starting a new game.

- **Trigger:** Loading screen when starting a new game (internal dispatch).

### `Story_TalkToCameraMan`

- **Address:** `0x82988F38` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** talking to the camera man, child scene (vtable slot 17)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 17 (offset 68) -> bctr. Triggers the conversation with the camera man in the childhood scene.

- **Trigger:** Talk to the camera man during the child (Albion) scene.

### `Story_LookLittleSparrow`

- **Address:** `0x82988F48` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Role:** 'Look, little sparrow' LT prompt (vtable slot 45)

- **Does:** Virtual-call thunk: object in r3 -> vtable slot 45 (offset 180) -> bctr. Fires after the new-game intro, tied to the 'look, little sparrow' moment gated on the LT button.

- **Trigger:** After the new-game intro video, at the 'look, little sparrow' LT prompt.

### `Story_ShootBeetleInWarehouse`

- **Address:** `0x82DDC4D8` · **Size:** 7 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** **high**

- **Role:** shooting the Beetle in the warehouse (vtable slot 9, arg-shuffled)

- **Does:** Variant virtual-call thunk: takes an object in r4 (moves it to r3), reads vtable[0] then slot 9 (offset 36) of the nested object, and tail-calls it with a shifted argument set (r4<-r5). Fires the shooting interaction on the Beetle target in the warehouse.

- **Trigger:** Shoot the Beetle (target) in the warehouse as the child.

### `LoadingScreen_Virtual36`

- **Address:** `0x82EAA980` · **Size:** 9 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** low

- **Role:** loading-screen virtual call (vtable slot 36, null-checked)

- **Does:** Null-checked virtual-call thunk: checks r4 for null, swaps args (r4<-r3, r3<-r4), reads vtable slot 36 (offset 144) of the object, and tail-calls it. Part of the new-game loading-screen group.

- **Trigger:** Loading screen when starting a new game (internal dispatch).


## B. Memory Management (heap)

_The custom heap allocator / free / manager. Identified by NtAllocateVirtualMemory / NtFreeVirtualMemory plus critical-section guards._

**7 functions.**

### `Heap_AllocateBlock_82238828`

- **Address:** `0x82238828` · **Size:** 495 insns · **Category:** body · **Region:** game · **Confidence:** medium

- **Role:** heap block allocator (game-side memory manager)

- **Does:** Large (495-instruction) memory allocator. Guards state with RtlEnter/LeaveCriticalSection, carves blocks out of a region (linked-list free lists at fixed object offsets), and falls back to NtAllocateVirtualMemory when the region is exhausted; raises RtlRaiseException on failure. This is the core block-alloc path of the game's custom heap.

- **Trigger:** Internal â€” runs whenever the engine allocates a heap block.

- **Imports:** `__imp__NtAllocateVirtualMemory`, `__imp__RtlEnterCriticalSection`, `__imp__RtlLeaveCriticalSection`, `__imp__RtlRaiseException`

- **Calls:** `sub_82238FCC`, `sub_8223900C`, `sub_82CA3190`, `sub_82CBE800`, `sub_82CBF130`

### `Heap_FreeBlock_822394F0`

- **Address:** `0x822394F0` · **Size:** 124 insns · **Category:** body · **Region:** game · **Confidence:** medium

- **Role:** heap block free (game-side memory manager)

- **Does:** 124-instruction companion to the allocator: returns a block to the free list under RtlEnter/LeaveCriticalSection and releases whole regions via NtFreeVirtualMemory when they become empty. Part of the same custom heap as Heap_AllocateBlock_82238828.

- **Trigger:** Internal â€” runs whenever the engine frees a heap block.

- **Imports:** `__imp__NtFreeVirtualMemory`, `__imp__RtlEnterCriticalSection`, `__imp__RtlLeaveCriticalSection`

- **Calls:** `sub_82239058`, `sub_822396C8`, `sub_82239708`, `sub_82CBE800`, `sub_82CBF370`

### `MemMgr_82CBF740`

- **Address:** `0x82CBF740` · **Size:** 325 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** MemMgr routine (325 insns, SDK)

- **Does:** [SDK runtime] Body function, 325 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeGetCurrentProcessType, __imp__NtAllocateVirtualMemory, __imp__NtFreeVirtualMemory, __imp__NtQueryVirtualMemory, __imp__RtlInitializeCriticalSection. Calls: sub_82CA3190, sub_82CAA2E0, sub_82CBEFC8, sub_82CBF760. First: `lwz r11,0(r8)`; last: `b 0x82ca2c1c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeGetCurrentProcessType`, `__imp__NtAllocateVirtualMemory`, `__imp__NtFreeVirtualMemory`, `__imp__NtQueryVirtualMemory`, `__imp__RtlInitializeCriticalSection`

- **Calls:** `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CBEFC8`, `sub_82CBF760`

### `MemMgr_82CBF760`

- **Address:** `0x82CBF760` · **Size:** 317 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** MemMgr routine (317 insns, SDK)

- **Does:** [SDK runtime] Body function, 317 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeGetCurrentProcessType, __imp__NtAllocateVirtualMemory, __imp__NtFreeVirtualMemory, __imp__NtQueryVirtualMemory, __imp__RtlInitializeCriticalSection. Calls: sub_82CA3190, sub_82CBEFC8. First: `mr r8,r8`; last: `b 0x82ca2c1c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeGetCurrentProcessType`, `__imp__NtAllocateVirtualMemory`, `__imp__NtFreeVirtualMemory`, `__imp__NtQueryVirtualMemory`, `__imp__RtlInitializeCriticalSection`

- **Calls:** `sub_82CA3190`, `sub_82CBEFC8`

### `MemMgr_82CBF770`

- **Address:** `0x82CBF770` · **Size:** 319 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** MemMgr routine (319 insns, SDK)

- **Does:** [SDK runtime] Body function, 319 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeGetCurrentProcessType, __imp__NtAllocateVirtualMemory, __imp__NtFreeVirtualMemory, __imp__NtQueryVirtualMemory, __imp__RtlInitializeCriticalSection. Calls: sub_82CA3190, sub_82CBEFC8. First: `mr r30,r3`; last: `b 0x82ca2c1c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeGetCurrentProcessType`, `__imp__NtAllocateVirtualMemory`, `__imp__NtFreeVirtualMemory`, `__imp__NtQueryVirtualMemory`, `__imp__RtlInitializeCriticalSection`

- **Calls:** `sub_82CA3190`, `sub_82CBEFC8`

### `MemFree_82CBFD74`

- **Address:** `0x82CBFD74` · **Size:** 446 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** MemFree routine (446 insns, SDK)

- **Does:** [SDK runtime] Body function, 446 insns, no prologue (mid-function target or leaf). Calls imports: __imp__NtFreeVirtualMemory, __imp__RtlCompareMemoryUlong, __imp__RtlEnterCriticalSection, __imp__RtlRaiseException. Calls: sub_82238790, sub_82239468, sub_82CA3190, sub_82CAA2E0, sub_82CBE800. First: `clrlwi. r11,r23,31`; last: `b 0x82cc04b0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__NtFreeVirtualMemory`, `__imp__RtlCompareMemoryUlong`, `__imp__RtlEnterCriticalSection`, `__imp__RtlRaiseException`

- **Calls:** `sub_82238790`, `sub_82239468`, `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CBE800`, `sub_82CBEA48`, `sub_82CC031C`, `sub_82CC0494`

### `MemFree_82CBFD9C`

- **Address:** `0x82CBFD9C` · **Size:** 436 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** MemFree routine (436 insns, SDK)

- **Does:** [SDK runtime] Body function, 436 insns, no prologue (mid-function target or leaf). Calls imports: __imp__NtFreeVirtualMemory, __imp__RtlCompareMemoryUlong, __imp__RtlRaiseException. Calls: sub_82238790, sub_82239468, sub_82CA3190, sub_82CAA2E0, sub_82CBE800. First: `addi r30,r20,-16`; last: `b 0x82cc04b0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__NtFreeVirtualMemory`, `__imp__RtlCompareMemoryUlong`, `__imp__RtlRaiseException`

- **Calls:** `sub_82238790`, `sub_82239468`, `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CBE800`, `sub_82CBEA48`, `sub_82CC031C`, `sub_82CC0494`


## C. Synchronization, Threads & Kernel

_Critical-section and thread primitives and kernel bug-check paths (RtlEnter/Leave/InitializeCriticalSection, ExTerminateThread, KeBugCheck)._

**14 functions.**

### `BugCheck_82CA9660`

- **Address:** `0x82CA9660` · **Size:** 54 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** BugCheck routine (54 insns, SDK)

- **Does:** [SDK runtime] Body function, 54 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeBugCheck. Calls: sub_82CA95D8, sub_82CA9710, sub_82CA9758. First: `lis r11,-31949`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeBugCheck`

- **Calls:** `sub_82CA95D8`, `sub_82CA9710`, `sub_82CA9758`

### `BugCheck_82CA9710`

- **Address:** `0x82CA9710` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** BugCheck routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeBugCheck. Calls: sub_82CA9758. First: `mr r8,r8`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeBugCheck`

- **Calls:** `sub_82CA9758`

### `Sync_82CAF2C0`

- **Address:** `0x82CAF2C0` · **Size:** 89 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (89 insns, SDK)

- **Does:** [SDK runtime] Body function, 89 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection. Calls: sub_82CA3C68, sub_82CA5DC0, sub_82CA88E0, sub_82CA8970, sub_82CAF40C. First: `mr r30,r25`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`

- **Calls:** `sub_82CA3C68`, `sub_82CA5DC0`, `sub_82CA88E0`, `sub_82CA8970`, `sub_82CAF40C`, `sub_82CAF424`, `sub_82CAFE08`, `sub_82CB5B78`

### `BugCheck_82CB57CC`

- **Address:** `0x82CB57CC` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** BugCheck routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeBugCheck. First: `mtctr r11`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeBugCheck`

### `BugCheck_82CB57D4`

- **Address:** `0x82CB57D4` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** BugCheck routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeBugCheck. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeBugCheck`

### `BugCheck_82CB57E0`

- **Address:** `0x82CB57E0` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** BugCheck routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). Calls imports: __imp__KeBugCheck. First: `lis r3,-16384`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__KeBugCheck`

### `Sync_82CB8D48`

- **Address:** `0x82CB8D48` · **Size:** 28 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (28 insns, SDK)

- **Does:** [SDK runtime] Body function, 28 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection. Calls: sub_82CB5B78, sub_82CB8D7C, sub_82CB8DB8. First: `lwz r11,8(r30)`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`

- **Calls:** `sub_82CB5B78`, `sub_82CB8D7C`, `sub_82CB8DB8`

### `Sync_82CB8D7C`

- **Address:** `0x82CB8D7C` · **Size:** 15 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (15 insns, SDK)

- **Does:** [SDK runtime] Body function, 15 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection. Calls: sub_82CB8DB8. First: `mr r8,r8`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`

- **Calls:** `sub_82CB8DB8`

### `Sync_82CB8E64`

- **Address:** `0x82CB8E64` · **Size:** 115 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (115 insns, SDK)

- **Does:** [SDK runtime] Body function, 115 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection, __imp__RtlLeaveCriticalSection. Calls: sub_82CAAE18, sub_82CAFF48, sub_82CB5B78, sub_82CB8CF8, sub_82CB8F00. First: `lis r11,-31921`; last: `b 0x82ca2c28`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`, `__imp__RtlLeaveCriticalSection`

- **Calls:** `sub_82CAAE18`, `sub_82CAFF48`, `sub_82CB5B78`, `sub_82CB8CF8`, `sub_82CB8F00`, `sub_82CB9018`, `sub_82CB9030`, `sub_82CB9054`

### `Sync_82CB8ECC`

- **Address:** `0x82CB8ECC` · **Size:** 50 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (50 insns, SDK)

- **Does:** [SDK runtime] Body function, 50 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection, __imp__RtlLeaveCriticalSection. Calls: sub_82CB5B78, sub_82CB8F00, sub_82CB9018, sub_82CB9054. First: `lwz r11,8(r30)`; last: `b 0x82cb8e74`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`, `__imp__RtlLeaveCriticalSection`

- **Calls:** `sub_82CB5B78`, `sub_82CB8F00`, `sub_82CB9018`, `sub_82CB9054`

### `Sync_82CB8F00`

- **Address:** `0x82CB8F00` · **Size:** 37 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Sync routine (37 insns, SDK)

- **Does:** [SDK runtime] Body function, 37 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlEnterCriticalSection, __imp__RtlLeaveCriticalSection. Calls: sub_82CB9018, sub_82CB9054. First: `mr r8,r8`; last: `b 0x82cb8e74`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlEnterCriticalSection`, `__imp__RtlLeaveCriticalSection`

- **Calls:** `sub_82CB9018`, `sub_82CB9054`

### `Thread_82CCA428`

- **Address:** `0x82CCA428` · **Size:** 22 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Thread routine (22 insns, SDK)

- **Does:** [SDK runtime] Body function, 22 insns, no prologue (mid-function target or leaf). Calls imports: __imp__ExTerminateThread. Calls: sub_82CBBED0, sub_82CC16C0. First: `li r3,1`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__ExTerminateThread`

- **Calls:** `sub_82CBBED0`, `sub_82CC16C0`

### `Thread_82CCA448`

- **Address:** `0x82CCA448` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Thread routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). Calls imports: __imp__ExTerminateThread. Calls: sub_82CBBED0. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__ExTerminateThread`

- **Calls:** `sub_82CBBED0`

### `Thread_82CCA454`

- **Address:** `0x82CCA454` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Thread routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls imports: __imp__ExTerminateThread. Calls: sub_82CBBED0. First: `lwz r3,80(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__ExTerminateThread`

- **Calls:** `sub_82CBBED0`


## D. Exceptions & Fatal Errors

_RtlRaiseException paths and the fatal-error handler (DbgPrint + XamLoaderTerminateTitle)._

**6 functions.**

### `FatalError_82CBB98C`

- **Address:** `0x82CBB98C` · **Size:** 115 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** FatalError routine (115 insns, SDK)

- **Does:** [SDK runtime] Body function, 115 insns, no prologue (mid-function target or leaf). Calls imports: __imp__DbgPrint, __imp__XamLoaderTerminateTitle. Calls: sub_822EA8C0, sub_82CA97B8, sub_82CAC798, sub_82CBB570, sub_82CBB788. First: `lis r9,-31921`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__DbgPrint`, `__imp__XamLoaderTerminateTitle`

- **Calls:** `sub_822EA8C0`, `sub_82CA97B8`, `sub_82CAC798`, `sub_82CBB570`, `sub_82CBB788`, `sub_82CBBED0`, `sub_82CC16C0`, `sub_82CC1990`

### `FatalError_82CBBB24`

- **Address:** `0x82CBBB24` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** FatalError routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls imports: __imp__XamLoaderTerminateTitle. Calls: sub_82CBBED0. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__XamLoaderTerminateTitle`

- **Calls:** `sub_82CBBED0`

### `FatalError_82CBBB30`

- **Address:** `0x82CBBB30` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** FatalError routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls imports: __imp__XamLoaderTerminateTitle. Calls: sub_82CBBED0. First: `bl 0x832b230c`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__XamLoaderTerminateTitle`

- **Calls:** `sub_82CBBED0`

### `Exception_82CC02FC`

- **Address:** `0x82CC02FC` · **Size:** 86 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Exception routine (86 insns, SDK)

- **Does:** [SDK runtime] Body function, 86 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlRaiseException. Calls: sub_82238790, sub_82239468, sub_82CA3190, sub_82CAA2E0, sub_82CC031C. First: `lhz r11,2(r11)`; last: `b 0x82cc04b0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlRaiseException`

- **Calls:** `sub_82238790`, `sub_82239468`, `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CC031C`, `sub_82CC0494`, `sub_82CC04B0`

### `Exception_82CC031C`

- **Address:** `0x82CC031C` · **Size:** 78 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Exception routine (78 insns, SDK)

- **Does:** [SDK runtime] Body function, 78 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlRaiseException. Calls: sub_82238790, sub_82239468, sub_82CA3190, sub_82CAA2E0, sub_82CC0494. First: `mr r8,r8`; last: `b 0x82cc04b0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlRaiseException`

- **Calls:** `sub_82238790`, `sub_82239468`, `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CC0494`, `sub_82CC04B0`

### `Exception_82CC032C`

- **Address:** `0x82CC032C` · **Size:** 88 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Exception routine (88 insns, SDK)

- **Does:** [SDK runtime] Body function, 88 insns, no prologue (mid-function target or leaf). Calls imports: __imp__RtlRaiseException. Calls: sub_82238790, sub_82239468, sub_82CA3190, sub_82CAA2E0, sub_82CC0494. First: `lwz r30,124(r31)`; last: `b 0x82cc04b0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__RtlRaiseException`

- **Calls:** `sub_82238790`, `sub_82239468`, `sub_82CA3190`, `sub_82CAA2E0`, `sub_82CC0494`, `sub_82CC04B0`


## E. Filesystem / Volume

_Volume / filesystem queries (NtQueryVolumeInformationFile)._

**1 functions.**

### `FsVolume_82CBC820`

- **Address:** `0x82CBC820` · **Size:** 81 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** FsVolume routine (81 insns, SDK)

- **Does:** [SDK runtime] Body function, 81 insns, no prologue (mid-function target or leaf). Calls imports: __imp__NtQueryVolumeInformationFile. Calls: sub_82CAA2E0, sub_82CBC930, sub_82CBC97C, sub_82CBC9C4, sub_82CC0750. First: `cmplwi cr6,r28,0`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Imports:** `__imp__NtQueryVolumeInformationFile`

- **Calls:** `sub_82CAA2E0`, `sub_82CBC930`, `sub_82CBC97C`, `sub_82CBC9C4`, `sub_82CC0750`, `sub_82CC1C18`


## F. String & Math Helpers

_SDK string copy and float->integer conversion helpers._

**2 functions.**

### `Math_FloatIndexStore_824D56C8`

- **Address:** `0x824D56C8` · **Size:** 15 insns · **Category:** body · **Region:** game · **Confidence:** low

- **Role:** float -> integer index store

- **Does:** Loads a float from a global table and a scalar, computes an integer via fcfid/fmadd/fctiwz (round-to-int of a scaled value), then stores that integer at *(r3 + 420). Converts a floating coordinate/parameter into a quantized integer index stored on an object.

- **Trigger:** Internal math helper (no direct player trigger).

### `Str_BoundedCopy_82C8D630`

- **Address:** `0x82C8D630` · **Size:** 28 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Role:** SDK bounded string copy (strncpy-like)

- **Does:** Copies bytes from src to dst up to a maximum length, stopping at a 0x80-high-byte terminator (the SDK's wide/extended string sentinel). On normal copy it stores the resulting length; on the short-init path it writes a 2-byte length header (sth) and a sentinel byte. Classic Xbox 360 SDK string copy with a size field.

- **Trigger:** Internal SDK string handling (no direct player trigger).


## G. Virtual-Dispatch Thunks (vtable)

_`lwz vtptr,0(rX); lwz m,OFF(vtptr); mtctr; bctr` — call the object's virtual method at slot OFF/4. The slot number is the method; the address is the binding site._

**48 functions.**

### `VtableSlotX3173_822A2AB0`

- **Address:** `0x822A2AB0` · **Size:** 59 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 3173 (offset 12692) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot29_824F0600`

- **Address:** `0x824F0600` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Virtual-call thunk: reads the vtable pointer from the object, selects slot 29 (byte offset 116), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX6922_825EF568`

- **Address:** `0x825EF568` · **Size:** 7 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 6922 (offset 27688) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot5_82903F70`

- **Address:** `0x82903F70` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Virtual-call thunk: reads the vtable pointer from the object, selects slot 5 (byte offset 20), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot2_82903F80`

- **Address:** `0x82903F80` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Virtual-call thunk: reads the vtable pointer from the object, selects slot 2 (byte offset 8), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX3_829FCB00`

- **Address:** `0x829FCB00` · **Size:** 6 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 3 (offset 12) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX4_82B56870`

- **Address:** `0x82B56870` · **Size:** 14 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 4 (offset 16) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX4765_82BBC898`

- **Address:** `0x82BBC898` · **Size:** 8 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 4765 (offset 19060) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX1_82BE9680`

- **Address:** `0x82BE9680` · **Size:** 559 insns · **Category:** vtable_x · **Region:** game · **Confidence:** medium

- **Does:** [game code] Variant virtual dispatch: selects vtable slot 1 (offset 4) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82BE9F3C`, `sub_82BEA25C`, `sub_82BEA3D0`, `sub_82BEA438`, `sub_82BEA698`, `sub_82BEA6E8`, `sub_82BEA860`

### `VtableSlot1_82BFA4A8`

- **Address:** `0x82BFA4A8` · **Size:** 4 insns · **Category:** vtable · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Virtual-call thunk: reads the vtable pointer from the object, selects slot 1 (byte offset 4), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX17_82C09188`

- **Address:** `0x82C09188` · **Size:** 9 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 17 (offset 68) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX7_82C0B400`

- **Address:** `0x82C0B400` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 7 (offset 28) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot7_82C1DC50`

- **Address:** `0x82C1DC50` · **Size:** 4 insns · **Category:** vtable · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Virtual-call thunk: reads the vtable pointer from the object, selects slot 7 (byte offset 28), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot6_82C1DC60`

- **Address:** `0x82C1DC60` · **Size:** 4 insns · **Category:** vtable · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Virtual-call thunk: reads the vtable pointer from the object, selects slot 6 (byte offset 24), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX3_82C1DC80`

- **Address:** `0x82C1DC80` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 3 (offset 12) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX12_82C2F500`

- **Address:** `0x82C2F500` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 12 (offset 48) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX10_82C4C2C8`

- **Address:** `0x82C4C2C8` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 10 (offset 40) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX11_82C4C2E8`

- **Address:** `0x82C4C2E8` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 11 (offset 44) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX6_82C4C300`

- **Address:** `0x82C4C300` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 6 (offset 24) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX2_82C4C320`

- **Address:** `0x82C4C320` · **Size:** 7 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 2 (offset 8) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX2_82C4C5A8`

- **Address:** `0x82C4C5A8` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 2 (offset 8) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX1_82C4C5C8`

- **Address:** `0x82C4C5C8` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 1 (offset 4) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX1_82C4C5E8`

- **Address:** `0x82C4C5E8` · **Size:** 9 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 1 (offset 4) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX4_82CE5D50`

- **Address:** `0x82CE5D50` · **Size:** 18 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 4 (offset 16) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX3_82EF41D8`

- **Address:** `0x82EF41D8` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 3 (offset 12) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX4_82EF41F0`

- **Address:** `0x82EF41F0` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 4 (offset 16) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot26_82F04420`

- **Address:** `0x82F04420` · **Size:** 4 insns · **Category:** vtable · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Virtual-call thunk: reads the vtable pointer from the object, selects slot 26 (byte offset 104), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX10_82F1D508`

- **Address:** `0x82F1D508` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 10 (offset 40) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX4_82F1D520`

- **Address:** `0x82F1D520` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 4 (offset 16) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX11_82F1D538`

- **Address:** `0x82F1D538` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 11 (offset 44) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX5_82F1D550`

- **Address:** `0x82F1D550` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 5 (offset 20) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX6_82F1D568`

- **Address:** `0x82F1D568` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 6 (offset 24) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX7_82F1D580`

- **Address:** `0x82F1D580` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 7 (offset 28) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX8_82F1D598`

- **Address:** `0x82F1D598` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 8 (offset 32) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX9_82F1D5B0`

- **Address:** `0x82F1D5B0` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 9 (offset 36) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX14_82F2C3A8`

- **Address:** `0x82F2C3A8` · **Size:** 6 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 14 (offset 56) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX3_82F67E18`

- **Address:** `0x82F67E18` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 3 (offset 12) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX6_82F67E40`

- **Address:** `0x82F67E40` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 6 (offset 24) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX7_82F67E68`

- **Address:** `0x82F67E68` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 7 (offset 28) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX2_82FE7850`

- **Address:** `0x82FE7850` · **Size:** 66 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 2 (offset 8) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX2_83046BE8`

- **Address:** `0x83046BE8` · **Size:** 69 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 2 (offset 8) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlot95_83056810`

- **Address:** `0x83056810` · **Size:** 4 insns · **Category:** vtable · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Virtual-call thunk: reads the vtable pointer from the object, selects slot 95 (byte offset 380), and tail-calls it.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX133_83056BC0`

- **Address:** `0x83056BC0` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 133 (offset 532) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX134_83056BE8`

- **Address:** `0x83056BE8` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 134 (offset 536) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX133_83056C10`

- **Address:** `0x83056C10` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 133 (offset 532) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX134_83056C38`

- **Address:** `0x83056C38` · **Size:** 10 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 134 (offset 536) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX151_83057068`

- **Address:** `0x83057068` · **Size:** 8 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 151 (offset 604) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `VtableSlotX3_831C5CD8`

- **Address:** `0x831C5CD8` · **Size:** 20 insns · **Category:** vtable_x · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Variant virtual dispatch: selects vtable slot 3 (offset 12) with extra arg shuffling / null checks, then bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.


## H. Switch / Jump-Table Dispatchers

_Index a jump table by an integer argument (rlwinm/lwzx/mtctr/bctr) and tail-call the selected case._

**72 functions.**

### `SwitchDispatch_8217F250`

- **Address:** `0x8217F250` · **Size:** 35 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8222B3C8`

- **Address:** `0x8222B3C8` · **Size:** 31 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822D7C50`

- **Address:** `0x822D7C50` · **Size:** 18 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822D7CB0`

- **Address:** `0x822D7CB0` · **Size:** 23 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822D7D58`

- **Address:** `0x822D7D58` · **Size:** 31 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822D7DF8`

- **Address:** `0x822D7DF8` · **Size:** 30 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822D7EA0`

- **Address:** `0x822D7EA0` · **Size:** 21 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_822EAA98`

- **Address:** `0x822EAA98` · **Size:** 24 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_823A5A28`

- **Address:** `0x823A5A28` · **Size:** 66 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_824063B0`

- **Address:** `0x824063B0` · **Size:** 56 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82415A20`

- **Address:** `0x82415A20` · **Size:** 105 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8241BAA8`

- **Address:** `0x8241BAA8` · **Size:** 67 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8242C5E0`

- **Address:** `0x8242C5E0` · **Size:** 25 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_824B7E70`

- **Address:** `0x824B7E70` · **Size:** 32 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_824B7560`, `sub_824B7B40`, `sub_824B7BF0`, `sub_824B7C98`

### `SwitchDispatch_824BDE60`

- **Address:** `0x824BDE60` · **Size:** 34 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_824CAC90`

- **Address:** `0x824CAC90` · **Size:** 82 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_824D2800`

- **Address:** `0x824D2800` · **Size:** 27 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8250B778`

- **Address:** `0x8250B778` · **Size:** 15 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82520C00`

- **Address:** `0x82520C00` · **Size:** 22 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82522E18`

- **Address:** `0x82522E18` · **Size:** 83 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82592C80`

- **Address:** `0x82592C80` · **Size:** 21 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82583140`, `sub_825832B0`, `sub_828CE240`

### `SwitchDispatch_8259AF48`

- **Address:** `0x8259AF48` · **Size:** 26 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_823F7678`

### `SwitchDispatch_825A0B58`

- **Address:** `0x825A0B58` · **Size:** 24 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82583140`, `sub_825A0BE8`, `sub_828CE240`

### `SwitchDispatch_82622360`

- **Address:** `0x82622360` · **Size:** 39 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82275368`

### `SwitchDispatch_826224B8`

- **Address:** `0x826224B8` · **Size:** 35 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82275368`

### `SwitchDispatch_8272B2C0`

- **Address:** `0x8272B2C0` · **Size:** 79 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82771250`

- **Address:** `0x82771250` · **Size:** 24 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_827D30C0`

- **Address:** `0x827D30C0` · **Size:** 135 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8283A008`

- **Address:** `0x8283A008` · **Size:** 25 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8283FF98`

- **Address:** `0x8283FF98` · **Size:** 23 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8289FB40`

- **Address:** `0x8289FB40` · **Size:** 146 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_8299F668`

- **Address:** `0x8299F668` · **Size:** 32 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82A2F3E8`

- **Address:** `0x82A2F3E8` · **Size:** 65 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82A34680`

- **Address:** `0x82A34680` · **Size:** 41 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82B38270`

- **Address:** `0x82B38270` · **Size:** 47 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82B57148`

- **Address:** `0x82B57148` · **Size:** 24 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82B809E8`

- **Address:** `0x82B809E8` · **Size:** 133 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82B8F5E8`

- **Address:** `0x82B8F5E8` · **Size:** 30 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82BC8260`

- **Address:** `0x82BC8260` · **Size:** 27 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82BC9B28`

- **Address:** `0x82BC9B28` · **Size:** 70 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82BE0100`

- **Address:** `0x82BE0100` · **Size:** 13 insns · **Category:** switchdisp · **Region:** game · **Confidence:** medium

- **Does:** [game code] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C56828`

- **Address:** `0x82C56828` · **Size:** 25 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C89840`

- **Address:** `0x82C89840` · **Size:** 22 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C89638`, `sub_82C898B8`, `sub_82C89A0C`

### `SwitchDispatch_82C898B8`

- **Address:** `0x82C898B8` · **Size:** 24 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C89960`

### `SwitchDispatch_82C89960`

- **Address:** `0x82C89960` · **Size:** 21 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C8C9F0`

- **Address:** `0x82C8C9F0` · **Size:** 64 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C8A698`

### `SwitchDispatch_82C8CF00`

- **Address:** `0x82C8CF00` · **Size:** 37 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C8D3F0`

- **Address:** `0x82C8D3F0` · **Size:** 72 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C8D750`

- **Address:** `0x82C8D750` · **Size:** 46 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C8DAE8`

- **Address:** `0x82C8DAE8` · **Size:** 22 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C97120`

- **Address:** `0x82C97120` · **Size:** 13 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C99CB8`

- **Address:** `0x82C99CB8` · **Size:** 49 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C99E38`

### `SwitchDispatch_82C99ED0`

- **Address:** `0x82C99ED0` · **Size:** 49 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C9A060`

### `SwitchDispatch_82C9A118`

- **Address:** `0x82C9A118` · **Size:** 49 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C9A2A8`

### `SwitchDispatch_82C9B420`

- **Address:** `0x82C9B420` · **Size:** 33 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C9D280`

- **Address:** `0x82C9D280` · **Size:** 78 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C9DB00`

- **Address:** `0x82C9DB00` · **Size:** 49 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C9E878`

- **Address:** `0x82C9E878` · **Size:** 29 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82C9E9B8`

- **Address:** `0x82C9E9B8` · **Size:** 43 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82CA1A00`

- **Address:** `0x82CA1A00` · **Size:** 42 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82CA1E98`

- **Address:** `0x82CA1E98` · **Size:** 41 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA1C38`

### `SwitchDispatch_82D7F5B0`

- **Address:** `0x82D7F5B0` · **Size:** 76 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82D82B48`

- **Address:** `0x82D82B48` · **Size:** 78 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82D97098`

- **Address:** `0x82D97098` · **Size:** 225 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82D9E998`

- **Address:** `0x82D9E998` · **Size:** 17 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82D9E9F0`

- **Address:** `0x82D9E9F0` · **Size:** 25 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82DA60B0`

- **Address:** `0x82DA60B0` · **Size:** 184 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82DA8888`

### `SwitchDispatch_82DDC208`

- **Address:** `0x82DDC208` · **Size:** 113 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82DE4C10`

- **Address:** `0x82DE4C10` · **Size:** 118 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82DE6750`

- **Address:** `0x82DE6750` · **Size:** 57 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82E0F3A8`

- **Address:** `0x82E0F3A8` · **Size:** 44 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `SwitchDispatch_82E33B90`

- **Address:** `0x82E33B90` · **Size:** 36 insns · **Category:** switchdisp · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Switch / jump-table dispatcher: indexes a table by an integer argument and tail-calls the selected case.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.


## I. Trampolines & Getters

_Small helpers that return a constant / global / adjusted pointer or tail-call another function._

**91 functions.**

### `TailCall_82238FCC`

- **Address:** `0x82238FCC` · **Size:** 6 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Does:** [game code] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C20.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_8223900C`

### `TailCall_822396C8`

- **Address:** `0x822396C8` · **Size:** 6 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Does:** [game code] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82239708`

### `RetAddr_82267568`

- **Address:** `0x82267568` · **Size:** 3 insns · **Category:** const · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Const thunk: builds and returns the data address 0x83499170.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetAddr_82267C88`

- **Address:** `0x82267C88` · **Size:** 3 insns · **Category:** const · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Const thunk: builds and returns the data address 0x8334E2D4.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetConst67_8274B798`

- **Address:** `0x8274B798` · **Size:** 2 insns · **Category:** retimm · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Returns the immediate constant 67 in r3.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetConst173_82996258`

- **Address:** `0x82996258` · **Size:** 2 insns · **Category:** retimm · **Region:** game · **Confidence:** **high**

- **Does:** [game code] Returns the immediate constant 173 in r3.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82B84350`

- **Address:** `0x82B84350` · **Size:** 2 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Does:** [game code] Small trampoline (2 insns) that sets up a value and tail-calls 0x821FC1F0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821FC1F0`

### `TailCall_82BE9668`

- **Address:** `0x82BE9668` · **Size:** 6 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Does:** [game code] Small trampoline (6 insns) that sets up a value and tail-calls 0x82BE9680.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82BE9680`

### `TailCall_82BEA25C`

- **Address:** `0x82BEA25C` · **Size:** 5 insns · **Category:** tailcall · **Region:** game · **Confidence:** medium

- **Does:** [game code] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C1C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82BEA3D0`

### `TailCall_82C000F8`

- **Address:** `0x82C000F8` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C106A8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C106A8`

### `TailCall_82C00108`

- **Address:** `0x82C00108` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C0B660.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C0B660`

### `TailCall_82C00110`

- **Address:** `0x82C00110` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C0B678.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C0B678`

### `TailCall_82C00118`

- **Address:** `0x82C00118` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C0B680.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C0B680`

### `TailCall_82C00120`

- **Address:** `0x82C00120` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C0B688.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C0B688`

### `TailCall_82C00128`

- **Address:** `0x82C00128` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C0B690.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C0B690`

### `TailCall_82C0B680`

- **Address:** `0x82C0B680` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C49670.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C49670`

### `TailCall_82C0B688`

- **Address:** `0x82C0B688` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82C4BDD8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C4BDD8`

### `TailCall_82C14D58`

- **Address:** `0x82C14D58` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82C1DBA8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C1DBA8`

### `TailCall_82C5CFBC`

- **Address:** `0x82C5CFBC` · **Size:** 1 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (1 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82C867E8`

- **Address:** `0x82C867E8` · **Size:** 3 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (3 insns) that sets up a value and tail-calls 0x82C863E8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C863E8`

### `TailCall_82CA36C4`

- **Address:** `0x82CA36C4` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C24.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA36DC`

### `TailCall_82CA4DFC`

- **Address:** `0x82CA4DFC` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA4DD0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4E30`

### `TailCall_82CA5064`

- **Address:** `0x82CA5064` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA4FC0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA50F4`

### `TailCall_82CA53D0`

- **Address:** `0x82CA53D0` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5408`

### `TailCall_82CA7300`

- **Address:** `0x82CA7300` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C30.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA7318`

### `TailCall_82CA7C90`

- **Address:** `0x82CA7C90` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C3C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CA7C9C`

- **Address:** `0x82CA7C9C` · **Size:** 3 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (3 insns) that sets up a value and tail-calls 0x82CA2C3C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CA8060`

- **Address:** `0x82CA8060` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CA806C`

- **Address:** `0x82CA806C` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CA8154`

- **Address:** `0x82CA8154` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA81A0`

### `TailCall_82CA827C`

- **Address:** `0x82CA827C` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C30.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA82C0`

### `TailCall_82CA91E8`

- **Address:** `0x82CA91E8` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C24.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA9220`

### `TailCall_82CAB2D4`

- **Address:** `0x82CAB2D4` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CAB2A4.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB308`

### `TailCall_82CAF40C`

- **Address:** `0x82CAF40C` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAF424`

### `TailCall_82CAF658`

- **Address:** `0x82CAF658` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAF690`

### `TailCall_82CAFAFC`

- **Address:** `0x82CAFAFC` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAFB34`

### `TailCall_82CAFC88`

- **Address:** `0x82CAFC88` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAFCC0`

### `TailCall_82CAFEE4`

- **Address:** `0x82CAFEE4` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAFF18`

### `TailCall_82CB0184`

- **Address:** `0x82CB0184` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB01BC`

### `TailCall_82CB49B0`

- **Address:** `0x82CB49B0` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CB4934.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CB4EAC`

- **Address:** `0x82CB4EAC` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `TailCall_82CB4FDC`

- **Address:** `0x82CB4FDC` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CB4FEC`

- **Address:** `0x82CB4FEC` · **Size:** 3 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (3 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `TailCall_82CB5918`

- **Address:** `0x82CB5918` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C30.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB5930`

### `TailCall_82CB68F4`

- **Address:** `0x82CB68F4` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB692C`

### `TailCall_82CB6C90`

- **Address:** `0x82CB6C90` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C28.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB6CC8`

### `TailCall_82CB9018`

- **Address:** `0x82CB9018` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C28.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB9030`

### `TailCall_82CBA370`

- **Address:** `0x82CBA370` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C3C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBA3A8`

### `TailCall_82CBA8D0`

- **Address:** `0x82CBA8D0` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C3C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBA8E4`

### `TailCall_82CC0494`

- **Address:** `0x82CC0494` · **Size:** 4 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (4 insns) that sets up a value and tail-calls 0x82CC04B0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CC04B0`

### `TailCall_82CC04B0`

- **Address:** `0x82CC04B0` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C14.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CC04E8`

### `TailCall_82CD1690`

- **Address:** `0x82CD1690` · **Size:** 1 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (1 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82CE1510`

- **Address:** `0x82CE1510` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE0C68.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE0C68`

### `TailCall_82CE1520`

- **Address:** `0x82CE1520` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE11E0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE11E0`

### `TailCall_82CE1548`

- **Address:** `0x82CE1548` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE0C20.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE0C20`

### `TailCall_82CE3750`

- **Address:** `0x82CE3750` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3400.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3400`

### `TailCall_82CE3760`

- **Address:** `0x82CE3760` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3220.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3220`

### `TailCall_82CE3768`

- **Address:** `0x82CE3768` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3238.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3238`

### `TailCall_82CE3770`

- **Address:** `0x82CE3770` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3460.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3460`

### `TailCall_82CE3798`

- **Address:** `0x82CE3798` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE0BE8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE0BE8`

### `TailCall_82CE37A0`

- **Address:** `0x82CE37A0` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3228.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3228`

### `TailCall_82CE37A8`

- **Address:** `0x82CE37A8` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82CE3640.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CE3640`

### `TailCall_82E7E4F8`

- **Address:** `0x82E7E4F8` · **Size:** 4 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (4 insns) that sets up a value and tail-calls 0x82E7E388.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82E7E388`

### `RetAddr_82E8F9E8`

- **Address:** `0x82E8F9E8` · **Size:** 3 insns · **Category:** const · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Const thunk: builds and returns the data address 0x83345BAC.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetAddr_82EB7ED0`

- **Address:** `0x82EB7ED0` · **Size:** 3 insns · **Category:** const · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Const thunk: builds and returns the data address 0x8334CD2C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetAddr_82EC1918`

- **Address:** `0x82EC1918` · **Size:** 3 insns · **Category:** const · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Const thunk: builds and returns the data address 0x8334E1C0.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetAddr_82EC4E58`

- **Address:** `0x82EC4E58` · **Size:** 3 insns · **Category:** const · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Const thunk: builds and returns the data address 0x8334E94C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `RetConst0_82F04430`

- **Address:** `0x82F04430` · **Size:** 2 insns · **Category:** retimm · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Returns the immediate constant 0 in r3.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82F0BAF8`

- **Address:** `0x82F0BAF8` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82F10110.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82F10110`

### `TailCall_82F0BB08`

- **Address:** `0x82F0BB08` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82F10110.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82F10110`

### `RetConst1_82F279D8`

- **Address:** `0x82F279D8` · **Size:** 2 insns · **Category:** retimm · **Region:** SDK · **Confidence:** **high**

- **Does:** [SDK runtime] Returns the immediate constant 1 in r3.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_82F38DA0`

- **Address:** `0x82F38DA0` · **Size:** 4 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (4 insns) that sets up a value and tail-calls 0x82F328D8.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82F328D8`

### `TailCall_82F56690`

- **Address:** `0x82F56690` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82F57290.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82F57290`

### `TailCall_82FE7EF8`

- **Address:** `0x82FE7EF8` · **Size:** 2 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (2 insns) that sets up a value and tail-calls 0x82FEC260.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82FEC260`

### `TailCall_82FE7F00`

- **Address:** `0x82FE7F00` · **Size:** 3 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (3 insns) that sets up a value and tail-calls 0x82FEC558.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82FEC558`

### `TailCall_83001184`

- **Address:** `0x83001184` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C24.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830011BC`

### `TailCall_830013A8`

- **Address:** `0x830013A8` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C30.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830013C0`

### `TailCall_8300164C`

- **Address:** `0x8300164C` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C34.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_83001684`

### `TailCall_830017A0`

- **Address:** `0x830017A0` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830017D8`

### `TailCall_83002F18`

- **Address:** `0x83002F18` · **Size:** 5 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (5 insns) that sets up a value and tail-calls 0x82CA2C3C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_83002F2C`

### `TailCall_83003414`

- **Address:** `0x83003414` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x8300339C.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_83003494`

### `TailCall_8300342C`

- **Address:** `0x8300342C` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_83003444`

### `IndirectCall_83095890`

- **Address:** `0x83095890` · **Size:** 68 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `TailCall_830F0980`

- **Address:** `0x830F0980` · **Size:** 6 insns · **Category:** tailcall · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Small trampoline (6 insns) that sets up a value and tail-calls 0x82CA2C38.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830F09B8`

### `IndirectCall_8310FC68`

- **Address:** `0x8310FC68` · **Size:** 32 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831B31B8`

- **Address:** `0x831B31B8` · **Size:** 30 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831FBA60`

- **Address:** `0x831FBA60` · **Size:** 26 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831FBAC8`

- **Address:** `0x831FBAC8` · **Size:** 25 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831FBB30`

- **Address:** `0x831FBB30` · **Size:** 23 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831FBB90`

- **Address:** `0x831FBB90` · **Size:** 102 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `IndirectCall_831FBD28`

- **Address:** `0x831FBD28` · **Size:** 91 insns · **Category:** indirect · **Region:** SDK · **Confidence:** medium

- **Does:** [SDK runtime] Indirect-call thunk: computes a target and jumps via mtctr/bctr.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.


## J. Unnamed Body Functions

_Functions with real code but no resolved import or strong signature (many are mid-function pointer targets). Named by address; description states only what the assembly shows. Lowest confidence._

**138 functions.**

### `Func_82B387D0`

- **Address:** `0x82B387D0` · **Size:** 14 insns · **Category:** body · **Region:** game · **Confidence:** low

- **Role:** Func routine (14 insns, game)

- **Does:** [game code] Body function, 14 insns, no prologue (mid-function target or leaf). Calls: sub_82CC0D58. First: `addi r6,r31,80`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CC0D58`

### `Func_82B387E8`

- **Address:** `0x82B387E8` · **Size:** 8 insns · **Category:** body · **Region:** game · **Confidence:** low

- **Role:** Func routine (8 insns, game)

- **Does:** [game code] Body function, 8 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82C1DC98`

- **Address:** `0x82C1DC98` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CA6320. First: `lis r10,-32247`; last: `b 0x82ca6320`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA6320`

### `Func_82C59698`

- **Address:** `0x82C59698` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_82C597F0. First: `bl 0x82c597f0`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C597F0`

### `Func_82C5AC4C`

- **Address:** `0x82C5AC4C` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CA8210. First: `bl 0x82ca8210`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA8210`

### `Func_82C5DA68`

- **Address:** `0x82C5DA68` · **Size:** 18 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (18 insns, SDK)

- **Does:** [SDK runtime] Body function, 18 insns, no prologue (mid-function target or leaf). Calls: sub_82C60BD8, sub_82C60E18, sub_82C61080. First: `bl 0x82c60e18`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C60BD8`, `sub_82C60E18`, `sub_82C61080`

### `Func_82C5DA94`

- **Address:** `0x82C5DA94` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_82C60BD8. First: `bl 0x82c60bd8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82C60BD8`

### `Func_82C89A0C`

- **Address:** `0x82C89A0C` · **Size:** 3 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (3 insns, SDK)

- **Does:** [SDK runtime] Body function, 3 insns, no prologue (mid-function target or leaf). First: `stw r4,0(r6)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82C8D820`

- **Address:** `0x82C8D820` · **Size:** 30 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (30 insns, SDK)

- **Does:** [SDK runtime] Body function, 30 insns, no prologue (mid-function target or leaf). First: `lwz r10,0(r6)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CA33AC`

- **Address:** `0x82CA33AC` · **Size:** 20 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (20 insns, SDK)

- **Does:** [SDK runtime] Body function, 20 insns, no prologue (mid-function target or leaf). Calls: sub_82CA97A8, sub_82CAC520, sub_82CBBF60, sub_82CC0728. First: `lwz r3,88(r11)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA97A8`, `sub_82CAC520`, `sub_82CBBF60`, `sub_82CC0728`

### `Func_82CA33E4`

- **Address:** `0x82CA33E4` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). Calls: sub_82CA97A8. First: `bl 0x82ca97a8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA97A8`

### `Func_82CA3614`

- **Address:** `0x82CA3614` · **Size:** 50 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (50 insns, SDK)

- **Does:** [SDK runtime] Body function, 50 insns, no prologue (mid-function target or leaf). Calls: sub_82CA36C4, sub_82CA36DC, sub_82CA8570, sub_82CAACD0. First: `lis r24,-31921`; last: `b 0x82ca2c24`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA36C4`, `sub_82CA36DC`, `sub_82CA8570`, `sub_82CAACD0`

### `Func_82CA47C4`

- **Address:** `0x82CA47C4` · **Size:** 28 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (28 insns, SDK)

- **Does:** [SDK runtime] Body function, 28 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4854, sub_82CAB770, sub_82CAF038, sub_82CAF450. First: `lbz r11,0(r30)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4854`, `sub_82CAB770`, `sub_82CAF038`, `sub_82CAF450`

### `Func_82CA4814`

- **Address:** `0x82CA4814` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4854. First: `mr r8,r8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4854`

### `Func_82CA4A64`

- **Address:** `0x82CA4A64` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4920, sub_82CA4AB8. First: `mr r3,r30`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4920`, `sub_82CA4AB8`

### `Func_82CA4A70`

- **Address:** `0x82CA4A70` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4AB8. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4AB8`

### `Func_82CA4DE4`

- **Address:** `0x82CA4DE4` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4AF0, sub_82CA4E30. First: `mr r6,r30`; last: `b 0x82ca4dd0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4AF0`, `sub_82CA4E30`

### `Func_82CA4FB0`

- **Address:** `0x82CA4FB0` · **Size:** 51 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (51 insns, SDK)

- **Does:** [SDK runtime] Body function, 51 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4F00, sub_82CA5064, sub_82CA507C, sub_82CA50F4, sub_82CA88E0. First: `lis r11,-31921`; last: `b 0x82ca4fc0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4F00`, `sub_82CA5064`, `sub_82CA507C`, `sub_82CA50F4`, `sub_82CA88E0`

### `Func_82CA5004`

- **Address:** `0x82CA5004` · **Size:** 30 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (30 insns, SDK)

- **Does:** [SDK runtime] Body function, 30 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4F00, sub_82CA5064, sub_82CA50F4. First: `lwz r11,0(r29)`; last: `b 0x82ca4fc0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4F00`, `sub_82CA5064`, `sub_82CA50F4`

### `Func_82CA507C`

- **Address:** `0x82CA507C` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CA50A4. First: `mr r8,r8`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA50A4`

### `Func_82CA51C0`

- **Address:** `0x82CA51C0` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4F00, sub_82CA521C. First: `mr r3,r30`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4F00`, `sub_82CA521C`

### `Func_82CA51CC`

- **Address:** `0x82CA51CC` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CA521C. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA521C`

### `Func_82CA53BC`

- **Address:** `0x82CA53BC` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5260, sub_82CA5408. First: `mr r5,r29`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5260`, `sub_82CA5408`

### `Func_82CA56DC`

- **Address:** `0x82CA56DC` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5440, sub_82CA5730. First: `mr r3,r30`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5440`, `sub_82CA5730`

### `Func_82CA56E8`

- **Address:** `0x82CA56E8` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5730. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5730`

### `Func_82CA71C8`

- **Address:** `0x82CA71C8` · **Size:** 84 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (84 insns, SDK)

- **Does:** [SDK runtime] Body function, 84 insns, no prologue (mid-function target or leaf). Calls: sub_8223F990, sub_82CA7300, sub_82CA7318, sub_82CAB4E0, sub_82CAB5B8. First: `lwz r11,12(r30)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_8223F990`, `sub_82CA7300`, `sub_82CA7318`, `sub_82CAB4E0`, `sub_82CAB5B8`, `sub_82CAB630`, `sub_82CAB770`, `sub_82CAF6C8`

### `Func_82CA7C70`

- **Address:** `0x82CA7C70` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). Calls: sub_82CAC610. First: `bl 0x82cac610`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAC610`

### `Func_82CA8038`

- **Address:** `0x82CA8038` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CA8060. First: `addic. r27,r27,-1`; last: `b 0x82ca8038`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA8060`

### `Func_82CA8128`

- **Address:** `0x82CA8128` · **Size:** 16 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (16 insns, SDK)

- **Does:** [SDK runtime] Body function, 16 insns, no prologue (mid-function target or leaf). Calls: sub_82CA81A0. First: `addic. r28,r28,-1`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA81A0`

### `Func_82CA8248`

- **Address:** `0x82CA8248` · **Size:** 18 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (18 insns, SDK)

- **Does:** [SDK runtime] Body function, 18 insns, no prologue (mid-function target or leaf). Calls: sub_82CA82C0. First: `stw r28,80(r31)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA82C0`

### `Func_82CA8B14`

- **Address:** `0x82CA8B14` · **Size:** 23 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (23 insns, SDK)

- **Does:** [SDK runtime] Body function, 23 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4AF0, sub_82CA8B90, sub_82CAB4E0, sub_82CAB5B8. First: `mr r3,r30`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4AF0`, `sub_82CA8B90`, `sub_82CAB4E0`, `sub_82CAB5B8`

### `Func_82CA8B44`

- **Address:** `0x82CA8B44` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CA8B90. First: `mr r8,r8`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA8B90`

### `Func_82CA9064`

- **Address:** `0x82CA9064` · **Size:** 103 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (103 insns, SDK)

- **Does:** [SDK runtime] Body function, 103 insns, no prologue (mid-function target or leaf). Calls: sub_82CA91E8, sub_82CA9220, sub_82CAB630, sub_82CAB770, sub_82CAF6C8. First: `lwz r11,12(r30)`; last: `b 0x82ca2c24`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA91E8`, `sub_82CA9220`, `sub_82CAB630`, `sub_82CAB770`, `sub_82CAF6C8`, `sub_82CB5958`

### `Func_82CAB2B8`

- **Address:** `0x82CAB2B8` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_82CAAF88, sub_82CAB308. First: `mr r7,r30`; last: `b 0x82cab2a4`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAAF88`, `sub_82CAB308`

### `Func_82CAF620`

- **Address:** `0x82CAF620` · **Size:** 20 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (20 insns, SDK)

- **Does:** [SDK runtime] Body function, 20 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAF478, sub_82CAF658, sub_82CAF690. First: `lwzx r11,r27,r28`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAF478`, `sub_82CAF658`, `sub_82CAF690`

### `Func_82CAFAB0`

- **Address:** `0x82CAFAB0` · **Size:** 25 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (25 insns, SDK)

- **Does:** [SDK runtime] Body function, 25 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAB7A8, sub_82CAF788, sub_82CAFAFC, sub_82CAFB34. First: `lwzx r11,r27,r28`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAB7A8`, `sub_82CAF788`, `sub_82CAFAFC`, `sub_82CAFB34`

### `Func_82CAFC28`

- **Address:** `0x82CAFC28` · **Size:** 30 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (30 insns, SDK)

- **Does:** [SDK runtime] Body function, 30 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAB7A8, sub_82CAFC88, sub_82CAFCC0, sub_82CB8C28. First: `lwzx r11,r28,r29`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAB7A8`, `sub_82CAFC88`, `sub_82CAFCC0`, `sub_82CB8C28`, `sub_82CC0758`, `sub_82CC1130`

### `Func_82CAFE98`

- **Address:** `0x82CAFE98` · **Size:** 25 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (25 insns, SDK)

- **Does:** [SDK runtime] Body function, 25 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5DC0, sub_82CAB770, sub_82CAFEE4, sub_82CAFF18, sub_82CB5B78. First: `lwzx r11,r29,r30`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5DC0`, `sub_82CAB770`, `sub_82CAFEE4`, `sub_82CAFF18`, `sub_82CB5B78`

### `Func_82CB0138`

- **Address:** `0x82CB0138` · **Size:** 25 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (25 insns, SDK)

- **Does:** [SDK runtime] Body function, 25 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAB7A8, sub_82CAFFA8, sub_82CB0184, sub_82CB01BC. First: `lwzx r11,r27,r28`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAB7A8`, `sub_82CAFFA8`, `sub_82CB0184`, `sub_82CB01BC`

### `Func_82CB0FB0`

- **Address:** `0x82CB0FB0` · **Size:** 68 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (68 insns, SDK)

- **Does:** [SDK runtime] Body function, 68 insns, no prologue (mid-function target or leaf). Calls: sub_82CB1040, sub_82CB10E0. First: `cmpwi cr6,r29,8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB1040`, `sub_82CB10E0`

### `Func_82CB1040`

- **Address:** `0x82CB1040` · **Size:** 32 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (32 insns, SDK)

- **Does:** [SDK runtime] Body function, 32 insns, no prologue (mid-function target or leaf). Calls: sub_82CB10E0. First: `mr r8,r8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB10E0`

### `Func_82CB1390`

- **Address:** `0x82CB1390` · **Size:** 32 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (32 insns, SDK)

- **Does:** [SDK runtime] Body function, 32 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5DC0, sub_82CB13EC, sub_82CB1410. First: `lwz r10,4(r30)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5DC0`, `sub_82CB13EC`, `sub_82CB1410`

### `Func_82CB13EC`

- **Address:** `0x82CB13EC` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). Calls: sub_82CB1410. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB1410`

### `Func_82CB4938`

- **Address:** `0x82CB4938` · **Size:** 36 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (36 insns, SDK)

- **Does:** [SDK runtime] Body function, 36 insns, no prologue (mid-function target or leaf). Calls: sub_82CB4748, sub_82CB49B0, sub_82CB49DC, sub_82CB5800, sub_82CBA3E0. First: `cmpw cr6,r28,r27`; last: `b 0x82cb4934`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB4748`, `sub_82CB49B0`, `sub_82CB49DC`, `sub_82CB5800`, `sub_82CBA3E0`

### `Func_82CB4974`

- **Address:** `0x82CB4974` · **Size:** 21 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (21 insns, SDK)

- **Does:** [SDK runtime] Body function, 21 insns, no prologue (mid-function target or leaf). Calls: sub_82CB4748, sub_82CB49B0, sub_82CBA3E0. First: `lwz r11,4(r11)`; last: `b 0x82cb4934`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB4748`, `sub_82CB49B0`, `sub_82CBA3E0`

### `Func_82CB49C0`

- **Address:** `0x82CB49C0` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). First: `lwz r27,204(r31)`; last: `b 0x82cb4934`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB49DC`

- **Address:** `0x82CB49DC` · **Size:** 15 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (15 insns, SDK)

- **Does:** [SDK runtime] Body function, 15 insns, no prologue (mid-function target or leaf). Calls: sub_82CB4748, sub_82CB4A18, sub_82CB5800. First: `mr r8,r8`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB4748`, `sub_82CB4A18`, `sub_82CB5800`

### `Func_82CB4B1C`

- **Address:** `0x82CB4B1C` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). First: `lwz r3,24(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB4B28`

- **Address:** `0x82CB4B28` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB4B38`

- **Address:** `0x82CB4B38` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). Calls: sub_82CB57A0. First: `bl 0x82cb57a0`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `Func_82CB4CB8`

- **Address:** `0x82CB4CB8` · **Size:** 129 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (129 insns, SDK)

- **Does:** [SDK runtime] Body function, 129 insns, no prologue (mid-function target or leaf). Calls: sub_82CAA2E0, sub_82CB4E9C, sub_82CB5800, sub_82CBA500. First: `rlwinm. r11,r10,0,28,28`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAA2E0`, `sub_82CB4E9C`, `sub_82CB5800`, `sub_82CBA500`

### `Func_82CB4E9C`

- **Address:** `0x82CB4E9C` · **Size:** 8 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (8 insns, SDK)

- **Does:** [SDK runtime] Body function, 8 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB4F18`

- **Address:** `0x82CB4F18` · **Size:** 55 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (55 insns, SDK)

- **Does:** [SDK runtime] Body function, 55 insns, no prologue (mid-function target or leaf). Calls: sub_82CB4C48, sub_82CB4FDC. First: `mr r6,r29`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB4C48`, `sub_82CB4FDC`

### `Func_82CB56B0`

- **Address:** `0x82CB56B0` · **Size:** 15 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (15 insns, SDK)

- **Does:** [SDK runtime] Body function, 15 insns, no prologue (mid-function target or leaf). First: `lwz r3,0(r30)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB56BC`

- **Address:** `0x82CB56BC` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB56CC`

- **Address:** `0x82CB56CC` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CB57A0. First: `bl 0x82cb57a0`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `Func_82CB582C`

- **Address:** `0x82CB582C` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). Calls: sub_82CB57A0. First: `mtctr r11`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `Func_82CB5834`

- **Address:** `0x82CB5834` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_82CB57A0. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `Func_82CB5840`

- **Address:** `0x82CB5840` · **Size:** 4 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (4 insns, SDK)

- **Does:** [SDK runtime] Body function, 4 insns, no prologue (mid-function target or leaf). Calls: sub_82CB57A0. First: `bl 0x82cb57a0`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB57A0`

### `Func_82CB5894`

- **Address:** `0x82CB5894` · **Size:** 33 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (33 insns, SDK)

- **Does:** [SDK runtime] Body function, 33 insns, no prologue (mid-function target or leaf). Calls: sub_82CA49D8, sub_82CA5DC0, sub_82CB5918. First: `li r28,3`; last: `b 0x82cb58a0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA49D8`, `sub_82CA5DC0`, `sub_82CB5918`

### `Func_82CB5BB4`

- **Address:** `0x82CB5BB4` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). First: `mtctr r11`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB5BC0`

- **Address:** `0x82CB5BC0` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CB5BD0`

- **Address:** `0x82CB5BD0` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CC0750. First: `lis r11,-16384`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CC0750`

### `Func_82CB68A8`

- **Address:** `0x82CB68A8` · **Size:** 25 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (25 insns, SDK)

- **Does:** [SDK runtime] Body function, 25 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAB7A8, sub_82CB6168, sub_82CB68F4, sub_82CB692C. First: `lwzx r11,r27,r28`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAB7A8`, `sub_82CB6168`, `sub_82CB68F4`, `sub_82CB692C`

### `Func_82CB6C48`

- **Address:** `0x82CB6C48` · **Size:** 24 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (24 insns, SDK)

- **Does:** [SDK runtime] Body function, 24 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAB7A8, sub_82CB6AA0, sub_82CB6C90, sub_82CB6CC8. First: `lwzx r11,r27,r28`; last: `b 0x82ca2c28`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAB7A8`, `sub_82CB6AA0`, `sub_82CB6C90`, `sub_82CB6CC8`

### `Func_82CB89E8`

- **Address:** `0x82CB89E8` · **Size:** 23 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (23 insns, SDK)

- **Does:** [SDK runtime] Body function, 23 insns, no prologue (mid-function target or leaf). Calls: sub_82CB8428, sub_82CB8A64. First: `mr r8,r6`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB8428`, `sub_82CB8A64`

### `Func_82CB8A08`

- **Address:** `0x82CB8A08` · **Size:** 15 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (15 insns, SDK)

- **Does:** [SDK runtime] Body function, 15 insns, no prologue (mid-function target or leaf). Calls: sub_82CB8A64. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CB8A64`

### `Func_82CBA360`

- **Address:** `0x82CBA360` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CBA120, sub_82CBA3A8. First: `mr r4,r30`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBA120`, `sub_82CBA3A8`

### `Func_82CBA76C`

- **Address:** `0x82CBA76C` · **Size:** 94 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (94 insns, SDK)

- **Does:** [SDK runtime] Body function, 94 insns, no prologue (mid-function target or leaf). Calls: sub_82CA5DC0, sub_82CAB678, sub_82CBA8E4, sub_82CBAF58, sub_82CBAFC0. First: `bl 0x82cbb0c0`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA5DC0`, `sub_82CAB678`, `sub_82CBA8E4`, `sub_82CBAF58`, `sub_82CBAFC0`, `sub_82CBB028`, `sub_82CBB090`, `sub_82CBB0A0`

### `Func_82CBAE6C`

- **Address:** `0x82CBAE6C` · **Size:** 16 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (16 insns, SDK)

- **Does:** [SDK runtime] Body function, 16 insns, no prologue (mid-function target or leaf). Calls: sub_82CBA740, sub_82CBAE88, sub_82CBAEAC. First: `lwz r11,27636(r30)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBA740`, `sub_82CBAE88`, `sub_82CBAEAC`

### `Func_82CBAE88`

- **Address:** `0x82CBAE88` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). Calls: sub_82CBAEAC. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBAEAC`

### `Func_82CBAF00`

- **Address:** `0x82CBAF00` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CBAB80, sub_82CBAF34. First: `mr r3,r30`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBAB80`, `sub_82CBAF34`

### `Func_82CBAF0C`

- **Address:** `0x82CBAF0C` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_82CBAF34. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBAF34`

### `Func_82CBC888`

- **Address:** `0x82CBC888` · **Size:** 55 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (55 insns, SDK)

- **Does:** [SDK runtime] Body function, 55 insns, no prologue (mid-function target or leaf). Calls: sub_82CAA2E0, sub_82CBC930, sub_82CBC97C, sub_82CBC9C4, sub_82CC0750. First: `cmplwi cr6,r26,0`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAA2E0`, `sub_82CBC930`, `sub_82CBC97C`, `sub_82CBC9C4`, `sub_82CC0750`

### `Func_82CBC930`

- **Address:** `0x82CBC930` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82CBC9C4. First: `mr r8,r8`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBC9C4`

### `Func_82CBC940`

- **Address:** `0x82CBC940` · **Size:** 16 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (16 insns, SDK)

- **Does:** [SDK runtime] Body function, 16 insns, no prologue (mid-function target or leaf). Calls: sub_82CAF450, sub_82CC1C18. First: `lis r3,-16384`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAF450`, `sub_82CC1C18`

### `Func_82CBC97C`

- **Address:** `0x82CBC97C` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_82CBC9C4. First: `mr r8,r8`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CBC9C4`

### `Func_82CC04A4`

- **Address:** `0x82CC04A4` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). Calls: sub_82CC04E8. First: `li r11,0`; last: `b 0x82ca2c14`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CC04E8`

### `Func_82CD182C`

- **Address:** `0x82CD182C` · **Size:** 8 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (8 insns, SDK)

- **Does:** [SDK runtime] Body function, 8 insns, no prologue (mid-function target or leaf). Calls: sub_822F2020. First: `bl 0x822f2020`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_822F2020`

### `Func_82CD18AC`

- **Address:** `0x82CD18AC` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). Calls: sub_822F1DB0, sub_82CD1608. First: `bl 0x82cd1608`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_822F1DB0`, `sub_82CD1608`

### `Func_82CD7948`

- **Address:** `0x82CD7948` · **Size:** 4 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (4 insns, SDK)

- **Does:** [SDK runtime] Body function, 4 insns, no prologue (mid-function target or leaf). First: `lwz r11,12(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CD8650`

- **Address:** `0x82CD8650` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). First: `li r11,6`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CDBAE0`

- **Address:** `0x82CDBAE0` · **Size:** 5 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (5 insns, SDK)

- **Does:** [SDK runtime] Body function, 5 insns, no prologue (mid-function target or leaf). First: `mr r10,r3`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82CE5D98`

- **Address:** `0x82CE5D98` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). First: `lbz r10,60(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82E86188`

- **Address:** `0x82E86188` · **Size:** 17 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (17 insns, SDK)

- **Does:** [SDK runtime] Body function, 17 insns, no prologue (mid-function target or leaf). First: `lwz r9,8(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82E87A10`

- **Address:** `0x82E87A10` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). First: `lis r11,-32256`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82EF8D20`

- **Address:** `0x82EF8D20` · **Size:** 5 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (5 insns, SDK)

- **Does:** [SDK runtime] Body function, 5 insns, no prologue (mid-function target or leaf). First: `li r11,-1`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82F2C2E8`

- **Address:** `0x82F2C2E8` · **Size:** 5 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (5 insns, SDK)

- **Does:** [SDK runtime] Body function, 5 insns, no prologue (mid-function target or leaf). First: `lwz r11,28(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82F2C3C0`

- **Address:** `0x82F2C3C0` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). First: `lwz r11,28(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82F435A8`

- **Address:** `0x82F435A8` · **Size:** 9 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (9 insns, SDK)

- **Does:** [SDK runtime] Body function, 9 insns, no prologue (mid-function target or leaf). First: `lfs f0,72(r4)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_82FB7470`

- **Address:** `0x82FB7470` · **Size:** 4 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (4 insns, SDK)

- **Does:** [SDK runtime] Body function, 4 insns, no prologue (mid-function target or leaf). First: `lwz r11,24(r4)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_83000858`

- **Address:** `0x83000858` · **Size:** 103 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (103 insns, SDK)

- **Does:** [SDK runtime] Body function, 103 insns, no prologue (mid-function target or leaf). Calls: sub_82170CC8, sub_82CA3C68, sub_82CAB678, sub_82CAB770, sub_82CAC520. First: `cmpwi cr6,r27,0`; last: `b 0x82ca2c28`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82170CC8`, `sub_82CA3C68`, `sub_82CAB678`, `sub_82CAB770`, `sub_82CAC520`, `sub_830005D8`, `sub_83000728`, `sub_830009AC`

### `Func_830009AC`

- **Address:** `0x830009AC` · **Size:** 18 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (18 insns, SDK)

- **Does:** [SDK runtime] Body function, 18 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_830009F4. First: `mr r8,r8`; last: `b 0x82ca2c28`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_830009F4`

### `Func_83000AF0`

- **Address:** `0x83000AF0` · **Size:** 140 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (140 insns, SDK)

- **Does:** [SDK runtime] Body function, 140 insns, no prologue (mid-function target or leaf). Calls: sub_82170CC8, sub_8221EE38, sub_82CA6CF8, sub_82CAB678, sub_82CAB770. First: `lis r11,-31946`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82170CC8`, `sub_8221EE38`, `sub_82CA6CF8`, `sub_82CAB678`, `sub_82CAB770`, `sub_82CAF298`, `sub_82CAF558`, `sub_82CB8AE8`

### `Func_83000CF4`

- **Address:** `0x83000CF4` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_83000D40. First: `mr r8,r8`; last: `b 0x82ca2c2c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_83000D40`

### `Func_83001060`

- **Address:** `0x83001060` · **Size:** 79 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (79 insns, SDK)

- **Does:** [SDK runtime] Body function, 79 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB630, sub_82CAB770, sub_82CAF6C8, sub_83001184, sub_830011BC. First: `lwz r11,12(r30)`; last: `b 0x82ca2c24`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB630`, `sub_82CAB770`, `sub_82CAF6C8`, `sub_83001184`, `sub_830011BC`

### `Func_83001318`

- **Address:** `0x83001318` · **Size:** 42 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (42 insns, SDK)

- **Does:** [SDK runtime] Body function, 42 insns, no prologue (mid-function target or leaf). Calls: sub_82CA3C68, sub_82CA4E68, sub_82CAF720, sub_830013A8, sub_830013C0. First: `mr r3,r30`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA3C68`, `sub_82CA4E68`, `sub_82CAF720`, `sub_830013A8`, `sub_830013C0`

### `Func_83001514`

- **Address:** `0x83001514` · **Size:** 84 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (84 insns, SDK)

- **Does:** [SDK runtime] Body function, 84 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB630, sub_82CAB770, sub_82CAF6C8, sub_82CB5958, sub_8300164C. First: `lwz r11,12(r30)`; last: `b 0x82ca2c34`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB630`, `sub_82CAB770`, `sub_82CAF6C8`, `sub_82CB5958`, `sub_8300164C`, `sub_83001684`

### `Func_8300172C`

- **Address:** `0x8300172C` · **Size:** 35 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (35 insns, SDK)

- **Does:** [SDK runtime] Body function, 35 insns, no prologue (mid-function target or leaf). Calls: sub_82CAF6C8, sub_830017D8. First: `lwz r11,12(r30)`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAF6C8`, `sub_830017D8`

### `Func_83002974`

- **Address:** `0x83002974` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_82CA97A8, sub_82CC0728. First: `lwz r3,88(r11)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA97A8`, `sub_82CC0728`

### `Func_8300298C`

- **Address:** `0x8300298C` · **Size:** 6 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (6 insns, SDK)

- **Does:** [SDK runtime] Body function, 6 insns, no prologue (mid-function target or leaf). Calls: sub_82CA97A8. First: `bl 0x82ca97a8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA97A8`

### `Func_83002E84`

- **Address:** `0x83002E84` · **Size:** 42 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (42 insns, SDK)

- **Does:** [SDK runtime] Body function, 42 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4E68, sub_82CB0068, sub_83002F18, sub_83002F2C. First: `mr r3,r30`; last: `b 0x82ca2c3c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4E68`, `sub_82CB0068`, `sub_83002F18`, `sub_83002F2C`

### `Func_8300338C`

- **Address:** `0x8300338C` · **Size:** 40 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (40 insns, SDK)

- **Does:** [SDK runtime] Body function, 40 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4920, sub_82CA88E0, sub_83003414, sub_8300342C, sub_83003494. First: `lis r11,-31921`; last: `b 0x8300339c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4920`, `sub_82CA88E0`, `sub_83003414`, `sub_8300342C`, `sub_83003494`

### `Func_830033E0`

- **Address:** `0x830033E0` · **Size:** 19 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (19 insns, SDK)

- **Does:** [SDK runtime] Body function, 19 insns, no prologue (mid-function target or leaf). Calls: sub_82CA4920, sub_83003414, sub_83003494. First: `lwz r11,0(r30)`; last: `b 0x8300339c`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CA4920`, `sub_83003414`, `sub_83003494`

### `Func_830046B4`

- **Address:** `0x830046B4` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_830043D0, sub_83004708. First: `mr r3,r30`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830043D0`, `sub_83004708`

### `Func_830046C0`

- **Address:** `0x830046C0` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_83004708. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_83004708`

### `Func_83040FEC`

- **Address:** `0x83040FEC` · **Size:** 224 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (224 insns, SDK)

- **Does:** [SDK runtime] Body function, 224 insns, no prologue (mid-function target or leaf). Calls: sub_82B56750, sub_82C43198, sub_8301DE30, sub_83026048, sub_8302D8A8. First: `lwz r3,8(r30)`; last: `b 0x83040db8`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82B56750`, `sub_82C43198`, `sub_8301DE30`, `sub_83026048`, `sub_8302D8A8`

### `Func_8304100C`

- **Address:** `0x8304100C` · **Size:** 216 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (216 insns, SDK)

- **Does:** [SDK runtime] Body function, 216 insns, no prologue (mid-function target or leaf). Calls: sub_82B56750, sub_82C43198, sub_8301DE30, sub_83026048, sub_8302D8A8. First: `mr r8,r8`; last: `b 0x83040db8`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82B56750`, `sub_82C43198`, `sub_8301DE30`, `sub_83026048`, `sub_8302D8A8`

### `Func_8304101C`

- **Address:** `0x8304101C` · **Size:** 227 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (227 insns, SDK)

- **Does:** [SDK runtime] Body function, 227 insns, no prologue (mid-function target or leaf). Calls: sub_82B56750, sub_82C43198, sub_8301DE30, sub_83026048, sub_8302D8A8. First: `lwz r30,676(r31)`; last: `b 0x83040db8`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82B56750`, `sub_82C43198`, `sub_8301DE30`, `sub_83026048`, `sub_8302D8A8`

### `Func_830ACB28`

- **Address:** `0x830ACB28` · **Size:** 30 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (30 insns, SDK)

- **Does:** [SDK runtime] Body function, 30 insns, no prologue (mid-function target or leaf). First: `lwz r6,4(r4)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_830F07A8`

- **Address:** `0x830F07A8` · **Size:** 28 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (28 insns, SDK)

- **Does:** [SDK runtime] Body function, 28 insns, no prologue (mid-function target or leaf). Calls: sub_82CAB770, sub_82CAF450, sub_830F0838, sub_830F09F0. First: `lhz r11,0(r30)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82CAB770`, `sub_82CAF450`, `sub_830F0838`, `sub_830F09F0`

### `Func_830F07F8`

- **Address:** `0x830F07F8` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). Calls: sub_830F0838. First: `mr r8,r8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830F0838`

### `Func_830F0914`

- **Address:** `0x830F0914` · **Size:** 33 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (33 insns, SDK)

- **Does:** [SDK runtime] Body function, 33 insns, no prologue (mid-function target or leaf). Calls: sub_82240578, sub_82CB7DA0, sub_82CC1798, sub_830F0980, sub_830F09B8. First: `bl 0x82240578`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82240578`, `sub_82CB7DA0`, `sub_82CC1798`, `sub_830F0980`, `sub_830F09B8`

### `Func_830F1208`

- **Address:** `0x830F1208` · **Size:** 23 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (23 insns, SDK)

- **Does:** [SDK runtime] Body function, 23 insns, no prologue (mid-function target or leaf). Calls: sub_830F0C48, sub_830F1284. First: `mr r8,r6`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830F0C48`, `sub_830F1284`

### `Func_830F1228`

- **Address:** `0x830F1228` · **Size:** 15 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (15 insns, SDK)

- **Does:** [SDK runtime] Body function, 15 insns, no prologue (mid-function target or leaf). Calls: sub_830F1284. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830F1284`

### `Func_830FB43C`

- **Address:** `0x830FB43C` · **Size:** 28 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (28 insns, SDK)

- **Does:** [SDK runtime] Body function, 28 insns, no prologue (mid-function target or leaf). Calls: sub_830FA9D8, sub_830FB490. First: `lwz r11,236(r31)`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FA9D8`, `sub_830FB490`

### `Func_830FB490`

- **Address:** `0x830FB490` · **Size:** 7 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (7 insns, SDK)

- **Does:** [SDK runtime] Body function, 7 insns, no prologue (mid-function target or leaf). First: `mr r8,r8`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_830FB4A0`

- **Address:** `0x830FB4A0` · **Size:** 14 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (14 insns, SDK)

- **Does:** [SDK runtime] Body function, 14 insns, no prologue (mid-function target or leaf). First: `lis r11,-32248`; last: `b 0x82ca2c38`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_830FD788`

- **Address:** `0x830FD788` · **Size:** 41 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (41 insns, SDK)

- **Does:** [SDK runtime] Body function, 41 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_82B96C90, sub_830FD3B0, sub_830FD478, sub_830FD7A8. First: `lwz r3,772(r30)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_82B96C90`, `sub_830FD3B0`, `sub_830FD478`, `sub_830FD7A8`, `sub_830FD7E4`

### `Func_830FD7A8`

- **Address:** `0x830FD7A8` · **Size:** 33 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (33 insns, SDK)

- **Does:** [SDK runtime] Body function, 33 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_82B96C90, sub_830FD478, sub_830FD7E4. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_82B96C90`, `sub_830FD478`, `sub_830FD7E4`

### `Func_830FD7B8`

- **Address:** `0x830FD7B8` · **Size:** 30 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (30 insns, SDK)

- **Does:** [SDK runtime] Body function, 30 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_82B96C90, sub_830FD478, sub_830FD7E4. First: `lwz r30,132(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_82B96C90`, `sub_830FD478`, `sub_830FD7E4`

### `Func_830FD7C8`

- **Address:** `0x830FD7C8` · **Size:** 26 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (26 insns, SDK)

- **Does:** [SDK runtime] Body function, 26 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_82B96C90, sub_830FD478, sub_830FD7E4. First: `lwz r3,768(r30)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_82B96C90`, `sub_830FD478`, `sub_830FD7E4`

### `Func_830FD7E4`

- **Address:** `0x830FD7E4` · **Size:** 19 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (19 insns, SDK)

- **Does:** [SDK runtime] Body function, 19 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_830FD478. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_830FD478`

### `Func_830FD7F4`

- **Address:** `0x830FD7F4` · **Size:** 16 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (16 insns, SDK)

- **Does:** [SDK runtime] Body function, 16 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_830FD478. First: `lwz r30,132(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_830FD478`

### `Func_830FDCEC`

- **Address:** `0x830FDCEC` · **Size:** 20 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (20 insns, SDK)

- **Does:** [SDK runtime] Body function, 20 insns, no prologue (mid-function target or leaf). Calls: sub_82D0DAE0, sub_830FD760. First: `lwz r4,140(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82D0DAE0`, `sub_830FD760`

### `Func_830FDCFC`

- **Address:** `0x830FDCFC` · **Size:** 16 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (16 insns, SDK)

- **Does:** [SDK runtime] Body function, 16 insns, no prologue (mid-function target or leaf). Calls: sub_82D0DAE0. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82D0DAE0`

### `Func_830FDD08`

- **Address:** `0x830FDD08` · **Size:** 13 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (13 insns, SDK)

- **Does:** [SDK runtime] Body function, 13 insns, no prologue (mid-function target or leaf). Calls: sub_82D0DAE0. First: `lwz r11,132(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_82D0DAE0`

### `Func_830FE168`

- **Address:** `0x830FE168` · **Size:** 58 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (58 insns, SDK)

- **Does:** [SDK runtime] Body function, 58 insns, no prologue (mid-function target or leaf). Calls: sub_830FCE20, sub_830FDBD8, sub_830FDCB8, sub_830FDD48, sub_830FDE28. First: `lwz r5,260(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FCE20`, `sub_830FDBD8`, `sub_830FDCB8`, `sub_830FDD48`, `sub_830FDE28`, `sub_830FE220`

### `Func_830FE220`

- **Address:** `0x830FE220` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_830FDCB8. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FDCB8`

### `Func_830FE230`

- **Address:** `0x830FE230` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_830FCEA8, sub_830FDCB8. First: `lwz r4,244(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FCEA8`, `sub_830FDCB8`

### `Func_830FE2E8`

- **Address:** `0x830FE2E8` · **Size:** 50 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (50 insns, SDK)

- **Does:** [SDK runtime] Body function, 50 insns, no prologue (mid-function target or leaf). Calls: sub_830FCE20, sub_830FDBD8, sub_830FDCB8, sub_830FDD48, sub_830FE010. First: `lwz r5,88(r31)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FCE20`, `sub_830FDBD8`, `sub_830FDCB8`, `sub_830FDD48`, `sub_830FE010`, `sub_830FE380`

### `Func_830FE380`

- **Address:** `0x830FE380` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_830FDCB8. First: `mr r8,r8`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FDCB8`

### `Func_830FE390`

- **Address:** `0x830FE390` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_830FCEA8, sub_830FDCB8. First: `li r4,0`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_830FCEA8`, `sub_830FDCB8`

### `Func_83117EDC`

- **Address:** `0x83117EDC` · **Size:** 61 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (61 insns, SDK)

- **Does:** [SDK runtime] Body function, 61 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18, sub_831176A0, sub_831178D8, sub_83117BA0. First: `mr r6,r30`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`, `sub_831176A0`, `sub_831178D8`, `sub_83117BA0`

### `Func_83117F88`

- **Address:** `0x83117F88` · **Size:** 12 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (12 insns, SDK)

- **Does:** [SDK runtime] Body function, 12 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18. First: `mr r8,r8`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`

### `Func_83117F94`

- **Address:** `0x83117F94` · **Size:** 10 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (10 insns, SDK)

- **Does:** [SDK runtime] Body function, 10 insns, no prologue (mid-function target or leaf). Calls: sub_821F5F18. First: `lwz r30,84(r31)`; last: `b 0x82ca2c30`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_821F5F18`

### `Func_831C5D28`

- **Address:** `0x831C5D28` · **Size:** 8 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (8 insns, SDK)

- **Does:** [SDK runtime] Body function, 8 insns, no prologue (mid-function target or leaf). First: `lwz r11,16(r3)`; last: `blr`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

### `Func_831FFA08`

- **Address:** `0x831FFA08` · **Size:** 11 insns · **Category:** body · **Region:** SDK · **Confidence:** low

- **Role:** Func routine (11 insns, SDK)

- **Does:** [SDK runtime] Body function, 11 insns, no prologue (mid-function target or leaf). Calls: sub_831DF3D0. First: `mr r11,r3`; last: `b 0x831df3d0`.

- **Trigger:** Internal call — reached as part of normal engine/SDK operation, no direct player action.

- **Calls:** `sub_831DF3D0`

