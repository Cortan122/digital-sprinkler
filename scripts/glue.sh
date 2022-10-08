#!/bin/bash

[ "$#" -lt 2 ] && (>&2 echo "Not enough arguments") && exit 1

colls="$1"
shift
let "rows = ($#+$colls-1)/$colls"

filename="$(basename -- "$1")"
extension="png" #"${filename##*.}"

arr=()
for i in $(seq 1 "$rows") ;do
  temp_file=$(mktemp "tmp.$(printf "%06d" $i).XXXXXXXXXX.$extension")
  arr+=("$temp_file")
  >&2 echo "appending" "${@:1:$colls}"
  convert "${@:1:$colls}" +append "$temp_file"
  shift "$colls"
done

>&2 echo "appending" "${arr[@]}"
temp_file=$(mktemp "tmp.XXXXXXXXXX.$extension")
convert "${arr[@]}" -append "$temp_file"
cat "$temp_file"
rm "${arr[@]}" "$temp_file"
