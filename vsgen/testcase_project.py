#!/usr/bin/python3

import uuid
from string import Template
import sys
import os

from vsutil import *

def generate_testcase_project(vcxproj_output_path, test_case, command_line, output_directory = None, working_directory = None):
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
    vcxproj_target = test_name.removeprefix("apics_") + ".vcxproj"

    dxvk_remix_project_guid = dxvk_remix_guid()
    test_project_guid = generate_guid(test_name)
    if working_directory == None:
        working_directory = "$(SolutionDir)\\" + os.path.join("..\\tests\\rtx\\dxvk_rt_testing\\", pathsep_to_backslash(test_case))

    if not os.path.isabs(test_case_executable):
        test_case_executable = os.path.join(working_directory, test_case_executable)

    if output_directory == None:
        output_directory = working_directory

    projt = Template(open("testcase_project.vcxproj.template").read())
    d = projt.safe_substitute(test_project_guid=test_project_guid, 
                              test_case_outputdir=output_directory,
                              copy_target=test_name,
                              dxvk_remix_project_guid=dxvk_remix_project_guid)

    write_file_if_not_identical(vcxproj_output_path, vcxproj_target, d)

    # don't touch user files if they already exist, otherwise fill them in
    user_target = os.path.join(vcxproj_output_path, vcxproj_target + ".user")
    if not os.path.exists(user_target):
        usert = Template(open("testcase_project.vcxproj.user.template").read())
        d = usert.safe_substitute(test_case_executable=test_case_executable.replace("&", "&amp;"),
                                  test_case_commandline_arguments=test_case_commandline_arguments,
                                  test_case_rtxconf_directory=os.path.join(working_directory.replace("apics", "configs"), "rtx.conf").replace("\\\\", "\\"),
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
