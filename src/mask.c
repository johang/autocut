#include "mask.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define PIPELINE \
	"filesrc name=src ! " \
	"decodebin ! " \
	"videoconvert ! " \
	"video/x-raw,format=RGB ! " \
	"appsink name=sink sync=false max-buffers=10"

typedef struct {
	gint width;
	gint height;
} Stream;

static void
end_of_stream(GstAppSink *appsink, gpointer user_data)
{
	g_print("%s\n", __func__);
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

	g_print("Got preroll: %s %dx%d\n",
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

	GstMapInfo mapinfo;

	if (!gst_buffer_map(buffer, &mapinfo, GST_MAP_READ))
		goto out;

	g_print("%ld %p\n", mapinfo.size, mapinfo.data);

	gst_buffer_unmap(buffer, &mapinfo);

out:
	gst_sample_unref(sample);

	return GST_FLOW_OK;
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

	gst_init(&argc, &argv);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	GstElement *pipeline = gst_parse_launch(PIPELINE, &error);

	GstElement *mysink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	GstElement *mysrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");

	gst_app_sink_set_callbacks(
		GST_APP_SINK(mysink),
		&callbacks,
		g_malloc0(sizeof (Stream)),
		g_free);

	g_object_set(
		G_OBJECT(mysrc),
		"location",
		"test.mp4",
		NULL);

	gst_object_unref(G_OBJECT(mysink));
	gst_object_unref(G_OBJECT(mysrc));

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	return 0;
}
