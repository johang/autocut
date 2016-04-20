#include "mask.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

int
main(int argc, char **argv) {
	g_set_application_name("mask");

	gst_init(&argc, &argv);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	return 0;
}
