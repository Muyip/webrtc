/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCVideoSource+Private.h"

#include "webrtc/base/checks.h"
#include "webrtc/sdk/objc/Framework/Classes/objcvideotracksource.h"

// TODO(magjed): Refactor this class and target ObjcVideoTrackSource only once
// RTCAVFoundationVideoSource is gone. See http://crbug/webrtc/7177 for more
// info.
@implementation RTCVideoSource {
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> _nativeVideoSource;
}

- (instancetype)initWithNativeVideoSource:
    (rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>)nativeVideoSource {
  RTC_DCHECK(nativeVideoSource);
  if (self = [super initWithNativeMediaSource:nativeVideoSource
                                         type:RTCMediaSourceTypeVideo]) {
    _nativeVideoSource = nativeVideoSource;
  }
  return self;
}

- (instancetype)initWithNativeMediaSource:
    (rtc::scoped_refptr<webrtc::MediaSourceInterface>)nativeMediaSource
                                     type:(RTCMediaSourceType)type {
  RTC_NOTREACHED();
  return nil;
}

- (NSString *)description {
  NSString *stateString = [[self class] stringForState:self.state];
  return [NSString stringWithFormat:@"RTCVideoSource( %p ): %@", self, stateString];
}

- (void)capturer:(RTCVideoCapturer *)capturer didCaptureVideoFrame:(RTCVideoFrame *)frame {
  static_cast<webrtc::ObjcVideoTrackSource *>(_nativeVideoSource.get())->OnCapturedFrame(frame);
}

- (void)adaptOutputFormatToWidth:(int)width height:(int)height fps:(int)fps {
  static_cast<webrtc::ObjcVideoTrackSource *>(_nativeVideoSource.get())
      ->OnOutputFormatRequest(width, height, fps);
}

#pragma mark - Private

- (rtc::scoped_refptr<webrtc::VideoTrackSourceInterface>)nativeVideoSource {
  return _nativeVideoSource;
}

@end
