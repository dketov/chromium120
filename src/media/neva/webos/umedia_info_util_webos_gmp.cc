// Copyright 2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "media/neva/webos/umedia_info_util_webos_gmp.h"

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"

namespace media {

std::string SourceInfoToJson(const std::string& media_id,
                             const struct ums::source_info_t& value) {
  base::Value::Dict eventInfo;
  base::Value::Dict sourceInfo;
  std::string res;

  eventInfo.Set("type", "sourceInfo");
  eventInfo.Set("mediaId", media_id.c_str());

  sourceInfo.Set("container", value.container.c_str());
  sourceInfo.Set("seekable", value.seekable);
  sourceInfo.Set("numPrograms", static_cast<int>(value.programs.size()));

  base::Value::List programInfos;
  for (int i = 0; i < value.programs.size(); i++) {
    base::Value::Dict programInfo;
    programInfo.Set("duration", static_cast<double>(value.duration));

    int numAudioTracks = 0;
    if (value.programs[i].audio_stream > 0 &&
        value.programs[i].audio_stream < value.audio_streams.size()) {
      numAudioTracks = 1;
    }
    programInfo.Set("numAudioTracks", numAudioTracks);
    base::Value::List audioTrackInfos;
    for (int j = 0; j < numAudioTracks; j++) {
      base::Value::Dict audioTrackInfo;
      int asi = value.programs[i].audio_stream;

      audioTrackInfo.Set("codec",
                                  value.audio_streams[asi].codec.c_str());
      audioTrackInfo.Set("bitRate", static_cast<int>(value.audio_streams[asi].bit_rate));
      audioTrackInfo.Set("sampleRate", static_cast<int>(
                               value.audio_streams[asi].sample_rate));

      audioTrackInfos.Append(std::move(audioTrackInfo));
    }
    if (numAudioTracks)
      programInfo.Set("audioTrackInfo", std::move(audioTrackInfos));

    int numVideoTracks = 0;
    if (value.programs[i].video_stream > 0 &&
        value.programs[i].video_stream < value.video_streams.size()) {
      numVideoTracks = 1;
    }

    base::Value::List videoTrackInfos;
    for (int j = 0; j < numVideoTracks; j++) {
      base::Value::Dict videoTrackInfo;
      int vsi = value.programs[i].video_stream;

      float frame_rate = ((float)value.video_streams[vsi].frame_rate.num) /
                         ((float)value.video_streams[vsi].frame_rate.den);

      videoTrackInfo.Set("codec",
                                  value.video_streams[vsi].codec.c_str());
      videoTrackInfo.Set("width", static_cast<int>(value.video_streams[vsi].width));
      videoTrackInfo.Set("height", static_cast<int>(value.video_streams[vsi].height));
      videoTrackInfo.Set("frameRate", frame_rate);

      videoTrackInfo.Set("bitRate", static_cast<int>(value.video_streams[vsi].bit_rate));

      videoTrackInfos.Append(std::move(videoTrackInfo));
    }
    if (numVideoTracks)
      programInfo.Set("videoTrackInfo", std::move(videoTrackInfos));

    programInfos.Append(std::move(programInfo));
  }
  sourceInfo.Set("programInfo", std::move(programInfos));

  eventInfo.Set("info", std::move(sourceInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Source info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " source_info=" << res;
  return res;
}

// refer to uMediaServer/include/public/dto_type.h
std::string VideoInfoToJson(const std::string& media_id,
                            const struct ums::video_info_t& value) {
  base::Value::Dict eventInfo;
  base::Value::Dict videoInfo;
  base::Value::Dict frameRate;
  std::string res;

  eventInfo.Set("type", "videoInfo");
  eventInfo.Set("mediaId", media_id.c_str());

  videoInfo.Set("width", static_cast<int>(value.width));
  videoInfo.Set("height", static_cast<int>(value.height));
  frameRate.Set("num", value.frame_rate.num);
  frameRate.Set("den", value.frame_rate.den);
  videoInfo.Set("frameRate", std::move(frameRate));
  videoInfo.Set("codec", value.codec.c_str());
  videoInfo.Set("bitRate", static_cast<int>(value.bit_rate));

  eventInfo.Set("info", std::move(videoInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Video info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " video_info=" << res;
  return res;
}

std::string AudioInfoToJson(const std::string& media_id,
                            const struct ums::audio_info_t& value) {
  base::Value::Dict eventInfo;
  base::Value::Dict audioInfo;
  std::string res;

  eventInfo.Set("type", "audioInfo");
  eventInfo.Set("mediaId", media_id.c_str());

  audioInfo.Set("sampleRate", static_cast<int>(value.sample_rate));
  audioInfo.Set("codec", value.codec.c_str());
  audioInfo.Set("bitRate", static_cast<int>(value.bit_rate));

  eventInfo.Set("info", std::move(audioInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Audio info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " audio_info=" << res;
  return res;
}

}  // namespace media
