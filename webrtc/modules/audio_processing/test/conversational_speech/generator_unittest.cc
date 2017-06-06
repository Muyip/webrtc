/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file consists of unit tests for webrtc::test::conversational_speech
// members. Part of them focus on accepting or rejecting different
// conversational speech setups. A setup is defined by a set of audio tracks and
// timing information).
// The docstring at the beginning of each TEST_F(ConversationalSpeechTest,
// MultiEndCallSetup*) function looks like the drawing below and indicates which
// setup is tested.
//
//    Accept:
//    A 0****.....
//    B .....1****
//
// The drawing indicates the following:
// - the illustrated setup should be accepted,
// - there are two speakers (namely, A and B),
// - A is the first speaking, B is the second one,
// - each character after the speaker's letter indicates a time unit (e.g., 100
//   ms),
// - "*" indicates speaking, "." listening,
// - numbers indicate the turn index in std::vector<Turn>.
//
// Note that the same speaker can appear in multiple lines in order to depict
// cases in which there are wrong offsets leading to self cross-talk (which is
// rejected).

// MSVC++ requires this to be set before any other includes to get M_PI.
#define _USE_MATH_DEFINES

#include <stdio.h>
#include <cmath>
#include <map>
#include <memory>

#include "webrtc/base/logging.h"
#include "webrtc/base/pathutils.h"
#include "webrtc/common_audio/wav_file.h"
#include "webrtc/modules/audio_processing/test/conversational_speech/config.h"
#include "webrtc/modules/audio_processing/test/conversational_speech/mock_wavreader_factory.h"
#include "webrtc/modules/audio_processing/test/conversational_speech/multiend_call.h"
#include "webrtc/modules/audio_processing/test/conversational_speech/timing.h"
#include "webrtc/modules/audio_processing/test/conversational_speech/wavreader_factory.h"
#include "webrtc/test/gmock.h"
#include "webrtc/test/gtest.h"
#include "webrtc/test/testsupport/fileutils.h"

namespace webrtc {
namespace test {
namespace {

using conversational_speech::LoadTiming;
using conversational_speech::SaveTiming;
using conversational_speech::MockWavReaderFactory;
using conversational_speech::MultiEndCall;
using conversational_speech::Turn;
using conversational_speech::WavReaderFactory;

const char* const audiotracks_path = "/path/to/audiotracks";
const char* const timing_filepath = "/path/to/timing_file.txt";
const char* const output_path = "/path/to/output_dir";

const std::vector<Turn> expected_timing = {
    {"A", "a1", 0},
    {"B", "b1", 0},
    {"A", "a2", 100},
    {"B", "b2", -200},
    {"A", "a3", 0},
    {"A", "a3", 0},
};
const std::size_t kNumberOfTurns = expected_timing.size();

// Default arguments for MockWavReaderFactory ctor.
// Fake audio track parameters.
constexpr int kDefaultSampleRate = 48000;
const std::map<std::string, const MockWavReaderFactory::Params>
    kDefaultMockWavReaderFactoryParamsMap = {
  {"t300", {kDefaultSampleRate, 1u, 14400u}},  // 0.3 seconds.
  {"t500", {kDefaultSampleRate, 1u, 24000u}},  // 0.5 seconds.
  {"t1000", {kDefaultSampleRate, 1u, 48000u}},  // 1.0 seconds.
};
const MockWavReaderFactory::Params& kDefaultMockWavReaderFactoryParams =
    kDefaultMockWavReaderFactoryParamsMap.at("t500");

std::unique_ptr<MockWavReaderFactory> CreateMockWavReaderFactory() {
  return std::unique_ptr<MockWavReaderFactory>(
      new MockWavReaderFactory(kDefaultMockWavReaderFactoryParams,
                               kDefaultMockWavReaderFactoryParamsMap));
}

void CreateSineWavFile(const std::string& filepath,
                       const MockWavReaderFactory::Params& params,
                       float frequency = 440.0f) {
  // Create samples.
  constexpr double two_pi = 2.0 * M_PI;
  std::vector<int16_t> samples(params.num_samples);
  for (std::size_t i = 0; i < params.num_samples; ++i) {
    // TODO(alessiob): the produced tone is not pure, improve.
    samples[i] = std::lround(32767.0f * std::sin(
        two_pi * i * frequency / params.sample_rate));
  }

  // Write samples.
  WavWriter wav_writer(filepath, params.sample_rate, params.num_channels);
  wav_writer.WriteSamples(samples.data(), params.num_samples);
}

}  // namespace

using testing::_;

// TODO(alessiob): Remove fixture once conversational_speech fully implemented
// and replace TEST_F with TEST.
class ConversationalSpeechTest : public testing::Test {
 public:
  ConversationalSpeechTest() {
    rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
  }
};

TEST_F(ConversationalSpeechTest, Settings) {
  const conversational_speech::Config config(
      audiotracks_path, timing_filepath, output_path);

  // Test getters.
  EXPECT_EQ(audiotracks_path, config.audiotracks_path());
  EXPECT_EQ(timing_filepath, config.timing_filepath());
  EXPECT_EQ(output_path, config.output_path());
}

TEST_F(ConversationalSpeechTest, TimingSaveLoad) {
  // Save test timing.
  const std::string temporary_filepath = webrtc::test::TempFilename(
      webrtc::test::OutputPath(), "TempTimingTestFile");
  SaveTiming(temporary_filepath, expected_timing);

  // Create a std::vector<Turn> instance by loading from file.
  std::vector<Turn> actual_timing = LoadTiming(temporary_filepath);
  std::remove(temporary_filepath.c_str());

  // Check size.
  EXPECT_EQ(expected_timing.size(), actual_timing.size());

  // Check Turn instances.
  for (size_t index = 0; index < expected_timing.size(); ++index) {
    EXPECT_EQ(expected_timing[index], actual_timing[index])
        << "turn #" << index << " not matching";
  }
}

TEST_F(ConversationalSpeechTest, MultiEndCallCreate) {
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are 5 unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(5);

  // Inject the mock wav reader factory.
  conversational_speech::MultiEndCall multiend_call(
      expected_timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(5u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(6u, multiend_call.speaking_turns().size());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupFirstOffsetNegative) {
  const std::vector<Turn> timing = {
      {"A", "t500", -100},
      {"B", "t500", 0},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupSimple) {
  // Accept:
  // A 0****.....
  // B .....1****
  constexpr std::size_t expected_duration = kDefaultSampleRate;
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", 0},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(1u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(2u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupPause) {
  // Accept:
  // A 0****.......
  // B .......1****
  constexpr std::size_t expected_duration = kDefaultSampleRate * 1.2;
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", 200},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(1u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(2u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalk) {
  // Accept:
  // A 0****....
  // B ....1****
  constexpr std::size_t expected_duration = kDefaultSampleRate * 0.9;
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", -100},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(1u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(2u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupInvalidOrder) {
  // Reject:
  // A ..0****
  // B .1****.  The n-th turn cannot start before the (n-1)-th one.
  const std::vector<Turn> timing = {
      {"A", "t500", 200},
      {"B", "t500", -600},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalkThree) {
  // Accept:
  // A 0****2****...
  // B ...1*********
  constexpr std::size_t expected_duration = kDefaultSampleRate * 1.3;
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t1000", -200},
      {"A", "t500", -800},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(2u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(3u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupSelfCrossTalkNearInvalid) {
  // Reject:
  // A 0****......
  // A ...1****...
  // B ......2****
  //      ^  Turn #1 overlaps with #0 which is from the same speaker.
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"A", "t500", -200},
      {"B", "t500", -200},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupSelfCrossTalkFarInvalid) {
  // Reject:
  // A 0*********
  // B 1**.......
  // C ...2**....
  // A ......3**.
  //         ^  Turn #3 overlaps with #0 which is from the same speaker.
  const std::vector<Turn> timing = {
      {"A", "t1000", 0},
      {"B", "t300", -1000},
      {"C", "t300", 0},
      {"A", "t300", 0},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalkMiddleValid) {
  // Accept:
  // A 0*********..
  // B ..1****.....
  // C .......2****
  constexpr std::size_t expected_duration = kDefaultSampleRate * 1.2;
  const std::vector<Turn> timing = {
      {"A", "t1000", 0},
      {"B", "t500", -800},
      {"C", "t500", 0},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(3u, multiend_call.speaker_names().size());
  EXPECT_EQ(2u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(3u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalkMiddleInvalid) {
  // Reject:
  // A 0*********
  // B ..1****...
  // C ....2****.
  //       ^  Turn #2 overlaps both with #0 and #1 (cross-talk with 3+ speakers
  //          not permitted).
  const std::vector<Turn> timing = {
      {"A", "t1000", 0},
      {"B", "t500", -800},
      {"C", "t500", -300},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalkMiddleAndPause) {
  // Accept:
  // A 0*********..
  // B .2****......
  // C .......3****
  constexpr std::size_t expected_duration = kDefaultSampleRate * 1.2;
  const std::vector<Turn> timing = {
      {"A", "t1000", 0},
      {"B", "t500", -900},
      {"C", "t500", 100},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(3u, multiend_call.speaker_names().size());
  EXPECT_EQ(2u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(3u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupCrossTalkFullOverlapValid) {
  // Accept:
  // A 0****
  // B 1****
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", -500},
  };
  auto mock_wavreader_factory = CreateMockWavReaderFactory();

  // There is one unique audio track to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(1);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(2u, multiend_call.speaker_names().size());
  EXPECT_EQ(1u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(2u, multiend_call.speaking_turns().size());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupLongSequence) {
  // Accept:
  // A 0****....3****.5**.
  // B .....1****...4**...
  // C ......2**.......6**..
  constexpr std::size_t expected_duration = kDefaultSampleRate * 1.9;
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", 0},
      {"C", "t300", -400},
      {"A", "t500", 0},
      {"B", "t300", -100},
      {"A", "t300", -100},
      {"C", "t300", -200},
  };
  auto mock_wavreader_factory = std::unique_ptr<MockWavReaderFactory>(
      new MockWavReaderFactory(kDefaultMockWavReaderFactoryParams,
                               kDefaultMockWavReaderFactoryParamsMap));

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_TRUE(multiend_call.valid());

  // Test.
  EXPECT_EQ(3u, multiend_call.speaker_names().size());
  EXPECT_EQ(2u, multiend_call.audiotrack_readers().size());
  EXPECT_EQ(7u, multiend_call.speaking_turns().size());
  EXPECT_EQ(expected_duration, multiend_call.total_duration_samples());
}

TEST_F(ConversationalSpeechTest, MultiEndCallSetupLongSequenceInvalid) {
  // Reject:
  // A 0****....3****.6**
  // B .....1****...4**..
  // C ......2**.....5**..
  //                 ^ Turns #4, #5 and #6 overlapping (cross-talk with 3+
  //                   speakers not permitted).
  const std::vector<Turn> timing = {
      {"A", "t500", 0},
      {"B", "t500", 0},
      {"C", "t300", -400},
      {"A", "t500", 0},
      {"B", "t300", -100},
      {"A", "t300", -200},
      {"C", "t300", -200},
  };
  auto mock_wavreader_factory = std::unique_ptr<MockWavReaderFactory>(
      new MockWavReaderFactory(kDefaultMockWavReaderFactoryParams,
                               kDefaultMockWavReaderFactoryParamsMap));

  // There are two unique audio tracks to read.
  EXPECT_CALL(*mock_wavreader_factory, Create(_)).Times(2);

  conversational_speech::MultiEndCall multiend_call(
      timing, audiotracks_path, std::move(mock_wavreader_factory));
  EXPECT_FALSE(multiend_call.valid());
}

TEST_F(ConversationalSpeechTest, MultiEndCallWavReaderAdaptorSine) {
  // Parameters with which wav files are created.
  constexpr int duration_seconds = 5;
  const int sample_rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};

  for (int sample_rate : sample_rates) {
    const rtc::Pathname temp_filename(
        OutputPath(), "TempSineWavFile_" + std::to_string(sample_rate)
            + ".wav");

    // Write wav file.
    const std::size_t num_samples = duration_seconds * sample_rate;
    MockWavReaderFactory::Params params = {sample_rate, 1u, num_samples};
    CreateSineWavFile(temp_filename.pathname(), params);
    LOG(LS_VERBOSE) << "wav file @" << sample_rate << " Hz created ("
        << num_samples << " samples)";

    // Load wav file and check if params match.
    WavReaderFactory wav_reader_factory;
    auto wav_reader = wav_reader_factory.Create(temp_filename.pathname());
    EXPECT_EQ(sample_rate, wav_reader->SampleRate());
    EXPECT_EQ(1u, wav_reader->NumChannels());
    EXPECT_EQ(num_samples, wav_reader->NumSamples());

    // Clean up.
    remove(temp_filename.pathname().c_str());
  }
}

}  // namespace test
}  // namespace webrtc
