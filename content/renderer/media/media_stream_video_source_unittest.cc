// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_task_environment.h"
#include "build/build_config.h"
#include "content/child/child_process.h"
#include "content/renderer/media/media_stream_video_source.h"
#include "content/renderer/media/media_stream_video_track.h"
#include "content/renderer/media/mock_constraint_factory.h"
#include "content/renderer/media/mock_media_stream_video_sink.h"
#include "content/renderer/media/mock_media_stream_video_source.h"
#include "content/renderer/media/video_track_adapter.h"
#include "media/base/limits.h"
#include "media/base/video_frame.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/web/WebHeap.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::SaveArg;

namespace content {

ACTION_P(RunClosure, closure) {
  closure.Run();
}

class MediaStreamVideoSourceTest : public ::testing::Test {
 public:
  MediaStreamVideoSourceTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI),
        child_process_(new ChildProcess()),
        number_of_successful_constraints_applied_(0),
        number_of_failed_constraints_applied_(0),
        result_(MEDIA_DEVICE_OK),
        result_name_(""),
        mock_source_(new MockMediaStreamVideoSource(
            media::VideoCaptureFormat(gfx::Size(1280, 720),
                                      1000.0,
                                      media::PIXEL_FORMAT_I420),
            false)) {
    mock_source_->DisableStopForRestart();
    media::VideoCaptureFormats formats;
    formats.push_back(media::VideoCaptureFormat(gfx::Size(1280, 720), 30,
                                                media::PIXEL_FORMAT_I420));
    formats.push_back(media::VideoCaptureFormat(gfx::Size(640, 480), 30,
                                                media::PIXEL_FORMAT_I420));
    formats.push_back(media::VideoCaptureFormat(gfx::Size(352, 288), 30,
                                                media::PIXEL_FORMAT_I420));
    formats.push_back(media::VideoCaptureFormat(gfx::Size(320, 240), 30,
                                                media::PIXEL_FORMAT_I420));
    webkit_source_.Initialize(blink::WebString::FromASCII("dummy_source_id"),
                              blink::WebMediaStreamSource::kTypeVideo,
                              blink::WebString::FromASCII("dummy_source_name"),
                              false /* remote */);
    webkit_source_.SetExtraData(mock_source_);
  }

  void TearDown() override {
    webkit_source_.Reset();
    blink::WebHeap::CollectAllGarbageForTesting();
  }

 protected:
  MediaStreamVideoSource* source() { return mock_source_; }

  // Create a track that's associated with |webkit_source_|.
  blink::WebMediaStreamTrack CreateTrack(const std::string& id) {
    bool enabled = true;
    return MediaStreamVideoTrack::CreateVideoTrack(
        mock_source_,
        base::Bind(&MediaStreamVideoSourceTest::OnConstraintsApplied,
                   base::Unretained(this)),
        enabled);
  }

  blink::WebMediaStreamTrack CreateTrack(
      const std::string& id,
      const VideoTrackAdapterSettings& adapter_settings,
      const base::Optional<bool>& noise_reduction,
      bool is_screencast,
      double min_frame_rate) {
    bool enabled = true;
    return MediaStreamVideoTrack::CreateVideoTrack(
        mock_source_, adapter_settings, noise_reduction, is_screencast,
        min_frame_rate,
        base::Bind(&MediaStreamVideoSourceTest::OnConstraintsApplied,
                   base::Unretained(this)),
        enabled);
  }

  blink::WebMediaStreamTrack CreateTrackAndStartSource(
      int width,
      int height,
      double frame_rate,
      bool detect_rotation = false) {
    blink::WebMediaStreamTrack track = CreateTrack(
        "123",
        VideoTrackAdapterSettings(width, height, 0.0, HUGE_VAL, frame_rate),
        base::Optional<bool>(), false, 0.0);

    EXPECT_EQ(0, NumberOfSuccessConstraintsCallbacks());
    mock_source_->StartMockedSource();
    // Once the source has started successfully we expect that the
    // ConstraintsCallback in MediaStreamSource::AddTrack completes.
    EXPECT_EQ(1, NumberOfSuccessConstraintsCallbacks());
    return track;
  }

  int NumberOfSuccessConstraintsCallbacks() const {
    return number_of_successful_constraints_applied_;
  }

  int NumberOfFailedConstraintsCallbacks() const {
    return number_of_failed_constraints_applied_;
  }

  content::MediaStreamRequestResult error_type() const { return result_; }
  blink::WebString error_name() const { return result_name_; }

  MockMediaStreamVideoSource* mock_source() { return mock_source_; }

  const blink::WebMediaStreamSource& webkit_source() { return webkit_source_; }

  void TestSourceCropFrame(int capture_width,
                           int capture_height,
                           int expected_width,
                           int expected_height) {
    // Configure the track to crop to the expected resolution.
    blink::WebMediaStreamTrack track =
        CreateTrackAndStartSource(expected_width, expected_height, 30.0);

    // Produce frames at the capture resolution.
    MockMediaStreamVideoSink sink;
    sink.ConnectToTrack(track);
    DeliverVideoFrameAndWaitForRenderer(capture_width, capture_height, &sink);
    EXPECT_EQ(1, sink.number_of_frames());

    // Expect the delivered frame to be cropped.
    EXPECT_EQ(expected_height, sink.frame_size().height());
    EXPECT_EQ(expected_width, sink.frame_size().width());
    sink.DisconnectFromTrack();
  }

  void DeliverVideoFrameAndWaitForRenderer(int width,
                                           int height,
                                           MockMediaStreamVideoSink* sink) {
    base::RunLoop run_loop;
    base::Closure quit_closure = run_loop.QuitClosure();
    EXPECT_CALL(*sink, OnVideoFrame()).WillOnce(RunClosure(quit_closure));
    scoped_refptr<media::VideoFrame> frame =
        media::VideoFrame::CreateBlackFrame(gfx::Size(width, height));
    mock_source()->DeliverVideoFrame(frame);
    run_loop.Run();
  }

  void DeliverRotatedVideoFrameAndWaitForRenderer(
      int width,
      int height,
      MockMediaStreamVideoSink* sink) {
    DeliverVideoFrameAndWaitForRenderer(height, width, sink);
  }

  void DeliverVideoFrameAndWaitForTwoRenderers(
      int width,
      int height,
      MockMediaStreamVideoSink* sink1,
      MockMediaStreamVideoSink* sink2) {
    base::RunLoop run_loop;
    base::Closure quit_closure = run_loop.QuitClosure();
    EXPECT_CALL(*sink1, OnVideoFrame());
    EXPECT_CALL(*sink2, OnVideoFrame()).WillOnce(RunClosure(quit_closure));
    scoped_refptr<media::VideoFrame> frame =
        media::VideoFrame::CreateBlackFrame(gfx::Size(width, height));
    mock_source()->DeliverVideoFrame(frame);
    run_loop.Run();
  }

  void TestTwoTracksWithDifferentSettings(int capture_width,
                                          int capture_height,
                                          int expected_width1,
                                          int expected_height1,
                                          int expected_width2,
                                          int expected_height2) {
    blink::WebMediaStreamTrack track1 =
        CreateTrackAndStartSource(expected_width1, expected_height1,
                                  MediaStreamVideoSource::kDefaultFrameRate);

    blink::WebMediaStreamTrack track2 =
        CreateTrack("dummy",
                    VideoTrackAdapterSettings(
                        expected_width2, expected_height2, 0.0, HUGE_VAL,
                        MediaStreamVideoSource::kDefaultFrameRate),
                    base::Optional<bool>(), false, 0.0);

    MockMediaStreamVideoSink sink1;
    sink1.ConnectToTrack(track1);
    EXPECT_EQ(0, sink1.number_of_frames());

    MockMediaStreamVideoSink sink2;
    sink2.ConnectToTrack(track2);
    EXPECT_EQ(0, sink2.number_of_frames());

    DeliverVideoFrameAndWaitForTwoRenderers(capture_width, capture_height,
                                            &sink1, &sink2);

    EXPECT_EQ(1, sink1.number_of_frames());
    EXPECT_EQ(expected_width1, sink1.frame_size().width());
    EXPECT_EQ(expected_height1, sink1.frame_size().height());

    EXPECT_EQ(1, sink2.number_of_frames());
    EXPECT_EQ(expected_width2, sink2.frame_size().width());
    EXPECT_EQ(expected_height2, sink2.frame_size().height());

    sink1.DisconnectFromTrack();
    sink2.DisconnectFromTrack();
  }

  void ReleaseTrackAndSourceOnAddTrackCallback(
      const blink::WebMediaStreamTrack& track_to_release) {
    track_to_release_ = track_to_release;
  }

 private:
  void OnConstraintsApplied(MediaStreamSource* source,
                            MediaStreamRequestResult result,
                            const blink::WebString& result_name) {
    ASSERT_EQ(source, webkit_source().GetExtraData());

    if (result == MEDIA_DEVICE_OK) {
      ++number_of_successful_constraints_applied_;
    } else {
      result_ = result;
      result_name_ = result_name;
      ++number_of_failed_constraints_applied_;
    }

    if (!track_to_release_.IsNull()) {
      mock_source_ = nullptr;
      webkit_source_.Reset();
      track_to_release_.Reset();
    }
  }
  const base::test::ScopedTaskEnvironment scoped_task_environment_;
  const std::unique_ptr<ChildProcess> child_process_;
  blink::WebMediaStreamTrack track_to_release_;
  int number_of_successful_constraints_applied_;
  int number_of_failed_constraints_applied_;
  content::MediaStreamRequestResult result_;
  blink::WebString result_name_;
  blink::WebMediaStreamSource webkit_source_;
  // |mock_source_| is owned by |webkit_source_|.
  MockMediaStreamVideoSource* mock_source_;
};

TEST_F(MediaStreamVideoSourceTest, AddTrackAndStartSource) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  mock_source()->StartMockedSource();
  EXPECT_EQ(1, NumberOfSuccessConstraintsCallbacks());
}

TEST_F(MediaStreamVideoSourceTest, AddTwoTracksBeforeSourceStarts) {
  blink::WebMediaStreamTrack track1 = CreateTrack("123");
  blink::WebMediaStreamTrack track2 = CreateTrack("123");
  EXPECT_EQ(0, NumberOfSuccessConstraintsCallbacks());
  mock_source()->StartMockedSource();
  EXPECT_EQ(2, NumberOfSuccessConstraintsCallbacks());
}

TEST_F(MediaStreamVideoSourceTest, AddTrackAfterSourceStarts) {
  blink::WebMediaStreamTrack track1 = CreateTrack("123");
  mock_source()->StartMockedSource();
  EXPECT_EQ(1, NumberOfSuccessConstraintsCallbacks());
  blink::WebMediaStreamTrack track2 = CreateTrack("123");
  EXPECT_EQ(2, NumberOfSuccessConstraintsCallbacks());
}

TEST_F(MediaStreamVideoSourceTest, AddTrackAndFailToStartSource) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  mock_source()->FailToStartMockedSource();
  EXPECT_EQ(1, NumberOfFailedConstraintsCallbacks());
}

TEST_F(MediaStreamVideoSourceTest, MandatoryAspectRatio4To3) {
  TestSourceCropFrame(1280, 720, 960, 720);
}

TEST_F(MediaStreamVideoSourceTest, ReleaseTrackAndSourceOnSuccessCallBack) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  ReleaseTrackAndSourceOnAddTrackCallback(track);
  mock_source()->StartMockedSource();
  EXPECT_EQ(1, NumberOfSuccessConstraintsCallbacks());
}

TEST_F(MediaStreamVideoSourceTest, TwoTracksWithVGAAndWVGA) {
  TestTwoTracksWithDifferentSettings(640, 480, 640, 480, 640, 360);
}

TEST_F(MediaStreamVideoSourceTest, TwoTracksWith720AndWVGA) {
  TestTwoTracksWithDifferentSettings(1280, 720, 1280, 720, 640, 360);
}

TEST_F(MediaStreamVideoSourceTest, SourceChangeFrameSize) {
  // Expect the source to start capture with the supported resolution.
  // Disable frame-rate adjustment in spec-compliant mode to ensure no frames
  // are dropped.
  blink::WebMediaStreamTrack track = CreateTrackAndStartSource(800, 700, 0.0);

  MockMediaStreamVideoSink sink;
  sink.ConnectToTrack(track);
  EXPECT_EQ(0, sink.number_of_frames());
  DeliverVideoFrameAndWaitForRenderer(320, 240, &sink);
  EXPECT_EQ(1, sink.number_of_frames());
  // Expect the delivered frame to be passed unchanged since its smaller than
  // max requested.
  EXPECT_EQ(320, sink.frame_size().width());
  EXPECT_EQ(240, sink.frame_size().height());

  DeliverVideoFrameAndWaitForRenderer(640, 480, &sink);
  EXPECT_EQ(2, sink.number_of_frames());
  // Expect the delivered frame to be passed unchanged since its smaller than
  // max requested.
  EXPECT_EQ(640, sink.frame_size().width());
  EXPECT_EQ(480, sink.frame_size().height());

  DeliverVideoFrameAndWaitForRenderer(1280, 720, &sink);
  EXPECT_EQ(3, sink.number_of_frames());
  // Expect a frame to be cropped since its larger than max requested.
  EXPECT_EQ(800, sink.frame_size().width());
  EXPECT_EQ(700, sink.frame_size().height());

  sink.DisconnectFromTrack();
}

TEST_F(MediaStreamVideoSourceTest, RotatedSourceDetectionDisabled) {
  source()->SetDeviceRotationDetection(false /* enabled */);

  // Expect the source to start capture with the supported resolution.
  // Disable frame-rate adjustment in spec-compliant mode to ensure no frames
  // are dropped.
  blink::WebMediaStreamTrack track =
      CreateTrackAndStartSource(1280, 720, 0.0, true);

  MockMediaStreamVideoSink sink;
  sink.ConnectToTrack(track);
  EXPECT_EQ(0, sink.number_of_frames());
  DeliverVideoFrameAndWaitForRenderer(1280, 720, &sink);
  EXPECT_EQ(1, sink.number_of_frames());
  // Expect the delivered frame to be passed unchanged since it is the same size
  // as the source native format.
  EXPECT_EQ(1280, sink.frame_size().width());
  EXPECT_EQ(720, sink.frame_size().height());

  DeliverRotatedVideoFrameAndWaitForRenderer(1280, 720, &sink);
  EXPECT_EQ(2, sink.number_of_frames());
  // Expect the delivered frame to be cropped because the rotation is not
  // detected.
  EXPECT_EQ(720, sink.frame_size().width());
  EXPECT_EQ(720, sink.frame_size().height());

  sink.DisconnectFromTrack();
}

TEST_F(MediaStreamVideoSourceTest, RotatedSourceDetectionEnabled) {
  source()->SetDeviceRotationDetection(true /* enabled */);

  // Expect the source to start capture with the supported resolution.
  // Disable frame-rate adjustment in spec-compliant mode to ensure no frames
  // are dropped.
  blink::WebMediaStreamTrack track =
      CreateTrackAndStartSource(1280, 720, 0.0, true);

  MockMediaStreamVideoSink sink;
  sink.ConnectToTrack(track);
  EXPECT_EQ(0, sink.number_of_frames());
  DeliverVideoFrameAndWaitForRenderer(1280, 720, &sink);
  EXPECT_EQ(1, sink.number_of_frames());
  // Expect the delivered frame to be passed unchanged since it is the same size
  // as the source native format.
  EXPECT_EQ(1280, sink.frame_size().width());
  EXPECT_EQ(720, sink.frame_size().height());

  DeliverRotatedVideoFrameAndWaitForRenderer(1280, 720, &sink);
  EXPECT_EQ(2, sink.number_of_frames());
  // Expect the delivered frame to be passed unchanged since it is detected as
  // a valid frame on a rotated device.
  EXPECT_EQ(720, sink.frame_size().width());
  EXPECT_EQ(1280, sink.frame_size().height());

  sink.DisconnectFromTrack();
}

// Test that a source producing no frames change the source ReadyState to muted.
// that in a reasonable time frame the muted state turns to false.
TEST_F(MediaStreamVideoSourceTest, MutedSource) {
  // Setup the source for support a frame rate of 999 fps in order to test
  // the muted event faster. This is since the frame monitoring uses
  // PostDelayedTask that is dependent on the source frame rate.
  // Note that media::limits::kMaxFramesPerSecond is 1000.
  blink::WebMediaStreamTrack track = CreateTrackAndStartSource(
      640, 480, media::limits::kMaxFramesPerSecond - 2);
  MockMediaStreamVideoSink sink;
  sink.ConnectToTrack(track);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  base::RunLoop run_loop;
  base::Closure quit_closure = run_loop.QuitClosure();
  bool muted_state = false;
  EXPECT_CALL(*mock_source(), DoSetMutedState(_))
      .WillOnce(DoAll(SaveArg<0>(&muted_state), RunClosure(quit_closure)));
  run_loop.Run();
  EXPECT_EQ(muted_state, true);

  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateMuted);

  base::RunLoop run_loop2;
  base::Closure quit_closure2 = run_loop2.QuitClosure();
  EXPECT_CALL(*mock_source(), DoSetMutedState(_))
      .WillOnce(DoAll(SaveArg<0>(&muted_state), RunClosure(quit_closure2)));
  DeliverVideoFrameAndWaitForRenderer(640, 480, &sink);
  run_loop2.Run();

  EXPECT_EQ(muted_state, false);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  sink.DisconnectFromTrack();
}

TEST_F(MediaStreamVideoSourceTest, ReconfigureTrack) {
  blink::WebMediaStreamTrack track = CreateTrackAndStartSource(
      640, 480, media::limits::kMaxFramesPerSecond - 2);
  MockMediaStreamVideoSink sink;
  sink.ConnectToTrack(track);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  MediaStreamVideoTrack* native_track =
      MediaStreamVideoTrack::GetVideoTrack(track);
  blink::WebMediaStreamTrack::Settings settings;
  native_track->GetSettings(settings);
  EXPECT_EQ(settings.width, 640);
  EXPECT_EQ(settings.height, 480);
  EXPECT_EQ(settings.frame_rate, media::limits::kMaxFramesPerSecond - 2);
  EXPECT_EQ(settings.aspect_ratio, 640.0 / 480.0);

  source()->ReconfigureTrack(
      native_track, VideoTrackAdapterSettings(630, 470, 0, HUGE_VAL, 30.0));
  native_track->GetSettings(settings);
  EXPECT_EQ(settings.width, 630);
  EXPECT_EQ(settings.height, 470);
  EXPECT_EQ(settings.frame_rate, 30.0);
  EXPECT_EQ(settings.aspect_ratio, 630.0 / 470.0);

  // Produce a frame in the source native format and expect the delivered frame
  // to have the new track format.
  DeliverVideoFrameAndWaitForRenderer(640, 480, &sink);
  EXPECT_EQ(1, sink.number_of_frames());
  EXPECT_EQ(630, sink.frame_size().width());
  EXPECT_EQ(470, sink.frame_size().height());
}

TEST_F(MediaStreamVideoSourceTest, ReconfigureStoppedTrack) {
  blink::WebMediaStreamTrack track = CreateTrackAndStartSource(
      640, 480, media::limits::kMaxFramesPerSecond - 2);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  MediaStreamVideoTrack* native_track =
      MediaStreamVideoTrack::GetVideoTrack(track);
  blink::WebMediaStreamTrack::Settings settings;
  native_track->GetSettings(settings);
  EXPECT_EQ(settings.width, 640);
  EXPECT_EQ(settings.height, 480);
  EXPECT_EQ(settings.frame_rate, media::limits::kMaxFramesPerSecond - 2);
  EXPECT_EQ(settings.aspect_ratio, 640.0 / 480.0);

  // Reconfiguring a stopped track should have no effect since it is no longer
  // associated with the source.
  native_track->Stop();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateEnded);

  source()->ReconfigureTrack(
      native_track, VideoTrackAdapterSettings(630, 470, 0, HUGE_VAL, 30.0));
  blink::WebMediaStreamTrack::Settings stopped_settings;
  native_track->GetSettings(stopped_settings);
  EXPECT_EQ(stopped_settings.width, -1);
  EXPECT_EQ(stopped_settings.height, -1);
  EXPECT_EQ(stopped_settings.frame_rate, -1);
  EXPECT_EQ(stopped_settings.aspect_ratio, -1);
}

// Test that restart fails on a source without restart support.
TEST_F(MediaStreamVideoSourceTest, FailedRestart) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  mock_source()->StartMockedSource();
  EXPECT_EQ(1, NumberOfSuccessConstraintsCallbacks());
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  // The source does not support Restart/StopForRestart.
  mock_source()->StopForRestart(
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_RUNNING);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  // Verify that Restart() fails with INVALID_STATE when not called after a
  // successful StopForRestart().
  mock_source()->Restart(
      media::VideoCaptureFormat(),
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::INVALID_STATE);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->StopSource();
  // Verify that StopForRestart() fails with INVALID_STATE when called when the
  // source is not running.
  mock_source()->StopForRestart(
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::INVALID_STATE);
      }));
}

// Test that restart succeeds on a source with restart support.
TEST_F(MediaStreamVideoSourceTest, SuccessfulRestart) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  mock_source()->EnableStopForRestart();
  mock_source()->EnableRestart();
  mock_source()->StartMockedSource();
  EXPECT_EQ(NumberOfSuccessConstraintsCallbacks(), 1);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->StopForRestart(
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_STOPPED);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  // Verify that StopForRestart() fails with INVALID_STATE called after the
  // source is already stopped.
  mock_source()->StopForRestart(
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::INVALID_STATE);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->Restart(
      media::VideoCaptureFormat(),
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_RUNNING);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  // Verify that Restart() fails with INVALID_STATE if the source has already
  // started.
  mock_source()->Restart(
      media::VideoCaptureFormat(),
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::INVALID_STATE);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->StopSource();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateEnded);
}

// Test that restart fails on a source without restart support.
TEST_F(MediaStreamVideoSourceTest, FailedRestartAfterStopForRestart) {
  blink::WebMediaStreamTrack track = CreateTrack("123");
  mock_source()->EnableStopForRestart();
  mock_source()->DisableRestart();
  mock_source()->StartMockedSource();
  EXPECT_EQ(NumberOfSuccessConstraintsCallbacks(), 1);
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->StopForRestart(
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_STOPPED);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->Restart(
      media::VideoCaptureFormat(),
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_STOPPED);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  // Another failed attempt to verify that the source remains in the correct
  // state.
  mock_source()->Restart(
      media::VideoCaptureFormat(),
      base::BindOnce([](MediaStreamVideoSource::RestartResult result) {
        EXPECT_EQ(result, MediaStreamVideoSource::RestartResult::IS_STOPPED);
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateLive);

  mock_source()->StopSource();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(track.Source().GetReadyState(),
            blink::WebMediaStreamSource::kReadyStateEnded);
}

}  // namespace content
