// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_GEOLOCATION_WEBOS_GEOLOCATION_PROVIDER_NEVA_H_
#define SERVICES_DEVICE_GEOLOCATION_WEBOS_GEOLOCATION_PROVIDER_NEVA_H_

#include "base/threading/thread_checker.h"
#include "services/device/public/cpp/geolocation/location_provider.h"
#include "services/device/public/mojom/geolocation_internals.mojom.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace device {
class GeolocationRequestGeoplugin;

class GeolocationProviderNeva : public LocationProvider {
 public:
  GeolocationProviderNeva(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  GeolocationProviderNeva(const GeolocationProviderNeva&) = delete;
  GeolocationProviderNeva& operator=(const GeolocationProviderNeva&) = delete;
  ~GeolocationProviderNeva() override;

  bool is_permission_granted() const { return is_permission_granted_; }
  void OnLocationResponse(mojom::GeopositionResultPtr result,
                          bool server_error);

  // LocationProvider implementations
  void FillDiagnostics(mojom::GeolocationDiagnostics& diagnostics) override;
  void SetUpdateCallback(
      const LocationProviderUpdateCallback& callback) override;
  void StartProvider(bool high_accuracy) override;
  void StopProvider() override;
  const mojom::GeopositionResult* GetPosition() override;
  void OnPermissionGranted() override;

 private:
  void RequestPosition();
  bool is_permission_granted_ = false;
  base::ThreadChecker thread_checker_;
  mojom::GeopositionResultPtr result_;
  LocationProviderUpdateCallback location_provider_update_callback_;
  const std::unique_ptr<GeolocationRequestGeoplugin> request_;
  base::WeakPtrFactory<GeolocationProviderNeva> weak_factory_{this};
};

}  // namespace device

#endif  // SERVICES_DEVICE_GEOLOCATION_WEBOS_GEOLOCATION_PROVIDER_NEVA_H_
