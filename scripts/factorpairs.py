#!/usr/bin/env python3
import glob, itertools
from PIL import Image

ratio = 16/9

def lostSpace(pair):
  x,y,d = pair
  w,h = size
  imw = max(w*x, h*y*ratio)
  imh = max(h*y, w*x/ratio)
  return imw*imh - w*x*h*y + d*w*h

def factor(n, d):
  n += d
  t = ([(i,n//i,d), (n//i,i,d)] for i in range(1, int(n**.5)+1) if n % i == 0)
  return itertools.chain(*t)

files = glob.glob("*.jpg")
with Image.open(files[1]) as im:
  size = im.size

n = len(files)
ranges = (factor(n, i) for i in range(10))
pairs = itertools.chain(*ranges)
res = min(pairs, key=lostSpace)
print(res[0])
