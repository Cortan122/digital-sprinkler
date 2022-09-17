#!/bin/sh

input_file="$(realpath -- "$1")"
output_file="$(realpath -- "$2")"
cd -- "$(dirname -- "$1")"

f="$(basename "$input_file" .tex)"
i="$f.tex"
[ "$i" -ot "$f.pdf" ] && exit 0
grep -Fq '\begin{document}' "$i" || exit 0

if [ -f "$f.ipynb" ]; then
  if pandoc --list-input-formats | grep -Fq ipynb; then
    [ "$f.ipynb" -ot "$f.ipynb.tex" ] || pandoc "$f.ipynb" -o "$f.ipynb.tex" --extract-media=pandoc_media
  else
    jupyter nbconvert --to pdf "$f.ipynb"
    exit 0
  fi
fi

cmd="xelatex"
grep -Fq '\usepackage[english,russian]{babel}' "$i" && cmd="pdflatex"
grep -Fq '\usepackage[report]{styledoc19}' "$i" && cmd="pdflatex"

"$cmd" "$i" || exit 1
grep -Fq 'Rerun to get cross-references right.' "$f.log" && "$cmd" "$i"
grep -Fq 'Table widths have changed. Rerun LaTeX.' "$f.log" && "$cmd" "$i"

if grep -Fq 'There were undefined references.' "$f.log"; then
  bibtex "$f" || exit 1
  "$cmd" "$i" || exit 1
  "$cmd" "$i" || exit 1
fi

cp "$f.pdf" "$output_file"

# todo:
# touch -d "$(git log --format="%ai" -- "$i" | tail -n -1)" "$output_file"
