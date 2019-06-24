/* GStreamer
* Copyright (C) 2019 Jakob <fixme@example.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
* Boston, MA 02110-1335, USA.
*/
/**
* SECTION:element-gstblurfilter
*
* The blurfilter element blurs or sharpens each frame in a grayscale video.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _USE_MATH_DEFINES


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "gstblurfilter.h"
#include <cmath>


GST_DEBUG_CATEGORY_STATIC(gst_blur_filter_debug_category);
#define GST_CAT_DEFAULT gst_blur_filter_debug_category

/* Quick fix to make GParamFlags enums cooperate with | */
inline GParamFlags operator | (GParamFlags lhs, GParamFlags rhs)
{
	return static_cast<GParamFlags>(static_cast<int>(lhs) | static_cast<int>(rhs));
}


static void gst_blur_filter_set_property(GObject * object,
	guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_blur_filter_get_property(GObject * object,
	guint property_id, GValue * value, GParamSpec * pspec);
static gboolean gst_blur_filter_src_event(GstBaseTransform * trans,
	GstEvent * event);
static GstFlowReturn gst_blur_filter_transform_frame(GstVideoFilter * filter,
	GstVideoFrame * inframe, GstVideoFrame * outframe);
float gaussian1d(float sigma, int x);
static void xyconvolution(float * preimage, float * postimage, float * kernel,
	int kernelsize, int width, int height, float weight);
static void gst_blur_filter_convolution(GstBlurFilter * blurfilter,
	GstVideoFrame * dest, const GstVideoFrame * src);

enum
{
	PROP_0,
	PROP_SIGMA,
	PROP_FILTERING
};


/* Only designed and properly tested for I420 */
#define VIDEO_SRC_CAPS \
    GST_VIDEO_CAPS_MAKE("{ I420 }")

#define VIDEO_SINK_CAPS \
    GST_VIDEO_CAPS_MAKE("{ I420 }")


/* class initialization */
G_DEFINE_TYPE_WITH_CODE(GstBlurFilter, gst_blur_filter, GST_TYPE_VIDEO_FILTER,
	GST_DEBUG_CATEGORY_INIT(gst_blur_filter_debug_category, "blurfilter", 0,
		"debug category for blurfilter filter"));


/* Filter class initialization */
static void
gst_blur_filter_class_init(GstBlurFilterClass * klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
	GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

	gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
		gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
			gst_caps_from_string(VIDEO_SRC_CAPS)));
	gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass),
		gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
			gst_caps_from_string(VIDEO_SINK_CAPS)));

	gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass),
		"Blur filter", "Generic", "Low and High pass smooth gaussian video filter",
		"Jakob");

	gobject_class->set_property = gst_blur_filter_set_property;
	gobject_class->get_property = gst_blur_filter_get_property;
	
	video_filter_class->transform_frame = GST_DEBUG_FUNCPTR(gst_blur_filter_transform_frame);
	base_transform_class->src_event = GST_DEBUG_FUNCPTR(gst_blur_filter_src_event);

	/* Install class properties */
	g_object_class_install_property(gobject_class, PROP_SIGMA,
		g_param_spec_double("sigma", "Sigma", "Sigma value of gaussian kernel",
			0.0, 100.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property(gobject_class, PROP_FILTERING,
		g_param_spec_int("filtering", "Filtering", "1 for high pass, -1 for low pass",
			-1, 1, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}


/* Initialize start values for the filter parameters */
static void
gst_blur_filter_init(GstBlurFilter *blurfilter)
{
	blurfilter->filtering = 0;
	blurfilter->sigma = 0.0;
	g_print("Blur- and sharpening filter for grayscale video\n");
	g_print("Press '+' for high pass filtering and '-' for low pass filtering\n");
}

static void keypress_handler(GstBlurFilter * blurfilter, const gchar * key)
{
	if (g_str_equal(key, "+"))
	{
		double sigma = blurfilter->sigma;
		int filtering = blurfilter->filtering;
		if (filtering == 0)
		{
			sigma = 0.5;
			filtering = 1;
			g_print("High-pass filtering\n");
		}
		else if (filtering == 1)
			sigma += 0.5;
		else if (filtering == -1 && sigma == 0.5)
		{
			sigma = 0.0;
			filtering = 0;
			g_print("No filter\n");
		}
		else if (filtering == -1)
			sigma -= 0.5;
		else
			g_print("We should not have come here..\n");
		blurfilter->sigma = sigma;
		blurfilter->filtering = filtering;
		g_print("Sigma set to %.1f\n", sigma);

	}
	else if (g_str_equal(key, "-"))
	{
		double sigma = blurfilter->sigma;
		int filtering = blurfilter->filtering;
		if (filtering == 0)
		{
			sigma = 0.5;
			filtering = -1;
			g_print("Low-pass filtering\n");
		}
		else if (filtering == -1)
			sigma += 0.5;
		else if (filtering == 1 && sigma == 0.5)
		{
			sigma = 0.0;
			filtering = 0;
			g_print("No filter\n");
		}
		else if (filtering == 1)
			sigma -= 0.5;
		else
			g_print("We should not have come here..\n");
		blurfilter->sigma = sigma;
		blurfilter->filtering = filtering;
		g_print("Sigma set to %.1f\n", sigma);
	}
}

/* Event function for handling navigation events e.g. key-presses */
static gboolean
gst_blur_filter_src_event(GstBaseTransform * trans, GstEvent * event)
{
	GstBlurFilter *blurfilter = GST_BLUR_FILTER(trans);
	const gchar *type;
	const gchar *key;

	/* If a navigation event happens */
	if (GST_EVENT_TYPE(event) == GST_EVENT_NAVIGATION)
	{
		const GstStructure *s = gst_event_get_structure(event);

		/* Get the type of event */
		type = gst_structure_get_string(s, "event");
		if (g_str_equal(type, "key-release"))
		{
			/* Get the key-press once the key has been released */
			key = gst_structure_get_string(s, "key");
			if (g_str_equal(key, "+") || g_str_equal(key, "-"))
			{
				/* Mutex lock the filter */
				GST_OBJECT_LOCK(blurfilter);
				keypress_handler(blurfilter, key);
				GST_OBJECT_UNLOCK(blurfilter);
			}
		}
	}

	return GST_BASE_TRANSFORM_CLASS(gst_blur_filter_parent_class)->src_event(trans, event);

}

/* Property setter for external access */
void
gst_blur_filter_set_property(GObject * object, guint property_id,
	const GValue * value, GParamSpec * pspec)
{
	GstBlurFilter *blurfilter = GST_BLUR_FILTER(object);

	switch (property_id) {
	case PROP_SIGMA:
		blurfilter->sigma = g_value_get_double(value);
		g_print("Sigma set to %.1f\n", blurfilter->sigma);
		break;
	case PROP_FILTERING:
		blurfilter->filtering = g_value_get_int(value);
		if (blurfilter->filtering == 0)
			g_print("No filter\n");
		else if (blurfilter->filtering > 0)
			g_print("High-pass filtering\n");
		else
			g_print("Low-pass filtering\n");
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

/* Property getters for external access */
void
gst_blur_filter_get_property(GObject * object, guint property_id,
	GValue * value, GParamSpec * pspec)
{
	GstBlurFilter *blurfilter = GST_BLUR_FILTER(object);

	switch (property_id) {
	case PROP_SIGMA:
		g_value_set_double(value, blurfilter->sigma);
		break;
	case PROP_FILTERING:
		g_value_set_int(value, blurfilter->filtering);
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

/* Computes the 1-dimensional gaussian function, given distance x and StDev sigma*/
float gaussian1d(float sigma, int x)
{
	float e = 1 / (sqrt(2 * M_PI)*sigma)*exp(-(pow(x, 2)) / (2 * pow(sigma,2)));
	return e;
}

/* 
 *	Computes the 2D convolution of the image and the kernel. This funcition only
 *	works for separable kernels, as is the case with the gaussian kernel.
 */
static void xyconvolution(float * preimage, float * postimage, float * kernel, int kernelsize, int width, int height, float weight)
{
	float tmp;
	float *tempimage = new float[height*width];
	int kernelradius = (kernelsize - 1) / 2;

	/* Computes the convolution between image and kernel in the x-dim first */
	for (int y = 0; y < height; ++y)
	{
		for (int x = kernelradius; x < width - kernelradius; ++x)
		{
			tmp = 0;
			if (x >= kernelradius && x < width - kernelradius)
			{
				for (int k = 0; k < kernelsize; ++k)
				{
					tmp += (preimage[y*width + x + k - kernelradius] * kernel[k]);
				}
			}
			tempimage[y*width + x] = tmp / weight;
		}
	}

	/* Computes the convolution between the intermediate image previously 
	   created and the kernel in the y-dim */
	for (int y = kernelradius; y < height - kernelradius; ++y)
	{
		for (int x = kernelradius; x < width - kernelradius; ++x)
		{
			tmp = 0;
			for (int k = 0; k < kernelsize; ++k)
			{
				tmp += (tempimage[(y + k - kernelradius)*width + x] * kernel[k]);
			}
			postimage[y*width + x] = tmp / weight;
		}
	}
	
	/* Clear allocated memory */
	delete[] tempimage;
}

/* Main function for the actual filtering */
static void gst_blur_filter_convolution(GstBlurFilter * blurfilter, GstVideoFrame * dest, const GstVideoFrame * src)
{
	/* Initialize base values for the frame */
	gint x, y;
	guint8 const *s;
	guint8 *d;
	gint src_y_stride, src_y_width, src_y_height;
	gint src_u_stride, src_u_width, src_u_height;
		
	gint src_v_stride,  src_v_width, src_v_height;
	gint dest_y_stride, dest_y_width, dest_y_height;
	gint dest_u_stride, dest_u_width, dest_u_height;
	gint dest_v_stride, dest_v_width, dest_v_height;
	gint src_y_depth, src_u_depth, src_v_depth;

	src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(src, 0);
	src_y_width = GST_VIDEO_FRAME_COMP_WIDTH(src, 0);
	src_y_height = GST_VIDEO_FRAME_COMP_HEIGHT(src, 0);

	dest_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(dest, 0);
	dest_y_width = GST_VIDEO_FRAME_COMP_WIDTH(dest, 0);
	dest_y_height = GST_VIDEO_FRAME_COMP_HEIGHT(dest, 0);

	dest_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE(dest, 1);
	dest_u_width = GST_VIDEO_FRAME_COMP_WIDTH(dest, 1);
	dest_u_height = GST_VIDEO_FRAME_COMP_HEIGHT(dest, 1);

	dest_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE(dest, 2);
	dest_v_width = GST_VIDEO_FRAME_COMP_WIDTH(dest, 2);
	dest_v_height = GST_VIDEO_FRAME_COMP_HEIGHT(dest, 2);

	src_y_depth = GST_VIDEO_FRAME_COMP_DEPTH(src, 0);
	src_u_depth = GST_VIDEO_FRAME_COMP_DEPTH(src, 1);
	src_v_depth = GST_VIDEO_FRAME_COMP_DEPTH(src, 2);

	/* Get sigma value for the gaussian function */
	float sigma = blurfilter->sigma;
	int filtering = blurfilter->filtering;
	/* The kernel size is set to four times sigma plus 1 */
	int kernelradius = 2 * sigma;
	int kernelsize = 2 * kernelradius + 1;
	float kernelweight = 0;

	float *kernel;
	float *preimage;
	float *postimage;

	/* Allocate memory for arrays of kernel and images */
	kernel = new float[kernelsize];
	preimage = new float[(src_y_height + kernelsize - 1)*(src_y_width + kernelsize - 1)];
	postimage = new float[(src_y_height + kernelsize - 1)*(src_y_width + kernelsize - 1)];

	/* Get pointers to Y-values for the in- and outframe */
	s = GST_VIDEO_FRAME_COMP_DATA(src, 0);
	d = GST_VIDEO_FRAME_COMP_DATA(dest, 0);

	/* Copy the Y-plane directly if filtering is disabled */
	if (filtering == 0)
	{
		gst_video_frame_copy_plane(dest, src, 0);
		goto UVframe;
	}
	
	/* Compute and save the 1-dim kernel and its normalizing weight */
	for (int i = 0; i < kernelsize; i++)
	{
		kernel[i] = gaussian1d(sigma, i - kernelradius);
		kernelweight += kernel[i];
	}

	/* Copy the inframe to preimage and zero-pad it with kernelradius 
	 * in each direction */
	for (y = 0; y < src_y_height + kernelsize - 1; ++y)
	{
		for (x = 0; x < src_y_width + kernelsize - 1; ++x)
		{
			if (x >= kernelradius && x < src_y_width + kernelradius && y >= kernelradius && y < src_y_height + kernelradius)
				preimage[y*(src_y_width + kernelsize - 1) + x] = s[(y - kernelradius)*src_y_stride + x - kernelradius];
			else
			{
				preimage[y*(src_y_width + kernelsize - 1) + x] = 0;
			}
		}
	}

	/* Compute the 2d convolution */
	xyconvolution(preimage, postimage, kernel, kernelsize, src_y_width + kernelsize - 1, src_y_height + kernelsize - 1, kernelweight);

	for (y = 0; y < dest_y_height; ++y)
	{
		for (x = 0; x < dest_y_width; ++x)
		{
			/* Set the convoluted image as the outframe if low pass filtering, remove it from the inframe 
			 * and add the difference as well as the inframe to the outframe if high pass filtering */
			d[y*dest_y_stride + x] = s[y*src_y_stride + x] + filtering * (s[y*src_y_stride + x] - postimage[(y + kernelradius)*(src_y_width + kernelsize - 1) + x + kernelradius]);
		}
	}

UVframe:
	s = GST_VIDEO_FRAME_COMP_DATA(src, 1);
	d = GST_VIDEO_FRAME_COMP_DATA(dest, 1);

	/* Each pixel in the UV-colour plane is set to 128 to ensure greyscale.
	 * If greyscale is assumed, this can be changed to gst_video_frame_copy_plane 
	 * as well for simplicity */
	for (y = 0; y < dest_u_height; y++)
	{
		for (x = 0; x < dest_u_width; x++)
		{
			d[y*dest_u_stride + x] = pow(2,src_u_depth - 1);
		}
	}

	s = GST_VIDEO_FRAME_COMP_DATA(src, 2);
	d = GST_VIDEO_FRAME_COMP_DATA(dest, 2);

	for (y = 0; y < dest_v_height; y++)
	{
		for (x = 0; x < dest_v_width; x++)
		{
			d[y*dest_v_stride + x] = pow(2,src_v_depth - 1);
		}
	}

	/* Free allocated memory */
	delete[] kernel;
	delete[] preimage;
	delete[] postimage;
}


/* Frame transformation function */
static GstFlowReturn
gst_blur_filter_transform_frame(GstVideoFilter * filter, GstVideoFrame * inframe,
	GstVideoFrame * outframe)
{

	GstBlurFilter *blurfilter = GST_BLUR_FILTER(filter);

	/* Mutex lock the filter */
	GST_OBJECT_LOCK(blurfilter);
	gst_blur_filter_convolution(blurfilter, outframe, inframe);
	GST_OBJECT_UNLOCK(blurfilter);

	return GST_FLOW_OK;
}


/* Boilerplate plugin initialization */
static gboolean
plugin_init(GstPlugin * plugin)
{
	return gst_element_register(plugin, "blurfilter", GST_RANK_NONE,
		GST_TYPE_BLUR_FILTER);
}


/* Plugin definitions */
#ifndef VERSION
#define VERSION "0.1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "SimplePackage"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "Package name"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://origin.org/"
#endif

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	blurfilter,
	"Low and High Pass gaussian blur filter",
	plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)


