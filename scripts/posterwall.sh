#!/bin/sh

input_file="$(realpath -- "$1")"
output_file="$(realpath -- "$2")"
dirname="$(dirname "$(realpath -- "$0")")"

mkdir -p posters
cd posters
rm -f *.jpg

# old regex: https://cdn.myanimelist.net/images/anime/[0-9]*/[0-9]*\.jpg
for i in `grep -Po 'https?://[^\x00-\x1f"<>^\x60{|}]*\.jpg' "$input_file"`; do
  curl "$i" -sLO &
  echo "$i"
done
wait
echo done

"$dirname"/glue.sh "$("$dirname"/factorpairs.py)" *.jpg > "$output_file"
