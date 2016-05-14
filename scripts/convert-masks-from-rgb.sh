#!/bin/sh
# Convert PNGs to 24-bit RGB

WIDTH=704
HEIGHT=576

for MASK in "$@"
do
  if [ ! -f "$MASK" ]
  then
     echo "$MASK" not found
  else
     convert \
       -size ${WIDTH}x${HEIGHT} \
       -depth 8 \
       -colorspace RGB \
       "$MASK" \
       $(basename "$MASK").bmp
     echo Converted "$MASK"
  fi
done
