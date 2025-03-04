// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_GEOLOCATION_LOCATION_ARBITRATOR_H_
#define DEVICE_GEOLOCATION_LOCATION_ARBITRATOR_H_

#include <stdint.h>
#include <memory>
#include <vector>

#include "base/callback_forward.h"
#include "base/cancelable_callback.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "device/geolocation/access_token_store.h"
#include "device/geolocation/geolocation_export.h"
#include "device/geolocation/geolocation_provider.h"
#include "device/geolocation/geoposition.h"
#include "device/geolocation/location_provider.h"
#include "net/url_request/url_request_context_getter.h"

namespace net {
class URLRequestContextGetter;
}

namespace device {
class AccessTokenStore;
class GeolocationDelegate;
class LocationProvider;

// This class is responsible for handling updates from multiple underlying
// providers and resolving them to a single 'best' location fix at any given
// moment.
class DEVICE_GEOLOCATION_EXPORT LocationArbitrator : public LocationProvider {
 public:
  // Number of milliseconds newer a location provider has to be that it's worth
  // switching to this location provider on the basis of it being fresher
  // (regardles of relative accuracy). Public for tests.
  static const int64_t kFixStaleTimeoutMilliseconds;

  LocationArbitrator(std::unique_ptr<GeolocationDelegate> delegate,
                     const GeolocationProvider::RequestContextProducer
                         request_context_producer,
                     const std::string& api_key);
  ~LocationArbitrator() override;

  static GURL DefaultNetworkProviderURL();
  bool HasPermissionBeenGrantedForTest() const;

  // LocationProvider implementation.
  void SetUpdateCallback(
      const LocationProviderUpdateCallback& callback) override;
  bool StartProvider(bool enable_high_accuracy) override;
  void StopProvider() override;
  const Geoposition& GetPosition() override;
  void OnPermissionGranted() override;

 protected:
  // These functions are useful for injection of dependencies in derived
  // testing classes.
  virtual scoped_refptr<AccessTokenStore> NewAccessTokenStore();
  virtual std::unique_ptr<LocationProvider> NewNetworkLocationProvider(
      scoped_refptr<net::URLRequestContextGetter> context,
      const std::string& api_key);
  virtual std::unique_ptr<LocationProvider> NewSystemLocationProvider();
  virtual base::Time GetTimeNow() const;

 private:
  friend class TestingLocationArbitrator;

  scoped_refptr<AccessTokenStore> GetAccessTokenStore();

  // Provider will either be added to |providers_| or
  // deleted on error (e.g. it fails to start).
  void RegisterProvider(std::unique_ptr<LocationProvider> provider);
  void RegisterSystemProvider();

  // Tell all registered providers to start.
  bool DoStartProviders();

  // Response callback for request_context_callback_.
  void OnRequestContextResponse(
      scoped_refptr<net::URLRequestContextGetter> context_getter);

  // Gets called when a provider has a new position.
  void OnLocationUpdate(const LocationProvider* provider,
                        const Geoposition& new_position);

  // Returns true if |new_position| is an improvement over |old_position|.
  // Set |from_same_provider| to true if both the positions came from the same
  // provider.
  bool IsNewPositionBetter(const Geoposition& old_position,
                           const Geoposition& new_position,
                           bool from_same_provider) const;

  const std::unique_ptr<GeolocationDelegate> delegate_;
  const GeolocationProvider::RequestContextProducer request_context_producer_;
  const std::string api_key_;

  scoped_refptr<AccessTokenStore> access_token_store_;
  LocationProvider::LocationProviderUpdateCallback arbitrator_update_callback_;

  // CancelableCallback to prevent OnRequestContextReponse from being called
  // multiple times in case request_context_callback_ is invoked multiple times.
  base::CancelableCallback<void(scoped_refptr<net::URLRequestContextGetter>)>
      request_context_response_callback_;

  std::vector<std::unique_ptr<LocationProvider>> providers_;
  bool enable_high_accuracy_;
  // The provider which supplied the current |position_|
  const LocationProvider* position_provider_;
  bool is_permission_granted_;
  // The current best estimate of our position.
  Geoposition position_;

  // Tracks whether providers should be running.
  bool is_running_;

  DISALLOW_COPY_AND_ASSIGN(LocationArbitrator);
};

// Factory functions for the various types of location provider to abstract
// over the platform-dependent implementations.
std::unique_ptr<LocationProvider> NewSystemLocationProvider();

}  // namespace device

#endif  // DEVICE_GEOLOCATION_LOCATION_ARBITRATOR_H_
