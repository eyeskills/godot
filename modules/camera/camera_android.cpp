/*************************************************************************/
/*  camera_android.cpp                                                       */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "camera_android.h"

#include <cassert>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>              /* low-level i/o */
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "platform/android/java_godot_wrapper.h"
#include "platform/android/os_android.h"

#include "libusb.h"
#include "libuvc/libuvc.h"
#include "libuvc/libuvc_internal.h"

const char* libuvc_error_name(int e){return uvc_strerror((uvc_error_t)e);}

namespace {
	extern "C" void uvc_callback(struct uvc_frame* frame, void* userptr) {
		print_line(vformat("uvc_callback(frame=%d, userptr=%d)\n", frame, userptr));
		reinterpret_cast<CameraFeedAndroid*>(userptr)->frame_callback(frame);
	}
}

static libusb_context* usb_ctx{nullptr};
static uvc_context* uvc_ctx{nullptr};

// Map file descriptors to associated feeds.
static Map<int, Ref<CameraFeedAndroid>> uvc_devices{};

// simple MJPG version: https://gist.github.com/mike168m/6dd4eb42b2ec906e064d

CameraFeedAndroid::CameraFeedAndroid()
		: fd{-1}, uvc_devh{nullptr}, stream_ctrl{nullptr} { }

Ref<CameraFeedAndroid> CameraFeedAndroid::create(int fd, String name)
{
	print_line(vformat("CameraFeedAndroid::create_feed(fd=%d)", fd));

	if (uvc_devices.has(fd))
		destroy(fd); // should have been destroyed already, do it now

	libusb_device_handle* usb_devh;
	int status = libusb_wrap_sys_device(usb_ctx, (intptr_t)fd, &usb_devh);
	ERR_FAIL_COND_V_MSG(status != 0, {},
		vformat("libusb_wrap_sys_device(): %s (%d)", libusb_error_name(status), status));

	// libuvc init
	uvc_device* uvc_dev;
	uvc_dev = (uvc_device*)malloc(sizeof(uvc_device)); // XXX THIS LEAKS!!??
	uvc_dev->ctx = uvc_ctx;
	uvc_dev->ref = 0;
	uvc_dev->usb_dev = libusb_get_device(usb_devh);

	uvc_device_handle* uvc_devh;
	status = uvc_open_with_usb_devh(uvc_dev, &uvc_devh, usb_devh);
	ERR_FAIL_COND_V_MSG(status != 0, {},
		vformat("uvc_open_with_usb_devh: %s (%d)", libuvc_error_name(status), status));

	uvc_print_diag(uvc_devh, NULL);

	/* Try to negotiate a 160x120 14 fps YUYV stream profile */
	struct uvc_stream_ctrl* stream_ctrl =
		(struct uvc_stream_ctrl*)malloc(sizeof(struct uvc_stream_ctrl));
	status = uvc_get_stream_ctrl_format_size(
		uvc_devh, stream_ctrl, /* result stored in ctrl */
		UVC_FRAME_FORMAT_ANY, /* YUV 422, aka YUV 4:2:2. try _COMPRESSED */
		160, 120, 15); /* width, height, fps */
	if (status != 0) {
		uvc_close(uvc_devh);
		ERR_FAIL_V_MSG({},
			vformat("uvc_get_stream_ctrl_format_size: %s (%d)", libuvc_error_name(status), status));
	}
	uvc_print_stream_ctrl(stream_ctrl, NULL);

	Ref<CameraFeedAndroid> feed;
	feed.instance();
	feed->set_name(name);
	feed->fd = fd;
	feed->uvc_devh = uvc_devh;
	feed->stream_ctrl = stream_ctrl;

	uvc_devices.insert(fd, feed);
	return feed;
}

// Can’t do this in ~CameraFeedAndroid because it might get called
// much later, when the last Ref goes out of scope.
Ref<CameraFeedAndroid> CameraFeedAndroid::destroy(int fd) {
	auto p_feed = uvc_devices.find(fd);
	if (!p_feed)
		return {};
	Ref<CameraFeedAndroid> feed{p_feed->value()};
	uvc_devices.erase(fd);

	// Make sure we stop recording if we are.
	if (feed->is_active())
		feed->deactivate_feed();
	free(feed->stream_ctrl);
	uvc_close(feed->uvc_devh);
	feed->fd = -1;
	return feed;
}

bool CameraFeedAndroid::activate_feed() {
	if (fd < 0)
		return false;
	int status = uvc_start_streaming(uvc_devh, stream_ctrl, uvc_callback, this, 0);
	ERR_FAIL_COND_V_MSG(status != 0, false,
		vformat("uvc_start_streaming: %s (%d)", libuvc_error_name(status), status));
	return true;
}

///@TODO we should probably have a callback method here that is being called by the
// camera API which provides frames and call back into the CameraServer to update our texture

void CameraFeedAndroid::deactivate_feed(){
	if (fd < 0)
		return;
	/* End the stream. Blocks until last callback is serviced */
	uvc_stop_streaming(uvc_devh);
}

/* This callback function runs once per frame. Use it to perform any
 * quick processing you need, or have it put the frame into your application's
 * input queue. If this function takes too long, you'll start losing frames. */
void CameraFeedAndroid::frame_callback(uvc_frame_t *frame) {
   print_line("uvc_camera_feed_callback triggered");
  uvc_frame_t *bgr;
  uvc_error_t ret;
  Ref<Image> p_rgb_img;
  PoolVector<uint8_t> img_data;
  img_data.resize(3 * frame->width * frame->height);
  PoolVector<uint8_t>::Write w = img_data.write();
  /* We'll convert the image from YUV/JPEG to BGR, so allocate space */
  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  ERR_FAIL_COND_MSG(!bgr, "unable to allocate bgr frame!");

  /* Do the BGR conversion */
  ret = uvc_any2bgr(frame, bgr);
  if (ret) {
    uvc_perror(ret, "uvc_any2bgr");
    uvc_free_frame(bgr);
    return;
  }
  memcpy(w.ptr(), bgr, 3 * frame->width * frame->height);

  p_rgb_img.instance();
  p_rgb_img->create(frame->width, frame->height, 0, Image::FORMAT_RGB8, img_data);
  set_RGB_img(p_rgb_img);

  uvc_free_frame(bgr);
}

CameraAndroid::CameraAndroid() {
	// Initialize libusb and libuvc if we haven’t yet.
	if (!usb_ctx) {
		int status = libusb_init(&usb_ctx);
		ERR_FAIL_COND_MSG(status != 0,
			vformat("libusb_init: %s (%d)", libusb_error_name(status), status));
		status = uvc_init(&uvc_ctx, usb_ctx);
		ERR_FAIL_COND_MSG(status != 0,
			vformat("uvc_init: %s (%d)", libuvc_error_name(status), status));
	}
	//libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, verbose);

	// Find cameras active right now
	//add_active_cameras();
	//update_feeds();
};

CameraAndroid::~CameraAndroid() {
	uvc_exit(uvc_ctx);
	libusb_exit(usb_ctx);
	uvc_ctx = nullptr;
	usb_ctx = nullptr;
};

void CameraAndroid::update_feeds() {
}

void CameraAndroid::add_active_cameras() {
	///@TODO scan through any active cameras and create CameraFeedAndroid objects for them
}
