/*************************************************************************/
/*  camera_android.h                                                         */
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

#ifndef CAMERAANDROID_H
#define CAMERAANDROID_H

#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"

#include <pthread.h>

struct libusb_context;
struct libusb_device_handle;
struct uvc_context;
struct uvc_device;
struct uvc_device_handle;
struct uvc_stream_ctrl;
struct uvc_frame;

class CameraFeedAndroid : public CameraFeed {
public:
  CameraFeedAndroid();
  virtual ~CameraFeedAndroid();

  bool activate_feed();
  void activate_feed_thread();
  void deactivate_feed();
  void set_device(void*);
  void frameCallback(uvc_frame *frame);
private:
  libusb_context* usb_ctx;
  libusb_device_handle* usb_devh;
  uvc_context* uvc_ctx;
  uvc_device* uvc_dev;
  uvc_device_handle* uvc_devh;
  uvc_stream_ctrl* uvc_stream_ctrl_;
  pthread_t uvc_thread;
};


class CameraAndroid : public CameraServer {
public:
  CameraAndroid();
  ~CameraAndroid()=default;
private:
  void add_active_cameras();
  void update_feeds();
};

#endif /* CAMERAANDROID_H */
