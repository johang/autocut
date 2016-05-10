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

#include <stdlib.h>
#include <math.h>
#include <glib.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define PIPELINE_RGB \
	"filesrc name=src ! " \
	"decodebin ! " \
	"videoconvert ! " \
	"video/x-raw,format=RGB ! " \
	"queue ! " \
	"appsink name=sink sync=false max-buffers=10"

#define PIPELINE_YUV \
	"filesrc name=src ! " \
	"decodebin ! " \
	"videoconvert ! " \
	"video/x-raw,format=I420 ! " \
	"queue ! " \
	"appsink name=sink sync=false max-buffers=10"

typedef struct {
	gint width;
	gint height;
	gint fps_num;
	gint fps_den;
	guint frame_num;
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

static gboolean opt_debug = FALSE;

static gdouble opt_threshold = 6;

static gchar *opt_clip = NULL;

static gboolean opt_format_rgb = FALSE;

static gboolean opt_format_yuv = FALSE;

static gboolean opt_dump = FALSE;

static GList *masks = NULL;

static GMainLoop *loop = NULL;

static gdouble
to_double(GstClockTime pts)
{
	guint s = pts / 1000000000ULL;
	guint ms = pts % 1000000000ULL;

	// Timestamp in seconds, as a double
	return ((gdouble) s) + (((gdouble) ms) / 1000000000ULL);
}

static gdouble
get_yuv_score(gsize size, guint8 *maskdat, guint8 *framedat)
{
	guint sad = 0;
	guint pixels = 0;

	gint i = 0;

	for (i = 0; i < 2 * size / 3; i++) {
		// Skip black pixels in mask
		if (!maskdat[i])
			continue;

		pixels++;

		// Averige absolute difference for Y
		sad += ABS(maskdat[i] - framedat[i]);
	}

	if (pixels > 0)
		return ((gdouble) sad) / pixels;
	else
		return G_MAXDOUBLE;
}

static gdouble
get_rgb_score(gsize size, guint8 *maskdat, guint8 *framedat)
{
	guint sad = 0;
	guint pixels = 0;

	gint i = 0;

	for (i = 0; i < size; i += 3) {
		// Skip black pixels in mask
		if (!maskdat[i + 0] && !maskdat[i + 1] && !maskdat[i + 2])
			continue;

		pixels++;

		gint dr = maskdat[i + 0] - framedat[i + 0];
		gint dg = maskdat[i + 1] - framedat[i + 1];
		gint db = maskdat[i + 2] - framedat[i + 2];

		// Averige absolute difference for R, G, B
		sad += (ABS(dr) + ABS(dg) + ABS(db)) / 3;
	}

	if (pixels > 0)
		return ((gdouble) sad) / pixels;
	else
		return G_MAXDOUBLE;
}

static void
dump_frame_to_file(gchar *mask_name, guint frame_num, guint8 *framedat,
		gsize framesz)
{
	gchar filename[128];

	if (!(g_snprintf(filename, sizeof (filename), "dump-%s-%d.bin",
			mask_name, frame_num) > 0))
		return;

	GError *error = NULL;

	if (!g_file_set_contents(filename, (gchar *) framedat, framesz,
			&error))
		g_warning("Failed to dump buffer to file: %s",
			error->message);

	g_clear_error(&error);
}

static void
compare_mask_to_frame(gpointer data, gpointer user_data)
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

	gdouble score = G_MAXDOUBLE;

	if (opt_format_rgb)
		score = get_rgb_score(MIN(masksz, framesz), maskdat, framedat);
	else if (opt_format_yuv)
		score = get_yuv_score(MIN(masksz, framesz), maskdat, framedat);

	if (score < opt_threshold) {
		// Print line if the mask is close enough to the frame
		g_print("%s\t%0.1f\t%0.3f\n", mask->name, score,
			buf->timestamp);

		if (opt_dump)
			// Write frame to a file with unique name
			dump_frame_to_file(mask->name, buf->stream->frame_num,
				framedat, framesz);
	}
}

static GstFlowReturn
new_preroll(GstAppSink *sink, gpointer user_data)
{
	Stream *stream = (Stream *) user_data;

	GstSample *sample = gst_app_sink_pull_preroll(sink);

	GstStructure *structure = gst_caps_get_structure(
		gst_sample_get_caps(sample),
		0);

	const gchar *format = NULL;

	if (!gst_structure_get_int(structure, "width", &stream->width))
		goto out;

	if (!gst_structure_get_int(structure, "height", &stream->height))
		goto out;

	if (!gst_structure_get_fraction(structure, "framerate",
			&stream->fps_num, &stream->fps_den))
		goto out;

	if (!(format = gst_structure_get_string(structure, "format")))
		goto out;

	g_info("Pre-rolled %s (format %s %s, resolution %dx%d)",
		opt_clip,
		gst_structure_get_name(structure),
		format,
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
	buf.timestamp = to_double(GST_BUFFER_PTS(buffer));

	g_list_foreach(masks, compare_mask_to_frame, &buf);

	gst_buffer_unmap(buffer, &buf.map);

	glong f = lrint(to_double(GST_BUFFER_DURATION(buffer)) *
		buf.stream->fps_num / buf.stream->fps_den);

	if (f > 1)
		g_warning("%ld missing frames at %.3f", f - 1, buf.timestamp);

out:
	gst_sample_unref(sample);

	buf.stream->frame_num++;

	return GST_FLOW_OK;
}

static void
load_mask(gchar *path)
{
	GError *error = NULL;

	GMappedFile *file = g_mapped_file_new(path, FALSE, &error);

	if (!file)
		g_critical("Failed to load mask: %s", error->message);

	Mask *mask = g_malloc0(sizeof (Mask));

	mask->file = file;
	mask->name = g_path_get_basename(path);

	// Put this mask at the end of the list of masks
	masks = g_list_append(masks, mask);

	g_info("Loaded mask (name %s, size %lu)",
		path,
		g_mapped_file_get_length(file));
}

static void
my_log_handler(const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data)
{
	switch (log_level & G_LOG_LEVEL_MASK) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
		g_printerr("ERROR: %s\n", message);
		break;
	case G_LOG_LEVEL_WARNING:
		g_printerr("WARNING: %s\n", message);
		break;
	case G_LOG_LEVEL_INFO:
		g_printerr("%s\n", message);
		break;
	case G_LOG_LEVEL_DEBUG:
		if (opt_debug)
			g_printerr("%s\n", message);
		break;
	default:
		break;
	}

	if (log_level & G_LOG_FLAG_FATAL)
		exit(1);
}

static gboolean
my_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
	GError *error = NULL;

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_ERROR:
		gst_message_parse_error(message, &error, NULL);

		g_critical("%s: %s",
			GST_OBJECT_NAME(message->src),
			error->message);
		break;
	case GST_MESSAGE_EOS:
		g_main_loop_quit(loop);
		break;
	default:
		break;
	}

	return TRUE;
}

static GstAppSinkCallbacks callbacks = {
	NULL,
	new_preroll,
	new_sample,
};

static GOptionEntry options[] = {
	{ "debug", 'd', 0, G_OPTION_ARG_NONE, &opt_debug,
		"Enable debug messages", NULL },
	{ "threshold", 't', 0, G_OPTION_ARG_DOUBLE, &opt_threshold,
		"Max average pixel value difference", "N" },
	{ "rgb", 0, 0, G_OPTION_ARG_NONE, &opt_format_rgb,
		"Use RGB masks", NULL },
	{ "yuv", 0, 0, G_OPTION_ARG_NONE, &opt_format_yuv,
		"Use YUV masks (better performance)", NULL },
	{ "dump", 0, 0, G_OPTION_ARG_NONE, &opt_dump,
		"Dump all matching frames", NULL },
	{ NULL },
};

int
main(int argc, char **argv)
{
	GError *error = NULL;

	g_set_application_name("mask");

	// These will exit the program
	g_log_set_fatal_mask("AUTOCUT", G_LOG_LEVEL_CRITICAL);

	// Register my own handler
	g_log_set_handler(
		"AUTOCUT",
		G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL,
		my_log_handler,
		NULL);

	GOptionContext *context = g_option_context_new("CLIP MASKS...");

	g_option_context_add_main_entries(context, options, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error))
		g_critical("Option parsing failed: %s", error->message);

	if (!(opt_format_rgb ^ opt_format_yuv)) {
		opt_format_rgb = TRUE;
		opt_format_yuv = FALSE;
	}

	gst_init(&argc, &argv);

	gint i = 0;

	for (; i < argc; i++) {
		if (i == 1)
			opt_clip = argv[i];
		else if (i > 1)
			load_mask(argv[i]);
	}

	g_debug("Video clip file: %s", opt_clip);
	g_debug("Pixel threshold: %.2f", opt_threshold);
	g_debug("Number of masks: %u", g_list_length(masks));
	g_debug("RGB masks: %s", opt_format_rgb ? "yes" : "no");
	g_debug("YUV masks: %s", opt_format_yuv ? "yes" : "no");
	g_debug("Dump all matching frames: %s", opt_dump ? "yes" : "no");

	if (!opt_clip)
		g_critical("No video clip");

	if (g_list_length(masks) <= 0)
		g_critical("No masks loaded");

	Stream *stream = g_malloc0(sizeof (Stream));

	loop = g_main_loop_new(NULL, FALSE);

	GstElement *pipeline = NULL;

	if (opt_format_rgb)
		pipeline = gst_parse_launch(PIPELINE_RGB, &error);
	else if (opt_format_yuv)
		pipeline = gst_parse_launch(PIPELINE_YUV, &error);

	if (!pipeline)
		g_critical("Failed to parse pipeline");

	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

	guint bus_watch_id = gst_bus_add_watch(bus, my_bus_callback, NULL);

	gst_object_unref(bus);

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
		opt_clip,
		NULL);

	gst_object_unref(G_OBJECT(mysink));
	gst_object_unref(G_OBJECT(mysrc));

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

	g_main_loop_run(loop);

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_NULL);

	g_object_unref(pipeline);

	g_source_remove (bus_watch_id);

	g_main_loop_unref(loop);

	return 0;
}
