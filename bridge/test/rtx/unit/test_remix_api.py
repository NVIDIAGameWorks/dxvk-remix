import argparse
import os
import sys
import subprocess


parser = argparse.ArgumentParser(allow_abbrev=False)
parser.add_argument("--x86_2_x64", default=False, action="store_true", required=False)
parser.add_argument("--x64_2_x86", default=False, action="store_true", required=False)
parser.add_argument("--executablesDir", default=os.path.join("."), required=False)
parser.add_argument("--artifactDir", default=os.path.join(".", "_test_remix_api"), required=False)
args = parser.parse_args()

log_f = None

def x86_2_x64() -> bool:
    write_x86_exe = "test_remix_api_write_x86.exe"
    read_x64_exe = "test_remix_api_read_x64.exe"
    x86_written_bin = "test_remix_api_x86_written.bin"
    return x_2_x_template(write_x86_exe, read_x64_exe, x86_written_bin)

def x64_2_x86() -> bool:
    write_x64_exe = "test_remix_api_write_x64.exe"
    read_x86_exe = "test_remix_api_read_x86.exe"
    x64_written_bin = "test_remix_api_x64_written.bin"
    return x_2_x_template(write_x64_exe, read_x86_exe, x64_written_bin)

def x_2_x_template(write_exe, read_exe, output_bin):
    write_exe_path = os.path.abspath(os.path.join(args.executablesDir, write_exe))
    if not check_exe_existence(write_exe, write_exe_path):
        return False
    read_exe_path = os.path.abspath(os.path.join(args.executablesDir, read_exe))
    if not check_exe_existence(read_exe, read_exe_path):
        return False
    bin_path = os.path.abspath(os.path.join(args.artifactDir, output_bin))
    return serialize_write_then_read(write_exe_path,
                                     read_exe_path,
                                     bin_path)

def check_exe_existence(exeName : str, path : str) -> bool:
    if not os.path.exists(path):
        print("Cannot find {} at: {}".format(exeName, path), file=log_f)
        return False
    return True

def serialize_write_then_read(write_exe : os.path,
                              read_exe : os.path,
                              bin : os.path) -> False:

    write_cmd = "{} {}".format(write_exe, bin)
    print(write_cmd, file = log_f)
    p = subprocess.Popen(write_cmd, stdout=log_f, stderr=log_f)
    write_return_code = p.wait()
    if write_return_code != 0:
        print("FAILED with return code {}".format(str(write_return_code)), file = log_f)
        return False
    read_cmd = "{} {}".format(read_exe, bin)
    print(read_cmd, file = log_f)
    p = subprocess.Popen(read_cmd, stdout=log_f, stderr=log_f)
    read_return_code = p.wait()
    if read_return_code != 0:
        print("FAILED with return code {}".format(str(read_return_code)))
        return False
    
    print("SUCCESS", file = log_f)
    return True

if __name__ == "__main__" :
    if not os.path.isdir(args.artifactDir):
        os.makedirs(args.artifactDir, exist_ok = True)

    bPassed = True
    if args.x86_2_x64:
        log_path = os.path.join(args.artifactDir, "x86_2_x64.log")
        log_f = open(log_path,'w')
        print("TEST: x86_2_x64", file = log_f)
        bPassed = bPassed and x86_2_x64()
        log_f.close()
    if args.x64_2_x86:
        log_path = os.path.join(args.artifactDir, "x64_2_x86.log")
        log_f = open(log_path,'w')
        print("TEST: x64_2_x86", file = log_f)
        bPassed = bPassed and x64_2_x86()
        log_f.close()

    os._exit(0 if bPassed else 1)