#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

'fbcode_builder steps to build & test Wangle'

import specs.gmock as gmock
import specs.folly as folly
import specs.fizz as fizz
import specs.sodium as sodium

from shell_quoting import ShellQuoted


def fbcode_builder_spec(builder):
    builder.add_option(
        'wangle/_build:cmake_defines',
        {
            'BUILD_SHARED_LIBS': 'OFF',
            'BUILD_TESTS': 'ON',
        }
    )
    return {
        'depends_on': [gmock, folly, fizz, sodium],
        'steps': [
            builder.fb_github_cmake_install('wangle/_build', '../wangle'),
            builder.step(
                'Run wangle tests', [
                    builder.run(
                        ShellQuoted('ctest --output-on-failure -j {n}')
                        .format(n=builder.option('make_parallelism'), )
                    )
                ]
            ),
        ]
    }


config = {
    'github_project': 'facebook/wangle',
    'fbcode_builder_spec': fbcode_builder_spec,
}
