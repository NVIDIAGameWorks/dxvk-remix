#!/usr/bin/python3

import os
import sys

from dxvk_project import generate_dxvk_project
from testcase_project import generate_testcase_project
from sln import generate_sln
from vsutil import *

# usage: generate_vs_project_files.py <dxvk-cpp-defines> <output-path-1>,<executable-1.exe> <output-path-2>,<executable-2.exe> ...
# <dxvk-cpp-defines> is a list of CPP defs separated by semicolons: VAR1=abc;VAR2;VAR3=2
# can be empty (or a single semicolon), but must be present

vcxproj_output_dir = "../_vs"

os.chdir(os.path.dirname(os.path.realpath(__file__)))

if not os.path.exists(vcxproj_output_dir):
    os.mkdir(vcxproj_output_dir)

dxvk_cpp_defines = sys.argv[1]
test_case_projects = []
for a in sys.argv[2:]:
    project, exe = tuple(str.split(a, ','))
    generate_testcase_project(vcxproj_output_dir, project, exe, None, None)
    test_case_projects.append(project)

games = load_game_targets()
for g in games:
    project = "Games/" + g
    commandline = games[g]['commandline']
    working_dir = games[g]['workingdir']
    output_dir = games[g]['outputdir']

    generate_testcase_project(vcxproj_output_dir, project, commandline, output_dir, working_dir)
    test_case_projects.append(project)

generate_dxvk_project(vcxproj_output_dir, dxvk_cpp_defines)
generate_sln(vcxproj_output_dir, test_case_projects)
