// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/constants.h"

namespace viz {

const uint32_t kDefaultActivationDeadlineInFrames = 4u;

#if defined(USE_NEVA_APPRUNTIME)
const uint32_t kDefaultVizFMPTimeout = 1000u;
#endif

}  // namespace viz
