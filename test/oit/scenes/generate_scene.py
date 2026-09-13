"""Generate a self-contained glTF / GLB transparency review scene (stdlib only)."""
import base64
import json
import math
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
blob = bytearray()
g = dict(asset={'version': '2.0', 'generator': 'Filament OIT review scene'},
         scene=0, scenes=[{'name': 'OIT review - six stations', 'nodes': []}],
         nodes=[], meshes=[], materials=[], accessors=[], bufferViews=[],
         extensionsUsed=['KHR_materials_unlit'])

def view(data, target=None):
    blob.extend(b'\0' * (-len(blob) % 4))
    result = {'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data)}
    if target: result['target'] = target
    g['bufferViews'].append(result)
    blob.extend(data)
    return len(g['bufferViews']) - 1

def accessor(values, kind, components, integer=False):
    flat = [x for row in values for x in row]
    v = view(struct.pack('<' + ('I' if integer else 'f') * len(flat), *flat),
             34963 if integer else 34962)
    a = dict(bufferView=v, componentType=5125 if integer else 5126,
             count=len(values), type=kind)
    if kind == 'VEC3':
        a['min'] = [min(row[i] for row in values) for i in range(components)]
        a['max'] = [max(row[i] for row in values) for i in range(components)]
    g['accessors'].append(a)
    return len(g['accessors']) - 1

def material(name, color, mode='OPAQUE', texture=None):
    pbr = dict(baseColorFactor=color, metallicFactor=0, roughnessFactor=1)
    if texture is not None: pbr['baseColorTexture'] = {'index': texture}
    m = dict(name=name, pbrMetallicRoughness=pbr, alphaMode=mode,
             doubleSided=True, extensions={'KHR_materials_unlit': {}})
    if mode == 'MASK': m['alphaCutoff'] = 0.5
    g['materials'].append(m)
    return len(g['materials']) - 1

def mesh(name, positions, triangles, mat, uv=None):
    attrs = {'POSITION': accessor(positions, 'VEC3', 3)}
    if uv: attrs['TEXCOORD_0'] = accessor(uv, 'VEC2', 2)
    g['meshes'].append({'name': name, 'primitives': [dict(attributes=attrs,
        indices=accessor([(x,) for t in triangles for x in t], 'SCALAR', 1, True), material=mat)]})
    g['nodes'].append({'name': name, 'mesh': len(g['meshes']) - 1})
    g['scenes'][0]['nodes'].append(len(g['nodes']) - 1)

def quad(name, x, y, z, width, height, mat, tilt=0):
    mesh(name, [(x-width/2,y-height/2,z-tilt), (x+width/2,y-height/2,z+tilt),
                (x+width/2,y+height/2,z+tilt), (x-width/2,y+height/2,z-tilt)],
         [(0,1,2),(0,2,3)], mat, [(0,0),(1,0),(1,1),(0,1)])

def sphere(name, cx, cy, cz, radius, mat):
    p=[]; tris=[]; rings=16; sectors=32
    for j in range(rings+1):
        theta=math.pi*j/rings
        for i in range(sectors+1):
            phi=2*math.pi*i/sectors
            p.append((cx+radius*math.sin(theta)*math.cos(phi),
                      cy+radius*math.cos(theta), cz+radius*math.sin(theta)*math.sin(phi)))
    for j in range(rings):
        for i in range(sectors):
            a=j*(sectors+1)+i; b=a+sectors+1
            if j: tris.append((a,a+1,b))
            if j<rings-1: tris.append((a+1,b+1,b))
    mesh(name,p,tris,mat)

# Small checker alpha texture; avoid external image dependencies.
raw=b''.join(b'\0'+b''.join(bytes((255,255,255,255 if (x//4+y//4)%2 else 0))
                          for x in range(32)) for y in range(32))
def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,32,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
g['images']=[{'bufferView':view(png),'mimeType':'image/png','name':'Alpha checker'}]
g['samplers']=[{'magFilter':9728,'minFilter':9728,'wrapS':33071,'wrapT':33071}]
g['textures']=[{'source':0,'sampler':0}]
red=material('BLEND red 0.45',[1,.08,.03,.45],'BLEND')
green=material('BLEND green 0.45',[.04,1,.18,.45],'BLEND')
blue=material('BLEND blue 0.45',[.03,.25,1,.45],'BLEND')
low=material('BLEND low alpha 0.035',[.1,.7,1,.035],'BLEND')
yellow=material('OPAQUE yellow',[1,.7,.08,1])
mask=material('MASK checker',[1,.8,.15,1],'MASK',0)
back=material('OPAQUE dark reference',[.045,.055,.08,1])
white=material('OPAQUE reference tick',[.65,.7,.8,1])
for idx,(x,y) in enumerate([(-3.3,1.65),(0,1.65),(3.3,1.65),(-3.3,-1.65),(0,-1.65),(3.3,-1.65)],1):
    quad(f'{idx} background',x,y,-1.35,3,2.9,back)
    for n in range(idx): quad(f'{idx} count marker {n+1}',x-1.25+n*.21,y-1.32,1.1,.12,.10,white)
# Same geometry centers, opposite slopes: object sorting cannot resolve intersections.
for mat,tilt in [(red,.8),(green,-.8),(blue,0)]:
    quad('1 intersecting sheet',-3.3,1.65,0,2.35,2.2,mat,tilt)
sphere('2 red shell',-.38,1.65,0,.95,red)
sphere('2 blue shell',.38,1.65,.15,.95,blue)
for i in range(32):
    quad(f'3 low alpha layer {i:02}',3.3+.16*math.sin(i),1.65+.12*math.cos(i),-.9+i*.055,2.15,2.05,low)
quad('4 rear blue',-3.3,-1.65,-.6,2.5,2.2,blue)
quad('4 opaque occluder',-3.3,-1.65,.1,.8,2.4,yellow)
quad('4 front red',-3.3,-1.65,.65,2.2,.7,red)
quad('5 rear red',0,-1.65,-.6,2.5,2.3,red)
quad('5 checker occluder',0,-1.65,.1,2.4,2.3,mask)
quad('5 front blue',0,-1.65,.65,.8,2.4,blue)
for i in range(12):
    a=2*math.pi*i/12
    quad(f'6 particle {i:02}',3.3+.7*math.cos(a),-1.65+.65*math.sin(a),.6*math.sin(a*3),1.3,1.3,[red,green,blue][i%3],.25*math.cos(a))
g['cameras']=[{'name':'Front overview','type':'perspective','perspective':{'yfov':.65,'znear':.1,'zfar':100,'aspectRatio':1.6}}]
g['nodes'].append({'name':'Overview camera','camera':0,'translation':[0,0,16]})
g['scenes'][0]['nodes'].append(len(g['nodes'])-1)
g['buffers']=[{'byteLength':len(blob)}]
# GLB and glTF contain exactly the same scene and embedded resources.
j=json.dumps(g,separators=(',',':')).encode(); j+=b' '*(-len(j)%4)
b=bytes(blob)+b'\0'*(-len(blob)%4)
glb=struct.pack('<4sII',b'glTF',2,12+8+len(j)+8+len(b))+struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(b),b'BIN\0')+b
(ROOT/'oit-review.glb').write_bytes(glb)
g['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(blob).decode()
(ROOT/'oit-review.gltf').write_text(json.dumps(g,indent=2)+'\n')
print(f'Generated {len(g["meshes"])} meshes, {len(g["materials"])} materials; GLB {len(glb)} bytes')
