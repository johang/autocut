#!/usr/bin/env python
#
# Copyright 2016 Johan Gunnarsson <johan.gunnarsson@gmail.com>
#
# This file is part of AutoCut.
#
# AutoCut is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# AutoCut is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with AutoCut.  If not, see <http://www.gnu.org/licenses/>.

import subprocess, sys, os, tempfile

START_OFFSET = -0.25
STOP_OFFSET = -1.75
THRESHOLD = 10.0
RATE = 10

QUEUE = os.path.abspath("autocut-queue")
CUTS = os.path.abspath("autocut-cuts")

# Extra parameters to ffmpeg/avconv
EXTRA = " \
    -vf crop=676:480:15:48 \
    -c:a aac -ac 1 -b:a 192k -strict -2 \
    -c:v libx264 -preset fast -tune animation -crf 20 \
    -movflags faststart"

def transcode_game(clip, target, start, stop):
	ret = subprocess.call(
		"avconv -y -ss %.2f -t %.2f -i '%s' %s '%s'" % (
			start + START_OFFSET,
			stop - start + STOP_OFFSET,
			clip,
			EXTRA,
			target),
		shell=True)

	if ret == 0:
		os.symlink(target, tempfile.mktemp(".mp4", "game-", QUEUE))

def analyze_hits(hits):
	this_start = []
	this_non_start = []

	for mask, score, ts in hits:
		if mask.startswith("start."):
			if len(this_start) and len(this_non_start):
				# Found a game -- output it
				yield (max(this_start), min(this_non_start))

				this_start = [float(ts)]
			else:
				this_start.append(float(ts))

			this_non_start = []
		else:
			this_non_start.append(float(ts))

	if len(this_start) and len(this_non_start):
		# Output the last game
		yield (max(this_start), min(this_non_start))

def analyze_clip(clip, masks):
	try:
		analysis = subprocess.Popen(
			"mask --threshold %.2f --rgb --rate %d '%s' %s" % (
				THRESHOLD,
				RATE,
				clip,
				" ".join(["'%s'" % m for m in masks])),
			shell=True,
			stdout=subprocess.PIPE)

		return analyze_hits([
			x.strip().split("\t")
			for x in analysis.stdout.readlines()])
	finally:
		analysis.kill()

def ends(f, suffixes):
	return any([f.endswith(s) for s in suffixes])

if __name__ == "__main__":
	if not os.path.exists(CUTS):
		os.makedirs(CUTS)

	if not os.path.exists(QUEUE):
		os.makedirs(QUEUE)

	masks = [a for a in sys.argv[1:] if ends(a, (".rgb", ".yuv"))]
	clips = [a for a in sys.argv[1:] if ends(a, (".mp4", ".mkv", ".webm"))]

	for clip in clips:
		path = os.path.join(CUTS, os.path.basename(clip))

		if os.path.exists(path):
			continue

		os.mkdir(path)

		for (n, (start, stop)) in enumerate(analyze_clip(clip, masks)):
			transcode_game(
				clip,
				os.path.join(path, "cut-%d.mp4" % (n)),
				start,
				stop)
