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
#include <log.h>

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
extern int uvccamera_libusb_fd;
extern int uvccamera_libusb_busnum;
extern int uvccamera_libusb_devnum;

namespace {
  extern "C" void uvc_callback(struct uvc_frame* frame, void* userptr) {
    reinterpret_cast<CameraFeedAndroid*>(userptr)->frameCallback(frame);
  }
}

CameraFeedAndroid::CameraFeedAndroid():uvc_dev(NULL),uvc_stream_ctrl_(NULL){
  ///@TODO implement this, should store information about our available camera
  // simple MJPG version: https://gist.github.com/mike168m/6dd4eb42b2ec906e064d
  int status;
  LOGI("libusb get_godot_java()\n");
  GodotJavaWrapper *godot_java = ((OS_Android *)OS::get_singleton())->get_godot_java();
  LOGI("libusb get_usb_devices()\n");
  godot_java->get_usb_devices();

  while(!uvccamera_libusb_fd) {
	  LOGI("waiting for uvc callback\n");
	  sleep(1); // wait for callback to happen
  }
  
  // libusb init
  int fd = uvccamera_libusb_fd; //java.getFd();
  LOGI("libusb_init(): fd:%i\n", fd);
  status = libusb_init(&usb_ctx);
  LOGI("libusb_init(): %s (%i)\n", libusb_error_name(status), status);
  //status = libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, verbose);
  status = libusb_wrap_sys_device(usb_ctx, (intptr_t)fd, &usb_devh);
  LOGI("libusb_wrap_sys_device(): %s (%i)\n", libusb_error_name(status), status);

  // libuvc init
  status = uvc_init(&uvc_ctx, usb_ctx);
  LOGI("uvc_init: %s (%i) ", libuvc_error_name(status), status);

  uvc_dev = (uvc_device*) malloc(sizeof(uvc_device));
  uvc_dev->ctx = uvc_ctx;
  uvc_dev->ref = 1;
  uvc_dev->usb_dev = libusb_get_device(usb_devh);
  LOGI("uvc device: %p\n", (void*)uvc_dev->usb_dev);

  status = uvc_open_with_usb_devh(uvc_dev, &uvc_devh, usb_devh);
  LOGI("uvc_open: %s (%i) ", libuvc_error_name(status), status);
  uvc_print_diag(uvc_devh, NULL);
  /* Try to negotiate a 160x120 14 fps YUYV stream profile */
  uvc_stream_ctrl_ = (struct uvc_stream_ctrl*) malloc(sizeof(struct uvc_stream_ctrl));
  status = uvc_get_stream_ctrl_format_size(
      uvc_devh, uvc_stream_ctrl_, /* result stored in ctrl */
      UVC_FRAME_FORMAT_YUYV, /* YUV 422, aka YUV 4:2:2. try _COMPRESSED */
      160, 120, 14 /* width, height, fps */
  );
  LOGI("uvc_get_stream_ctrl_format_size: %s (%i) ", libuvc_error_name(status), status);
  uvc_print_stream_ctrl(uvc_stream_ctrl_, NULL);
}

CameraFeedAndroid::~CameraFeedAndroid() {
  // make sure we stop recording if we are!
  if (is_active()) {
    deactivate_feed();
  };

  ///@TODO free up anything used by this
  free(uvc_dev);
  free(uvc_stream_ctrl_);
}

namespace {
  extern "C" void* activate_feed_callback(void* userptr) {
    reinterpret_cast<CameraFeedAndroid*>(userptr)->activate_feed_thread();
    return NULL;
  }
}
bool CameraFeedAndroid::activate_feed() {
  ///@TODO this should activate our camera and start the process of capturing frames
  int status = pthread_create(&uvc_thread,NULL,activate_feed_callback,this);
  LOGI("uvc activate feed: %s (%i) ", strerror(status), status);
  //pthread_detach(uvc_thread);
  return status?false:true;
}

void CameraFeedAndroid::activate_feed_thread() {
  LOGI("uvc_start_streaming");
  int status = uvc_start_streaming(uvc_devh, uvc_stream_ctrl_, uvc_callback, this, 0);
  LOGI("uvc_start_streaming: %s (%i)", libuvc_error_name(status), status);
  sleep(10);
  uvc_stop_streaming(uvc_devh);
  LOGI("uvc_stop_streaming");
}

///@TODO we should probably have a callback method here that is being called by the
// camera API which provides frames and call back into the CameraServer to update our texture

void CameraFeedAndroid::deactivate_feed(){
  ///@TODO this should deactivate our camera and stop the process of capturing frames
  /* End the stream. Blocks until last callback is serviced */
}

void CameraFeedAndroid::set_device(void* p_device) {
  assert(p_device);
  //device.swap(p_device); // ! can't copy unique_ptr
  // get some info
  //set_name((char*)device->get_name());
}

/* This callback function runs once per frame. Use it to perform any
 * quick processing you need, or have it put the frame into your application's
 * input queue. If this function takes too long, you'll start losing frames. */
void CameraFeedAndroid::frameCallback(uvc_frame_t *frame) {
  LOGI("uvc_camera_feed_callback triggered");
  uvc_frame_t *bgr;
  uvc_error_t ret;
  Ref<Image> p_rgb_img;
  PoolVector<uint8_t> img_data;
  img_data.resize(3 * frame->width * frame->height);
  PoolVector<uint8_t>::Write w = img_data.write();
  /* We'll convert the image from YUV/JPEG to BGR, so allocate space */
  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    LOGI("unable to allocate bgr frame!");
    return;
  }

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
  // Find cameras active right now
  update_feeds();
  add_active_cameras();

  // need to add something that will react to devices being connected/removed...
};

void CameraAndroid::update_feeds() {
  // replace this by inotify
  Ref<CameraFeedAndroid> newfeed;
  newfeed.instance();
  newfeed->set_name("Android Dummy Device");
  //newfeed->activate_feed();
  add_feed(newfeed);
}

void CameraAndroid::add_active_cameras(){
  ///@TODO scan through any active cameras and create CameraFeedAndroid objects for them
}
