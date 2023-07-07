// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#ifndef DICE_TEST_FRAMEWORK_H_
#define DICE_TEST_FRAMEWORK_H_

#include "gtest/gtest.h"

#ifndef DICE_USE_GTEST
// Use pigweed's pw_unit_test::light framework instead of upstream gtest.
#include "pw_unit_test/simple_printing_event_handler.h"
#endif

#endif  // DICE_TEST_FRAMEWORK_H_
