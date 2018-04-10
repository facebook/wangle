#!/usr/bin/env python
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
'Facebook-specific additions to the fbcode_builder spec for Wangle'

config = read_fbcode_builder_config('fbcode_builder_config.py')
config['legocastle_opts'] = {
    'alias': 'wangle-oss',
    'oncall': 'wangle',
    'build_name': 'Open-source build for Wangle',
    'legocastle_os': 'ubuntu_16.04',
}
