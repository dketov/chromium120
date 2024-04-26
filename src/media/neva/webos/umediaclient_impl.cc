// Copyright 2018 LG Electronics, Inc.
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

#include "media/neva/webos/umediaclient_impl.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_util.h"
#include "media/base/media_switches_neva.h"
#include "media/base/video_util.h"
#include "media/neva/media_util.h"
#include "media/neva/webos/umedia_info_util_webos.h"
#include "media/neva/webos/umediaclient_extension.h"
#include "media/neva/webos/webos_media_pipeline_error.h"
#include "neva/logging.h"
#include "third_party/jsoncpp/source/include/json/json.h"

namespace {
const char kUdpUrl[] = "udp://";
const char kRtpUrl[] = "rtp://";
const char kRtspUrl[] = "rtsp://";

const char* WebOSMediaClientPreloadToString(
    media::WebOSMediaClient::Preload preload) {
#define STRINGIFY_WEBOSMEDIACLIENT_PRELOAD_CASE(state) \
  case media::WebOSMediaClient::Preload::state:        \
    return #state

  switch (preload) {
    STRINGIFY_WEBOSMEDIACLIENT_PRELOAD_CASE(kPreloadNone);
    STRINGIFY_WEBOSMEDIACLIENT_PRELOAD_CASE(kPreloadMetaData);
    STRINGIFY_WEBOSMEDIACLIENT_PRELOAD_CASE(kPreloadAuto);
  }
  return "null";
}

}  // namespace

namespace media {

// static
std::unique_ptr<WebOSMediaClient> WebOSMediaClient::Create(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    base::WeakPtr<WebOSMediaClient::EventListener> event_listener,
    const std::string& app_id) {
  return std::make_unique<UMediaClientImpl>(main_task_runner,
                                            std::move(event_listener), app_id);
}

UMediaClientImpl::UMediaClientImpl(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    base::WeakPtr<WebOSMediaClient::EventListener> event_listener,
    const std::string& app_id)
    : event_listener_(std::move(event_listener)),
      main_task_runner_(main_task_runner),
#if defined(USE_GST_MEDIA)
      luna_service_client_(app_id),
#endif
      app_id_(app_id),
      umediaclient_extension_(
          UMediaClientExtension::Create(AsWeakPtr(), main_task_runner)) {
  // NOTE: AsWeakPtr() will create new valid WeakPtr even after it is
  // invalidated.
  // On our case, UMediaClientImpl will invalidate weakptr on its dtor
  // then will cleanup umediaclient message loop. (~UMediaClientImpl ->
  // ~SupportsWeakPtr -> ~uMediaClient -> ~WebOSMediaClient)
  // In this situation, calling AsWeakPtr() on umediaclient message loop will
  // cause this problem. To prevent this problem, we store on |weak_ptr_| and
  // use it on umediaclient message loop.
  weak_ptr_ = AsWeakPtr();
}

UMediaClientImpl::~UMediaClientImpl() noexcept {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  weak_ptr_.reset();

  if (!MediaId().empty() && (is_loading() || is_loaded())) {
    UnloadInternal();
  }
  uMediaServer::uMediaClient::waitForStop();
}

void UMediaClientImpl::Load(bool video,
                            double current_time,
                            bool is_local_source,
                            const std::string& url,
                            const std::string& mime_type,
                            const std::string& referrer,
                            const std::string& user_agent,
                            const std::string& cookies,
                            const std::string& payload) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "url=" << url << " payload=" << payload;

  video_ = video;
  current_time_ = current_time;
  is_local_source_ = is_local_source;
  url_ = url;
  mime_type_ = mime_type;
  referrer_ = referrer;
  user_agent_ = user_agent;
  cookies_ = cookies;
  buffering_state_have_meta_data_ = false;

  NEVA_VLOGTF(2) << "currentTime: " << current_time_;
  updated_payload_ = UpdateMediaOption(payload, current_time_);

#if UMS_INTERNAL_API_VERSION == 2
  using std::placeholders::_1;
  set_source_info_callback(
      std::bind(&UMediaClientImpl::onSourceInfo, this, _1));
  set_video_info_callback(std::bind(&UMediaClientImpl::onVideoInfo, this, _1));
  set_audio_info_callback(std::bind(&UMediaClientImpl::onAudioInfo, this, _1));
#endif
  if (use_pipeline_preload_) {
    PreloadInternal(url_.c_str(), kMedia, updated_payload_.c_str());
  } else {
    LoadInternal();
  }
}

void UMediaClientImpl::Seek(base::TimeDelta time,
                            const media::PipelineStatusCB& seek_cb) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);

  ended_ = false;
  current_time_ = time.InSecondsF();
  seek_cb_ = seek_cb;
  is_seeking_ = true;
  uMediaServer::uMediaClient::seek(time.InSecondsF() * 1000);
}

float UMediaClientImpl::GetPlaybackRate() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return playback_rate_;
}

void UMediaClientImpl::SetPlaybackRate(float playback_rate) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "rate=" << playback_rate;
  if (MediaId().empty()) {
    playback_rate_ = playback_rate;
    return;
  }

  if (playback_rate == 0.0f) {
    // * -> paused
    requests_pause_ = true;
    playback_rate_on_paused_ = playback_rate_;
    uMediaServer::uMediaClient::pause();
  } else if (playback_rate_ == 0.0f) {
    // paused -> play
    requests_play_ = true;
    if (playback_rate_on_paused_ != 1.0f || playback_rate != 1.0f) {
      if (playback_rate_on_paused_ != playback_rate) {
        uMediaServer::uMediaClient::setPlayRate(
            playback_rate, CheckAudioOutput(playback_rate));
      }
    }
    if (use_pipeline_preload_ && !(is_loading() || is_loaded())) {
      LoadInternal();
    } else {
      uMediaServer::uMediaClient::play();
    }
    playback_rate_on_eos_ = 0.0f;
  } else if (playback_rate != 0.0f && playback_rate != playback_rate_) {
    // play -> play /w different rate
    uMediaServer::uMediaClient::setPlayRate(playback_rate,
                                            CheckAudioOutput(playback_rate));
    if (playback_rate_ < 0.0f && playback_rate > 0.0f && ended_) {
      NEVA_VLOGTF(1) << "reset ended_ as false. playbackrate: " << playback_rate
                     << " / playbackrate_: " << playback_rate_;
      ended_ = false;
    }
  }

  playback_rate_ = playback_rate;
}

void UMediaClientImpl::SetPlaybackVolume(double volume, bool forced) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "volume_ [" << volume_ << "] => volume [" << volume << "]";
  if (volume_ == volume && !forced) {
    return;
  }

  volume_ = volume;

  if (MediaId().empty() || loading_state_ != LOADING_STATE_LOADED) {
    return;
  }

  int volume_level = (int)(volume * 100);
  int duration = 0;
  EaseType type = kEaseTypeLinear;

  NEVA_DVLOGTF(1) << "volume_level [" << volume_level << "]";
  uMediaServer::uMediaClient::setVolume(volume_level, duration, type);
}

bool UMediaClientImpl::SelectTrack(const MediaTrackType type,
                                   const std::string& id) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NOTIMPLEMENTED();
  return false;
}

void UMediaClientImpl::Suspend(SuspendReason reason) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  bool force_unload = false;
  NEVA_DVLOGTF(1) << "- MediaId: " << MediaId();

  is_suspended_ = true;
#if defined(USE_GST_MEDIA)
  force_unload = true;
#endif

  if (force_unload) {
    released_media_resource_ = true;
    UnloadInternal();
    return;
  }

  if (use_pipeline_preload_ && !(is_loading() || is_loaded())) {
    return;
  }

  if (is_loading() || is_loaded()) {
    NEVA_VLOGTF(1) << "call uMediaServer::uMediaClient::notifyBackground()";
    uMediaServer::uMediaClient::notifyBackground();
  }
}

void UMediaClientImpl::Resume() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "- MediaId: " << MediaId()
                  << " loading_state=" << loading_state_
                  << " IsReleasedMediaResource=" << IsReleasedMediaResource()
                  << " current_time_=" << current_time_;
  is_suspended_ = false;

  if (IsReleasedMediaResource()) {
    if (is_loading()) {
      pending_loading_action_ = LOADING_ACTION_NONE;
      return;
    }
    updated_payload_ = UpdateMediaOption(updated_payload_, current_time_);
    LoadInternal();
    return;
  }

  if (use_pipeline_preload_ && !is_loaded()) {
    return;
  }

  NotifyForeground();
}

bool UMediaClientImpl::IsRecoverableOnResume() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

void UMediaClientImpl::SetPreload(Preload preload) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "app_id_=" << app_id_
                  << " preload=" << WebOSMediaClientPreloadToString(preload);
#if defined(USE_GST_MEDIA)
  // g-media-pipeline doesn't support preload
  preload = WebOSMediaClient::Preload::kPreloadNone;
#endif

  if (use_pipeline_preload_ && !(is_loading() || is_loaded()) &&
      preload_ == WebOSMediaClient::Preload::kPreloadMetaData &&
      preload == WebOSMediaClient::Preload::kPreloadAuto) {
    LoadInternal();
    use_pipeline_preload_ = false;
  }
  preload_ = preload;
}

bool UMediaClientImpl::IsPreloadable(const std::string& content_media_option) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  UpdateMediaOption(content_media_option, 0.0f);
  return use_pipeline_preload_;
}

std::string UMediaClientImpl::MediaId() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return uMediaServer::uMediaClient::getMediaId();
}

media::Ranges<base::TimeDelta> UMediaClientImpl::GetBufferedTimeRanges() const {
  // We support only one range.
  media::Ranges<base::TimeDelta> ranges;
  ranges.Add(base::Seconds(buffer_start_), base::Seconds(buffer_end_));
  return ranges;
}

bool UMediaClientImpl::DidLoadingProgress() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (!duration_) {
    return false;
  }

  int64_t current_buffer_end = buffer_end_;
  bool did_loading_progress =
      current_buffer_end != buffer_end_at_last_didLoadingProgress_;
  buffer_end_at_last_didLoadingProgress_ = current_buffer_end;

  return did_loading_progress;
}

bool UMediaClientImpl::UsesIntrinsicSize() const {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return true;
}

void UMediaClientImpl::Unload() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (MediaId().empty()) {
    return;
  }

  NEVA_DVLOGTF(1) << "- MediaId: " << MediaId();
  if (is_loaded()) {
    UnloadInternal();
    released_media_resource_ = true;
  } else if (is_loading()) {
    pending_loading_action_ = LOADING_ACTION_UNLOAD;
  }
}

bool UMediaClientImpl::IsSupportedBackwardTrickPlay() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NOTIMPLEMENTED();
  return false;
}

bool UMediaClientImpl::IsSupportedPreload() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return false;
}

void UMediaClientImpl::ResetPlayerState() {
  is_seeking_ = false;
  requests_play_ = false;
  requests_pause_ = false;
}

void UMediaClientImpl::SetDisableAudio(bool disable) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  audio_disabled_ = disable;
}

void UMediaClientImpl::SetMediaLayerId(const std::string& media_layer_id) {
  media_layer_id_ = media_layer_id;
}

bool UMediaClientImpl::onPlaying() {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchPlaying, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchPlaying() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (!requests_play_) {
    NEVA_DVLOGTF(1) << "ignored";
    return;
  }
  NEVA_DVLOGTF(2);

  // UMediaClientExtension needs this call to connect audio sink right before
  // playing.
  SetPlaybackVolume(volume_, true);

  requests_play_ = false;
  ended_ = false;

  if (event_listener_) {
    event_listener_->OnPlaybackStateChanged(true);
  }

  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(PlaybackNotificationToJson(
        MediaId(), PlaybackNotification::NotifyPlaying));
  }
}

bool UMediaClientImpl::onPaused() {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchPaused, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchPaused() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (!requests_pause_) {
    NEVA_DVLOGTF(1) << "ignored";
    return;
  }

  NEVA_DVLOGTF(2);
  requests_pause_ = false;

  if (event_listener_) {
    event_listener_->OnPlaybackStateChanged(false);
  }

  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(PlaybackNotificationToJson(
        MediaId(), PlaybackNotification::NotifyPaused));
  }
}

bool UMediaClientImpl::onSeekDone() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchSeekDone, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchSeekDone() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (!is_seeking_) {
    return;
  }
  is_seeking_ = false;

  if (!seek_cb_.is_null()) {
    std::move(seek_cb_).Run(media::PIPELINE_OK);
  }
  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(PlaybackNotificationToJson(
        MediaId(), PlaybackNotification::NotifySeekDone));
  }
}

bool UMediaClientImpl::onEndOfStream() {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchEndOfStream,
                                weak_ptr_, (playback_rate_ > 0.0f)));
  return true;
}

void UMediaClientImpl::DispatchEndOfStream(bool isForward) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "isForward=" << isForward;
  bool ignore_eos = false;

  playback_rate_on_eos_ = playback_rate_;

  if (!ignore_eos) {
    ended_ = true;

    if (duration_ > 0.0f) {
      current_time_ = (playback_rate_ < 0.0f) ? 0.0f : duration_;
    }
    if (event_listener_) {
      event_listener_->OnPlaybackEnded();
    }
  }

  if (IsRequiredUMSInfo() && event_listener_) {
    PlaybackNotification notification =
        playback_rate_on_eos_ >= 0.0f
            ? PlaybackNotification::NotifyEndOfStreamForward
            : PlaybackNotification::NotifyEndOfStreamBackward;
    event_listener_->OnUMSInfoUpdated(
        PlaybackNotificationToJson(MediaId(), notification));
  }
}

bool UMediaClientImpl::onLoadCompleted() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchLoadCompleted, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchLoadCompleted() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);

  if (is_loaded()) {
    NEVA_VLOGTF(2) << "ignore duplicated loadCompleted event";
    return;
  }

  loading_state_ = LOADING_STATE_LOADED;
  released_media_resource_ = false;
  if (pending_loading_action_ == LOADING_ACTION_UNLOAD) {
    released_media_resource_ = true;
    UnloadInternal();
    pending_loading_action_ = LOADING_ACTION_NONE;
    return;
  }

  pending_loading_action_ = LOADING_ACTION_NONE;

  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(PlaybackNotificationToJson(
        MediaId(), PlaybackNotification::NotifyLoadCompleted));
  }

  if (IsNotSupportedSourceInfo()) {
    has_audio_ = true;
    has_video_ = video_;
    updated_source_info_ = true;
    if (event_listener_) {
      buffering_state_have_meta_data_ = true;
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kHaveMetadata);
    }
  }

  SetPlaybackVolume(volume_, true);

  if (updated_source_info_ && event_listener_) {
    event_listener_->OnBufferingStatusChanged(
        WebOSMediaClient::BufferingState::kLoadCompleted);
  }

  if (use_pipeline_preload_) {
    if (playback_rate_ == 0.0f) {
      requests_play_ = false;
    } else {
      uMediaServer::uMediaClient::play();
    }
  }
}

bool UMediaClientImpl::onPreloadCompleted() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchPreloadCompleted, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchPreloadCompleted() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);

  if (is_loaded()) {
    return;
  }

  // If don't use pipeline preload, skip preloadCompleted event
  if (!use_pipeline_preload_) {
    return;
  }

  loading_state_ = LOADING_STATE_PRELOADED;

  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(PlaybackNotificationToJson(
        MediaId(), PlaybackNotification::NotifyPreloadCompleted));
  }

  if (IsNotSupportedSourceInfo()) {
    has_audio_ = true;
    has_video_ = video_;
    updated_source_info_ = true;
    if (event_listener_) {
      NEVA_DVLOGTF(2) << "buffering_state_have_meta_data_="
                      << buffering_state_have_meta_data_;
      buffering_state_have_meta_data_ = true;
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kHaveMetadata);
    }
  }

  if (updated_source_info_ && event_listener_) {
    event_listener_->OnBufferingStatusChanged(
        WebOSMediaClient::BufferingState::kPreloadCompleted);
  }
}

bool UMediaClientImpl::onUnloadCompleted() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchUnloadCompleted, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchUnloadCompleted() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (loading_state_ == LOADING_STATE_UNLOADED) {
    NEVA_VLOGTF(1) << "ignore duplicated UnloadCompleted event";
    return;
  }
  media_id.clear();
  loading_state_ = LOADING_STATE_UNLOADED;
  if (pending_loading_action_ == LOADING_ACTION_LOAD) {
    LoadInternal();
    pending_loading_action_ = LOADING_ACTION_NONE;
  }
}

bool UMediaClientImpl::onCurrentTime(int64_t currentTime) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchCurrentTime,
                                weak_ptr_, currentTime));
  return true;
}

void UMediaClientImpl::DispatchCurrentTime(int64_t currentTime) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (ended_) {
    NEVA_VLOGTF(1) << "ignore currentTime event on ended - " << currentTime;
    return;
  }

  if (is_seeking_) {
    NEVA_VLOGTF(1) << "ignore currentTime event on seeking - " << currentTime;
    return;
  }
  current_time_ = static_cast<double>(currentTime) / 1000.0;
  if (event_listener_) {
    event_listener_->OnTimeUpdated(base::Milliseconds(currentTime));
  }
}

bool UMediaClientImpl::onBufferRange(
    const struct uMediaServer::buffer_range_t& bufferRange) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchBufferRange,
                                weak_ptr_, bufferRange));
  return true;
}

void UMediaClientImpl::DispatchBufferRange(
    const struct uMediaServer::buffer_range_t& bufferRange) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  buffer_end_ = static_cast<double>(bufferRange.endTime);
  if (duration_ > 0) {
    if (bufferRange.remainingTime == -1 || buffer_end_ > duration_) {
      buffer_end_ = duration_;
    }
  }

  if (buffer_remaining_ != bufferRange.remainingTime && event_listener_) {
    buffer_remaining_ = bufferRange.remainingTime;
    if (bufferRange.remainingTime > 0) {
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kWebOSNetworkStateLoading);
    } else if (bufferRange.remainingTime == -1) {
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kWebOSNetworkStateLoaded);
    }
  }
}

#if UMS_INTERNAL_API_VERSION == 2
bool UMediaClientImpl::onSourceInfo(
    const struct ums::source_info_t& sourceInfo) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchSourceInfo,
                                weak_ptr_, sourceInfo));
  return true;
}

bool UMediaClientImpl::onAudioInfo(const struct ums::audio_info_t& audioInfo) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchAudioInfo, weak_ptr_,
                                audioInfo));
  return true;
}

bool UMediaClientImpl::onVideoInfo(const struct ums::video_info_t& videoInfo) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchVideoInfo, weak_ptr_,
                                videoInfo));
  return true;
}

void UMediaClientImpl::DispatchSourceInfo(
    const struct ums::source_info_t& sourceInfo) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (sourceInfo.programs.size() > 0) {
    if (sourceInfo.duration >= 0) {
      double updated_duration = sourceInfo.duration / 1000.;
      if (duration_ != updated_duration) {
        duration_ = updated_duration;
        if (event_listener_) {
          event_listener_->OnDurationChanged();
        }
      }
    }

    has_video_ = (sourceInfo.programs[0].video_stream > 0) ? true : false;
    has_audio_ = (sourceInfo.programs[0].audio_stream > 0) ? true : false;

    if (IsInsufficientSourceInfo()) {
      has_audio_ = true;
      has_video_ = video_;
    }

    if (has_video_) {
      uint32_t video_stream = sourceInfo.programs[0].video_stream;
      if (video_stream > 0 && video_stream < sourceInfo.video_streams.size()) {
        ums::video_info_t video_info = sourceInfo.video_streams[video_stream];
        gfx::Size naturalVideoSize(video_info.width, video_info.height);

        if (natural_video_size_ != naturalVideoSize) {
          coded_video_size_ = natural_video_size_ = naturalVideoSize;
          if (event_listener_) {
            event_listener_->OnVideoSizeChanged();
          }
        }
      }
    }
  }
  if (event_listener_) {
    buffering_state_have_meta_data_ = true;
    event_listener_->OnBufferingStatusChanged(
        WebOSMediaClient::BufferingState::kHaveMetadata);
  }

  if (!updated_source_info_ && event_listener_) {
    if (is_loaded()) {
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kLoadCompleted);
    } else if (loading_state_ == LOADING_STATE_PRELOADED) {
      event_listener_->OnBufferingStatusChanged(
          WebOSMediaClient::BufferingState::kPreloadCompleted);
    }
  }
  updated_source_info_ = true;

  if (IsRequiredUMSInfo() && event_listener_) {
    std::string json_string = SourceInfoToJson(MediaId(), sourceInfo);

    if (previous_source_info_ != json_string) {
      previous_source_info_ = json_string;
      event_listener_->OnUMSInfoUpdated(json_string);
    }
  }
}

void UMediaClientImpl::DispatchAudioInfo(
    const struct ums::audio_info_t& audioInfo) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  has_audio_ = true;
  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(AudioInfoToJson(MediaId(), audioInfo));
  }
}

void UMediaClientImpl::DispatchVideoInfo(
    const struct ums::video_info_t& videoInfo) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  has_video_ = true;
  gfx::Size naturalVideoSize(videoInfo.width, videoInfo.height);

  if (natural_video_size_ != naturalVideoSize) {
    coded_video_size_ = natural_video_size_ = naturalVideoSize;
    if (event_listener_) {
      event_listener_->OnVideoSizeChanged();
    }
  }

  if (IsRequiredUMSInfo() && event_listener_) {
    std::string json_string = VideoInfoToJson(MediaId(), videoInfo);

    if (previous_video_info_ != json_string) {
      previous_video_info_ = json_string;
      event_listener_->OnUMSInfoUpdated(json_string);
    }
  }
}
#endif  // UMS_INTERNAL_API_VERSION == 2

bool UMediaClientImpl::onError(int64_t errorCode,
                               const std::string& errorText) {
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchError, weak_ptr_,
                                errorCode, errorText));
  return true;
}

void UMediaClientImpl::DispatchError(int64_t errorCode,
                                     const std::string& errorText) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "MediaId=" << MediaId() << " errorCode=" << errorCode
                  << " msg=" << errorText;

  // ignore buffer full/low error
  if (errorCode == SMP_BUFFER_FULL || errorCode == SMP_BUFFER_LOW) {
    return;
  }

  media::PipelineStatus status = CheckErrorCode(errorCode);
  if (status != media::PIPELINE_OK) {
    if (is_loaded()) {
      UnloadInternal();
    }
  }

  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(
        ErrorInfoToJson(MediaId(), errorCode, errorText));
  }

  if (status != media::PIPELINE_OK && event_listener_) {
    event_listener_->OnError(status);
  }
}

bool UMediaClientImpl::onExternalSubtitleTrackInfo(
    const struct uMediaServer::external_subtitle_track_info_t& trackInfo) {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchExternalSubtitleTrackInfo,
                     weak_ptr_, trackInfo));
  return true;
}

void UMediaClientImpl::DispatchExternalSubtitleTrackInfo(
    const struct uMediaServer::external_subtitle_track_info_t& trackInfo) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (IsRequiredUMSInfo() && event_listener_) {
    event_listener_->OnUMSInfoUpdated(
        ExternalSubtitleTrackInfoToJson(MediaId(), trackInfo));
  }
}

media::PipelineStatus UMediaClientImpl::CheckErrorCode(int64_t errorCode) {
  media::PipelineStatus status = media::PIPELINE_OK;

  // refer following error code
  // http://collab.lge.com/main/display/NC50Platform/error
  if (SMP_STATUS_IS_100_GENERAL_ERROR(errorCode)) {
    status = media::PIPELINE_ERROR_INITIALIZATION_FAILED;
    // Ignore 101(Command Not Supported) status
    if (errorCode == SMP_COMMAND_NOT_SUPPORTED) {
      status = media::PIPELINE_OK;
    }
  } else if (SMP_STATUS_IS_200_PLAYBACK_ERROR(errorCode)) {
    // Playback related statuss (200 range)
    status = media::PIPELINE_ERROR_ABORT;

    // Ignore 200(Audio Codec Not Supported) status
    // when there is no audio track.
    if (errorCode == SMP_AUDIO_CODEC_NOT_SUPPORTED &&
        (!has_audio_ || has_video_ || video_)) {
      status = media::PIPELINE_OK;
    }
    // Ignore 201(Video Codec Not Supported) status
    // when there is no video track.
    if (errorCode == SMP_VIDEO_CODEC_NOT_SUPPORTED && !has_video_) {
      status = media::PIPELINE_OK;
    }
    // Ignore 210(Unknown Subtitle) status
    if (errorCode == SMP_UNKNOWN_SUBTITLE) {
      status = media::PIPELINE_OK;
    }
    // 202(Media not found)
    if (errorCode == SMP_MEDIA_NOT_FOUND) {
      status = media::PIPELINE_ERROR_INITIALIZATION_FAILED;
      if (media_transport_type_ == "MPEG-DASH") {
        status = media::PIPELINE_ERROR_NETWORK;
      }
    }
  } else if (SMP_STATUS_IS_300_NETWORK_ERROR(errorCode)) {
    // Network related statuss (300 range)
    status = media::PIPELINE_ERROR_NETWORK;
  } else if (SMP_STATUS_IS_400_SERVER_ERROR(errorCode)) {
    // Server related statuss (400 range)
    status = media::PIPELINE_ERROR_NETWORK;
  } else if (SMP_STATUS_IS_500_DRM_ERROR(errorCode)) {
    // DRM related statuss (500 range)
    status = media::PIPELINE_ERROR_DECRYPT;
  } else if (SMP_STATUS_IS_600_RM_ERROR(errorCode)) {
    // resource is released by policy action
    if (errorCode == SMP_RM_RELATED_ERROR) {
      status = media::PIPELINE_ERROR_RESOURCE_IS_RELEASED;
      released_media_resource_ = true;
#if defined(USE_GST_MEDIA)
      // force paused playback state
      pause();
      DispatchPaused();
#endif
    }
    // allocation resources status
    if (errorCode == SMP_RESOURCE_ALLOCATION_ERROR ||
        errorCode == SMP_DVR_RESOURCE_ALLOCATION_ERROR) {
      status = media::PIPELINE_ERROR_ABORT;
    }
  } else if (SMP_STATUS_IS_700_API_ERROR(errorCode)) {
    // API functionality failure,
    // but not critical status for playback (700 range)
    if (errorCode == SMP_SEEK_FAILURE) {
      if (is_seeking_) {
        is_seeking_ = false;
      }
      if (!seek_cb_.is_null()) {
        std::move(seek_cb_).Run(media::PIPELINE_OK);
      }
    }
    status = media::PIPELINE_OK;
  } else if (SMP_STATUS_IS_1000_PIPELINE_ERROR(errorCode)) {
    // uMS send when the pipeline crash occurred
    status = media::PIPELINE_ERROR_ABORT;
  } else if (SMP_STATUS_IS_40000_STREAMING_ERROR(errorCode)) {
    // Streaming Protocol related status (40000 ~ 49999 range)
    status = media::DEMUXER_ERROR_NO_SUPPORTED_STREAMS;
  }

  return status;
}

bool UMediaClientImpl::onUserDefinedChanged(const char* message) {
  if (!message) {
    return true;
  }

  std::string msg = std::string(message);
  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&UMediaClientImpl::DispatchUserDefinedChanged,
                                weak_ptr_, msg));
  return true;
}

void UMediaClientImpl::DispatchUserDefinedChanged(const std::string& message) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (IsRequiredUMSInfo() && event_listener_) {
    std::string json_string = UserDefinedInfoToJson(MediaId(), message);

    if (previous_user_defined_changed_ == json_string) {
      return;
    }

    previous_user_defined_changed_ = json_string;
    if (event_listener_) {
      event_listener_->OnUMSInfoUpdated(json_string);
    }
  }
}

bool UMediaClientImpl::onBufferingStart() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchBufferingStart, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchBufferingStart() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (event_listener_) {
    event_listener_->OnBufferingStatusChanged(
        WebOSMediaClient::BufferingState::kWebOSBufferingStart);
  }
}

bool UMediaClientImpl::onBufferingEnd() {
  main_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&UMediaClientImpl::DispatchBufferingEnd, weak_ptr_));
  return true;
}

void UMediaClientImpl::DispatchBufferingEnd() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1);
  if (event_listener_) {
    event_listener_->OnBufferingStatusChanged(
        WebOSMediaClient::BufferingState::kWebOSBufferingEnd);
  }
}

std::string UMediaClientImpl::UpdateMediaOption(const std::string& mediaOption,
                                                double start) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  Json::Reader reader;
  Json::FastWriter writer;
  Json::Value media_option;
  Json::Value http_header;
  std::string res;
  bool use_pipeline_preload = false;

  if (!mediaOption.empty()) {
    if (!reader.parse(mediaOption, media_option)) {
      NEVA_VLOGTF(2) << "json_reader.parse error";
    } else if (media_option.isObject()) {
      if (media_option.isMember("htmlMediaOption")) {
        if (media_option["htmlMediaOption"].isMember("useUMSMediaInfo")) {
          use_umsinfo_ =
              media_option["htmlMediaOption"]["useUMSMediaInfo"].asBool();
        }
        if (media_option["htmlMediaOption"].isMember("usePipelinePreload")) {
          use_pipeline_preload =
              media_option["htmlMediaOption"]["usePipelinePreload"].asBool();
        }
        if (media_option["htmlMediaOption"].isMember("useSetUri")) {
          use_set_uri_ = media_option["htmlMediaOption"]["useSetUri"].asBool();
        }
      }
      if (media_option.isMember("mediaTransportType")) {
        media_transport_type_ = media_option["mediaTransportType"].asString();

        // support legacy spec for smartshare
        if (media_option["mediaTransportType"].asString() == "USB") {
          media_option["mediaTransportType"] = "URI";
          is_usb_file_ = true;
        }
      }
    }
  }

  http_header["referer"] = referrer_;
  http_header["userAgent"] = user_agent_;
  http_header["cookies"] = cookies_;
  media_option["option"]["transmission"]["httpHeader"] = http_header;
  media_option["option"]["bufferControl"]["userBufferCtrl"] = false;
  media_option["option"]["appId"] = app_id_;

#if defined(USE_GAV)
  media_option["option"]["windowId"] = media_layer_id_;
#endif

  media_option["option"]["preload"] =
      (use_pipeline_preload_) ? "true" : "false";
  media_option["option"]["needAudio"] = !audio_disabled_;

  if (!use_set_uri_) {
    media_option["option"]["useSeekableRanges"] = true;
  }

  if (start) {
    media_option["option"]["transmission"]["playTime"]["start"] =
        static_cast<int64_t>(start * 1000.0);
  }

  // check contents type
  if (!media_option.isMember("mediaTransportType")) {
    if (!mime_type_.empty()) {
      if (mime_type_ == "application/vnd.apple.mpegurl" ||
          mime_type_ == "application/mpegurl" ||
          mime_type_ == "application/x-mpegurl" ||
          mime_type_ == "application/vnd.apple.mpegurl.audio" ||
          mime_type_ == "audio/mpegurl" || mime_type_ == "audio/x-mpegurl") {
        media_transport_type_ = "HLS";
      } else if (mime_type_ == "application/dash+xml") {
        media_transport_type_ = "MPEG-DASH";
      } else if (mime_type_ == "application/vnd.ms-sstr+xml") {
        media_transport_type_ = "MSIIS";
      }
    } else if (url_.find("m3u8") != std::string::npos) {
      media_transport_type_ = "HLS";
    }

    if (url_.find(kUdpUrl) != std::string::npos) {
      media_transport_type_ = "UDP";
    }
    if (url_.find(kRtpUrl) != std::string::npos) {
      media_transport_type_ = "RTP";
    }
    if (url_.find(kRtspUrl) != std::string::npos) {
      media_transport_type_ = "RTSP";
    }
    NEVA_VLOGTF(2) << "media_transport_type_ : " << media_transport_type_;

    if (!media_transport_type_.empty()) {
      media_option["mediaTransportType"] = media_transport_type_;
    }
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableAccurateSeek)) {
    media_option["option"]["seekMode"] = "accurate";
  }

  if (media_option.empty()) {
    return std::string();
  }

  res = writer.write(media_option);

  NEVA_VLOGTF(1) << "media_option: " << res;

  return res;
}

bool UMediaClientImpl::IsRequiredUMSInfo() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_ == "DLNA" || media_transport_type_ == "HLS-LG" ||
      media_transport_type_ == "USB" || media_transport_type_ == "MIRACAST" ||
      media_transport_type_ == "DPS" || media_transport_type_ == "CAMERA" ||
      use_umsinfo_) {
    return true;
  }
  return false;
}

bool UMediaClientImpl::IsInsufficientSourceInfo() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_ == "HLS" || media_transport_type_ == "MSIIS" ||
      media_transport_type_ == "WIDEVINE" || media_transport_type_ == "DPS") {
    return true;
  }
  return false;
}

bool UMediaClientImpl::IsAdaptiveStreaming() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_.compare(0, 3, "HLS") == 0 ||
      media_transport_type_ == "MIRACAST" || media_transport_type_ == "MSIIS" ||
      media_transport_type_ == "MPEG-DASH" ||
      media_transport_type_ == "WIDEVINE") {
    return true;
  }
  return false;
}

bool UMediaClientImpl::IsNotSupportedSourceInfo() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_ == "MIRACAST" || media_transport_type_ == "UDP" ||
      media_transport_type_ == "RTSP" || media_transport_type_ == "RTP") {
    return true;
  }
  return false;
}

bool UMediaClientImpl::IsAppName(const char* app_name) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  return base::StartsWith(app_id_, app_name,
                          base::CompareCase::INSENSITIVE_ASCII);
}

bool UMediaClientImpl::Is2kVideoAndOver() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (!video_) {
    return false;
  }

  if (natural_video_size_ == gfx::Size()) {
    return false;
  }

  float macro_blocks_of_2k = 8704.0;
  float macro_blocks_of_video =
      ceilf((float)natural_video_size_.width() / 16.0) *
      ceilf((float)natural_video_size_.height() / 16.0);

  NEVA_LOGTF(INFO) << "macro_blocks_of_2k=" << macro_blocks_of_2k
                   << " macro_blocks_of_video=" << macro_blocks_of_video;

  if (macro_blocks_of_video >= macro_blocks_of_2k) {
    return true;
  }

  return false;
}

bool UMediaClientImpl::IsSupportedAudioOutputOnTrickPlaying() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NOTIMPLEMENTED();
  return false;
}

bool UMediaClientImpl::IsSupportedSeekableRanges() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_ == "MPEG-DASH" ||
      media_transport_type_ == "MSIIS") {
    return true;
  } else if (media_transport_type_.compare(0, 3, "HLS") == 0) {
    if (use_set_uri_) {
      return false;
    }
    return true;
  }
  return false;
}

bool UMediaClientImpl::CheckAudioOutput(float playback_rate) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (playback_rate == 1.0f) {
    return true;
  }

  if (!IsSupportedAudioOutputOnTrickPlaying()) {
    return false;
  }

  if (playback_rate < 0.5f || playback_rate > 2.0f) {
    return false;
  }

  if (playback_rate == 2.0f && Is2kVideoAndOver()) {
    return false;
  }

  return true;
}

void UMediaClientImpl::LoadInternal() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  AudioStreamClass stream_type = kMedia;

  if (media_transport_type_ == "CAMERA") {
    stream_type = kCamera;
  }
  if (use_pipeline_preload_ && !is_suspended_) {
    NotifyForeground();
  }

  NEVA_DVLOGTF(1) << "call uMediaServer::uMediaClient::loadAsync()";
  LoadAsyncInternal(url_.c_str(), stream_type, updated_payload_.c_str());

  if (!use_pipeline_preload_ && !is_suspended_) {
    NotifyForeground();
  }
}

bool UMediaClientImpl::UnloadInternal() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  loading_state_ = LOADING_STATE_UNLOADING;
  return uMediaServer::uMediaClient::unload();
}

bool UMediaClientImpl::LoadAsyncInternal(const std::string& uri,
                                         AudioStreamClass audio_class,
                                         const std::string& media_payload) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "loading_state_ " << loading_state_ << " -> "
                  << LOADING_STATE_LOADING;
  loading_state_ = LOADING_STATE_LOADING;
  return uMediaServer::uMediaClient::loadAsync(uri, audio_class, media_payload);
}

bool UMediaClientImpl::PreloadInternal(const std::string& uri,
                                       AudioStreamClass audio_class,
                                       const std::string& media_payload) {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  NEVA_DVLOGTF(1) << "loading_state_ " << loading_state_ << " -> "
                  << LOADING_STATE_PRELOADING;
  loading_state_ = LOADING_STATE_PRELOADING;
  return uMediaServer::uMediaClient::preload(uri, audio_class, media_payload);
}

void UMediaClientImpl::NotifyForeground() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());

  NEVA_VLOGTF(1) << "call uMediaServer::uMediaClient::notifyForeground()";
  uMediaServer::uMediaClient::notifyForeground();
}

bool UMediaClientImpl::IsMpegDashContents() {
  NEVA_DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (media_transport_type_ == "MPEG-DASH") {
    return true;
  }
  return false;
}

bool UMediaClientImpl::Send(const std::string& message) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return false;
}
}  // namespace media
