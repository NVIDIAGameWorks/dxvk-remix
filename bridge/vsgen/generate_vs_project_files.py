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

import os
import sys

from bridge_project import generate_bridge_project
from testcase_project import generate_testcase_project
from sln import generate_sln
from vsutil import *

# usage: generate_vs_project_files.py <bridge-cpp-defines> <output-path-1>,<executable-1.exe> <output-path-2>,<executable-2.exe> ...
# <bridge-cpp-defines> is a list of CPP defs separated by semicolons: VAR1=abc;VAR2;VAR3=2
# can be empty (or a single semicolon), but must be present

vcxproj_output_dir = "../_vs"

os.chdir(os.path.dirname(os.path.realpath(__file__)))

if not os.path.exists(vcxproj_output_dir):
    os.mkdir(vcxproj_output_dir)

bridge_cpp_defines = sys.argv[1]
test_case_projects = []

games = load_game_targets()
for g in games:
    project = "Games/" + g
    commandline = games[g]['commandline']
    working_dir = games[g]['workingdir']
    output_dir = games[g]['outputdir']
    generate_testcase_project(vcxproj_output_dir, project, commandline, output_dir, working_dir)
    test_case_projects.append(project)

generate_bridge_project(vcxproj_output_dir, bridge_cpp_defines)
generate_sln(vcxproj_output_dir, test_case_projects)
