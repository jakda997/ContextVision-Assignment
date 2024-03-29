/*
 * GStreamer Media Player
 * Simple video player created to show an implementation of a basic low- 
 * and high pass filter, written for the pleasure of ContextVision.
 */

#include <gst/gst.h>
#include <stdio.h>


/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
	GstElement *pipeline;
	GstElement *source;
	GstElement *videoconvert;
	GstElement *videosink;
	GstElement *filter;
} CustomData;

/* Quick fix to make GstMessageType cooperate with or operators */
inline GstMessageType operator | (GstMessageType lhs, GstMessageType rhs)
{
	return static_cast<GstMessageType>(
		static_cast<int>(lhs) | static_cast<int>(rhs));
}

/* Handler for the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[])
{
	CustomData data;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	gboolean terminate = FALSE;
	char c;

	/* Initialize GStreamer */
	gst_init(&argc, &argv);

	/* Create the media player elements */
	data.source = gst_element_factory_make("uridecodebin", "source");
	data.videoconvert = gst_element_factory_make("videoconvert", "videoconvert");
	data.filter = gst_element_factory_make("blurfilter", "blurfilter");
	data.videosink = gst_element_factory_make("autovideosink", "videosink");

	/* Create the empty pipeline */
	data.pipeline = gst_pipeline_new("video-pipeline");

	if (!data.pipeline || !data.source || !data.videosink || !data.videoconvert || !data.filter)
	{
		g_printerr("All elements could not be created.\n");
		c = getchar();
		return -1;
	}
	

	/* Construct the pipeline and link everything but the source */
	gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.videoconvert, data.filter, data.videosink, NULL);

	if (gst_element_link(data.videoconvert, data.filter) != TRUE)
	{
		g_printerr("Elements could not be linked.\n");
		gst_object_unref(data.pipeline);
		c = getchar();
		return -1;
	}
	if (gst_element_link(data.filter, data.videosink) != TRUE)
	{
		g_printerr("Elements could not be linked.\n");
		gst_object_unref(data.pipeline);
		c = getchar();
		return -1;
	}

	/* Set the URI to the test video, must be set by the user */
	g_object_set(data.source, "uri", "file:///C:/Change/to/Address/to/Repository/ContextVision/media/testvideo4.mp4", NULL);

	/* Connect to pad-added signal for dynamic pipeline handling */
	g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

	/* Video playing loop */
	do
	{
		/* Start playing */
		ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
		if (ret == GST_STATE_CHANGE_FAILURE)
		{
			g_printerr("Unable to set pipeline to the playing state.\n");
			gst_object_unref(data.pipeline);
			c = getchar();
			return -1;
		}

		/* Listen to the bus */
		bus = gst_element_get_bus(data.pipeline);

		msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
			GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

		/* Parse message */
		if (msg != NULL)
		{
			GError *err;
			gchar *debug_info;

			switch (GST_MESSAGE_TYPE(msg))
			{
			case GST_MESSAGE_ERROR:
				gst_message_parse_error(msg, &err, &debug_info);
				g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
				g_printerr("Debugging information: %s \n", debug_info ? debug_info : "none");
				g_clear_error(&err);
				g_free(debug_info);
				terminate = TRUE;
				break;
			case GST_MESSAGE_EOS:
				g_print("End-Of-Stream reached.\n");
				terminate = FALSE;
				/* If end-of-stream reached, restart from the beginning */
				if (!gst_element_seek(data.pipeline,
					1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
					GST_SEEK_TYPE_SET, 0, 
					GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
					g_print("Seek failed!\n");
				}
				break;
			case GST_MESSAGE_STATE_CHANGED:
				/* We are only interested in state-changed messages from the pipeline */
				if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline))
				{
					GstState old_state, new_state, pending_state;
					gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
					g_print("Pipeline state changed from %s to %s:\n",
						gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
				}
				break;
			default:
				/* Should not be reached, but just in case */
				g_printerr("Unexpected message received.\n");
				break;
			}
			gst_message_unref(msg);
		}
	} while (!terminate);

	/* Free resources */
	gst_object_unref(bus);
	gst_element_set_state(data.pipeline, GST_STATE_NULL);
	gst_object_unref(data.pipeline);

	c = getchar();
	return 0;
}

/* Handler for the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data)
{
	GstPad *video_sink_pad = gst_element_get_static_pad(data->videoconvert, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;
	
	g_print("Received new pad %s from %s:\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked(video_sink_pad))
	{
		g_print("We are already linked. Ignoring.\n");
		goto exit;
	}

	/* Check the new pad's type */
	new_pad_caps = gst_pad_get_current_caps(new_pad);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);
	if (!g_str_has_prefix(new_pad_type, "video/x-raw"))
	{
		g_print("It has type '%s', which is not raw video. Ignoring.\n", new_pad_type);
		goto exit;
	}

	/* Attempt the link */
		ret = gst_pad_link(new_pad, video_sink_pad);
	if (GST_PAD_LINK_FAILED(ret))
	{
		g_print("Type is '%s', but link failed.\n", new_pad_type);
	}
	else
	{
		g_print("Link succeeded (type '%s'). \n", new_pad_type);
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref(new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref(video_sink_pad);
}