#include <iostream>
#include <sstream>

#include <gst/gst.h>
#include <glib.h>


static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (msg)) {

        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar  *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);

            g_printerr("Error: %s\n", error->message);
            g_error_free(error);

            g_main_loop_quit(loop);
            break;
        }
        default:
        break;
    }

    return TRUE;
}


static int runGstreamer(int port) {
    GMainLoop *loop;

    GstElement *pipeline, *source, *demux, *conv, *sink, *rtpdec, *h264dec, *h264parse;
    GstBus *bus;
    guint bus_watch_id;

    loop = g_main_loop_new(NULL, FALSE);

    /* Create gstreamer elements */
    pipeline  = gst_pipeline_new("video-player");
    source    = gst_element_factory_make("udpsrc", "udp-input");
    conv      = gst_element_factory_make("nvvidconv",  "converter");
    rtpdec    = gst_element_factory_make("rtph264depay",  "rtp-decode");
    h264dec   = gst_element_factory_make("nvv4l2decoder",  "h264-decode");
    h264parse = gst_element_factory_make("h264parse",  "h264-parse");
    sink      = gst_element_factory_make("ximagesink", "video-output");

    if (!pipeline || !source || !conv || !sink || !rtpdec || !h264dec || !h264parse) {
        g_printerr("One element could not be created. Exiting.\n");
        return -1;
    }

  /* element configuration */
  g_object_set(G_OBJECT(source), "port", port, NULL);

    /* bus message handler */
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    /* add all elements into the pipeline */
    gst_bin_add_many (GST_BIN (pipeline), source, rtpdec, h264dec, conv, sink, h264parse, NULL);

    /* link pipeline elements */
    GstCaps *caps_source;
    caps_source = gst_caps_new_simple("application/x-rtp",
            "encoding-name", G_TYPE_STRING, "H264",
            "payload", G_TYPE_INT, 96,
            "clock-rate", G_TYPE_INT, 90000,
            "media", G_TYPE_STRING, "video",
            NULL);

    if (!gst_element_link_filtered(source, rtpdec, caps_source)){
        g_warning("Failed to link rtpenc and sink!");
        gst_object_unref(pipeline);
        return -1;
    }
    gst_caps_unref(caps_source);

    if (!gst_element_link(rtpdec, h264parse)) {
        g_printerr("Elements rtpdec and h264parse could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    GstCaps *caps_dec;
    caps_dec = gst_caps_new_simple("video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            NULL);

    if (!gst_element_link_filtered(h264parse, h264dec, caps_dec)){
        g_warning("Failed to link h264parse and h264dec!");
        gst_object_unref(pipeline);
        return -1;
    }
    gst_caps_unref(caps_dec);

    if (!gst_element_link(h264dec, conv)) {
        g_printerr("Elements h264dec and conv could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    if (!gst_element_link(conv, sink)) {
        g_printerr("Elements conv and sink could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* set the pipeline to "playing" state*/
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* iterate */
    g_print("Running...\n");
    g_main_loop_run(loop);

    /* free resources */
    g_print("Returned, stopping playback\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);

    g_print("Deleting pipeline\n");
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}


int main(int argc, char *argv[]) {
    int port = 5000;

    for (int i=0; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            std::istringstream ss(argv[i + 1]);
            if (!(ss >> port)) {
                std::cerr << "Invalid port: " << argv[i + 1] << '\n';
            } else if (!ss.eof()) {
                std::cerr << "Trailing characters after port: " << argv[i + 1] << '\n';
            }
        }
    }

    if (port == 5000) {
        std::cout << "Port set to: " << port << " (Default)" << std::endl;
    } else {
        std::cout << "Port set to: " << port << std::endl;
    }

    /* Gstreamer initialisation */
    gst_init(&argc, &argv);

    return runGstreamer(port);
}