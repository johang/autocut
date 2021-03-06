#!/bin/sh
# Convert PNGs to 24-bit RGB

for MASK in "$@"
do
  if [ ! -f "$MASK" ]
  then
     echo "$MASK" not found
  else
     convert \
       -depth 8 \
       -colorspace RGB \
       "$MASK" \
       $(basename "$MASK").rgb
     echo Converted "$MASK"
  fi
done
