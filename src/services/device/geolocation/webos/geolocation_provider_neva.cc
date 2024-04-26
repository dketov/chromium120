// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/geolocation/webos/geolocation_provider_neva.h"

#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "services/device/geolocation/webos/geolocation_request_geoplugin.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace {
const int kDataCompleteWaitSeconds = 2;
}  // namespace

namespace device {

GeolocationProviderNeva::GeolocationProviderNeva(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : request_(new GeolocationRequestGeoplugin(url_loader_factory)) {}

GeolocationProviderNeva::~GeolocationProviderNeva() {
  DCHECK(thread_checker_.CalledOnValidThread());
  StopProvider();
}

void GeolocationProviderNeva::FillDiagnostics(
    mojom::GeolocationDiagnostics& diagnostics) {
  NOTIMPLEMENTED_LOG_ONCE();
}

void GeolocationProviderNeva::SetUpdateCallback(
    const LocationProviderUpdateCallback& callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  location_provider_update_callback_ = callback;
}

void GeolocationProviderNeva::StartProvider(bool high_accuracy) {
  DCHECK(thread_checker_.CalledOnValidThread());
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&GeolocationProviderNeva::RequestPosition,
                     weak_factory_.GetWeakPtr()),
      base::Seconds(kDataCompleteWaitSeconds));
}

void GeolocationProviderNeva::RequestPosition() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!is_permission_granted_)
    return;

  if (request_) {
    request_->GeopluginRequest(
        base::BindOnce(&GeolocationProviderNeva::OnLocationResponse,
                       weak_factory_.GetWeakPtr()));
    return;
  }
}

void GeolocationProviderNeva::OnLocationResponse(
    mojom::GeopositionResultPtr result,
    bool server_error) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Let listeners know that we now have a position available.
  if (!location_provider_update_callback_.is_null()) {
    result_ = std::move(result);
    location_provider_update_callback_.Run(this, result_.Clone());
  }
}

void GeolocationProviderNeva::StopProvider() {
  DCHECK(thread_checker_.CalledOnValidThread());
  weak_factory_.InvalidateWeakPtrs();
}

const mojom::GeopositionResult* GeolocationProviderNeva::GetPosition() {
  return result_.get();
}

void GeolocationProviderNeva::OnPermissionGranted() {
  is_permission_granted_ = true;
}

}  // namespace device
