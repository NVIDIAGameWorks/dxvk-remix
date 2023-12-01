#!/usr/bin/python3

import os
import sys
from string import Template

from vsutil import *

header_exts = [ ".h" ]
src_exts = [ ".cpp", ".c", ".build", ".conf" ]
shader_exts = [ ".comp", ".rgen", ".rchit", ".rmiss", ".frag", ".vert", ".geom", ".slang", ".slangh" ]
all_exts = header_exts + src_exts + shader_exts

def generate_dxvk_project(output_root_path, dxvk_cpp_defines):
    tree = { }
    vcxproj_file_references = []
    vcxproj_include_paths = {
            "../include": 1,
            "../include/vulkan/include": 1,
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
    # process_tree("../include", False)
    process_tree("../external", True)
    # add_file("..", "meson.build")
    # add_file("..", "dxvk.conf")

    # generate vcxproj

    # list of locations in the build directory where headers show up
    # this might change over time...
    build_output_search_paths = [ ".", "src/d3d9/d3d9.dll.p", "src/dxvk/libdxvk.a.p", "src/dxvk/rtx_shaders", "src/dxvk" ]

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

    include_search_path_debug = build_search_path("../_Comp64Debug")
    include_search_path_debugoptimized = build_search_path("../_Comp64DebugOptimized")
    include_search_path_release = build_search_path("../_Comp64Release")

    file_references = ""
    for ref in vcxproj_file_references:
        file_references += "    <ClCompile Include=\"" + pathsep_to_backslash(ref) + "\" />\n"

    # add a couple of interesting files at the root
    for f in [ "meson.build", "dxvk.conf" ]:
        file_references += "    <ClCompile Include=\"" + pathsep_to_backslash("../" + f) + "\" />\n"

    project_template = Template(open("dxvk-remix.vcxproj.template", "rt").read())
    data = project_template.safe_substitute(
        dxvk_remix_project_guid=dxvk_remix_guid(),
        dxvk_cpp_defines=dxvk_cpp_defines,
        include_search_path_debug=include_search_path_debug,
        include_search_path_debugoptimized=include_search_path_debugoptimized,
        include_search_path_release=include_search_path_release,
        file_references=file_references)

    write_file_if_not_identical(output_root_path, "dxvk-remix.vcxproj", data)

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
    if write_file_if_not_identical(output_root_path, "dxvk-remix.vcxproj.filters", data):
        print("Generated dxvk-remix.vcxproj.filters")
