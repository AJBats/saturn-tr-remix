// The lab's side of the renderer's modules (renderer/): each fed from the
// scene, and held to the game's way.
#ifndef LAB_RENDER_H
#define LAB_RENDER_H

#include <stdint.h>

#include "draws.h"
#include "faces.h"
#include "hints.h"
#include "pose.h"
#include "rooms.h"
#include "scene.h"
#include "textures.h"
#include "vertices.h"

// The room texture cache from the scene (labtextures.cpp): its set (RTEX,
// RTXI, INFO's pools) and the cache as the game's frame left it (DKEY, DSLT);
// texturesReset puts back the cache as the frame found it (DKY0, DSL0).
void texturesLabInit();
void texturesReset();

// What the checks hold it to: the uploads the game queued (DUPL); entries
// of two queues that differ (a count apart counting as all); the cache now
// against the game's after its frame (DKEY, DSLT as captured): keys and
// slots that differ; the queue's textures as staged against VDP1 VRAM now
// (before the lab uploads any, the scene's image: the game's own uploads):
// longwords that differ.
const TextureQueue& texturesCaptured();
int32_t texturesQueueOff(const TextureQueue &a, const TextureQueue &b);
int32_t texturesStateOff();
int32_t texturesVramOff(const TextureQueue &queue);

// The draw order hints from the scene (labhints.cpp): its level's (HINT) and
// Lara's room and block (LARA); the draws whose faces the frame's hints move
// from the scene's draws' things (DTHG), as hintsDraws from a draw list's
// hinted calls.
void hintsLabInit();
int32_t hintsSceneDraws(const DrawThing* things, uint32_t count, HintedDraw* out, int32_t room);

// The walk from the scene (labrooms.cpp): its level's rooms (RLVL, RPRT,
// RLGE, RGEO, CAUW) and its view (CAMR, RVIS); against the capture's (RVIS,
// ROOM, the fills drawn first): the rooms that differ (or a list of another
// length).
void roomsLabInit();
int32_t roomsSeenCheck();

// What the walk makes (the rooms listed, their jobs) overwritten with what it
// never makes, so a check after a frame that skipped it fails.
void roomsPoison();

// What the scene seldom gives the walk: made-up places and turns of the
// camera (portals past the draw distance, cut by the near plane, facing
// away), and the jobs made-up lists of rooms give (rooms without vertices,
// past the jobs' limit or the vertices' room, wet, dry, rippling), against
// the game's way on them: those that differ (and paths not taken).
int32_t roomsSeldomCheck();

// The draw list from the scene (labdraws.cpp): its level's tables (ANIM,
// AFRM, MODL, MIDX, MNOD, PNOD, SMSH, RLIT, RLVL, MESH, MGEO, SPRS), its view
// (CAMR) and calls (CALL, OITM): whether they fit the list, and the list
// made of them after fills of the walk's; against the game's way's: those
// that differ.
void drawsLabInit();
bool drawsLabFits();
void drawsLab(int32_t fills);
int32_t drawsCheck(const DrawList &list);

// What the draw list makes overwritten with what it never makes, and the
// list emptied, so a check after a frame that skipped it fails.
void drawsPoison();

// What the scene seldom gives it: made-up calls (items moved, turned and
// at any frame of animations of every rate, statics moved, turned and out of
// sight, sprites moved and shaded, viewports, rectangles, shadows on a wall)
// against the game's way on them: those that differ (and paths not taken).
int32_t drawsSeldomCheck();

// Pose from the scene (labpose.cpp): its level's (SINC, PNOD, LGHT), its view
// (CAMR); the items to pose: a draw list's (its draw i the scene's i + fills,
// the walk's fills coming first there), or (nullptr) the scene's (POSE,
// PLIT, STAT, SHDW, PFRM), the transforms as the scene's (XFRM) to start
// with; false: more items than the scene's.
void poseLabInit();
bool poseLabUse(const DrawList* list, int32_t fills = 0);

// The items' meshes' transforms in poseXforms garbled, so a frame that reads
// them before pose writes them shows.
void posePoison();

// The items posed on the master (their light from poseLight); against the
// game's (XFRM): the draws whose matrix or light differs.
void pose();
int32_t poseCheck();

// What the scene seldom gives pose: every item again, many times over, on
// made-up frames, lerps of every kind, extra angles, Lara's arms (with and
// without their own frames, turned by her basis or not, the shotgun's
// torso), against the game's way on the same: the matrices that differ.
int32_t poseSeldomCheck();

// The items' light (pose.s poseLightAsm); against the game's (XFRM): the
// draws that differ. Made up: items, rooms and lights the scene seldom gives
// (divisors of 0 and -1, a light on the point lit, own intensities, rooms
// without lights, overflowing radii) against the game's way on the same.
void poseLight();
int32_t poseLightCheck();
int32_t poseLightSeldomCheck();

// The statics' and shadows' transforms against the game's (XFRM): those that
// differ. Made up: quarter turns, intensities, places, the fog, lerped boxes,
// floors and yaws the scene seldom gives, against the game's way on them.
int32_t poseStaticsCheck();
int32_t poseStaticsSeldomCheck();

// Vertices from the scene (labvertices.inc): the walk's jobs and the scene's
// draws, the meshes also laid out for meshlight3.dsp (the bench's v3, run
// beside for its time). NULL, or what went wrong.
const char* verticesLabInit(const FaceRoom* rooms, uint32_t roomCount);

// The rooms' rotations on the DSP, a job a room, the master waiting for its
// end: the SH-2 cycles it took. Then its rotations against xform.s's
// arithmetic: those that differ.
uint32_t verticesRoomsDsp();
int32_t verticesRoomsDspCheck();

// The rooms' vertices projected from the DSP's rotations, into gVertices
// where the game put them; against the game's: the vertices that differ.
void verticesRooms();
int32_t verticesRoomsCheck(int32_t &vertices);

// The rooms' projection where the scene seldom takes it, made-up rotations
// through every path of both roomproject.s loops against a C model of its
// arithmetic: the vertices that differ.
int32_t verticesRoomsSeldomCheck();

// The meshes fed to the DSP: each one's job (its coefficients, how it's
// lit, whether it lies inside the frame), a shadow's vertices packed.
// Then the DSP run over them, the master waiting: the SH-2 cycles it took;
// and its output against meshxform.s's arithmetic: the vertices that differ.
// meshlight3.dsp's run over the same, fed from the jobs, for its time.
void verticesFeed();
uint32_t verticesMeshesDsp();

// The feeding (meshfeed.s) against its C model: on the scene's transforms,
// then on made-up ones through every case; the jobs that differ.
int32_t verticesFeedCheck();
uint32_t verticesMeshesDsp3();
int32_t verticesMeshesDspCheck();

// The meshes' vertices projected in place and shaded; against the game's:
// the vertices that differ (vertices: the meshes').
void verticesMeshes();
int32_t verticesMeshesCheck(int32_t &vertices);

// Stage 2 the new way as a frame would run it: the DSP turns the rooms then
// the meshes; the slave projects each room as soon as the DSP has turned it;
// the master feeds the meshes meanwhile, then projects each as soon as the
// DSP has made it, from the first, the slave joining from the last once its
// rooms are done. The SH-2 cycles from the start to both CPUs done, and
// each CPU's at work and waiting (polling the DSP, or the master the
// slave); the DSP's rooms (to their end as the master saw it), the gap to
// its meshes' start, and its meshes (to their end as the master saw it,
// its own jobs done).
struct Together
{
    uint32_t wall;
    uint32_t masterWork, masterWait;
    uint32_t slaveWork, slaveWait;
    uint32_t dspRooms, dspGap, dspMeshes;
};
Together verticesTogether();

// What the stages write overwritten with what they never make (the rooms'
// rotations, the meshes' outputs, the rooms' vertices), so a check after a
// run sees every value made in it.
void verticesPoison();

// What the scene seldom takes: the meshes' vertices projected as though none
// lay inside the frame, then as though each lay at the near plane (its z >>
// 14 64, so meshproject.s has it worked out from its mesh), each from the
// DSP's output again, against the game's: the vertices that differ.
int32_t verticesFallbacksCheck();

// The faces in C, what the assembly does, to check it against
// (labfaces.inc): a mesh's, a room's, a face made whole; and their checks;
// the join in C.
// The same in C: what facesMeshAsm does, to check it against.
void facesMeshC(const FaceSet* set, const Vertex* vertices, uint32_t mode, CommandList &list);

// The buckets' chains as facesMeshHinted and facesLinkHeld change them
// (commands linked, taken back out from a chain's head, middle and end, and
// linked again elsewhere), at random, against a model of each chain: the
// chains and firsts that differ (and paths not taken).
int32_t facesChainCheck();

void facesRoomC(const FaceRoom &room, RoomTextures &textures, RoomSplit &split, CommandList &list);

// The same in C: what facesOtherAsm does, to check it against.
void facesOther(const Face &face, const Vertex* half, int32_t depth, CommandList &list);

// facesOtherAsm against facesOther on made-up faces of every kind (sprites
// flipped or not and shaded, rectangles and lines of any place and
// size, voids reaching past each edge or off the frame, buckets shared, a
// list filling up): the commands, gouraud tables, buckets and cursors that
// differ (and paths not taken).
int32_t facesOtherCheck(const uint16_t* shadeGouraud);

// The lists' buckets far to near, in each the lists' chains in turn, from the
// command in slot first of cmds (whose link it sets) to the one in slot end;
// the heads emptied for the next frame. Each list's heads hold OT_SIZE + 1
// buckets, the last always empty.
void facesJoin(CommandList* const* lists, int32_t count, Command* cmds, int32_t first, int32_t end);

#endif
