"""
generate_and_patch_schema.py

Generate USD schema sources with usdGenSchema, then patch plugInfo.json
with plugin-specific keywords—all in one step.

Usage:
    generate_and_patch_schema.py \
        --schema-usda path/to/schema.usda \
        --outdir build/output/dir \
        --plugin-name MyPlugin \
        [--generated-file extra1.cpp --generated-file extra2.h ...]

Environment variables required:
    USD_SCHEMA_PYTHON                Path to the Python interpreter used by USD
    USD_NVUSD_BINDIR                 Directory containing the usdGenSchema binary
    USD_LIBPYTHON_DIR                Directory containing the pxr Python modules
    USD_LIBPYTHON_DIR                Directory for USD Python modules
"""
import argparse
import json
import os
import sys
import runpy

def replace_in_obj(obj, repl):
    if isinstance(obj, dict):
        return {k: replace_in_obj(v, repl) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [replace_in_obj(v, repl) for v in obj]
    elif isinstance(obj, str):
        s = obj
        for key, val in repl.items():
            placeholder = f'@{key}@'
            s = s.replace(placeholder, val)
        return s
    else:
        return obj

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema-usda',     required=True, help='Path to the .usda schema file')
    parser.add_argument('--outdir',          required=True, help='Directory to emit generated files')
    parser.add_argument('--plugin-name',     required=True, help='Name of your plugin (no extension)')
    parser.add_argument('--generated-file',  dest='generated_files', action='append', help='Additional generated files (optional)')
    args = parser.parse_args()

    # Gather environment and paths
    usd_py   = os.environ['USD_SCHEMA_PYTHON']
    usd_bind = os.environ['USD_NVUSD_BINDIR']
    libpy    = os.environ['USD_LIBPYTHON_DIR']
    nvlib    = os.environ.get('USD_NVUSD_LIBDIR', '')

    # Ensure Python can locate USD Python modules without PYTHONPATH
    sys.path.insert(0, libpy)

    # add DLL search directory so usdGenSchema native libs load
    os.add_dll_directory(nvlib)

    # Run the usdGenSchema script in-process via runpy
    usd_gen = os.path.join(usd_bind, 'usdGenSchema')
    print(f"Running usdGenSchema: {usd_gen} {args.schema_usda} {args.outdir}")

    # Temporarily override sys.argv so the script sees the correct parameters
    old_argv = sys.argv
    sys.argv = [usd_gen, args.schema_usda, args.outdir]
    try:
        # It will `import pxr` using our patched sys.path
        runpy.run_path(usd_gen, run_name='__main__', init_globals={
            '__file__': usd_gen,
            # make sure interpreter matches USD_SCHEMA_PYTHON
            '__loader__': None
        })
    finally:
        sys.argv = old_argv

    # patch plugInfo.json, stripping leading comments
    pluginfo = os.path.join(args.outdir, 'plugInfo.json')
    with open(pluginfo, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    start = next(i for i, l in enumerate(lines) if not l.lstrip().startswith('#'))
    data = json.loads(''.join(lines[start:]))

    repl = {
        'PLUG_INFO_LIBRARY_PATH': f"{args.plugin_name}.dll",
        'PLUG_INFO_NAME':         args.plugin_name,
        'PLUG_INFO_RESOURCE_PATH': '.',
        'PLUG_INFO_ROOT':         './../'
    }
    patched = replace_in_obj(data, repl)

    with open(pluginfo, 'w', encoding='utf-8') as f:
        json.dump(patched, f, indent=2, ensure_ascii=False)
        f.write('\n')

    return 0

if __name__ == '__main__':
    sys.exit(main())
