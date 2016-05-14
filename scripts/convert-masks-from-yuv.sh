#!/bin/sh
# Convert PNGs to 8-bit YUV 4:2:0

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
       -colorspace YUV \
       "$MASK" \
       $(basename "$MASK").bmp
     echo Converted "$MASK"
  fi
done
