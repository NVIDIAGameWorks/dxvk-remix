import sys

if len(sys.argv) != 2:
    print("ERROR: Must provide one and only one positional arg to echo.py", sys.stderr)
    exit(1)
print(sys.argv[1])
exit(0)