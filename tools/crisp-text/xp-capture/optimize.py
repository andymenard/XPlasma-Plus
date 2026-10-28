#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Balance XP pixel strokes on explicit integer grids for fractional UI scales.

Uses only XP GDI captures. No new rendering of the original Tahoma outlines.
Long vertical/horizontal strokes of equal source thickness get equal target
thickness. Extra pixels are preferentially distributed through counters/gaps;
baselines, source advance widths, and glyph topology remain constrained.
"""
import argparse,json,math
from pathlib import Path
from build import read_capture,build_font
SIZES=(14,17,19,22,25,28,30,33)
def rounded(value):return math.floor(value+.5)
def cells(record):
 w,h,left,top,advance,bitmap=record;stride=((w+31)//32)*4
 return {(left+x,top-y-1) for y in range(h if bitmap else 0) for x in range(w) if bitmap[y*stride+x//8]&(128>>(x%8))}
def longest(values):
 best=run=0;last=None
 for n in sorted(values):
  run=run+1 if last is not None and n==last+1 else 1;best=max(best,run);last=n
 return best

def axis_map(points,axis,scale):
 coords=[p[axis] for p in points];low=min(coords);high=max(coords)+1
 other=[p[1-axis] for p in points];threshold=max(3,math.ceil((max(other)-min(other)+1)*.55))
 strokes={c for c in range(low,high) if longest({p[1-axis] for p in points if p[axis]==c})>=threshold}
 # Anchor the baseline/origin exactly when a glyph extends across it.
 anchors=sorted({low,high}|({0} if low<0<high else set()));mapping={};stats=[]
 for a,b in zip(anchors,anchors[1:]):
  zones=[];start=a
  for c in range(a+1,b+1):
   if c==b or ((c in strokes)!=(start in strokes)):
    zones.append((start,c,start in strokes));start=c
  initial=rounded(a*scale);end=rounded(b*scale)
  # DP chooses zone boundaries, heavily penalizing inconsistent stems while
  # keeping their locations close to XP's proportions. Each source cell
  # remains at least one physical pixel: counters/diagonal connections survive.
  states={initial:(0,[])}
  for z0,z1,stem in zones:
   n=z1-z0;new={};ideal=n*scale;preferred=rounded(ideal)
   lo=max(n,math.floor(ideal)-2);hi=max(lo,math.ceil(ideal)+2)
   for pos,(cost,path) in states.items():
    for width in range(lo,hi+1):
     right=pos+width
     if right>end or right+(b-z1)>end:continue
     if z1==b and right!=end:continue
     score=cost+(width-preferred)**2*(1000 if stem else .5)+(right-z1*scale)**2*.3
     if right not in new or score<new[right][0]:new[right]=(score,path+[(z0,z1,pos,width,stem,preferred)])
   states=new
  if end not in states:raise ValueError('No topology-preserving target grid')
  for z0,z1,pos,width,stem,preferred in states[end][1]:
   for i in range(z1-z0+1):mapping[z0+i]=pos+rounded(i*width/(z1-z0))
   if stem:stats.append({'source':z1-z0,'target':width,'preferred':preferred})
 return mapping,stats

def optimize_record(record,target,base=11):
 scale=target/base;points=cells(record);advance=rounded(record[4]*scale)
 if not points:return (0,0,0,0,advance,b''),[]
 xs,xstats=axis_map(points,0,scale);ys,ystats=axis_map(points,1,scale);result=set()
 for x,y in points:
  for ty in range(ys[y],ys[y+1]):
   for tx in range(xs[x],xs[x+1]):result.add((tx,ty))
 left=min(x for x,y in result);right=max(x for x,y in result)+1;bottom=min(y for x,y in result);top=max(y for x,y in result)+1
 w=right-left;h=top-bottom;stride=((w+31)//32)*4;bitmap=bytearray(stride*h)
 for x,y in result:bitmap[(top-1-y)*stride+(x-left)//8]|=128>>((x-left)%8)
 return (w,h,left,top,advance,bytes(bitmap)),xstats+ystats

def build_optimized(capture,destination,target):
 meta,cmap,records=read_capture(capture);optimized={};stems=[]
 for i,record in records.items():optimized[i],stats=optimize_record(record,target,meta['ppem']);stems.extend(stats)
 family=f'XPlasma Tahoma Pixel {target}'
 report=build_font(capture,destination,design=(target,optimized,family))
 report.update(optimization='stem-balanced integer grid; baseline anchored; topology preserved',target_ppem=target,stem_constraints=len(stems),balanced_stems=sum(s['target']==s['preferred'] for s in stems))
 Path(destination).with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('output',type=Path);p.add_argument('--size',type=int,required=True,choices=SIZES);a=p.parse_args();print(json.dumps(build_optimized(a.capture,a.output,a.size),indent=2))
