# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This program wraps an arbitrary command since gn currently can only execute
scripts."""

import os
import subprocess
import sys
from shutil import copy2

args = sys.argv[1:]
args[0] = os.path.abspath(args[0])

if sys.platform == 'darwin':
    copy2(os.path.join(os.path.dirname(args[0]), 'libffmpeg.dylib'), os.path.dirname(os.path.dirname(args[0])))

sys.exit(subprocess.call(args))
