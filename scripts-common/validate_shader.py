import sys
import os
import re
import struct
import subprocess

remove_temp_file = False

def create_temp_file(shader_file):
    with open(shader_file, 'r') as f:
        shader_file_data = f.read()

    match = re.search(r'\{([^}]*)\}', shader_file_data, re.DOTALL)
    if not match:
        return ''

    array_text = match.group(1)
    array_text = re.sub(r'//.*', '', array_text)
    array_text = re.sub(r'/\*.*?\*/', '', array_text, flags=re.DOTALL)
    int_strings = [s.strip() for s in array_text.split(',') if s.strip()]
    int_values = [int(s, 0) for s in int_strings]

    spv_filename = shader_file + '.spv'
    with open(spv_filename, "wb") as out:
        out.write(struct.pack('<%dI' % len(int_values), *int_values))

    return spv_filename

def validate_shader(path_to_shaders, shader_file):
    if not os.path.exists(os.path.join(path_to_shaders, shader_file)):
        print(os.path.join(path_to_shaders, shader_file) + ' not found')
        return
    if shader_file.endswith('.h'):
        spv_file = create_temp_file(os.path.join(path_to_shaders, shader_file))
    else:
        spv_file = os.path.join(path_to_shaders, shader_file)
    if len(spv_file) > 0 and os.path.exists(spv_file):
        res = subprocess.Popen([spirv_val_path, '--scalar-block-layout', '--target-env', 'vulkan1.4', spv_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        stdout,stderr = res.communicate()
        if len(stdout) > 0 or len(stderr) > 0:
            print(spv_file)
            if len(stdout) > 0:
                print('SPIRV-val: ' + stdout)
            if len(stderr) > 0:
                print('SPIRV-val: ' + stderr)
            exit(1)
        if remove_temp_file:
            os.remove(spv_file)

if len(sys.argv) != 2:
    print(sys.argv[0] + ' <path to spv files>')
    exit(1)

# path to spirv-val provided by a build of spirv-tools in packman
spirv_val_path = os.path.join('..', 'external', 'spirv_tools', 'spirv-val.exe')
if not os.path.exists(spirv_val_path):
    print('spirv-val not found at ' + spirv_val_path)
    exit(1)

if sys.argv[1].endswith('.h') or sys.argv[1].endswith('.spv'):
    validate_shader('', sys.argv[1])
