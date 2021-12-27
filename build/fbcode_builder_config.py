#!/usr/bin/env python
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

"fbcode_builder steps to build & test Wangle"

import specs.fizz as fizz
import specs.fmt as fmt
import specs.folly as folly
import specs.gmock as gmock
import specs.sodium as sodium
from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    builder.add_option(
        "wangle/_build:cmake_defines",
        {
            "BUILD_SHARED_LIBS": "OFF",
            "BUILD_TESTS": "ON",
        },
    )
    return {
        "depends_on": [gmock, fmt, folly, fizz, sodium],
        "steps": [
            builder.fb_github_cmake_install("wangle/_build", "../wangle"),
            builder.step(
                "Run wangle tests",
                [
                    builder.run(
                        ShellQuoted("ctest --output-on-failure -j {n}").format(
                            n=builder.option("make_parallelism"),
                        )
                    )
                ],
            ),
        ],
    }


config = {
    "github_project": "facebook/wangle",
    "fbcode_builder_spec": fbcode_builder_spec,
}
