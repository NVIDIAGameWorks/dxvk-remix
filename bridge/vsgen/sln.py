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

# usage: generate_sln.py <path/to/output.sln> <project1_folder/project1_name> <project2_folder/project2_name> ...

nmake_project_type_guid = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"
folder_project_type_guid = "2150E333-8FDC-42A3-9474-1A3956D46DE8"

header_template = Template("""
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.1.32210.238
MinimumVisualStudioVersion = 10.0.40219.1
Project("{$nmake_project_type_guid}") = "$bridge_project_name", "${bridge_project_name}.vcxproj", "{$bridge_project_guid}"
EndProject
""")

test_project_template = Template("""Project("{$nmake_project_type_guid}") = "$project_name", "$project_file_name.vcxproj", "{$project_guid}"
	ProjectSection(ProjectDependencies) = postProject
		{$bridge_project_guid} = {$bridge_project_guid}
	EndProjectSection
EndProject
""")

folder_project_template = Template("""Project("{$folder_project_type_guid}") = "$folder_name", "$folder_name", "{$folder_guid}"
EndProject
""")

global_header = """
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
        Debug|x64 = Debug|x64
        Debug|x86 = Debug|x86
        DebugOptimized|x64 = DebugOptimized|x64
        DebugOptimized|x86 = DebugOptimized|x86
        Release|x64 = Release|x64
        Release|x86 = Release|x86
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution"""

global_project_section_template = Template("""
		{$project_guid}.Debug|x64.ActiveCfg = Debug|x64
		{$project_guid}.Debug|x64.Build.0 = Debug|x64
		{$project_guid}.DebugOptimized|x64.ActiveCfg = DebugOptimized|x64
		{$project_guid}.DebugOptimized|x64.Build.0 = DebugOptimized|x64
		{$project_guid}.Release|x64.ActiveCfg = Release|x64
		{$project_guid}.Release|x64.Build.0 = Release|x64
		{$project_guid}.Debug|x86.ActiveCfg = Debug|Win32
		{$project_guid}.Debug|x86.Build.0 = Debug|Win32
		{$project_guid}.DebugOptimized|x86.ActiveCfg = DebugOptimized|Win32
		{$project_guid}.DebugOptimized|x86.Build.0 = DebugOptimized|Win32
		{$project_guid}.Release|x86.ActiveCfg = Release|Win32
		{$project_guid}.Release|x86.Build.0 = Release|Win32""")
        
nested_project_header = """
	EndGlobalSection
	GlobalSection(SolutionProperties) = preSolution
		HideSolutionNode = FALSE
	EndGlobalSection
	GlobalSection(NestedProjects) = preSolution
"""

nested_project_template = Template("		{$project_guid} = {$folder_guid}\n")

footer_template = Template("""	EndGlobalSection
	GlobalSection(ExtensibilityGlobals) = postSolution
		SolutionGuid = {$solution_guid}
	EndGlobalSection
EndGlobal""")

output_file = "bridge-remix.sln"
old_output_file = "bridge.sln"


def generate_sln(output_root_path, test_cases):
	solution_guid = generate_guid(output_file)
	bridge_project_guid = generate_guid(bridge_project_name)
    
    # list of tuples (project_name, project_guid, folder_guid)
	projects = []
	# dict of folder_name to guid
	folders = {}
	for test_case in test_cases:
		folder_name = test_case.split('/')[0]
		folder_guid = generate_guid(folder_name)
		folders[folder_name] = folder_guid
		project_name = test_case.removeprefix("Games/").replace("/", "_")
		project_file_name = project_name
		project_guid = generate_guid(project_file_name)
		t = (project_name, project_file_name, project_guid, folder_guid)
		projects.append(t)

	output_data = header_template.safe_substitute(
		nmake_project_type_guid=nmake_project_type_guid,
		bridge_project_name=bridge_project_name,
		bridge_project_guid=bridge_project_guid)

	for project_name, project_file_name, project_guid, folder_guid in projects:
		output_data += test_project_template.safe_substitute(
			nmake_project_type_guid=nmake_project_type_guid,
			project_name=project_name,
			project_file_name=project_file_name,
			project_guid=project_guid,
			bridge_project_guid=bridge_project_guid)

	for folder_name in folders:
		folder_guid = generate_guid(folder_name)
		output_data += folder_project_template.safe_substitute(
			folder_project_type_guid=folder_project_type_guid,
			folder_name=folder_name,
			folder_guid=folder_guid)
            
	output_data +=  global_header

	output_data += global_project_section_template.safe_substitute(
		project_guid=bridge_project_guid)

	for project_name, project_file_name, project_guid, folder_guid in projects:
		output_data += global_project_section_template.safe_substitute(
			project_guid=project_guid)
            
	output_data += nested_project_header

	for project_name, project_file_name, project_guid, folder_guid in projects:
		output_data += nested_project_template.safe_substitute(
			project_guid=project_guid,
			folder_guid=folder_guid)
            
	output_data += footer_template.safe_substitute(
		solution_guid=solution_guid)

	if check_if_file_exists(output_root_path, old_output_file):
		print(
			'Error: solution file changed from bridge.sln to bridge-remix.sln --- please delete _vs/bridge.sln and rebuild')
		sys.exit(1)

	write_file_if_not_identical(output_root_path, output_file, output_data)
