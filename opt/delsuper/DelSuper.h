/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "PassManager.h"
#include "DexClass.h"

class DelSuperPass : public Pass {
 public:
  DelSuperPass() : Pass("DelSuperPass") {}

  void run_pass(DexStoresVector&, ConfigFiles&, PassManager&) override;
};
