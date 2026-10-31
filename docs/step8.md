# Step 8: TRS on the new renderer

The render lab (`lab/`) rebuilt TRS's renderer stage by stage, each stage
exact against the game's own on captured frames. Step 8 puts it in the game:
TRS draws every frame with it, and the old renderer goes.

Work happens on the branch `step8`; `main` keeps the old renderer playable
until the game runs on the new one, then `step8` merges.

## Stages

**A. The renderer as its own sources.** Its production modules move from
`lab/` to `renderer/`: the walk (rooms), the draw list (draws), pose and
light, vertices (the DSP's programs and their feeding), faces (rooms',
meshes', the rest), the draw order hints, the room texture cache, and the
frame's orchestration across both CPUs and the DSP. Each takes its inputs
explicitly (the level's data at load, the view and the calls each frame)
and owns its buffers, sized to the game's limits (`tools/ramaudit.py`).
Checks, C models, the game's way and the scene loader stay in the lab, which
feeds the modules from its scenes. Done when the lab builds from
`renderer/` and all eight scenes are exact.

**B. The level's data in the new formats.** `tools/pack_level.py` writes the
meshes as the renderer reads them (meshrot.dsp's groups, face sets,
materials) and the rooms' geometry (every room's vertices, then faces as
they fit) as a block the loader reads straight into high work RAM; the
level file keeps the rest. The remastered colour tables and the inventory's
item textures load into low work RAM, and the texture entries are read
where the level file holds them (issue 242). The lab's scenes take their
meshes from the same packed data. Done when the lab is exact on scenes made
that way and `ramaudit.py` shows every level fitting.

**C. The frame in game.** TRS's level load sets the renderer up from the
level; each frame the camera gives the walk its view, the game's draw calls
(models, statics, shadows, sprites, rectangles, text) become the renderer's
call records, and the frame runs as the lab's whole-frame run does: both
CPUs and the DSP, then the upload and VDP1's frame change. Done when every
level boots and plays in Mednafen.

Where C stands: the game draws with the renderer (`saturn/render.cpp` records
the calls, walks the rooms on the master for drawRooms, and runs the frame
at flush; `saturn/detail.cpp` lays out the texture cache the renderer
keeps; the level's high work RAM is taken in order at load, `os.cpp`). The
Caves and the ledge play at the old renderer's picture; the inventory (its
items under the ring's matrices, the scene dimmed behind it), the title's
text, Lara's muzzle flashes, the turned object quads (now the packer's)
and the DSP's end interrupt are in, and the HW_LOG trace shows the frame's
stages on both CPUs and the DSP. Left for C: every level booted and played
(cutscenes, flip maps, LEVEL7B's low work RAM).

**D. Checked and finished.** For the same frame of a recorded play, the
renderer's inputs in game against a capture's scene (made with the old
renderer: a worktree of the last commit that has it), and the frame drawn
against the old renderer's. Inventory, title, cutscenes, under water, flip
maps, animated textures (issue 243). The old renderer goes from TRS, with
the debug menu's rendering options and the polygon hunt; the lab keeps the
game's way it compares against (faces.s, flush.s, xform.s, meshxform.s and
the C around them, in `lab/`). Then the merge, and the Saturn.

## Why this order

Each stage keeps a check that already exists: A and B keep the lab's
exactness, C and D compare the game with captures of itself. The lab's
scenes are single frames: what only shows over many (animated textures, the
texture cache's long run, flip maps) is D's.
