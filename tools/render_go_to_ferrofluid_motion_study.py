#!/usr/bin/env python3
"""One rotating ferrofluid-style navigation GIF, pure Python/Pillow.

Opaque liquid volume with retracting conical spikes, not a filament/wireframe.
Illustrative synthetic envelope only; no live audio or runtime implementation.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageChops
from render_go_to_audio_knot_motion_study import envelope
from render_go_to_upper_states_study import background
from render_go_to_unified_study import lower
from render_main_screen_centered_study import caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
FPS, SECONDS = 20, 8


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def unit(v):
    length = math.sqrt(dot(v, v))
    return tuple(x / length for x in v) if length else (0., 0., 1.)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def mesh():
    p = (1 + math.sqrt(5)) / 2
    vertices = [unit(v) for v in ((-1,p,0),(1,p,0),(-1,-p,0),(1,-p,0),
                (0,-1,p),(0,1,p),(0,-1,-p),(0,1,-p),(p,0,-1),(p,0,1),(-p,0,-1),(-p,0,1))]
    faces = [(0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),(1,5,9),(5,11,4),
             (11,10,2),(10,7,6),(7,1,8),(3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),
             (4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1)]
    for _ in range(4):
        edges, refined = {}, []
        def middle(a, b):
            key = tuple(sorted((a, b)))
            if key not in edges:
                edges[key] = len(vertices)
                vertices.append(unit(tuple(x + y for x, y in zip(vertices[a], vertices[b]))))
            return edges[key]
        for a, b, c in faces:
            ab, bc, ca = middle(a,b), middle(b,c), middle(c,a)
            refined.extend(((a,ab,ca),(b,bc,ab),(c,ca,bc),(ab,bc,ca)))
        faces = refined
    # Spike centers coincide with mesh vertices, so their tips stay pointed.
    seeds = []
    for i in range(30):
        y = 1 - 2 * (i + .5) / 30
        angle = i * math.pi * (3 - math.sqrt(5))
        r = math.sqrt(1-y*y)
        desired = (r*math.cos(angle), y, r*math.sin(angle))
        seeds.append(max(vertices, key=lambda v: dot(v, desired)))
    profiles = []
    for x, y, z in vertices:
        spike = 0.
        for index, seed in enumerate(seeds):
            distance = math.acos(max(-1., min(1., dot((x,y,z), seed))))
            if distance < .34:
                cone = (1-distance/.34)**1.55 * (.83 + .17*math.sin(index*2.3)**2)
                spike = max(spike, cone)
        bulk = 1 + .065*math.sin(3*x+1)*math.cos(4*y-.2) + .038*math.sin(5*z+2*y)
        profiles.append((spike, bulk))
    return vertices, faces, profiles


VERTICES, FACES, PROFILES = mesh()
LIGHT = unit((-.45, .65, .70))
HALF = unit((LIGHT[0], LIGHT[1], LIGHT[2]+1))
VIOLET = unit((.78, .1, .62))


def liquid(image, time):
    level, attack = envelope(time)
    yaw = math.tau * time / SECONDS
    tilt = -.22 + .10 * math.sin(yaw)
    ca, sa, ct, st = math.cos(yaw), math.sin(yaw), math.cos(tilt), math.sin(tilt)
    points = []
    for (x,y,z), (spike, bulk) in zip(VERTICES, PROFILES):
        radius = (60+5*level)*bulk + (6+23*level+6*attack)*spike
        x, y, z = x*radius*1.045, y*radius*.94, z*radius
        x, z = x*ca+z*sa, -x*sa+z*ca
        y, z = y*ct-z*st, y*st+z*ct
        points.append((x,y,z))
    normals = [[0.,0.,0.] for _ in points]
    face_normals = []
    for a,b,c in FACES:
        av, bv, cv = points[a], points[b], points[c]
        n = cross(tuple(bv[k]-av[k] for k in range(3)), tuple(cv[k]-av[k] for k in range(3)))
        face_normals.append(n)
        for i in (a,b,c):
            for k in range(3):
                normals[i][k] += n[k]
    normals = [unit(n) for n in normals]
    projected = [(round(200+x), round(118-y)) for x,y,z in points]
    assert all(84 <= x <= 316 and 4 <= y <= 232 for x,y in projected)
    visible = [(sum(points[i][2] for i in face)/3, index, face)
               for index, face in enumerate(FACES) if face_normals[index][2] > 0]
    im = image.copy()
    draw = ImageDraw.Draw(im)
    for _, index, face in sorted(visible):
        n = unit(tuple(sum(normals[i][k] for i in face) for k in range(3)))
        diffuse = max(0., dot(n, LIGHT))
        specular = max(0., dot(n, HALF))**48
        rim = (1-max(0., n[2]))**3
        rx, ry = 2*n[0]*n[2], 2*n[1]*n[2]
        softbox = math.exp(-((ry-.33)/.20)**2) * math.exp(-((rx+.30)/.78)**2)
        violet = max(0., dot(n, VIOLET))**17 * (.35+.65*level)
        color = tuple(max(0,min(255,round(base + diffuse*d + specular*s + rim*r + softbox*b + violet*v)))
                      for base,d,s,r,b,v in ((13,21,195,43,79,32),(13,22,195,40,81,20),(18,28,202,57,91,53)))
        draw.polygon([projected[i] for i in face], fill=color)
    return im


def main():
    shared = background()
    top_base = shared.crop((0,0,400,240))
    bottom = lower(shared.crop((40,256,360,496)), current='sequencer')
    sheet = Image.new('RGB', (432,590), OUTSIDE)
    caption(sheet,16,12,'navigation / magnetic ink')
    caption(sheet,16,29,'solid liquid / slow turn / growing spikes')
    sheet.paste(bottom,(56,304))
    caption(sheet,16,557,'8 second loop / larger form / same menu')
    caption(sheet,16,575,'synthetic envelope / not live audio or runtime')
    # Global palette prevents static menus and the frozen blur from flickering.
    atlas = Image.new('RGB',(432,590+4*240),OUTSIDE)
    atlas.paste(sheet,(0,0))
    for i,time in enumerate((.80,1.8,3.52,6.8)):
        atlas.paste(liquid(top_base,time),(16,590+i*240))
    quantized = atlas.quantize(colors=248,method=Image.Quantize.MEDIANCUT)
    colors = quantized.getpalette()[:248*3]
    fixed = [(244,244,244),(181,154,232),(184,184,184),(94,94,94),OUTSIDE,(184,180,192),(13,13,18),(230,231,245)]
    palette = Image.new('P',(1,1))
    palette.putpalette(colors+[v for color in fixed for v in color])
    frames = []
    for index in range(FPS*SECONDS):
        time = (index/FPS+.8) % SECONDS
        frame = sheet.copy()
        frame.paste(liquid(top_base,time),(16,48))
        frame = frame.quantize(palette=palette,dither=Image.Dither.NONE)
        frames.append(frame.resize((864,1180),Image.Resampling.NEAREST))
    OUT.mkdir(parents=True,exist_ok=True)
    path = OUT/'go-to-ferrofluid-motion-2x.gif'
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=50,loop=0,disposal=1,optimize=False)
    # Verify the encoded artifact, not only the pre-encoding frames.
    with Image.open(path) as result:
        first = result.convert('RGB')
        elapsed, moving = 0, False
        for index in range(result.n_frames):
            result.seek(index)
            elapsed += result.info.get('duration',0)
            delta = ImageChops.difference(first,result.convert('RGB')).getbbox()
            if delta:
                moving = True
                assert 200 <= delta[0] < delta[2] <= 664
                assert 104 <= delta[1] < delta[3] <= 560
        assert elapsed == 8000 and moving and result.info['loop'] == 0
        count = result.n_frames
    print(f'{path.relative_to(ROOT)} / {elapsed}ms / {count} frames / {path.stat().st_size} bytes')


if __name__ == '__main__':
    main()
