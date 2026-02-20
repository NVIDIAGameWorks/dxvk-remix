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
    USD_NVUSD_LIBDIR                 Directory for USD native libs (optional)
"""
import argparse
import json
import os
import sys
import runpy
import logging

def replace_in_obj(obj, repl):
    if isinstance(obj, dict):
        return {k: replace_in_obj(v, repl) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [replace_in_obj(v, repl) for v in obj]
    elif isinstance(obj, str):
        s = obj
        for key, val in repl.items():
            placeholder = f'@{key}@'
            if placeholder in s:
                logging.debug(f"Replacing placeholder {placeholder} with {val} in string {s}")
            s = s.replace(placeholder, val)
        return s
    else:
        return obj


def restore_property_custom_data(source_schema_path, generated_schema_path):
    """usdGenSchema strips property-level customData.  This copies it from
    the source schema.usda back into the generated generatedSchema.usda
    using the Sdf.PropertySpec.customData API."""
    from pxr import Sdf

    src_layer = Sdf.Layer.FindOrOpen(source_schema_path)
    gen_layer = Sdf.Layer.FindOrOpen(generated_schema_path)
    if not src_layer or not gen_layer:
        logging.warning("Could not open layers for customData restoration")
        return

    patched_count = 0

    def _traverse(prim_path):
        nonlocal patched_count
        src_prim = src_layer.GetPrimAtPath(prim_path)
        gen_prim = gen_layer.GetPrimAtPath(prim_path)
        if not src_prim or not gen_prim:
            return

        for src_prop_spec in src_prim.properties:
            if not src_prop_spec.customData:
                continue
            gen_prop = gen_layer.GetPropertyAtPath(
                prim_path.AppendProperty(src_prop_spec.name))
            if gen_prop:
                gen_prop.customData = dict(src_prop_spec.customData)
                patched_count += 1

        for child_spec in gen_prim.nameChildren:
            _traverse(prim_path.AppendChild(child_spec.name))

    for root_spec in gen_layer.rootPrims:
        _traverse(Sdf.Path.absoluteRootPath.AppendChild(root_spec.name))

    if patched_count:
        gen_layer.Save()
        logging.info(f"Restored customData on {patched_count} properties in generatedSchema.usda")
    else:
        logging.info("No property customData to restore")

def main():
    logging.basicConfig(
        level=logging.DEBUG,
        format='[%(asctime)s] %(levelname)s: %(message)s',
        datefmt='%H:%M:%S',
        stream=sys.stdout      # send logs to stdout
    )
    logging.info("Starting generate_and_patch_schema.py")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema-usda',     required=True, help='Path to the .usda schema file')
    parser.add_argument('--outdir',          required=True, help='Directory to emit generated files')
    parser.add_argument('--plugin-name',     required=True, help='Name of your plugin (no extension)')
    parser.add_argument('--generated-file',  dest='generated_files', action='append', help='Additional generated files (optional)')
    args = parser.parse_args()
    logging.debug(f"Parsed arguments: {args}")

    # Gather environment and paths
    try:
        usd_py   = os.environ['USD_SCHEMA_PYTHON']
        usd_bind = os.environ['USD_NVUSD_BINDIR']
        libpy    = os.environ['USD_LIBPYTHON_DIR']
        nvlib    = os.environ.get('USD_NVUSD_LIBDIR', '')
        logging.debug(f"USD_SCHEMA_PYTHON={usd_py}")
        logging.debug(f"USD_NVUSD_BINDIR={usd_bind}")
        logging.debug(f"USD_LIBPYTHON_DIR={libpy}")
        logging.debug(f"USD_NVUSD_LIBDIR={nvlib}")
    except KeyError as e:
        logging.error(f"Missing required environment variable: {e}")
        sys.exit(1)

    # Prepare Python path and DLL search path
    logging.info("Injecting USD Python modules into sys.path")
    sys.path.insert(0, libpy)
    if nvlib:
        logging.info(f"Adding native lib directory for DLLs: {nvlib}")
        try:
            os.add_dll_directory(nvlib)
        except Exception as e:
            logging.warning(f"Failed to add DLL directory {nvlib}: {e}")

    # remove plugInfo.json if it exists, to avoid problems in usdGenSchema if the schema changes
    if os.path.isfile(os.path.join(args.outdir, 'plugInfo.json')):
        logging.info(f"Removing existing plugInfo.json at {args.outdir}")
        os.remove(os.path.join(args.outdir, 'plugInfo.json'))
        
    # Run the usdGenSchema script in-process
    usd_gen = os.path.join(usd_bind, 'usdGenSchema')
    logging.info(f"Running usdGenSchema script: {usd_gen}")
    logging.debug(f"Command-line args for usdGenSchema: {args.schema_usda} {args.outdir}")

    old_argv = sys.argv
    sys.argv = [usd_gen, args.schema_usda, args.outdir]
    try:
        # It will `import pxr` using our patched sys.path
        runpy.run_path(usd_gen, run_name='__main__', init_globals={
            '__file__': usd_gen,
            # make sure interpreter matches USD_SCHEMA_PYTHON
            '__loader__': None
        })
        logging.info("usdGenSchema completed successfully")
    except Exception as e:
        logging.exception(f"Error running usdGenSchema: {e}")
        sys.exit(1)
    finally:
        sys.argv = old_argv

    # Restore property-level customData that usdGenSchema strips.
    gen_schema = os.path.join(args.outdir, 'generatedSchema.usda')
    logging.info("Restoring property-level customData from source schema")
    try:
        restore_property_custom_data(args.schema_usda, gen_schema)
    except Exception as e:
        logging.warning(f"customData restoration failed (non-fatal): {e}")

    # patch plugInfo.json, stripping leading comments
    pluginfo = os.path.join(args.outdir, 'plugInfo.json')
    logging.info(f"Patching plugInfo.json: {pluginfo}")
    if not os.path.isfile(pluginfo):
        logging.error(f"plugInfo.json not found at {pluginfo}")
        sys.exit(1)

    try:
        with open(pluginfo, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        start = next(i for i, l in enumerate(lines) if not l.lstrip().startswith('#'))
        raw_json = ''.join(lines[start:])
        data = json.loads(raw_json)
        logging.debug(f"Loaded JSON data with keys: {list(data.keys())}")
    except Exception as e:
        logging.exception(f"Failed to read or parse plugInfo.json: {e}")
        sys.exit(1)

    repl = {
        'PLUG_INFO_LIBRARY_PATH': f"{args.plugin_name}.dll",
        'PLUG_INFO_NAME':         args.plugin_name,
        'PLUG_INFO_RESOURCE_PATH': '.',
        'PLUG_INFO_ROOT':         './../'
    }
    patched = replace_in_obj(data, repl)
    logging.debug("Applied placeholder replacements to JSON data")

    try:
        with open(pluginfo, 'w', encoding='utf-8') as f:
            json.dump(patched, f, indent=2, ensure_ascii=False)
            f.write('\n')
        logging.info("Successfully wrote patched plugInfo.json")
    except Exception as e:
        logging.exception(f"Failed to write patched plugInfo.json: {e}")
        sys.exit(1)

    logging.info("generate_and_patch_schema.py completed")
    return 0

if __name__ == '__main__':
    sys.exit(main())