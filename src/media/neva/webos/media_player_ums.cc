// Copyright 2017 LG Electronics, Inc.
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

#include "media/neva/webos/media_player_ums.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/task/bind_post_task.h"
#include "media/base/cdm_context.h"
#include "media/base/media_switches.h"
#include "media/base/pipeline.h"
#include "media/neva/media_util.h"
#include "neva/logging.h"
#include "ui/gfx/geometry/rect_f.h"

namespace {

const char* WebOSClientBufferingStateToString(
    media::WebOSMediaClient::BufferingState state) {
#define STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(state) \
  case media::WebOSMediaClient::BufferingState::state:   \
    return #state

  switch (state) {
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kHaveMetadata);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kLoadCompleted);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kPreloadCompleted);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kPrerollCompleted);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kWebOSBufferingStart);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kWebOSBufferingEnd);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kWebOSNetworkStateLoading);
    STRINGIFY_WEBOSCLIENT_BUFFERINGSTATE_CASE(kWebOSNetworkStateLoaded);
  }
  return "null";
}

}  // namespace

namespace media {

#define BIND_POST_TASK_TO_RENDER_LOOP(function) \
  base::BindPostTaskToCurrentDefault(base::BindRepeating(function, AsWeakPtr()))

MediaPlayerUMS::MediaPlayerUMS(
    MediaPlayerNevaClient* client,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const std::string& app_id)
    : client_(client), main_task_runner_(main_task_runner), app_id_(app_id) {
  NEVA_VLOGTF(1);
  umedia_client_ =
      WebOSMediaClient::Create(main_task_runner_, AsWeakPtr(), app_id_);
}

MediaPlayerUMS::~MediaPlayerUMS() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
}

void MediaPlayerUMS::Initialize(const bool is_video,
                                const double current_time,
                                const std::string& url,
                                const std::string& mime_type,
                                const std::string& referrer,
                                const std::string& user_agent,
                                const std::string& cookies,
                                const std::string& media_option,
                                const std::string& custom_option) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "app_id: " << app_id_ << " / url: " << url
                  << " / media_option: " << media_option;

  current_time_ = base::Seconds(current_time);
  umedia_client_->Load(is_video, current_time, false, url, mime_type, referrer,
                       user_agent, cookies, media_option);
}

void MediaPlayerUMS::Start() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  umedia_client_->SetPlaybackRate(playback_rate_);
  paused_ = false;
}

void MediaPlayerUMS::Pause() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  umedia_client_->SetPlaybackRate(0.0f);
  paused_ = true;
  paused_time_ = current_time_;
}

void MediaPlayerUMS::Seek(const base::TimeDelta& time) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  umedia_client_->Seek(
      time, BIND_POST_TASK_TO_RENDER_LOOP(&MediaPlayerUMS::OnSeekDone));
}

void MediaPlayerUMS::SetVolume(double volume) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  umedia_client_->SetPlaybackVolume(volume);
}

void MediaPlayerUMS::SetPoster(const GURL& poster) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
}

void MediaPlayerUMS::SetRate(double rate) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (!umedia_client_->IsSupportedBackwardTrickPlay() && rate < 0.0)
    return;

  playback_rate_ = rate;
  if (!paused_)
    umedia_client_->SetPlaybackRate(playback_rate_);
}

void MediaPlayerUMS::SetPreload(MediaPlayerNeva::Preload preload) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(1);
  switch (preload) {
    case MediaPlayerNeva::PreloadNone:
      umedia_client_->SetPreload(WebOSMediaClient::Preload::kPreloadNone);
      return;
    case MediaPlayerNeva::PreloadMetaData:
      umedia_client_->SetPreload(WebOSMediaClient::Preload::kPreloadMetaData);
      return;
    case MediaPlayerNeva::PreloadAuto:
      umedia_client_->SetPreload(WebOSMediaClient::Preload::kPreloadAuto);
      return;
  }
  NOTREACHED();
}

bool MediaPlayerUMS::IsPreloadable(const std::string& content_media_option) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(1);
  return umedia_client_->IsPreloadable(content_media_option);
}

bool MediaPlayerUMS::HasVideo() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(2);
  return umedia_client_->HasVideo();
}

bool MediaPlayerUMS::HasAudio() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(2);
  return umedia_client_->HasAudio();
}

bool MediaPlayerUMS::SelectTrack(const MediaTrackType type,
                                 const std::string& id) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(1);
  return umedia_client_->SelectTrack(type, id);
}

bool MediaPlayerUMS::UsesIntrinsicSize() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->UsesIntrinsicSize();
}

std::string MediaPlayerUMS::MediaId() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  return umedia_client_->MediaId();
}

void MediaPlayerUMS::Suspend(SuspendReason reason) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_VLOGTF(1);
  if (is_suspended_)
    return;

  is_suspended_ = true;
  umedia_client_->Suspend(reason);
}

void MediaPlayerUMS::Resume() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (!is_suspended_)
    return;

  is_suspended_ = false;
  umedia_client_->Resume();
}

bool MediaPlayerUMS::RequireMediaResource() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

bool MediaPlayerUMS::IsRecoverableOnResume() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return umedia_client_->IsRecoverableOnResume();
}

void MediaPlayerUMS::SetDisableAudio(bool disable) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  umedia_client_->SetDisableAudio(disable);
}

void MediaPlayerUMS::SetMediaLayerId(const std::string& media_layer_id) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  umedia_client_->SetMediaLayerId(media_layer_id);
}

media::Ranges<base::TimeDelta> MediaPlayerUMS::GetBufferedTimeRanges() const {
  return umedia_client_->GetBufferedTimeRanges();
}

void MediaPlayerUMS::OnPlaybackStateChanged(bool playing) {
  NEVA_DVLOGTF(1);
  if (playing)
    client_->OnMediaPlayerPlay();
  else
    client_->OnMediaPlayerPause();
}

void MediaPlayerUMS::OnPlaybackEnded() {
  NEVA_DVLOGTF(1);
  client_->OnPlaybackComplete();
}

void MediaPlayerUMS::OnSeekDone(PipelineStatus status) {
  NEVA_DVLOGTF(1);
  if (status != media::PIPELINE_OK) {
    client_->OnMediaError(ConvertToMediaError(status));
    return;
  }
  client_->OnSeekComplete(base::Seconds(umedia_client_->GetCurrentTime()));
}

void MediaPlayerUMS::OnError(PipelineStatus error) {
  NEVA_VLOGTF(1);
  client_->OnMediaError(ConvertToMediaError(error));
}

void MediaPlayerUMS::OnBufferingStatusChanged(
    WebOSMediaClient::BufferingState buffering_state) {
  NEVA_VLOGTF(2) << "state:"
                 << WebOSClientBufferingStateToString(buffering_state);

  // TODO(neva): Ensure following states.
  switch (buffering_state) {
    case WebOSMediaClient::BufferingState::kHaveMetadata: {
      gfx::Size coded_size = umedia_client_->GetCodedVideoSize();
      gfx::Size natural_size = umedia_client_->GetNaturalVideoSize();
      client_->OnMediaMetadataChanged(
          base::Seconds(umedia_client_->GetDuration()), coded_size,
          natural_size, true);
    } break;
    case WebOSMediaClient::BufferingState::kLoadCompleted:
      client_->OnLoadComplete();
      break;
    case WebOSMediaClient::BufferingState::kPreloadCompleted:
      client_->OnLoadComplete();
      break;
    case WebOSMediaClient::BufferingState::kPrerollCompleted:
      break;
    case WebOSMediaClient::BufferingState::kWebOSBufferingStart:
      client_->OnBufferingStateChanged(BufferingState::BUFFERING_HAVE_NOTHING);
      break;
    case WebOSMediaClient::BufferingState::kWebOSBufferingEnd:
      client_->OnBufferingStateChanged(BufferingState::BUFFERING_HAVE_ENOUGH);
      break;
    case WebOSMediaClient::BufferingState::kWebOSNetworkStateLoading:
      break;
    case WebOSMediaClient::BufferingState::kWebOSNetworkStateLoaded:
      break;
  }
}

void MediaPlayerUMS::OnDurationChanged() {
  NEVA_VLOGTF(1);
}

void MediaPlayerUMS::OnVideoSizeChanged() {
  NEVA_VLOGTF(1);
  client_->OnVideoSizeChanged(umedia_client_->GetCodedVideoSize(),
                              umedia_client_->GetNaturalVideoSize());
}

void MediaPlayerUMS::OnUMSInfoUpdated(const std::string& detail) {
  NEVA_VLOGTF(1);
  if (!detail.empty())
    client_->OnCustomMessage(
        media::MediaEventType::kMediaEventUpdateUMSMediaInfo, detail);
}

void MediaPlayerUMS::OnAudioTrackAdded(
    const std::vector<MediaTrackInfo>& audio_track_info) {
  client_->OnAudioTracksUpdated(audio_track_info);
}

void MediaPlayerUMS::OnVideoTrackAdded(const std::string& id,
                                       const std::string& kind,
                                       const std::string& language,
                                       bool enabled) {
  NOTIMPLEMENTED();
}

void MediaPlayerUMS::OnWaitingForDecryptionKey() {
  NOTIMPLEMENTED();
}

void MediaPlayerUMS::OnEncryptedMediaInitData(
    const std::string& init_data_type,
    const std::vector<uint8_t>& init_data) {
  NOTIMPLEMENTED();
}

void MediaPlayerUMS::OnTimeUpdated(base::TimeDelta current_time) {
  current_time_ = current_time;
  if (client_)
    client_->OnTimeUpdate(current_time_, base::TimeTicks::Now());
}

bool MediaPlayerUMS::Send(const std::string& message) const {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  return umedia_client_->Send(message);
}
}  // namespace media
