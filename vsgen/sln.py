#!/usr/bin/python3

import uuid
from string import Template
import sys
import os

from vsutil import *

# usage: generate_sln.py <path/to/output.sln> <project1_folder/project1_name> <project2_folder/project2_name> ...

# dxvk_remix_project_guid = "F9D37238-B68F-40CC-A509-5B02DC6FB609"
nmake_project_type_guid = "8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"
folder_project_type_guid = "2150E333-8FDC-42A3-9474-1A3956D46DE8"

header_template = Template("""
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.1.32210.238
MinimumVisualStudioVersion = 10.0.40219.1
Project("{$nmake_project_type_guid}") = "dxvk-remix", "dxvk-remix.vcxproj", "{$dxvk_remix_project_guid}"
EndProject
""")

test_project_template = Template("""Project("{$nmake_project_type_guid}") = "$project_name", "$project_file_name.vcxproj", "{$project_guid}"
	ProjectSection(ProjectDependencies) = postProject
		{$dxvk_remix_project_guid} = {$dxvk_remix_project_guid}
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
		DebugOptimized|x64 = DebugOptimized|x64
		Release|x64 = Release|x64
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution"""

global_project_section_template = Template("""
		{$project_guid}.Debug|x64.ActiveCfg = Debug|x64
		{$project_guid}.Debug|x64.Build.0 = Debug|x64
		{$project_guid}.DebugOptimized|x64.ActiveCfg = DebugOptimized|x64
		{$project_guid}.DebugOptimized|x64.Build.0 = DebugOptimized|x64
		{$project_guid}.Release|x64.ActiveCfg = Release|x64
		{$project_guid}.Release|x64.Build.0 = Release|x64""")

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

output_file = "dxvk-remix.sln"
old_output_file = "dxvk_rt.sln"

def generate_sln(output_root_path, test_cases):
	solution_guid = generate_guid(output_file)
	dxvk_remix_project_guid = dxvk_remix_guid()

	# list of tuples (project_name, project_guid, folder_guid)
	projects = []
	# dict of folder_name to guid
	folders = {}
	for test_case in test_cases:
		folder_name = test_case.removeprefix("apics/").split('/')[0]
		folder_guid = generate_guid(folder_name)
		folders[folder_name] = folder_guid

		project_name = test_case.removeprefix("apics/").removeprefix("Games/").replace("/", "_")
		project_file_name = project_name

		# guid_name = test_case.replace("/", "\\")
		project_guid = generate_guid(project_file_name)

		t = (project_name, project_file_name, project_guid, folder_guid)
		projects.append(t)

	output_data = header_template.safe_substitute(
		nmake_project_type_guid=nmake_project_type_guid,
		dxvk_remix_project_guid=dxvk_remix_project_guid)

	for project_name, project_file_name, project_guid, folder_guid in projects:
		output_data += test_project_template.safe_substitute(
			nmake_project_type_guid=nmake_project_type_guid,
			project_name=project_name,
			project_file_name=project_file_name,
			project_guid=project_guid,
			dxvk_remix_project_guid=dxvk_remix_project_guid)

	for folder_name in folders:
		folder_guid = generate_guid(folder_name)
		output_data += folder_project_template.safe_substitute(
			folder_project_type_guid=folder_project_type_guid,
			folder_name=folder_name,
			folder_guid=folder_guid)

	output_data += global_header

	output_data += global_project_section_template.safe_substitute(
		project_guid=dxvk_remix_project_guid)

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
		print('Error: solution file changed from dxvk_rt.sln to dxvk-remix.sln --- please delete _vs/dxvk_rt.sln and rebuild')
		sys.exit(1)

	write_file_if_not_identical(output_root_path, output_file, output_data)
