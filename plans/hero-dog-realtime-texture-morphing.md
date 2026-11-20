# Hero / dog black textures: realtime texture morphing (no readback)

**STATUS: IMPLEMENTED, verified in game** (hero face and dog no longer black
with `hero_dog_texture_readback = false`). Supersedes the targeted readback in
`hero-dog-texture-readback.md`, which has since been removed. The fix is now
always on; there is no config toggle.

## Root cause (from the XEX)

The hero's and dog's skin textures (age, morality, scars, makeup, ...) are built
by the game's texture morphing. A morph request becomes a morph job in
`sub_82A76018`, and the morph renderer `sub_82A69728` builds the texture in one
of two modes, chosen by a per-request "realtime" byte:

- **Baked (default, flag 0).** Each mip is drawn into a scratch render target
  and resolved to guest memory (the `0x12704000` scratch texture). The CPU then
  locks that texture READONLY and DXT-compresses it into the final texture
  (`bl sub_83008E98` at `0x82A6A3F4`; output formats D3DFMT_DXT1 / DXT2_3 / DXN).
  On a split-memory host the resolve never reaches the memory the CPU reads, so
  the compressed result is black. That is why a CPU readback of that one
  resolve fixed it.
- **Realtime (flag 1, "RealTimeTextureMorphing").** The morph is drawn and
  resolved straight into the final texture, and its mips are built on the GPU
  (`sub_821FC4A0`). Output formats are D3DFMT_A8R8G8B8 / X8R8G8B8. The CPU
  never reads the result, so it works with plain GPU resolves.

The format choice is in `sub_82A55EC0` (`0x82A56088..0x82A560E4`), using the
engine format table at `0x8331D988` (stride 0x70).

## Where the flag comes from

- `sub_8220EC00` (appearance update, the `TextureMorphs` block that Xenia's
  "Disable Texture Morphing" patch at `0x8220EF10` skips): request ctor
  `sub_82465E20` at `0x8220FA28`, flag from `lbz r7, 0xa6(r19)` at `0x8220FA18`.
  This is the path the hero and dog use (log: 4 requests for the dog, 20 for
  the hero, all with flag 0).
- `sub_82469850` (GraphicAppearanceMorph component, byte `+0xB2`, set from the
  character record "RealTimeTextureMorphing" or the script call
  SetRealTimeTextureMorphing): `lbz r27, 0xb2(r30)` at `0x82469904`.

Both end up in `sub_82A76018`, which copies the request byte with
`lbz r9, 0x20(r30)` at `0x82A7607C` into the job (`stb r9, 0x60(r31)`).

## Fix

One mid-asm hook, `fable2_hook_realtime_texture_morphing` at `0x82A7607C`
(after the load), sets `r9 = 1` so every morph job runs in realtime mode.
Always on. (It was the `[patches] realtime_texture_morphing` toggle, default
`true`; the toggle and the old `hero_dog_texture_readback` fallback were removed.)

Cost: the morphed textures are uncompressed (a few MB more guest memory). The
morphs are still only rebuilt when the appearance changes (a burst at load,
then nothing during normal play).

## Known separate issue

The dog shows small flickering light rectangles on its back and side. They
appear with the old readback method too, at 1x resolution scale, so they are
not caused by this change (see the "shifty dog" notes in
`hero-dog-shifty-texture-mipmap.md`). Not fixed here.

## Credit

The original diagnosis that the hero/dog textures need the resolve at
`0x12704000` on the CPU comes from just-harry's Unofficial Xenia femtofork for
Fable II. The "Disable Texture Morphing" Xenia patch (Guy) pointed at the
TextureMorphs block.
