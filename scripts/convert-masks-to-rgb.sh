#!/bin/sh
# Convert PNGs to 24-bit RGB

for MASK in "$@"
do
  if [ ! -f "$MASK" ]
  then
     echo "$MASK" not found
  else
     convert \
       "$MASK" \
       -depth 8 \
       -colorspace RGB \
       $(basename "$MASK" .png).rgb
     echo Converted "$MASK"
  fi
done
