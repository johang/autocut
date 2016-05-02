#!/bin/sh
# Convert PNGs to 8-bit YUV 4:2:0

for MASK in "$@"
do
  if [ ! -f "$MASK" ]
  then
     echo "$MASK" not found
  else
     convert \
       -depth 8 \
       -colorspace YUV \
       "$MASK" \
       $(basename "$MASK").yuv
     echo Converted "$MASK"
  fi
done
