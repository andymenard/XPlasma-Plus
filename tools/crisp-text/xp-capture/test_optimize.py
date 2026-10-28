# SPDX-License-Identifier: GPL-3.0-or-later
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from optimize import axis_map,optimize_record,cells,SIZES,rounded

def record(rows,left=0,top=None,advance=None):
 w=len(rows[0]);h=len(rows);stride=((w+31)//32)*4;data=bytearray(stride*h)
 for y,row in enumerate(rows):
  for x,c in enumerate(row):
   if c=='#':data[y*stride+x//8]|=128>>(x%8)
 return w,h,left,h if top is None else top,w+1 if advance is None else advance,bytes(data)
class OpticalDesignTests(unittest.TestCase):
 def test_equal_stems_get_equal_physical_widths(self):
  source=record(['#...#']*3+['#####']+['#...#']*3)
  for size in SIZES:
   points=cells(source);mapping,stats=axis_map(points,0,size/11)
   self.assertEqual(mapping[1]-mapping[0],rounded(size/11))
   self.assertEqual(mapping[5]-mapping[4],rounded(size/11))
   self.assertTrue(all(s['target']==s['preferred'] for s in stats))
 def test_baseline_and_advance_stay_anchored(self):
  source=record(['#...#']*3+['#####']+['#...#']*3,top=5,advance=7)
  for size in SIZES:
   mapping,_=axis_map(cells(source),1,size/11)
   self.assertEqual(mapping[0],0)
   output,_=optimize_record(source,size)
   self.assertEqual(output[4],rounded(7*size/11))
 def test_no_grid_cell_collapses_so_counters_remain_open(self):
  source=record(['#####','#...#','#...#','#...#','#####'])
  for size in SIZES:
   points=cells(source)
   for axis in [0,1]:
    mapping,_=axis_map(points,axis,size/11)
    self.assertTrue(all(mapping[n+1]>mapping[n] for n in range(min(mapping),max(mapping))))
   output,_=optimize_record(source,size);ink=cells(output)
   self.assertNotIn((output[2]+output[0]//2,output[3]-1-output[1]//2),ink)
 def test_integer_scales_are_exact_enlargements(self):
  source=record(['#.##','#..#','####'],left=-1,top=2,advance=5)
  for size in (22,33):
   factor=size//11
   output,_=optimize_record(source,size)
   expected={(x*factor+dx,y*factor+dy) for x,y in cells(source) for dx in range(factor) for dy in range(factor)}
   self.assertEqual(cells(output),expected)
   self.assertEqual(output[4],source[4]*factor)

 def test_empty_space_keeps_spacing_without_ink(self):
  for size in SIZES:
   output,stats=optimize_record((1,1,0,0,3,b''),size)
   self.assertEqual(cells(output),set());self.assertEqual(output[4],rounded(3*size/11))
if __name__=='__main__':unittest.main()
