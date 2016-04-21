#include "mask.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#define PIPELINE \
	"filesrc name=src ! " \
	"decodebin ! " \
	"videoconvert ! " \
	"video/x-raw,format=RGB ! " \
	"appsink name=sink sync=false max-buffers=10"

static void
end_of_stream(GstAppSink *appsink, gpointer user_data)
{
	g_print("%s\n", __func__);
}

static GstFlowReturn
new_preroll(GstAppSink *sink, gpointer user_data)
{
	g_print("%s\n", __func__);

	return GST_FLOW_OK;
}

static GstFlowReturn
new_sample(GstAppSink *sink, gpointer user_data)
{
	g_print("%s\n", __func__);

	return GST_FLOW_OK;
}

static GstAppSinkCallbacks callbacks = {
	end_of_stream,
	new_preroll,
	new_sample,
};

int
main(int argc, char **argv) {
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
		NULL,
		NULL);

	g_object_set(
		G_OBJECT(mysrc),
		"location",
		"test.mkv",
		NULL);

	gst_object_unref(G_OBJECT(mysink));
	gst_object_unref(G_OBJECT(mysrc));

	gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	return 0;
}
