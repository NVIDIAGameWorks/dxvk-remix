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
from string import Template

from vsutil import *

header_exts = [ ".h" ]
src_exts = [ ".cpp", ".c", ".build", ".conf" ]
all_exts = header_exts + src_exts

def generate_bridge_project(output_root_path, bridge_cpp_defines):
    tree = { }
    vcxproj_file_references = []
    vcxproj_include_paths = {
        "../ext": 1,
        "../public/include": 1,
    }
    
    def add_file(dirpath, filename):
        tree[dirpath].append(filename)
        vcxproj_file_references.append(os.path.join(dirpath, filename))

    def process_file(dirpath, filename, external):
        dirpath = pathsep_to_slash(dirpath)
        ext = os.path.splitext(filename)[1]

        if not external:
            if dirpath not in tree:
                tree[dirpath] = []

            if ext not in all_exts:
                return

            add_file(dirpath, filename)

        if ext in header_exts:
            # our include paths are messy, we may include stuff from any point in the dirpath
            path = dirpath
            while path != "" and path != "..":
                if path not in vcxproj_include_paths:
                    vcxproj_include_paths[path] = 1

                path, tail = os.path.split(path)
                
    def process_tree(root, external):
        for dirpath, dirnames, files in os.walk(root, followlinks=True):
            # project file is one dir above, adjust here so that relative paths point at the right place
            # dirpath = os.path.relpath(dirpath, "..")

            for f in files:
                process_file(dirpath, f, external)
    process_tree("../src", False)
    process_tree("../external", True)

    # generate vcxproj

    # list of locations in the build directory where headers show up
    # this might change over time...

    build_output_search_paths = [ "../src" ]
    
    def build_search_path(build_dir):
        # list of build directory paths with headers in them
        # (these need to come first due to header naming conflicts with external libs)
        p = [os.path.join(build_dir, x) for x in build_output_search_paths]
        # list of source paths with headers in them
        p += list(vcxproj_include_paths.keys())
        # everything to backslash
        p = [pathsep_to_backslash(x) for x in p]
        # convert to string with ';' as separator
        return ';'.join(p)

    include_search_path_debug_32 = build_search_path("../_compDebug_x86")
    include_search_path_debug_64 = build_search_path("../_compDebug_x64")

    include_search_path_debugoptimized_32 = build_search_path("../_compDebugOptimized_x86")
    include_search_path_debugoptimized_64 = build_search_path("../_compDebugOptimized_x64")

    include_search_path_release_32 = build_search_path("../_compRelease_x86")
    include_search_path_release_64 = build_search_path("../_compRelease_x64")

    file_references = ""
    for ref in vcxproj_file_references:
        file_references += "    <ClCompile Include=\"" + pathsep_to_backslash(ref) + "\" />\n"

    # add a couple of interesting files at the root
    for f in [ "meson.build" ]:
        file_references += "    <ClCompile Include=\"" + pathsep_to_backslash("../" + f) + "\" />\n"

    project_template = Template(open("bridge-remix.vcxproj.template", "rt").read())
        
    data = project_template.safe_substitute(
        bridge_remix_project_guid=generate_guid(bridge_project_name),
        bridge_cpp_defines=bridge_cpp_defines,
        include_search_path_debug_32=include_search_path_debug_32,
        include_search_path_debug_64=include_search_path_debug_64,
        include_search_path_debugoptimized_32=include_search_path_debugoptimized_32,
        include_search_path_debugoptimized_64=include_search_path_debugoptimized_64,
        include_search_path_release_32=include_search_path_release_32,
        include_search_path_release_64=include_search_path_release_64,
        file_references=file_references)

    project_name = bridge_project_name + ".vcxproj"
    write_file_if_not_identical(output_root_path, project_name, data)

    # generate vcxproj.filters
    filters_file_template = Template("""<?xml version="1.0" encoding="utf-8"?>
    <Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
    <ItemGroup>
    $filters
    </ItemGroup>
    <ItemGroup>
    $file_references
    </ItemGroup>
    </Project>
    """)

    filter_template = Template("""    <Filter Include="$filter_name">
        <UniqueIdentifier>{$filter_guid}</UniqueIdentifier>
        </Filter>
    """)
    reference_template = Template("""    <ClCompile Include="$path">
        <Filter>$filter_name</Filter>
        </ClCompile>
    """)

    filters = ""
    references = ""
    for path in tree:
        filter_name = pathsep_to_backslash(path[3:])
        filters += filter_template.safe_substitute(filter_name=filter_name, filter_guid=generate_guid(filter_name))
        
        for filename in tree[path]:
            fileref = pathsep_to_backslash(path + "/" + filename)
            references += reference_template.safe_substitute(path=fileref, filter_name=filter_name)

    data = filters_file_template.safe_substitute(filters=filters, file_references=references)
    project_filter_name = bridge_project_name + ".vcxproj.filters"
    if write_file_if_not_identical(output_root_path, project_filter_name, data):
        print("Generated " + project_filter_name)
