// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/fake_tether_disconnector.h"

namespace chromeos {

namespace tether {

FakeTetherDisconnector::FakeTetherDisconnector() {}

FakeTetherDisconnector::~FakeTetherDisconnector() {}

void FakeTetherDisconnector::DisconnectFromNetwork(
    const std::string& tether_network_guid,
    const base::Closure& success_callback,
    const network_handler::StringResultCallback& error_callback) {
  last_disconnected_tether_network_guid_ = tether_network_guid;

  if (disconnection_error_name_.empty())
    success_callback.Run();
  else
    error_callback.Run(disconnection_error_name_);
}

}  // namespace tether

}  // namespace chromeos
