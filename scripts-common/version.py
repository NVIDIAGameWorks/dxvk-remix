import sys
import subprocess

# Get name and version numbers
if len(sys.argv) != 2:
    print("VERSION_ERR_1")
    exit(0)
proj_version = sys.argv[1]

# Get git hash
git_rev_parse_proc = subprocess.run("git rev-parse --verify --short HEAD", capture_output=True)
if git_rev_parse_proc.returncode != 0:
    print("VERSION_ERR_2")
    exit(0)
git_hash = git_rev_parse_proc.stdout.decode('utf-8').replace('\n','')

# Check whether commit matches tagged version commit
git_tag_points_at_proc = subprocess.run("git tag --points-at " + git_hash, capture_output=True)
git_tag_points_at_output = git_tag_points_at_proc.stdout.decode('utf-8').replace('\n','')
b_found_tag = git_tag_points_at_proc.returncode == 0
b_same_version = False
if not b_found_tag:
    if git_tag_points_at_output.find('no tag exactly matches') == -1:
        print("VERSION_ERR_3")
        exit(0)
else:
    b_same_version = git_tag_points_at_output == proj_version

full_version = proj_version
if b_found_tag and not b_same_version:
    full_version += '+' + git_hash

print(full_version)
exit(0)