// Copyright 2017-2020 LG Electronics, Inc.
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

#include "media/neva/media_player_neva_factory.h"

#include "base/command_line.h"
#include "media/base/media_switches_neva.h"
#include "media/neva/webos/media_player_ums.h"
#include "net/base/mime_util.h"

#if defined(USE_NEVA_MEDIA_PLAYER_CAMERA)
#include "media/neva/webos/media_player_camera.h"
#endif

namespace {
// Also see //media/base/neva/webos/neva_mime_util_internal_webos.cc.
const char kPrefixOfWebOSMediaMimeType[] = "service/webos-*";

#if defined(USE_NEVA_MEDIA_PLAYER_CAMERA)
const char kPrefixOfWebOSCameraMimeType[] = "service/webos-camera";
#endif  // defined(USE_NEVA_MEDIA_PLAYER_CAMERA)
}  // namespace

namespace media {
MediaPlayerType MediaPlayerNevaFactory::GetMediaPlayerType(
    const std::string& mime_type) {
#if defined(USE_NEVA_MEDIA_PLAYER_CAMERA)
  if (net::MatchesMimeType(kPrefixOfWebOSCameraMimeType, mime_type)) {
    return MediaPlayerType::kMediaPlayerTypeCamera;
  }
#endif
  if (net::MatchesMimeType(kPrefixOfWebOSMediaMimeType, mime_type)) {
    return MediaPlayerType::kMediaPlayerTypeNone;
  }
  // If url has no webOS prefix than we should handle it as a normal
  // LoadTypeURL
  return MediaPlayerType::kMediaPlayerTypeUMS;
}

MediaPlayerNeva* MediaPlayerNevaFactory::CreateMediaPlayerNeva(
    MediaPlayerNevaClient* client,
    MediaPlayerType media_type,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const std::string& app_id) {
  switch (media_type) {
#if defined(USE_NEVA_MEDIA_PLAYER_CAMERA)
    case MediaPlayerType::kMediaPlayerTypeCamera:
      return new MediaPlayerCamera(client, main_task_runner, app_id);
      break;
#endif
    case MediaPlayerType::kMediaPlayerTypeUMS:
      return new MediaPlayerUMS(client, main_task_runner, app_id);
      break;
    default:
      break;
  }
  return nullptr;
}

}  // namespace media
