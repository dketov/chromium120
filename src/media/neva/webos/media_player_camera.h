// Copyright 2019-2020 LG Electronics, Inc.
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

#ifndef MEDIA_NEVA_WEBOS_MEDIA_PLAYER_CAMERA_H_
#define MEDIA_NEVA_WEBOS_MEDIA_PLAYER_CAMERA_H_

#include <cstdint>
#include <memory>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "media/neva/media_player_neva_interface.h"
#include "media/neva/webos/webos_mediaclient.h"

class GURL;

namespace media {

class MediaPlayerCamera : public base::SupportsWeakPtr<MediaPlayerCamera>,
                          public WebOSMediaClient::EventListener,
                          public media::MediaPlayerNeva {
 public:
  explicit MediaPlayerCamera(
      MediaPlayerNevaClient*,
      const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
      const std::string& app_id);
  MediaPlayerCamera(const MediaPlayerCamera&) = delete;
  MediaPlayerCamera& operator=(const MediaPlayerCamera&) = delete;

  ~MediaPlayerCamera() override;

  // media::MediaPlayerNeva implementation
  void Initialize(const bool is_video,
                  const double current_time,
                  const std::string& url,
                  const std::string& mime,
                  const std::string& referrer,
                  const std::string& user_agent,
                  const std::string& cookies,
                  const std::string& media_option,
                  const std::string& custom_option) override;

  // Starts the player.
  void Start() override;
  // Pauses the player.
  void Pause() override;
  // Performs seek on the player.
  void Seek(const base::TimeDelta& time) override {}
  // Sets the player volume.
  void SetVolume(double volume) override {}
  // Sets the poster image.
  void SetPoster(const GURL& poster) override {}

  void SetRate(double rate) override {}
  void SetPreload(Preload preload) override {}
  bool IsPreloadable(const std::string& content_media_option) override;
  bool HasVideo() override;
  bool HasAudio() override;
  bool SelectTrack(const MediaTrackType type, const std::string& id) override;
  bool UsesIntrinsicSize() const override;
  std::string MediaId() const override;
  void Suspend(SuspendReason reason) override {}
  void Resume() override {}
  bool IsRecoverableOnResume() const override { return true; }
  bool RequireMediaResource() const override;
  void SetDisableAudio(bool) override {}
  void SetMediaLayerId(const std::string& media_layer_id) override;

  // Implement WebOSMediaClient::EventListener
  void OnPlaybackStateChanged(bool playing) override;
  void OnPlaybackEnded() override;
  void OnBufferingStatusChanged(
      WebOSMediaClient::BufferingState buffering_state) override;
  void OnError(PipelineStatus error) override {}
  void OnDurationChanged() override {}
  void OnVideoSizeChanged() override;
  void OnAudioTrackAdded(
      const std::vector<MediaTrackInfo>& audio_track_info) override {}
  void OnVideoTrackAdded(const std::string& id,
                         const std::string& kind,
                         const std::string& language,
                         bool enabled) override {}
  void OnUMSInfoUpdated(const std::string& detail) override;
  void OnWaitingForDecryptionKey() override {}
  void OnEncryptedMediaInitData(const std::string& init_data_type,
                                const std::vector<uint8_t>& init_data) override;
  void OnTimeUpdated(base::TimeDelta current_time) override;
  // End of implement WebOSMediaClient::EventListener

 private:
  std::unique_ptr<WebOSMediaClient> umedia_client_;
  MediaPlayerNevaClient* client_;

  std::string app_id_;
  GURL url_;
  std::string mime_type_;
  std::string camera_id_;

  double playback_rate_ = 1.0f;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
};

}  // namespace media

#endif  // MEDIA_NEVA_WEBOS_MEDIA_PLAYER_CAMERA_H_
