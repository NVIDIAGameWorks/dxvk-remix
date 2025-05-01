#############################################################################
# Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#############################################################################

import uuid
import os
import sys
import difflib
import configparser

bridge_project_name = "bridge-remix"

def generate_guid(key):
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, key)).upper()

def pathsep_to_slash(path):
    return path.replace("\\", "/")

def pathsep_to_backslash(path):
    return path.replace("/", "\\")

def pathsep_to_underscore(path):
    return pathsep_to_backslash(path).replace("\\", "_")

# returns true if file exists
def check_if_file_exists(output_root_path, filename):
    output_root_path = pathsep_to_backslash(output_root_path)
    target = os.path.join(output_root_path, filename)
    return os.path.exists(target)

# returns true if file updated, false if not
def write_file_if_not_identical(output_root_path, filename, data):
    output_root_path = pathsep_to_backslash(output_root_path)
    target = os.path.join(output_root_path, filename)
    data = data.strip()
    if os.path.exists(target):
        orig = str(open(target, "rt").read()).strip()
        if orig == data:
            return False
        else:
            if target.endswith(".sln"):
                # debug: sln should rarely ever change, but VS is super finicky and ends up rewriting it
                # when it doesn't look *just* right; show a diff in this case so we can figure out what's changing
                print("sln has changed, diff:")
                origlist = orig.split('\n')
                newlist = data.split('\n')
                delta = difflib.unified_diff(origlist, newlist, fromfile='before', tofile='after')
                for l in delta:
                    if l[-1] == '\n':
                        print(l, end='')
                    else:
                        print(l)

    print(data, file=open(target, "wt"))
    print("Generated " + target)
    return True

# returns a dictionary such as:
# {
#     "Game Project Name": {
#         "path": "c:\\path\\to\working\\dir",
#         "commandline:" "game.exe -command-line-flags"
#     }
# }

def load_game_targets():
    if not os.path.exists("../gametargets.conf"):
        return {}
    
    config = configparser.ConfigParser(interpolation=configparser.ExtendedInterpolation())
    config.read("../gametargets.conf")

    ret = {}

    for section in config:
        if config.has_option(section, 'outputdir') and config.has_option(section, 'workingdir') and config.has_option(section, 'commandline'):
            ret[section] = {
                "outputdir": config.get(section, 'outputdir'),
                "workingdir": config.get(section, 'workingdir'),
                "commandline": config.get(section, 'commandline')
            }
    
    return ret
