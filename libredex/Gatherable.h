/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <list>
#include <vector>

class DexString;
class DexType;
class DexFieldRef;
class DexMethodRef;

class Gatherable {
 protected:
  Gatherable() {}

  virtual ~Gatherable(){};

 public:
  virtual void gather_strings(std::vector<DexString*>& lstring) const {}
  virtual void gather_types(std::vector<DexType*>& ltype) const {}
  virtual void gather_fields(std::vector<DexFieldRef*>& lfield) const {}
  virtual void gather_methods(std::vector<DexMethodRef*>& lmethod) const {}
};
