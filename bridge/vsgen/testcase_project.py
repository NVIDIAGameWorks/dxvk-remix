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
from string import Template
import sys
import os

from vsutil import *

def generate_testcase_project(vcxproj_output_path, test_case, command_line, output_directory, working_directory):
    # game projects do not get the prefix in the name
    test_case = test_case.removeprefix("Games/")

    # separate executable from arguments
    s = command_line.split(' ')
    test_case_executable = s[0]
    if len(test_case_executable) > 1:
        test_case_commandline_arguments = ' '.join(s[1:])
    else:
        test_case_commandline_arguments = ''

    test_name = pathsep_to_underscore(test_case)
    vcxproj_target = test_name + ".vcxproj"
    
    test_project_guid=generate_guid(test_name)
    
    if not os.path.isabs(test_case_executable):
        test_case_executable = os.path.join(working_directory, test_case_executable)


    copy_target = "copy_" + test_name.replace("-", "_")
    copy_target_server = copy_target + "_server"
    copy_target_launcher = copy_target + "_launcher"
    
    projt = Template(open("testcase_project.vcxproj.template").read())
    d = projt.safe_substitute(test_project_guid=test_project_guid,
                              copy_target=copy_target,
                              copy_target_server=copy_target_server,
                              copy_target_launcher=copy_target_launcher,
                              bridge_project_name=bridge_project_name,
                              bridge_project_guid = generate_guid(bridge_project_name))

    write_file_if_not_identical(vcxproj_output_path, vcxproj_target, d)

    # don't touch user files if they already exist, otherwise fill them in
    user_target = os.path.join(vcxproj_output_path, vcxproj_target + ".user")
    if not os.path.exists(user_target):
        usert = Template(open("testcase_project.vcxproj.user.template").read())
        d = usert.safe_substitute(test_case_executable=test_case_executable.replace("&", "&amp;"),
                                  test_case_commandline_arguments=test_case_commandline_arguments,
                                  test_case_working_directory=working_directory)

        print(d, file=open(user_target, "wt"))
        print("Generated " + pathsep_to_backslash(user_target))

    smartcmdline_target = os.path.join(vcxproj_output_path, test_name + ".args.json")
    if not os.path.exists(smartcmdline_target):
        scmdt = Template(open("testcase_project.args.json.template").read())
        d = scmdt.safe_substitute(test_project_guid=test_project_guid,
                                  test_case_commandline_arguments=test_case_commandline_arguments)
        
        print(d, file=open(smartcmdline_target, "wt"))
        print("Generated " + pathsep_to_backslash(smartcmdline_target))
