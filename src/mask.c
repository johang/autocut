/*
    Copyright 2016 Johan Gunnarsson <johan.gunnarsson@gmail.com>

    This file is part of AutoCut.

    AutoCut is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    AutoCut is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with AutoCut.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mask.h"

#include <glib.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define PIPELINE \
	"filesrc name=src ! " \
	"decodebin ! " \
	"videoconvert ! " \
	"video/x-raw,format=RGB ! " \
	"queue ! " \
	"appsink name=sink sync=false max-buffers=10"

typedef struct {
	gint width;
	gint height;
	GMainLoop *loop;
} Stream;

typedef struct {
	Stream *stream;
	GstMapInfo map;
	gdouble timestamp;
} StreamBuffer;

typedef struct {
	GMappedFile *file;
	gchar *name;
} Mask;

static GList *masks = NULL;

static gdouble
get_timestamp(GstBuffer *buffer)
{
	// Timestamp in nanoseconds
	GstClockTime pts = GST_BUFFER_PTS(buffer);

	guint s = pts / 1000000000ULL;
	guint ms = pts % 1000000000ULL;

	// Timestamp in seconds, as a double
	return ((gdouble) s) + (((gdouble) ms) / 1000000000ULL);
}

static void
compare_frames(gpointer data, gpointer user_data)
{
	// The mask to compare against
	Mask *mask = (Mask *) data;

	StreamBuffer *buf = (StreamBuffer *) user_data;

	gsize masksz = g_mapped_file_get_length(mask->file);
	gsize framesz = buf->map.size;

	// Ignore this combination if they don't have exact same size
	if (masksz != framesz)
		return;

	guint8 *maskdat = (guint8 *) g_mapped_file_get_contents(mask->file);
	guint8 *framedat = buf->map.data;

	guint sad = 0;
	guint pixels = 0;

	gint i = 0;

	for (i = 0; i < masksz; i += 3) {
		// Skip black pixels in mask
		if (!maskdat[i + 0] && !maskdat[i + 0] && !maskdat[i + 0])
			continue;

		pixels++;

		gint dr = maskdat[i + 0] - framedat[i + 0];
		gint dg = maskdat[i + 1] - framedat[i + 1];
		gint db = maskdat[i + 2] - framedat[i + 2];

		// Averige absolute difference for R, G, B
		sad += (ABS(dr) + ABS(dg) + ABS(db)) / 3;
	}

	gdouble score = ((gdouble) sad) / pixels;

	// Print line if the mask is close enough to the frame. 6 is just an
	// arbitrary number that seems to work well.
	if (score < 6)
		g_print("%s\t%0.1f\t%0.3f\n", mask->name, score,
			buf->timestamp);
}

static void
end_of_stream(GstAppSink *appsink, gpointer user_data)
{
	g_main_loop_quit(((Stream *) user_data)->loop);
}

static GstFlowReturn
new_preroll(GstAppSink *sink, gpointer user_data)
{
	Stream *stream = (Stream *) user_data;

	GstSample *sample = gst_app_sink_pull_preroll(sink);

	GstStructure *structure = gst_caps_get_structure(
		gst_sample_get_caps(sample),
		0);

	if (!gst_structure_get_int(structure, "width", &stream->width))
		goto out;

	if (!gst_structure_get_int(structure, "height", &stream->height))
		goto out;

	g_printerr("Pre-rolled video (format %s, resolution %dx%d)\n",
		gst_structure_get_name(structure),
		stream->width,
		stream->height);

out:
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static GstFlowReturn
new_sample(GstAppSink *sink, gpointer user_data)
{
	GstSample *sample = gst_app_sink_pull_sample(sink);

	if (!sample)
		return GST_FLOW_OK;

	GstBuffer *buffer = gst_sample_get_buffer(sample);

	if (!buffer)
		goto out;

	StreamBuffer buf;

	if (!gst_buffer_map(buffer, &buf.map, GST_MAP_READ))
		goto out;

	buf.stream = (Stream *) user_data;
	buf.timestamp = get_timestamp(buffer);

	g_list_foreach(masks, compare_frames, &buf);

	gst_buffer_unmap(buffer, &buf.map);

out:
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static gboolean
load_rgb(gchar *path, GError *error)
{
	GMappedFile *file = g_mapped_file_new(path, FALSE, &error);

	if (!file)
		return FALSE;

	Mask *mask = g_malloc0(sizeof (Mask));

	mask->file = file;
	mask->name = g_path_get_basename(path);

	// Put this mask at the end of the list of masks
	masks = g_list_append(masks, mask);

	g_printerr("Loaded RGB mask (name %s, size %lu)\n",
		path,
		g_mapped_file_get_length(file));

	return TRUE;
}

static GstAppSinkCallbacks callbacks = {
	end_of_stream,
	new_preroll,
	new_sample,
};

int
main(int argc, char **argv)
{
	GError *error = NULL;

	g_set_application_name("mask");

	if (!load_rgb("2-player-luigi-loser-mario-winner.rgb", error)) {
		return 1;
	}

	if (!load_rgb("2-player-luigi-winner-mario-loser.rgb", error)) {
		return 1;
	}

	if (!load_rgb("start.rgb", error)) {
		return 1;
	}

	gst_init(&argc, &argv);

	Stream *stream = g_malloc0(sizeof (Stream));

	stream->loop = g_main_loop_new(NULL, FALSE);

	GstElement *pipeline = gst_parse_launch(PIPELINE, &error);

	if (!pipeline) {
		return 1;
	}

	GstElement *mysink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	GstElement *mysrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");

	gst_app_sink_set_callbacks(
		GST_APP_SINK(mysink),
		&callbacks,
		stream,
		g_free);

	g_object_set(
		G_OBJECT(mysrc),
		"location",
		"test.mp4",
		NULL);

	gst_object_unref(G_OBJECT(mysink));
	gst_object_unref(G_OBJECT(mysrc));

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

	g_main_loop_run(stream->loop);

	g_object_unref(pipeline);

	g_main_loop_unref(stream->loop);

	return 0;
}
