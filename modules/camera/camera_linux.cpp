/*************************************************************************/
/*  camera_linux.cpp                                                       */
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

#include "camera_linux.h"

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
#include <linux/videodev2.h>

CameraFeedLinux::CameraFeedLinux(){
  ///@TODO implement this, should store information about our available camera
  // simple MJPG version: https://gist.github.com/mike168m/6dd4eb42b2ec906e064d
  // good explanation: http://jwhsmith.net/2014/12/capturing-a-webcam-stream-using-v4l2/
};

CameraFeedLinux::~CameraFeedLinux() {
  // make sure we stop recording if we are!
  if (is_active()) {
    deactivate_feed();
  };

  ///@TODO free up anything used by this
};

bool CameraFeedLinux::activate_feed() {
  ///@TODO this should activate our camera and start the process of capturing frames
  active = true;
  // set initial image
  auto frame = device->frame();
  Ref<Image> p_ycbcr_img;
  PoolVector<uint8_t> img_data;
  img_data.resize(3 * frame.width * frame.height);
  PoolVector<uint8_t>::Write w = img_data.write();
  //YUYV to YCbCr
  /*
  for(int i = 0; i < frame.width * frame.height; i++){
    int Y = frame[i * 2];
    int U = frame[i * 2 + 1];
    int Y2 = frame[i * 2 + 2];
    int V = frame[i * 2 + 3];
    img_data[i * 3] = Y;
    img_data[i * 3 + 1] = U;
    img_data[i * 3 + 2] = V;
    img_data[i * 3 + 3] = Y2;
    img_data[i * 3 + 4] = U;
    img_data[i * 3 + 5] = V;
  }*/
  memcpy(w.ptr(), frame.data, 3 * frame.width * frame.height);

  p_ycbcr_img.instance();
  p_ycbcr_img->create(frame.width, frame.height, 0, Image::FORMAT_RG8, img_data);
  set_YCbCr_img(p_ycbcr_img);
  return true;
};

///@TODO we should probably have a callback method here that is being called by the
// camera API which provides frames and call back into the CameraServer to update our texture

void CameraFeedLinux::deactivate_feed(){
  ///@TODO this should deactivate our camera and stop the process of capturing frames
};

void CameraFeedLinux::set_device(WebcamPtr p_device) {
  assert(p_device);
  device.swap(p_device); // ! can't copy unique_ptr
  // get some info
  set_name((char*)device->get_name());
};




CameraLinux::CameraLinux() {
  // Find cameras active right now
  update_feeds();
  add_active_cameras();

  // need to add something that will react to devices being connected/removed...
};

CameraLinux::~CameraLinux(){

};

void CameraLinux::update_feeds() {
  // replace this by inotify
  for (int i = 0; i < 4; i++){
    try {
      Ref<CameraFeedLinux> newfeed;
      newfeed.instance();
      auto webcam = std::make_unique<Webcam> ("/dev/video" + std::to_string(i), 640, 480);
      newfeed->set_device(std::move(webcam));
      //newfeed->activate_feed();
      add_feed(newfeed);
    } catch (const std::exception &ex) {
      printf("%s\n",ex.what());
    }
  }
}

void CameraLinux::add_active_cameras(){
  ///@TODO scan through any active cameras and create CameraFeedLinux objects for them
};
