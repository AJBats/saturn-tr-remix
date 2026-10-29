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
import textures

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

# Finer divide tables, still 16-bit values for muls.w: below FINE_DIST by
# z >> 2, 2^18 / i (divTable's scale, 2^20 / z, from a quarter of the steps);
# beyond it by z >> 4 as divTable, 2^20 / i (16 times divTable's, products
# shifted 4 more)
FINE_DIST = 4096
FINE_TABLE = np.array([0] + [min(0x7FFF, (1 << 18) // i) for i in range(1, FINE_DIST >> 2)], dtype=np.int64)
FAR_TABLE = np.array([0] + [min(0x7FFF, (1 << 20) // i) for i in range(1, 1281)], dtype=np.int64)


class Options:
    """What the lab changes; the defaults draw as the game does."""

    def __init__(self, **kw):
        self.divide = 'table'       # 'table': divTable[z >> 4]; 'fine': FINE_TABLE near; 'exact': a full divide
        self.turn = True            # gTurnLines
        self.turn_by_texture = False    # lines along the texture's strong edges where that costs < 1.5x lines
        self.lod = 4                # gTextureLod: 4 x texels a pixel along a line for the next mip level; 0 none
        self.far_shade = 2          # far (mip) faces take one shade when their corners are this close
        self.shade_tables = True    # evenly shaded faces through shaded colour tables
        self.hss = True             # high-speed shrink
        self.mip = None             # mip builder (texture, factor) -> (h x w colour indices, colours); None: the game's
        self.ripmap = False         # reduce along and across the lines apart (levels (fx, fy))
        self.rip = None             # ripmap builder (texture, fx, fy) -> (texels, colours)
        self.reuse = True           # quads keep their turn and mip level for a frame (faces.s REUSE)
        self.near_split = True      # quads the near plane cuts split 2x2 (else filled)
        self.para_triangles = False     # room triangles as parallelograms, the far half end codes
        self.contrast = False       # each face's colour table at less contrast by its room light (offline)
        self.flat_multiply = False  # each face one colour table: its colours times its light (VDP2 palettes)
        self.light_scale = 1.0      # each corner's light pulled toward neutral (16) to this share of its distance
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

    def __init__(self, pos, angle_x, angle_y, viewport=(0, 0, FRAME_W, FRAME_H)):
        self.pos = tuple(int(p) for p in pos)
        self.angle_x, self.angle_y = angle_x, angle_y
        self.rot = view_rotation(angle_x, angle_y)
        self.viewport = viewport

    def room_matrix(self, origin):
        """roomsQueue's RoomMatrix for a room whose points start at origin:
        the rotation, and the origin's view position << 8."""
        d = [origin[i] - self.pos[i] for i in range(3)]
        t = [asr(wrap32(sum(int(self.rot[r][c]) * d[c] for c in range(3))), 14 - 8) for r in range(3)]
        return self.rot.copy(), np.array(t, dtype=np.int64)


class Vertices:
    """The frame's projected vertices (render.h Vertex): screen x, y, view z,
    shade g, clip flags, and the lab's own: the room's shade without fog;
    rooms' and split faces' added as they come."""

    def __init__(self):
        self.parts = []
        self.count = 0

    def add(self, x, y, z, g, clip, s):
        base = self.count
        self.parts.append((x, y, z, g, clip, s))
        self.count += len(x)
        return base

    def arrays(self):
        if not self.parts:
            return [np.zeros(0, np.int64)] * 6
        return [np.concatenate([p[i] for p in self.parts]) for i in range(6)]


def project(R, T, v4, shade, viewport, opts):
    """xform.s: room-space points (64-unit steps) to Vertex fields."""
    z = T[2] + v4 @ R[2]
    clip = np.zeros(len(z), np.int64)
    near = z <= (VIEW_MIN << 8)
    far = ~near & (z >= (VIEW_MAX << 8))
    clip[near | far] = CLIP_PLANE
    z = np.where(near, VIEW_MIN << 8, np.where(far, VIEW_MAX << 8, z))
    x = np.clip(T[0] + v4 @ R[0], -0x7FFF00, 0x7FFF00)
    y = np.clip(T[1] + v4 @ R[1], -0x7FFF00, 0x7FFF00)
    zw = z >> 8
    if opts.divide == 'exact':
        # what the SH-2's divider could give: every bit of x, y and z, rounded
        # down as the table's products are
        px = np.clip(np.floor(256.0 * x / z), -2047, 2047).astype(np.int64)
        py = np.clip(np.floor(256.0 * y / z), -2047, 2047).astype(np.int64)
    else:
        d = DIV_TABLE[zw >> 4].astype(np.int64)
        px = np.clip((x >> 8) * d, -0x7FF000, 0x7FF000) >> 12
        py = np.clip((y >> 8) * d, -0x7FF000, 0x7FF000) >> 12
        if opts.divide == 'fine':
            close = zw < FINE_DIST
            df = FINE_TABLE[np.minimum(zw >> 2, (FINE_DIST >> 2) - 1)]
            dfar = FAR_TABLE[zw >> 4]
            px = np.where(close, np.clip((x >> 8) * df, -0x7FF000, 0x7FF000) >> 12,
                          np.clip((x >> 8) * dfar, -0x7FF0000, 0x7FF0000) >> 16)
            py = np.where(close, np.clip((y >> 8) * df, -0x7FF000, 0x7FF000) >> 12,
                          np.clip((y >> 8) * dfar, -0x7FF0000, 0x7FF0000) >> 16)
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
        self.kind = kind            # 'tex', 'para' (a triangle as a parallelogram: corners v0 v1 v2) or 'void'
        self.corners = corners      # vertex indices A, B, C, D as added (before turning)
        self.bucket = bucket
        self.tex = tex
        self.level = level
        self.turned = turned
        self.quadrant = quadrant    # a split quarter's quadrant, 0..3 clockwise from top left
        self.transparent = transparent


def span(x, y, a, b):
    return max(abs(int(x[a]) - int(x[b])), abs(int(y[a]) - int(y[b])))


def span_xy(p, q):
    return max(abs(p[0] - q[0]), abs(p[1] - q[1]))


def para_corner(x, y, a, b, c):
    """The parallelogram's fourth corner, v1 + v2 - v0 (clamped as flush.s)."""
    return (min(1023, max(-1024, int(x[b]) + int(x[c]) - int(x[a]))),
            min(1023, max(-1024, int(y[b]) + int(y[c]) - int(y[a]))))


class RoomDrawer:
    """Builds a frame's faces and VDP1 commands for a list of rooms."""

    def __init__(self, level, opts=None):
        self.level = level
        self.opts = opts or Options()
        self.kept = {}          # (room, quad) -> (level, turned): REUSE's decisions
        self.frame_index = 0
        self.facing = {}        # room -> per quad (run, plane) or None
        self.prefs = {}         # texture -> 'rows', 'cols' or None (turn_by_texture)
        self.room_vertices = {}     # room -> its vertices' first index in the frame's

    # --- faces ------------------------------------------------------------

    def quad_runs(self, room):
        if room.index not in self.facing:
            pts = [tuple(int(c) for c in v) for v in room.points]
            self.facing[room.index] = [pack_level.room_quad_facing(pts, q) for q in room.quads]
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

    def texture_pref(self, tex):
        """Which way a texture's strong edges run: VDP1 draws edges along its
        lines straight and steps edges across them a pixel line to line, so
        'rows' (draw unturned) when its vertical edges are the weaker by a
        quarter, 'cols' (turned) when its horizontal ones are."""
        if tex not in self.prefs:
            t = self.level.textures[tex]
            across_cols, across_rows = textures.edge_energy(t.texels(), t.colors)
            self.prefs[tex] = 'rows' if across_cols * 1.25 < across_rows else \
                'cols' if across_rows * 1.25 < across_cols else None
        return self.prefs[tex]

    def turn_decision(self, x, y, a, b, c, d, tex=None):
        """faces.s TURN: (turned, the lines' length, their count)."""
        as_is = max(span(x, y, a, d), span(x, y, b, c))
        turned = max(span(x, y, a, b), span(x, y, d, c))
        turn = self.opts.turn and as_is > turned
        if tex is not None and self.level.textures[tex].h % 8:
            return False, turned, as_is     # its transposed copy couldn't be drawn
        if self.opts.turn and self.opts.turn_by_texture and tex is not None:
            pref = self.texture_pref(tex)
            if pref == 'rows' and as_is <= 1.5 * turned:
                turn = False
            elif pref == 'cols' and turned <= 1.5 * as_is:
                turn = True
        return (True, as_is, turned) if turn else (False, turned, as_is)

    def add_room(self, room, R, T, viewport, camera, faces, verts):
        opts = self.opts
        v4 = room.points.astype(np.int64)
        shades = room.shades.astype(np.int64)
        base = verts.add(*project(R, T, v4, shades, viewport, opts), shades)
        self.room_vertices[room.index] = base
        x, y, z, g, clip, s = verts.arrays()
        runs = self.quad_runs(room)
        parity = self.frame_index & 1

        def backface(a, b, c):
            return (int(x[b]) - int(x[a])) * (int(y[c]) - int(y[a])) <= (int(x[c]) - int(x[a])) * (int(y[b]) - int(y[a]))

        for qi, (q0, q1, q2, q3, tex, transp) in enumerate(room.quads):
            a, b, c, d = base + q0, base + q1, base + q2, base + q3
            run = runs[qi]
            if run is not None:
                axis = run[0] >> 1
                plane = run[1] << 6
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
                x, y, z, g, clip, s = verts.arrays()
                continue
            if z[a] < NEAR_SPLIT_DIST:
                if backface(a, b, c):
                    continue
                turned = self.turn_decision(x, y, a, b, c, d, tex)[0]
                bucket = max(z[a], z[b], z[c], z[d]) >> OT_SHIFT
                faces.append(Face('tex', (a, b, c, d), bucket, tex, 0, turned, transparent=transp))
                continue
            key = (room.index, qi)
            if opts.reuse and ((qi ^ len(room.quads)) & 1) != parity and key in self.kept:
                level, turned = self.kept[key]
            else:
                turned, across, lines = self.turn_decision(x, y, a, b, c, d, tex)
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
            bucket = max(z[a], z[b], z[c]) >> OT_SHIFT
            if opts.para_triangles:
                p = para_corner(x, y, a, b, c)
                pa, pb, pd = (int(x[a]), int(y[a])), (int(x[b]), int(y[b])), (int(x[c]), int(y[c]))
                lines = max(span_xy(pa, pd), span_xy(pb, p))
                across = max(span_xy(pa, pb), span_xy(pd, p))
                level = self.lod_level(tex, False, across, lines)
                faces.append(Face('para', (a, b, c), bucket, tex, level))
                continue
            turned, across, lines = self.turn_decision(x, y, a, b, c, c, tex)
            level = self.lod_level(tex, turned, across, lines)
            faces.append(Face('tex', (a, b, c, c), bucket, tex, level, turned))

    def near_quad(self, room, qi, corners, R, T, viewport, faces, verts):
        """rooms.cpp faceAddRoomQuads for a quad a plane cuts: split 2x2 if it
        is near enough and the option is on, else filled."""
        x, y, z, g, clip, s = verts.arrays()
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
        r = room.points.astype(np.int64)
        r0, r1, r2, r3 = (r[i] for i in q[:4])
        pts = np.array([(r0 + r1) // 2, (r1 + r2) // 2, (r2 + r3) // 2, (r3 + r0) // 2, (r0 + r1 + r2 + r3) // 4])
        # the midpoints' shades: their corners' (Vertex.g) means
        ga, gb, gc, gd = g[a], g[b], g[c], g[d]
        sa, sb, sc, sd = s[a], s[b], s[c], s[d]
        mids = list(project(R, T, pts, np.zeros(5, np.int64), viewport, self.opts))
        mids[3] = np.array([(ga + gb) >> 1, (gb + gc) >> 1, (gc + gd) >> 1, (gd + ga) >> 1, (ga + gb + gc + gd) >> 2])
        mid = verts.add(*mids, np.array([(sa + sb) >> 1, (sb + sc) >> 1, (sc + sd) >> 1, (sd + sa) >> 1,
                                          (sa + sb + sc + sd) >> 2]))
        m01, m12, m23, m30, mc = range(mid, mid + 5)
        x, y, z, g, clip, s = verts.arrays()
        for quad, cs in enumerate(((a, m01, mc, m30), (m01, b, m12, mc), (mc, m12, c, m23), (m30, mc, m23, d))):
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


def para_texture(t):
    """A room triangle's texture for drawing it as a parallelogram v0 v1
    (v1 + v2 - v0) v2: the triangle in the half s + t <= 1, sampled from the
    texture as VDP1 maps it onto the triangle drawn with v2 twice (row t
    squeezed to (1 - t) of its width), the far half end codes. Colour 15
    becomes the end code, so texels of colour 15 take the nearest other."""
    src = t.texels().copy()
    if (src == 15).any():
        src[src == 15] = textures.nearest_colors(textures.to_rgb(t.colors)[15:16], t.colors,
                                                  [i for i in range(15) if not (t.transparent and i == 0)])[0]
    h, w = src.shape
    tt = (np.arange(h) + 0.5) / h
    ss = (np.arange(w) + 0.5) / w
    S, TT = np.meshgrid(ss, tt)
    inside = S + TT <= 1.0
    sx = np.clip((S / np.maximum(1.0 - TT, 1e-6) * w).astype(int), 0, w - 1)
    out = np.where(inside, src[np.arange(h)[:, None], sx], 15).astype(np.uint8)
    return out


class ShadeCache:
    """render.cpp's cache of shaded colour table copies (gShadeTables), to
    count what it does: 256 slots, a pair for each table and shade; a miss
    builds a copy (shadeTableMiss) in the pair's slot drawn longest ago,
    unless both were drawn this frame or the last, when the face keeps its
    gouraud. Tables are known by their colours: room textures' at their
    address in the game, any other (a mip level's, a table of less contrast)
    at the next free one. new_frame() before each frame's commands."""

    SLOTS = 256

    def __init__(self, level):
        self.addr = {}
        for t in level.textures:
            self.addr.setdefault(tuple(t.colors), level.table.entries[t.index][1])
        self.next = 0x7C000 >> 3
        self.keys = [None] * self.SLOTS
        self.frames = [0] * self.SLOTS
        self.frame = 2
        self.hits = self.built = self.failed = 0

    def new_frame(self):
        self.frame += 1
        self.hits = self.built = self.failed = 0

    def copy(self, colors, level):
        """Whether a face whose corners all take level is drawn through a
        shaded copy of colors (else with gouraud)."""
        c = tuple(colors)
        if c not in self.addr:
            self.addr[c] = self.next
            self.next += 4
        table = self.addr[c]
        key = (table, level)
        slot = ((level << 3) ^ table) & 0xFF
        for s in (slot, slot ^ 1):
            if self.keys[s] == key:
                self.frames[s] = self.frame
                self.hits += 1
                return True
        a = slot & ~1
        if min(self.frames[a], self.frames[a + 1]) + 2 > self.frame:
            self.failed += 1
            return False
        i = a + 1 if self.frames[a + 1] < self.frames[a] else a
        self.keys[i] = key
        self.frames[i] = self.frame
        self.built += 1
        return True


class TextureStore:
    """Texture variants (whole, transposed, mip levels, quadrants,
    parallelograms) as 4-bit data, made once. shades: a ShadeCache to draw
    through (None: every copy a face wants exists)."""

    def __init__(self, level, opts):
        self.level = level
        self.opts = opts
        self.cache = {}
        self.shades = None

    def texels(self, tex, level, turned, quadrant, para=False):
        """(texels as drawn, their 16 colours)."""
        key = (tex, level, turned, quadrant, para)
        if key not in self.cache:
            t = self.level.textures[tex]
            colors = t.colors
            if para:
                full = para_texture(t)
                if level == 0:
                    data = full
                else:
                    fx, fy = level if isinstance(level, tuple) else (1 << level, 1 << level)
                    data = textures.reduce(full, colors, 15, fx, fy)
            elif quadrant is not None:
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
        self.texture_bytes = 0

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
            self.texture_bytes += len(data)
            if addr + len(data) > VRAM_END:
                # the lab doesn't stop at VRAM's size: textures past it share
                # the region's start (the picture is then wrong; texture_bytes
                # tells how much the frame wanted)
                addr = VRAM_TEXTURES
            self.vram[addr:addr + len(data)] = data
            self.tex = (addr + len(data) + 7) & ~7
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


def contrast_colors(colors, mean, k):
    """Colours drawn k of their distance from the texture's mean colour."""
    m = textures.to_rgb([mean])[0]
    rgb = textures.to_rgb(colors)
    return [textures.from_rgb(m + (c - m) * k, colors[i] & 0x8000) for i, c in enumerate(rgb)]


def multiplied_colors(colors, level):
    """Colours times level / 16, as the PS1 shades (texel x light / 128)."""
    rgb = textures.to_rgb(colors) * (level / 16.0)
    return [textures.from_rgb(np.minimum(c, 31), colors[i] & 0x8000) for i, c in enumerate(rgb)]


def off_frame(x, y):
    return max(0, -x) + max(0, x - FRAME_W) + max(0, -y) + max(0, y - FRAME_H)


def build_commands(level, faces, verts, opts, store, cl=None):
    """flush: faces far to near (each bucket's in the reverse of the order
    they were added) into cl's commands. Returns cl."""
    cl = cl or CommandList()
    cl.add([CMD_SYSTEM_CLIP] + [0] * 9 + [FRAME_W - 1, FRAME_H - 1] + [0] * 4)
    cl.add([CMD_USER_CLIP] + [0] * 9 + [FRAME_W - 1, FRAME_H - 1] + [0] * 4)
    cl.add([CMD_LOCAL_COORD] + [0] * 15)
    arrays = verts.arrays()
    buckets = {}
    for f in faces:
        buckets.setdefault(f.bucket, []).append(f)
    for bucket in sorted(buckets, reverse=True):
        for f in reversed(buckets[bucket]):
            cl.add(face_command(level, f, arrays, opts, store, cl))
    return cl


def face_command(level, f, arrays, opts, store, cl):
    x, y, z, g, clip, s = arrays
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

    t = level.textures[f.tex]
    if f.kind == 'para':
        a, b, c = f.corners
        pts = [(int(x[a]), int(y[a])), (int(x[b]), int(y[b])), para_corner(x, y, a, b, c), (int(x[c]), int(y[c]))]
        shades = [int(g[a]), int(g[b]), int(g[c]), int(g[c])]
        statics = [int(s[a]), int(s[b]), int(s[c]), int(s[c])]
        offf = bool((clip[a] | clip[b] | clip[c]) & CLIP_FRAME)
    else:
        a, b, c, d = f.corners
        if f.turned:
            b, d = d, b
        pts = [(int(x[i]), int(y[i])) for i in (a, b, c, d)]
        shades = [int(g[i]) for i in (a, b, c, d)]
        statics = [int(s[i]) for i in (a, b, c, d)]
        offf = bool((clip[a] | clip[b] | clip[c] | clip[d]) & CLIP_FRAME)
    ctrl = CMD_DISTORTED_SPRITE
    if offf and off_frame(*pts[0]) + off_frame(*pts[3]) > off_frame(*pts[1]) + off_frame(*pts[2]):
        pts = [pts[1], pts[0], pts[3], pts[2]]
        shades = [shades[1], shades[0], shades[3], shades[2]]
        statics = [statics[1], statics[0], statics[3], statics[2]]
        ctrl ^= CMD_FLIP_H
    para = f.kind == 'para'
    texels, colors = store.texels(f.tex, f.level, f.turned, f.quadrant, para)
    th, tw = texels.shape
    srca = cl.texture((f.tex, f.level, f.turned, f.quadrant, para), texels)

    if opts.contrast:
        lit = sum(shade_level(v) for v in statics) / 4.0
        if lit < 16:
            colors = contrast_colors(colors, t.mean, max(4, round(lit / 2)) / 8.0)

    levels = [shade_level(v) for v in shades]
    if opts.light_scale != 1.0:
        levels = [int(round(16 + (lv - 16) * opts.light_scale)) for lv in levels]
    pmod = (PMOD_HSS if opts.hss else 0) | PMOD_LUT16 | (0 if para else PMOD_ECD_OFF)
    if not f.transparent:
        pmod |= PMOD_SPD_OFF
    if opts.flat_multiply:
        colr = cl.colors(multiplied_colors(colors, round(sum(levels) / 4.0)))
        grda = 0
    else:
        if f.level and opts.far_shade is not None and max(levels) - min(levels) <= opts.far_shade:
            levels = [(sum(levels) + 2) >> 2] * 4
        if (opts.shade_tables and levels.count(levels[0]) == 4
                and (store.shades is None or store.shades.copy(colors, levels[0]))):
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
    for k, (px, py) in enumerate(pts):
        w[6 + 2 * k] = px & 0xFFFF
        w[7 + 2 * k] = py & 0xFFFF
    w[14] = grda
    return w
