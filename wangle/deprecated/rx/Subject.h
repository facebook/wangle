/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/deprecated/rx/types.h> // must come first
#include <wangle/deprecated/rx/Observable.h>
#include <wangle/deprecated/rx/Observer.h>

namespace wangle {

/// Subject interface. A Subject is both an Observable and an Observer. There
/// is a default implementation of the Observer methods that just forwards the
/// observed events to the Subject's observers.
template <class T>
struct Subject : public Observable<T>, public Observer<T> {
  void onNext(const T& val) override {
    this->forEachObserver([&](Observer<T>* o){
      o->onNext(val);
    });
  }
  void onError(Error e) override {
    this->forEachObserver([&](Observer<T>* o){
      o->onError(e);
    });
  }
  void onCompleted() override {
    this->forEachObserver([](Observer<T>* o){
      o->onCompleted();
    });
  }
};

} // namespace wangle
