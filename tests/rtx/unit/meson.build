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

test_unit_deps = [ dxvk_extradep, util_dep, tracy_dep ]

nomalloc = environment({'MALLOC_PERTURB_': '0'})

tests = []
exe = executable('fastop_minmax',  files('test_fastop_minmax.cpp'),  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_minmax', exe, env: nomalloc)
tests += exe

exe = executable('fastop_copysubtract',  files('test_fastop_copysubtract.cpp'),  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_copysubtract', exe, env: nomalloc)
tests += exe

exe = executable('fastop_parallelmemcpy',  files('test_fastop_parallelmemcpy.cpp'),  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('fastop_parallelmemcpy', exe, env: nomalloc)
tests += exe

exe = executable('util_threadpool',  files('test_util_threadpool.cpp'),  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('util_threadpool', exe, env: nomalloc, timeout: 60)
tests += exe

exe = executable('test_intersection_helper_sat',  files('test_intersection_helper_sat.cpp'), include_directories : test_include_path,  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_intersection_helper_sat', exe, env: nomalloc)
tests += exe

exe = executable('test_pnext',  files('test_pnext.cpp'), include_directories : remix_api_include_path,  dependencies : test_unit_deps, install : true, win_subsystem : 'console', override_options: ['cpp_std='+dxvk_cpp_std])
test('test_pnext', exe, env: nomalloc)
tests += exe

alias_target('unit_tests', tests)