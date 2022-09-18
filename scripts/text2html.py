#!/usr/bin/env python3

import sys
import html
import os
import tempfile
import subprocess

Tempfile = lambda x: tempfile.NamedTemporaryFile(suffix=x, mode='w+', encoding='utf8')

CSS = '''\
<style>
  body {
    margin: 0;
    background-color: #303841;
  }

  .text {
    font-family: consolas, monospace;
    font-size: 14pt;
    color: #D8DEE9;
    background-color: #303841;
    border-spacing: 0;
    padding: 2px 10px 50px 0;
    width: 100%;
  }

  .name {
    font-family: sans-serif;
    background-color: #303841;
    border-radius: 7px 7px 0 0;
    color: white;
    padding: 7px 100px 7px 14px;
    margin: 3px 0 0 40px;
    display: inline-block;
    font-size: 12px;
    font-family: 'Segoe UI', Arial, sans-serif;
  }

  .header {
    background-color: #6D6D69;
  }

  .number {
    color: #848B95;
    padding: 0 25px 0 20px;
    text-align: right;
    vertical-align: top;
  }

  .line {
    margin: 0;
    padding: 0;
    white-space: pre-wrap;
  }
</style>
'''

def make_html(path, outfile=sys.stdout):
  print('<meta charset="UTF-8">', file=outfile)
  print(CSS, file=outfile)

  shortname = os.path.basename(path)
  print(f'<div class="header"><div class="name">{html.escape(shortname)}</div></div>', file=outfile)

  print('<table class="text">', file=outfile)
  for i, line in enumerate(open(path)):
    print(f'  <tr><td class="number">{i+1}</td><td class="line">{html.escape(line[:-1])}</td></tr>', file=outfile)
  print("</table>", file=outfile)
  outfile.flush()

def help(file):
  print("usage: ./txt_to_html.py file.txt > file.html", file=file)
  print("       ./txt_to_html.py file.txt file.html", file=file)
  print("       ./txt_to_html.py file.txt --image", file=file)
  print("       ./txt_to_html.py file.txt --image file.png", file=file)

def convert_image(htmlname, pngname):
  subprocess.run(['wkhtmltoimage', '--log-level', 'warn', '--width', '1000', htmlname, pngname], timeout=60)
  subprocess.run(['mogrify', pngname])

def main(argv):
  if len(argv) < 2:
    help(sys.stderr)
    exit()

  name = argv[1]
  if "--image" in argv and argv[2] == "--image" and len(argv) == 3:
    with Tempfile('.html') as htmlfile, Tempfile('.png') as pngfile:
      make_html(name, htmlfile)
      convert_image(htmlfile.name, pngfile.name)
      subprocess.run(['sxiv', pngfile.name])
  elif "--image" in argv and argv[2] == "--image" and len(argv) == 4:
    with Tempfile('.html') as htmlfile:
      make_html(name, htmlfile)
      convert_image(htmlfile.name, argv[3])
  elif "--help" in argv:
    help(sys.stdout)
  elif len(argv) == 2:
    make_html(name, sys.stdout)
  elif len(argv) == 3:
    make_html(name, open(argv[2], 'w'))
  else:
    help(sys.stderr)

if __name__ == '__main__':
  main(sys.argv)
