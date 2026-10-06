#!/usr/bin/env python

# Builds the Silverstone-style circuit (and the racing balls) in Blender.
#
# Run (from the scenes/ folder):
#   blender --background --python make-silverstone.py -- silverstone.blend ../dist/silverstone.track
#
# It writes:
#   - the .blend (collection "Main"), which export-meshes.py / export-scene.py turn into dist/silverstone.pnct + .scene
#   - a text file with the circuit's centre line, which the game's physics uses (so the model and the physics always agree)
#
# Everything is deterministic (fixed corner list).

import sys, math

args = []
for i in range(0, len(sys.argv)):
	if sys.argv[i] == '--':
		args = sys.argv[i+1:]
if len(args) != 2:
	print("\n\nUsage:\nblender --background --python make-silverstone.py -- <out.blend> <out.track>\n")
	exit(1)
out_blend, out_track = args

import bpy
import mathutils

# ----------------------------------------------------------------------------------------------
# track dimensions (game units; 1 unit ~ 5 m)

HALF_WIDTH = 2.25     # asphalt is 4.5 wide
WALL_DIST = 3.05      # invisible wall distance from the centre line (physics only; nothing is drawn there)
SPACING = 2.0         # centre line point spacing

# ----------------------------------------------------------------------------------------------
# centre line: a "turtle" walking the corners of Silverstone, clockwise.
#   ('S', name, length)            straight
#   ('A', name, degrees, radius)   arc; + is left, - is right
# (these numbers came from a small search for a layout that closes up, never crosses itself and keeps the long straights long)

SEGMENTS = [
	('S', 'Start/Finish', 96.08),
	('A', 'Copse', -92.70, 47.48),
	('S', '', 42.29),
	('A', 'Maggotts', 40.67, 23.93),
	('S', '', 6.0),
	('A', 'Becketts', -95.0, 28.12),
	('A', 'Becketts', 45.0, 18.16),
	('A', 'Chapel', -55.0, 39.04),
	('S', 'Hangar Straight', 171.97),
	('A', 'Stowe', -103.15, 33.52),
	('S', '', 52.07),
	('A', 'Vale', 60.0, 25.71),
	('A', 'Vale', -50.55, 28.71),
	('A', 'Club', -133.18, 21.65),
	('S', '', 29.53),
	('A', 'Abbey', 23.49, 35.31),
	('A', 'Abbey', -13.23, 47.17),
	('S', '', 17.76),
	('A', 'Farm', 40.0, 36.33),
	('A', 'Village', -68.55, 22.16),
	('S', '', 28.91),
	('A', 'The Loop', 153.47, 32.69),
	('S', '', 10.0),
	('A', 'Aintree', 42.25, 26.01),
	('S', 'Wellington Straight', 127.71),
	('A', 'Brooklands', 89.75, 18.95),
	('S', '', 4.0),
	('A', 'Luffield', -133.97, 20.64),
	('A', 'Woodcote', -109.29, 40.0),
	('S', 'Start/Finish', 70.52),
]

def walk_turtle():
	x, y, h = 0.0, 0.0, 0.0
	pts = [(x, y)]
	for seg in SEGMENTS:
		if seg[0] == 'S':
			length = seg[2]
			n = max(1, round(length / SPACING))
			for _ in range(n):
				x += math.cos(h) * length / n
				y += math.sin(h) * length / n
				pts.append((x, y))
		else:
			a = math.radians(seg[2]); r = seg[3]
			length = abs(a) * r
			n = max(1, round(length / SPACING))
			da = a / n
			for _ in range(n):
				hm = h + da / 2.0
				c = 2.0 * r * math.sin(abs(da) / 2.0)
				x += math.cos(hm) * c
				y += math.sin(hm) * c
				h += da
				pts.append((x, y))
	return pts

raw = walk_turtle()

# the turtle ends a little away from where it started: spread that error along the whole lap so it closes exactly
ex, ey = raw[-1][0] - raw[0][0], raw[-1][1] - raw[0][1]
cum = [0.0]
for i in range(1, len(raw)):
	cum.append(cum[-1] + math.hypot(raw[i][0] - raw[i-1][0], raw[i][1] - raw[i-1][1]))
closed = [(raw[i][0] - ex * cum[i] / cum[-1], raw[i][1] - ey * cum[i] / cum[-1]) for i in range(len(raw))]
closed.pop()  # last point duplicates the first

# resample to exactly uniform spacing:
def resample(pts):
	n = len(pts)
	segl = [math.hypot(pts[(i+1) % n][0] - pts[i][0], pts[(i+1) % n][1] - pts[i][1]) for i in range(n)]
	total = sum(segl)
	count = int(round(total / SPACING))
	step = total / count
	out = []
	i, acc = 0, 0.0
	for k in range(count):
		target = k * step
		while acc + segl[i] < target:
			acc += segl[i]; i += 1
		t = (target - acc) / segl[i]
		a, b = pts[i], pts[(i+1) % n]
		out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
	return out, total
P, LENGTH = resample(closed)
N = len(P)
DS = LENGTH / N

def tangent(i):
	a, b = P[(i - 1) % N], P[(i + 1) % N]
	dx, dy = b[0] - a[0], b[1] - a[1]
	l = math.hypot(dx, dy)
	return (dx / l, dy / l)
T = [tangent(i) for i in range(N)]
NORM = [(-t[1], t[0]) for t in T]  # left-hand normal

def point_at(s, lateral=0.0):
	f = (s / DS) % N
	i = int(math.floor(f)); t = f - i
	a, b = P[i % N], P[(i + 1) % N]
	na, nb = NORM[i % N], NORM[(i + 1) % N]
	x = a[0] + (b[0] - a[0]) * t + lateral * (na[0] + (nb[0] - na[0]) * t)
	y = a[1] + (b[1] - a[1]) * t + lateral * (na[1] + (nb[1] - na[1]) * t)
	return x, y

# ----------------------------------------------------------------------------------------------
# write the circuit data for the game:

with open(out_track, 'w') as f:
	f.write("silverstone 1\n")
	f.write("half_width %.3f\nwall_dist %.3f\n" % (HALF_WIDTH, WALL_DIST))
	f.write("length %.4f\ncount %d\n" % (LENGTH, N))
	f.write("points\n")
	for (x, y) in P:
		f.write("%.4f %.4f\n" % (x, y))
print("Track: %d points, length %.1f" % (N, LENGTH))

# ----------------------------------------------------------------------------------------------
# blender helpers

# start from an empty file:
bpy.ops.wm.read_factory_settings(use_empty=True)
main = bpy.data.collections.new("Main")
bpy.context.scene.collection.children.link(main)

def lin(r, g, b):
	"""colors below are written the way they should *look*; vertex colors are stored linear (the game's framebuffer is sRGB)"""
	return (r ** 2.2, g ** 2.2, b ** 2.2)

class Builder:
	def __init__(self):
		self.verts = []
		self.faces = []
		self.colors = []   # one color per face
	def v(self, x, y, z):
		self.verts.append((x, y, z)); return len(self.verts) - 1
	def face(self, idx, color):
		self.faces.append(tuple(idx)); self.colors.append(color)
	def quad(self, a, b, c, d, color):
		self.face((self.v(*a), self.v(*b), self.v(*c), self.v(*d)), color)
	def tri(self, a, b, c, color):
		self.face((self.v(*a), self.v(*b), self.v(*c)), color)

	def quad_toward(self, a, b, c, d, color, toward):
		"""a quad whose front face (normal) points toward the given point"""
		A, B, D = mathutils.Vector(a), mathutils.Vector(b), mathutils.Vector(d)
		n = (B - A).cross(D - A)
		if n.dot(mathutils.Vector(toward) - A) < 0:
			a, b, c, d = d, c, b, a
		self.quad(a, b, c, d, color)

	def build(self, name, smooth=False, location=(0, 0, 0)):
		mesh = bpy.data.meshes.new(name)
		mesh.from_pydata(self.verts, [], self.faces)
		mesh.update()
		layer = mesh.color_attributes.new(name="Col", type='FLOAT_COLOR', domain='CORNER')
		flat = []
		for poly, col in zip(mesh.polygons, self.colors):
			for _ in poly.loop_indices:
				flat.extend((col[0], col[1], col[2], 1.0))
		layer.data.foreach_set("color", flat)
		if smooth:
			for p in mesh.polygons:
				p.use_smooth = True
		obj = bpy.data.objects.new(name, mesh)
		obj.location = location
		main.objects.link(obj)
		return obj

# ----------------------------------------------------------------------------------------------
# strips along the centre line

def lateral_point(i, off, z):
	x, y = P[i % N]
	nx, ny = NORM[i % N]
	return (x + nx * off, y + ny * off, z)

def strip(b, off0, off1, z, color_of, step=1):
	"""a flat ribbon between two lateral offsets; color_of(i) picks each quad's color"""
	for i in range(0, N, step):
		j = i + step
		p = lateral_point(i, off0, z)
		b.quad_toward(p, lateral_point(j, off0, z), lateral_point(j, off1, z), lateral_point(i, off1, z), color_of(i), (p[0], p[1], z + 10.0))

# asphalt: one plain color
asph = Builder()
strip(asph, -HALF_WIDTH, HALF_WIDTH, 0.0, lambda i: lin(0.30, 0.31, 0.34))
asph.build("Asphalt")

# white edge lines + dashed pit-lane style marks:
lines = Builder()
for side in (-1.0, 1.0):
	strip(lines, side * (HALF_WIDTH - 0.55), side * (HALF_WIDTH - 0.35), 0.02, lambda i: lin(0.95, 0.95, 0.95))
lines.build("Lines")

# ----------------------------------------------------------------------------------------------
# start/finish: chequered line

line = Builder()
CELLS = 8
cell_w = 2.0 * HALF_WIDTH / CELLS
for ci in range(CELLS):
	for rj in range(2):
		col = lin(0.96, 0.96, 0.96) if (ci + rj) % 2 == 0 else lin(0.05, 0.05, 0.05)
		s0 = rj * 0.8; s1 = s0 + 0.8
		l0 = -HALF_WIDTH + ci * cell_w; l1 = l0 + cell_w
		x00, y00 = point_at(s0, l0); x01, y01 = point_at(s0, l1)
		x10, y10 = point_at(s1, l1); x11, y11 = point_at(s1, l0)
		line.quad((x00, y00, 0.025), (x01, y01, 0.025), (x10, y10, 0.025), (x11, y11, 0.025), col)
line.build("StartLine")

# ----------------------------------------------------------------------------------------------
# the racing balls: 12 solid-color spheres, one hue each; radius 1, the game scales them

NUM_BALLS = 12
def hsv_to_rgb(h, s, v):
	import colorsys
	return colorsys.hsv_to_rgb(h, s, v)
for i in range(NUM_BALLS):
	hue = i / NUM_BALLS
	base = lin(*hsv_to_rgb(hue, 0.85, 0.95))
	b = Builder()
	segs, rings = 24, 14
	def pt(kk, mm):
		th = math.pi * kk / rings; ph = 2 * math.pi * mm / segs
		return (math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th))
	for k in range(rings):
		for m in range(segs):
			b.quad(pt(k, m), pt(k, m+1), pt(k+1, m+1), pt(k+1, m), base)
	b.build("Ball.%02d" % i, smooth=True, location=(0, 0, 1.0 + 2.5 * i))

# floating marker above the player's ball (a small yellow diamond):
mk = Builder()
top, bot, w = (0, 0, 0.9), (0, 0, -0.9), 0.5
ring = [(w, w, 0), (-w, w, 0), (-w, -w, 0), (w, -w, 0)]
for k in range(4):
	mk.tri(top, ring[k], ring[(k+1) % 4], lin(1.0, 0.92, 0.25))
	mk.tri(bot, ring[(k+1) % 4], ring[k], lin(0.95, 0.75, 0.15))
mk.build("Marker", location=(0, 0, -50))

# ----------------------------------------------------------------------------------------------
# camera (the game moves it every frame) + file

cam_data = bpy.data.cameras.new("Camera")
cam_data.sensor_fit = 'VERTICAL'
cam_data.sensor_height = 24.0
cam_data.lens = 12.0 / math.tan(math.radians(55.0) / 2.0)
cam_data.clip_start = 0.5
cam_data.clip_end = 900.0
cam = bpy.data.objects.new("Camera", cam_data)
cam.location = (0, -15, 8)
cam.rotation_euler = (math.radians(70), 0, 0)
main.objects.link(cam)

bpy.ops.wm.save_as_mainfile(filepath=out_blend)
print("Saved", out_blend)
