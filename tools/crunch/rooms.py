"""The game's room drawing rebuilt in Python, from a view to a VDP1 command
list, so the crunch lab can change one step of it and draw the result with
Mednafen's VDP1 (vdp1.py). Each step follows the Saturn code it names:

    view matrix         openlara/common.cpp matrixSetView, matrixTranslateAbs
    vertices            saturn/xform.s (transform, projection, clip, fog)
    faces               saturn/faces.s room quads and triangles, with
                        rooms.cpp facingQuads and faceAddRoomQuads (near
                        quads, the near plane's 2x2 split)
    commands            saturn/flush.s / render.cpp faceCommand (turned and
                        mirrored corners, gouraud, far faces' one shade,
                        shaded colour tables)

Rooms only: no meshes, sprites or water effects. Options (Options) switch
the steps the lab studies; Options() is the game as it is.
"""
import numpy as np

from gamedata import DIV_TABLE, sincos, pack_texels
import pack_level  # noqa: E402 (gamedata puts tools/ on the path)

FRAME_W, FRAME_H = 352, 224
VIEW_MIN, VIEW_MAX = 64, 20480
FOG_MIN = 12288
OT_SHIFT = 4
OT_SIZE = (VIEW_MAX >> OT_SHIFT) + 1
NEAR_SPLIT_DIST = 2048
DETAIL_DIST = 4096

CLIP_FRAME, CLIP_LEFT, CLIP_RIGHT, CLIP_TOP, CLIP_BOTTOM, CLIP_PLANE = 1, 2, 4, 8, 16, 32
CLIP_DISCARD = 0x3E

CMD_DISTORTED_SPRITE = 0x0002
CMD_POLYGON = 0x0004
CMD_USER_CLIP = 0x0008
CMD_SYSTEM_CLIP = 0x0009
CMD_LOCAL_COORD = 0x000A
CMD_SKIP = 0x4000
CMD_END = 0x8000
CMD_FLIP_H = 0x0010
PMOD_GOURAUD = 0x0004
PMOD_LUT16 = 0x0008
PMOD_SPD_OFF = 0x0040
PMOD_ECD_OFF = 0x0080
PMOD_NO_ECD_SPD = 0x00C0
PMOD_HSS = 0x1000

# The lab's VDP1 VRAM: commands, gouraud tables, colour tables, textures
VRAM_CMDS, VRAM_GOURAUD, VRAM_CLUTS, VRAM_TEXTURES, VRAM_END = 0x00000, 0x20000, 0x28000, 0x30000, 0x80000


class Options:
    """What the lab changes; the defaults draw as the game does."""

    def __init__(self, **kw):
        self.exact_divide = False   # project with a full-precision divide instead of divTable
        self.turn = True            # gTurnLines
        self.lod = 4                # gTextureLod: 4 x texels a pixel along a line for the next mip level; 0 none
        self.far_shade = 2          # far (mip) faces take one shade when their corners are this close
        self.shade_tables = True    # evenly shaded faces through shaded colour tables
        self.hss = True             # high-speed shrink
        self.mip = None             # mip builder (texture, factor) -> (h x w colour indices, colours); None: the game's
        self.ripmap = False         # reduce along and across the lines apart (levels (fx, fy))
        self.reuse = True           # quads keep their turn and mip level for a frame (faces.s REUSE)
        self.near_split = True      # quads the near plane cuts split 2x2 (else filled)
        self.rip = None             # ripmap builder (texture, fx, fy) -> (texels, colours)
        for k, v in kw.items():
            if not hasattr(self, k):
                raise AttributeError(k)
            setattr(self, k, v)


def asr(v, n):
    return v >> n   # Python's >> floors, as GCC's arithmetic shift of a signed value


def wrap32(v):
    return ((int(v) + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def view_rotation(angle_x, angle_y):
    """matrixSetView's rotation: rows e0x, e1x, e2x (1.14)."""
    sx, cx = sincos(angle_x)
    sy, cy = sincos(angle_y)
    return np.array([[cy, 0, -sy],
                     [asr(sx * sy, 14), cx, asr(sx * cy, 14)],
                     [asr(cx * sy, 14), -sx, asr(cx * cy, 14)]], dtype=np.int64)


class View:
    """A camera as the game's: position (world units) and pitch, yaw (16-bit
    angles), and the viewport (RectMinMax)."""

    def __init__(self, pos, angle_x, angle_y, viewport=(0, 0, FRAME_W - 1, FRAME_H - 1)):
        self.pos = tuple(int(p) for p in pos)
        self.angle_x, self.angle_y = angle_x, angle_y
        self.rot = view_rotation(angle_x, angle_y)
        self.viewport = viewport

    def room_matrix(self, origin):
        """roomsQueue's RoomMatrix for a room whose packed vertices start at
        origin: the rotation, and the origin's view position << 8."""
        d = [origin[i] - self.pos[i] for i in range(3)]
        t = [asr(wrap32(sum(int(self.rot[r][c]) * d[c] for c in range(3))), 14 - 8) for r in range(3)]
        return self.rot.copy(), np.array(t, dtype=np.int64)


class Vertices:
    """The frame's projected vertices (render.h Vertex): screen x, y, view z,
    shade g, clip flags; rooms' and split faces' added as they come."""

    def __init__(self):
        self.parts = []
        self.count = 0

    def add(self, x, y, z, g, clip):
        base = self.count
        self.parts.append((x, y, z, g, clip))
        self.count += len(x)
        return base

    def arrays(self):
        if not self.parts:
            return [np.zeros(0, np.int64)] * 5
        return [np.concatenate([p[i] for p in self.parts]) for i in range(5)]


def project(R, T, v4, shade, viewport, opts):
    """xform.s: room-space points (vertex units times 4) to Vertex fields."""
    z = T[2] + v4 @ R[2]
    clip = np.zeros(len(z), np.int64)
    near = z <= (VIEW_MIN << 8)
    far = ~near & (z >= (VIEW_MAX << 8))
    clip[near | far] = CLIP_PLANE
    z = np.where(near, VIEW_MIN << 8, np.where(far, VIEW_MAX << 8, z))
    x = np.clip(T[0] + v4 @ R[0], -0x7FFF00, 0x7FFF00)
    y = np.clip(T[1] + v4 @ R[1], -0x7FFF00, 0x7FFF00)
    zw = z >> 8
    if opts.exact_divide:
        # what the SH-2's divider could give: every bit of x, y and z, rounded
        # down as the table's products are
        px = np.clip(np.floor(256.0 * x / z), -2047, 2047).astype(np.int64)
        py = np.clip(np.floor(256.0 * y / z), -2047, 2047).astype(np.int64)
    else:
        d = DIV_TABLE[zw >> 4].astype(np.int64)
        px = np.clip((x >> 8) * d, -0x7FF000, 0x7FF000) >> 12
        py = np.clip((y >> 8) * d, -0x7FF000, 0x7FF000) >> 12
    sx = px + FRAME_W // 2
    sy = py + FRAME_H // 2
    off = (sx < 0) | (sx > FRAME_W) | (sy < 0) | (sy > FRAME_H)
    clip |= np.where(off, CLIP_FRAME, 0)
    sx = np.where(off, np.clip(sx, -1024, 1023), sx)
    sy = np.where(off, np.clip(sy, -1024, 1023), sy)
    x0, y0, x1, y1 = viewport
    clip |= np.where(sx < x0, CLIP_LEFT, 0) | np.where(sx > x1, CLIP_RIGHT, 0)
    clip |= np.where(sy < y0, CLIP_TOP, 0) | np.where(sy > y1, CLIP_BOTTOM, 0)
    g = np.minimum((shade << 8) + np.maximum(0, zw - FOG_MIN), 8191) >> 8
    return sx, sy, zw, g, clip


class Face:
    __slots__ = ('kind', 'corners', 'tex', 'level', 'turned', 'quadrant', 'bucket', 'transparent')

    def __init__(self, kind, corners, bucket, tex=None, level=0, turned=False, quadrant=None, transparent=False):
        self.kind = kind            # 'tex' or 'void'
        self.corners = corners      # vertex indices A, B, C, D as added (before turning)
        self.bucket = bucket
        self.tex = tex
        self.level = level
        self.turned = turned
        self.quadrant = quadrant    # a split quarter's quadrant, 0..3 clockwise from top left
        self.transparent = transparent


def span(x, y, a, b):
    return max(abs(int(x[a]) - int(x[b])), abs(int(y[a]) - int(y[b])))


class RoomDrawer:
    """Builds a frame's faces and VDP1 commands for a list of rooms."""

    def __init__(self, level, opts=None):
        self.level = level
        self.opts = opts or Options()
        self.kept = {}          # (room, quad) -> (level, turned): REUSE's decisions
        self.frame_index = 0
        self.facing = {}        # room -> per quad (run, plane) or None
        self.room_vertices = {} # room -> its vertices' first index in the frame's

    # --- faces ------------------------------------------------------------

    def quad_runs(self, room):
        if room.index not in self.facing:
            packed = [tuple(int(c) for c in v) for v in room.vertices]
            self.facing[room.index] = [pack_level.room_quad_facing(packed, q) for q in room.quads]
        return self.facing[room.index]

    def lod_level(self, tex, turned, across, lines):
        """faces.s WHOLE: the mip level with about a texel a pixel along the lines."""
        lod = self.opts.lod
        if not lod:
            return 0
        t = self.level.textures[tex]
        if self.opts.ripmap:
            return self.rip_level(t, turned, across, lines)
        qw, qh = t.w // 2, t.h // 2
        ext4 = 4 * (t.h if turned else t.w)
        r0 = lod * across
        if not ext4 > r0 or qh % 8:
            return 0
        if not ext4 > 2 * r0 or (qw // 8) & 1 or qh % 16:
            return 1
        return 2

    def rip_level(self, t, turned, across, lines):
        """Ripmaps: (fx, fy), the texture's reduction across (x) and down (y),
        each the power of 2 up to 8 that leaves about a texel a pixel: along
        the lines by their length, across them by their count. The copy as
        drawn (transposed when turned) stays a multiple of 8 wide."""
        lod = self.opts.lod

        def factor(ext, pixels, unit):
            f = 1
            while f < 8 and 4 * ext > lod * pixels * f and (ext // (2 * f)) % unit == 0 and ext % (2 * f) == 0:
                f *= 2
            return f

        if turned:
            fy = factor(t.h, across, 8)     # along the lines: the copy's width
            fx = factor(t.w, lines, 1)
        else:
            fx = factor(t.w, across, 8)
            fy = factor(t.h, lines, 1)
        return 0 if fx == fy == 1 else (fx, fy)

    def turn_decision(self, x, y, a, b, c, d):
        """faces.s TURN: (turned, the lines' length, their count)."""
        as_is = max(span(x, y, a, d), span(x, y, b, c))
        turned = max(span(x, y, a, b), span(x, y, d, c))
        if self.opts.turn and as_is > turned:
            return True, as_is, turned
        return False, turned, as_is

    def add_room(self, room, R, T, viewport, camera, faces, verts):
        opts = self.opts
        v4 = room.vertices.astype(np.int64) * 4
        base = verts.add(*project(R, T, v4, room.shades.astype(np.int64), viewport, opts))
        self.room_vertices[room.index] = base
        x, y, z, g, clip = verts.arrays()
        runs = self.quad_runs(room)
        parity = self.frame_index & 1

        def backface(a, b, c):
            return (int(x[b]) - int(x[a])) * (int(y[c]) - int(y[a])) <= (int(x[c]) - int(x[a])) * (int(y[b]) - int(y[a]))

        for qi, (q0, q1, q2, q3, tex, transp) in enumerate(room.quads):
            a, b, c, d = base + q0, base + q1, base + q2, base + q3
            run = runs[qi]
            if run is not None:
                axis = run[0] >> 1
                plane = run[1] << 8
                facing = plane < camera[axis] if run[0] & 1 else plane > camera[axis]
                if not facing:
                    continue
            elif backface(a, b, c):
                continue
            cand = clip[a] & clip[b] & clip[c] & clip[d]
            cor = clip[a] | clip[b] | clip[c] | clip[d]
            if (cand & CLIP_DISCARD) or (cor & CLIP_PLANE):
                if cand & (CLIP_DISCARD & ~CLIP_PLANE):
                    continue
                if cand & CLIP_PLANE:
                    if (z[a] >> 4) == (VIEW_MAX >> 4):
                        faces.append(Face('void', (a, b, c, d), OT_SIZE - 1))
                    continue
                self.near_quad(room, qi, (a, b, c, d), R, T, viewport, faces, verts)
                x, y, z, g, clip = verts.arrays()
                continue
            if z[a] < NEAR_SPLIT_DIST:
                if backface(a, b, c):
                    continue
                turned = opts.turn and self.turn_decision(x, y, a, b, c, d)[0]
                bucket = max(z[a], z[b], z[c], z[d]) >> OT_SHIFT
                faces.append(Face('tex', (a, b, c, d), bucket, tex, 0, turned, transparent=transp))
                continue
            key = (room.index, qi)
            if opts.reuse and ((qi ^ len(room.quads)) & 1) != parity and key in self.kept:
                level, turned = self.kept[key]
            else:
                turned, across, lines = self.turn_decision(x, y, a, b, c, d)
                level = self.lod_level(tex, turned, across, lines)
                self.kept[key] = (level, turned)
            bucket = max(z[a], z[b], z[c], z[d]) >> OT_SHIFT
            faces.append(Face('tex', (a, b, c, d), bucket, tex, level, turned, transparent=transp))

        for (t0, t1, t2, tex) in room.triangles:
            a, b, c = base + t0, base + t1, base + t2
            cand = clip[a] & clip[b] & clip[c]
            cor = clip[a] | clip[b] | clip[c]
            if (cand & CLIP_DISCARD) or (cor & CLIP_PLANE):
                if cand & (CLIP_DISCARD & ~CLIP_PLANE):
                    continue
                if (cand & CLIP_PLANE) and (z[a] >> 4) != (VIEW_MAX >> 4):
                    continue
                if not backface(a, b, c):
                    faces.append(Face('void', (a, b, c, c), OT_SIZE - 1))
                continue
            if backface(a, b, c):
                continue
            turned, across, lines = self.turn_decision(x, y, a, b, c, c)
            level = self.lod_level(tex, turned, across, lines)
            bucket = max(z[a], z[b], z[c]) >> OT_SHIFT
            faces.append(Face('tex', (a, b, c, c), bucket, tex, level, turned))

    def near_quad(self, room, qi, corners, R, T, viewport, faces, verts):
        """rooms.cpp faceAddRoomQuads for a quad a plane cuts: split 2x2 if it
        is near enough and the option is on, else filled."""
        x, y, z, g, clip = verts.arrays()
        a, b, c, d = corners
        q = room.quads[qi]
        tex = q[4]
        cand = clip[a] & clip[b] & clip[c] & clip[d]
        if (cand & (CLIP_DISCARD & ~CLIP_PLANE)) or ((cand & CLIP_PLANE) and z[a] < VIEW_MAX):
            return
        if (int(x[b]) - int(x[a])) * (int(y[c]) - int(y[a])) <= (int(x[c]) - int(x[a])) * (int(y[b]) - int(y[a])):
            return
        zmin = min(z[a], z[b], z[c], z[d])
        if not (self.opts.near_split and zmin < DETAIL_DIST):
            faces.append(Face('void', corners, OT_SIZE - 1))
            return
        r = room.vertices.astype(np.int64)
        r0, r1, r2, r3 = (r[i] for i in q[:4])
        pts = np.array([(r0 + r1) * 2, (r1 + r2) * 2, (r2 + r3) * 2, (r3 + r0) * 2, r0 + r1 + r2 + r3])
        sh = room.shades.astype(np.int64)
        mid = verts.add(*project(R, T, pts, np.zeros(5, np.int64), viewport, self.opts))
        # the midpoints' shades: their corners' (Vertex.g) means
        parts = verts.parts[-1]
        ga, gb, gc, gd = g[a], g[b], g[c], g[d]
        parts[3][:] = [(ga + gb) >> 1, (gb + gc) >> 1, (gc + gd) >> 1, (gd + ga) >> 1, (ga + gb + gc + gd) >> 2]
        m01, m12, m23, m30, mc = range(mid, mid + 5)
        for quad, cs in enumerate(((a, m01, mc, m30), (m01, b, m12, mc), (mc, m12, c, m23), (m30, mc, m23, d))):
            x, y, z, g, clip = verts.arrays()
            cand = clip[cs[0]] & clip[cs[1]] & clip[cs[2]] & clip[cs[3]]
            cor = clip[cs[0]] | clip[cs[1]] | clip[cs[2]] | clip[cs[3]]
            if (cand & CLIP_DISCARD) or (cor & CLIP_PLANE):
                if not (cand & CLIP_DISCARD):
                    faces.append(Face('void', cs, OT_SIZE - 1))
                continue
            bucket = max(z[i] for i in cs) >> OT_SHIFT
            faces.append(Face('tex', cs, bucket, tex, 0, False, quad, q[5]))

    def faces(self, jobs):
        """The frame's faces and vertices for jobs (RoomJob: room, R, T,
        camera, viewport), in the order the game queues them."""
        faces = []
        verts = Vertices()
        self.room_vertices = {}
        for job in jobs:
            self.add_room(job.room, job.R, job.T, job.viewport, job.camera, faces, verts)
        self.frame_index += 1
        return faces, verts


class RoomJob:
    """rooms.cpp RoomJob: a room, its matrix, the camera's position from its
    origin, its portal viewport."""

    def __init__(self, room, R, T, camera, viewport):
        self.room, self.R, self.T = room, R, T
        self.camera = camera
        self.viewport = viewport

    @classmethod
    def from_view(cls, view, room, viewport=None):
        R, T = view.room_matrix(room.origin)
        camera = tuple(view.pos[i] - room.origin[i] for i in range(3))
        return cls(room, R, T, camera, viewport or view.viewport)


class TextureStore:
    """Texture variants (whole, transposed, mip levels, quadrants) as 4-bit
    data, made once."""

    def __init__(self, level, opts):
        self.level = level
        self.opts = opts
        self.cache = {}

    def texels(self, tex, level, turned, quadrant):
        """(texels as drawn, their 16 colours)."""
        key = (tex, level, turned, quadrant)
        if key not in self.cache:
            t = self.level.textures[tex]
            colors = t.colors
            if quadrant is not None:
                full = t.texels()
                hw, hh = t.w // 2, t.h // 2
                oy, ox = ((0, 0), (0, hw), (hh, hw), (hh, 0))[quadrant]
                data = full[oy:oy + hh, ox:ox + hw]
            elif level == 0:
                data = t.texels()
            elif isinstance(level, tuple):
                data, colors = self.opts.rip(t, *level)
            elif self.opts.mip is not None:
                data, colors = self.opts.mip(t, 1 << level)
            else:
                packed = pack_level.mip_texture(t.pixels, t.w, t.h, t.colors, t.transparent, 1 << level)
                b = np.frombuffer(packed, np.uint8).reshape(t.h >> level, (t.w >> level) // 2)
                data = np.empty((t.h >> level, t.w >> level), np.uint8)
                data[:, 0::2] = b >> 4
                data[:, 1::2] = b & 15
            if turned:
                data = data.T
            self.cache[key] = (np.ascontiguousarray(data), list(colors))
        return self.cache[key]


class CommandList:
    """VDP1 VRAM for one plot: the commands, then gouraud tables, colour
    tables and textures in their own regions (VRAM_*)."""

    def __init__(self):
        self.vram = bytearray(VRAM_END)
        self.cmds = []
        self.grd = VRAM_GOURAUD
        self.clut = VRAM_CLUTS
        self.tex = VRAM_TEXTURES
        self.cluts = {}
        self.textures = {}

    def add(self, words):
        self.cmds.append(words)

    def gouraud(self, values):
        addr = self.grd
        self.vram[addr:addr + 8] = np.array(values, '>u2').tobytes()
        self.grd += 8
        assert self.grd <= VRAM_CLUTS, 'gouraud tables overflow'
        return addr >> 3

    def colors(self, colors):
        key = tuple(colors)
        if key not in self.cluts:
            addr = self.clut
            self.vram[addr:addr + 32] = np.array(colors, '>u2').tobytes()
            self.clut += 32
            assert self.clut <= VRAM_TEXTURES, 'colour tables overflow'
            self.cluts[key] = addr >> 3
        return self.cluts[key]

    def texture(self, key, texels):
        if key not in self.textures:
            data = pack_texels(texels)
            addr = self.tex
            self.vram[addr:addr + len(data)] = data
            self.tex = (addr + len(data) + 7) & ~7
            if self.tex > VRAM_END:
                raise MemoryError('the frame\'s textures need more than VDP1 VRAM')
            self.textures[key] = addr >> 3
        return self.textures[key]

    def finish(self):
        words = list(self.cmds)
        words.append([CMD_END] + [0] * 15)
        assert len(words) * 32 <= VRAM_GOURAUD, 'commands overflow'
        data = np.array(words, '>u2').tobytes()
        self.vram[:len(data)] = data
        return bytes(self.vram)


def shade_level(g):
    """gShadeLevel at the Saturn shade curve: 31 (bright) .. 0."""
    return 31 - g if g < 16 else 16 - (g - 15)


def shaded_colors(colors, level):
    """shadeTableMiss: VDP1's gouraud add done to a colour table."""
    d = level - 16
    out = []
    for c in colors:
        r = min(31, max(0, (c & 31) + d))
        gr = min(31, max(0, ((c >> 5) & 31) + d))
        b = min(31, max(0, ((c >> 10) & 31) + d))
        out.append((c & 0x8000) | (b << 10) | (gr << 5) | r)
    return out


def off_frame(x, y):
    return max(0, -x) + max(0, x - FRAME_W) + max(0, -y) + max(0, y - FRAME_H)


def build_commands(level, faces, verts, opts, store, cl=None):
    """flush: faces far to near (each bucket's in the reverse of the order
    they were added) into cl's commands. Returns cl."""
    cl = cl or CommandList()
    cl.add([CMD_SYSTEM_CLIP] + [0] * 9 + [FRAME_W - 1, FRAME_H - 1] + [0] * 4)
    cl.add([CMD_USER_CLIP] + [0] * 9 + [FRAME_W - 1, FRAME_H - 1] + [0] * 4)
    cl.add([CMD_LOCAL_COORD] + [0] * 15)
    x, y, z, g, clip = verts.arrays()
    buckets = {}
    for f in faces:
        buckets.setdefault(f.bucket, []).append(f)
    for bucket in sorted(buckets, reverse=True):
        for f in reversed(buckets[bucket]):
            cl.add(face_command(level, f, x, y, g, clip, opts, store, cl))
    return cl


def face_command(level, f, x, y, g, clip, opts, store, cl):
    w = [0] * 16
    if f.kind == 'void':
        xs = [int(x[i]) for i in f.corners]
        ys = [int(y[i]) for i in f.corners]
        x0, y0 = max(min(xs), 0), max(min(ys), 0)
        x1, y1 = min(max(xs), FRAME_W - 1), min(max(ys), FRAME_H - 1)
        w[0] = CMD_POLYGON | (CMD_SKIP if x0 > x1 or y0 > y1 else 0)
        w[2] = PMOD_NO_ECD_SPD
        w[6:14] = [x0 & 0xFFFF, y0 & 0xFFFF, x1 & 0xFFFF, y0 & 0xFFFF, x1 & 0xFFFF, y1 & 0xFFFF, x0 & 0xFFFF,
                   y1 & 0xFFFF]
        return w

    a, b, c, d = f.corners
    if f.turned:
        b, d = d, b
    ctrl = CMD_DISTORTED_SPRITE
    if (clip[a] | clip[b] | clip[c] | clip[d]) & CLIP_FRAME and \
            off_frame(x[a], y[a]) + off_frame(x[d], y[d]) > off_frame(x[b], y[b]) + off_frame(x[c], y[c]):
        a, b, c, d = b, a, d, c
        ctrl ^= CMD_FLIP_H
    texels, colors = store.texels(f.tex, f.level, f.turned, f.quadrant)
    th, tw = texels.shape
    srca = cl.texture((f.tex, f.level, f.turned, f.quadrant), texels)

    levels = [shade_level(int(g[i])) for i in (a, b, c, d)]
    if f.level and opts.far_shade is not None and max(levels) - min(levels) <= opts.far_shade:
        levels = [(sum(levels) + 2) >> 2] * 4
    pmod = (PMOD_HSS if opts.hss else 0) | PMOD_LUT16 | PMOD_ECD_OFF
    if not f.transparent:
        pmod |= PMOD_SPD_OFF
    if opts.shade_tables and levels.count(levels[0]) == 4:
        colr = cl.colors(shaded_colors(colors, levels[0]))
        grda = 0
    else:
        pmod |= PMOD_GOURAUD
        colr = cl.colors(colors)
        grda = cl.gouraud([lv * 0x421 for lv in levels])
    w[0] = ctrl
    w[2] = pmod
    w[3] = colr
    w[4] = srca
    w[5] = ((tw // 8) << 8) | th
    for k, i in enumerate((a, b, c, d)):
        w[6 + 2 * k] = int(x[i]) & 0xFFFF
        w[7 + 2 * k] = int(y[i]) & 0xFFFF
    w[14] = grda
    return w
