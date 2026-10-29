"""The crunch lab's reference: the same rooms, view and lighting drawn as a
modern renderer would, to show what the Saturn's picture approximates.

Each room quad is two triangles (A B C, A C D) mapped with perspective-correct
texture coordinates, its texture filtered (trilinear from a box-filtered mip
chain, at SS x SS samples a pixel, box-filtered down) and depth-buffered.
Shading is the Saturn's: each corner's gouraud level added to every channel,
interpolated with perspective. Triangles of the rooms are their texture's
rectangle with its bottom edge pinched to the third corner, as VDP1 draws
them; here that corner takes the bottom edge's middle.
"""
import numpy as np

from rooms import FOG_MIN, FRAME_H, FRAME_W, VIEW_MIN, shade_level

SS = 4      # samples a pixel, each way


class TexturePyramid:
    """A texture's RGB (0..31) and coverage, halved level by level."""

    def __init__(self, texels, colors, transparent):
        pal = np.array([[(c >> s) & 31 for s in (0, 5, 10)] for c in colors], np.float32)
        rgb = pal[texels]
        alpha = np.ones(texels.shape, np.float32)
        if transparent:
            alpha[texels == 0] = 0
        rgb = rgb * alpha[..., None]
        self.levels = [(rgb, alpha)]
        while min(rgb.shape[:2]) > 1:
            h, w = rgb.shape[0] // 2 * 2, rgb.shape[1] // 2 * 2
            rgb = rgb[:h, :w].reshape(h // 2, 2, w // 2, 2, 3).mean(axis=(1, 3))
            alpha = alpha[:h, :w].reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))
            self.levels.append((rgb, alpha))

    def sample(self, u, v, lod):
        """Trilinear: u, v in texels of level 0 (texel centres at + 0.5)."""
        lod = np.clip(lod, 0, len(self.levels) - 1)
        lo = np.floor(lod).astype(np.int32)
        frac = (lod - lo)[..., None]
        out = np.zeros(u.shape + (4,), np.float32)
        for level in np.unique(lo):
            m = lo == level
            a = self._bilinear(level, u[m], v[m])
            b = self._bilinear(min(level + 1, len(self.levels) - 1), u[m], v[m])
            out[m] = a * (1 - frac[m]) + b * frac[m]
        return out

    def _bilinear(self, level, u, v):
        rgb, alpha = self.levels[level]
        h, w = alpha.shape
        s = 1.0 / (1 << level)
        x = u * s - 0.5
        y = v * s - 0.5
        x0 = np.floor(x).astype(np.int32)
        y0 = np.floor(y).astype(np.int32)
        fx = (x - x0)[:, None]
        fy = (y - y0)[:, None]
        x0c, x1c = np.clip(x0, 0, w - 1), np.clip(x0 + 1, 0, w - 1)
        y0c, y1c = np.clip(y0, 0, h - 1), np.clip(y0 + 1, 0, h - 1)
        c = np.concatenate([rgb, alpha[..., None]], axis=2)
        return ((c[y0c, x0c] * (1 - fx) + c[y0c, x1c] * fx) * (1 - fy) +
                (c[y1c, x0c] * (1 - fx) + c[y1c, x1c] * fx) * fy)


class Reference:
    def __init__(self, level):
        self.level = level
        self.pyramids = {}

    def pyramid(self, tex):
        if tex not in self.pyramids:
            t = self.level.textures[tex]
            self.pyramids[tex] = TexturePyramid(t.texels(), t.colors, t.transparent)
        return self.pyramids[tex]

    def render(self, jobs, textures=None):
        """jobs as rooms.RoomDrawer.faces takes them; textures(tex) may give
        another Texture for a texture index. RGB 0..255, FRAME_H x FRAME_W."""
        W, H = FRAME_W * SS, FRAME_H * SS
        self.color = np.zeros((H, W, 3), np.float32)
        self.depth = np.zeros((H, W), np.float32)     # 1 / z, 0 = nothing
        for job in jobs:
            room = job.room
            v4 = room.vertices.astype(np.float64) * 4
            view = (v4 @ job.R.T.astype(np.float64) + job.T.astype(np.float64)) / 256.0
            fog = np.maximum(0.0, view[:, 2] - FOG_MIN)
            g = np.minimum(room.shades * 256.0 + fog, 8191.0) / 256.0
            light = np.array([shade_level(int(min(31, x))) for x in np.floor(g)], np.float64) - 16
            cam = np.array(job.camera, np.float64)
            pts = room.vertices.astype(np.float64) * 256
            for (q0, q1, q2, q3, tex, transp) in room.quads:
                if self._back(pts, q0, q1, q2, cam):
                    continue
                t = self.level.textures[tex]
                uv = [(0, 0), (t.w, 0), (t.w, t.h), (0, t.h)]
                idx = (q0, q1, q2, q3)
                for tri in ((0, 1, 2), (0, 2, 3)):
                    self._triangle([view[idx[i]] for i in tri], [uv[i] for i in tri],
                                   [light[idx[i]] for i in tri], tex)
            for (t0, t1, t2, tex) in room.triangles:
                if self._back(pts, t0, t1, t2, cam):
                    continue
                t = self.level.textures[tex]
                self._triangle([view[t0], view[t1], view[t2]], [(0, 0), (t.w, 0), (t.w / 2, t.h)],
                               [light[t0], light[t1], light[t2]], tex)
        rgb = self.color.reshape(FRAME_H, SS, FRAME_W, SS, 3).mean(axis=(1, 3))
        return np.clip(rgb * (255.0 / 31.0), 0, 255).astype(np.uint8)

    @staticmethod
    def _back(pts, a, b, c, cam):
        n = np.cross(pts[b] - pts[a], pts[c] - pts[a])
        return np.dot(n, cam - pts[a]) >= 0

    def _triangle(self, p, uv, light, tex):
        # clip to the near plane in view space
        verts = [(np.asarray(p[i], np.float64), np.asarray(uv[i], np.float64), float(light[i])) for i in range(3)]
        poly = []
        for i in range(3):
            a, b = verts[i], verts[(i + 1) % 3]
            ina, inb = a[0][2] >= VIEW_MIN, b[0][2] >= VIEW_MIN
            if ina:
                poly.append(a)
            if ina != inb:
                s = (VIEW_MIN - a[0][2]) / (b[0][2] - a[0][2])
                poly.append((a[0] + (b[0] - a[0]) * s, a[1] + (b[1] - a[1]) * s, a[2] + (b[2] - a[2]) * s))
        for i in range(1, len(poly) - 1):
            self._raster((poly[0], poly[i], poly[i + 1]), tex)

    def _raster(self, tri, tex):
        z = np.array([v[0][2] for v in tri])
        sx = np.array([(FRAME_W / 2 + 256.0 * v[0][0] / v[0][2]) * SS for v in tri])
        sy = np.array([(FRAME_H / 2 + 256.0 * v[0][1] / v[0][2]) * SS for v in tri])
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
        if abs(area) < 1e-9:
            return
        x0 = max(int(np.floor(sx.min())), 0)
        x1 = min(int(np.ceil(sx.max())), FRAME_W * SS)
        y0 = max(int(np.floor(sy.min())), 0)
        y1 = min(int(np.ceil(sy.max())), FRAME_H * SS)
        if x0 >= x1 or y0 >= y1:
            return
        px, py = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area
        w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            return
        iz = 1.0 / z
        inv = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
        def persp(vals):
            return (w0 * vals[0] * iz[0] + w1 * vals[1] * iz[1] + w2 * vals[2] * iz[2]) / inv
        u = persp([v[1][0] for v in tri])
        v = persp([v[1][1] for v in tri])
        lt = persp([v[2] for v in tri])
        # level of detail: texels a sample, from the neighbouring samples
        du = np.hypot(*np.gradient(u)) if min(u.shape) > 1 else np.zeros_like(u)
        dv = np.hypot(*np.gradient(v)) if min(v.shape) > 1 else np.zeros_like(v)
        lod = np.log2(np.maximum(np.maximum(du, dv) * 1.0, 1e-6))
        depth = self.depth[y0:y1, x0:x1]
        nearer = inside & (inv > depth)
        if not nearer.any():
            return
        c = self.pyramid(tex).sample(u[nearer], v[nearer], lod[nearer])
        opaque = c[:, 3] >= 0.5
        rgb = np.clip(c[:, :3] / np.maximum(c[:, 3:], 1e-6) + lt[nearer][:, None], 0, 31)
        ys, xs = np.nonzero(nearer)
        ys, xs = ys[opaque], xs[opaque]
        self.color[y0 + ys, x0 + xs] = rgb[opaque]
        depth[ys, xs] = inv[nearer][opaque]
