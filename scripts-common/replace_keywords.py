import json, sys, os

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
    prog = os.path.basename(sys.argv[0])
    if len(sys.argv) < 4:
        print(f"Usage: {prog} <in.json> <out.json> KEY=VAL [KEY=VAL...]", file=sys.stderr)
        sys.exit(1)

    in_path, out_path = sys.argv[1], sys.argv[2]
    # parse KEY=VAL pairs
    repl = {}
    for kv in sys.argv[3:]:
        if '=' not in kv:
            print(f"Invalid replacement token: '{kv}', must be KEY=VAL", file=sys.stderr)
            sys.exit(1)
        key, val = kv.split('=', 1)
        repl[key] = val

    # Read and strip leading comment lines
    with open(in_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    comment_lines = []
    json_start = 0
    for i, line in enumerate(lines):
        if line.lstrip().startswith('#'):
            comment_lines.append(line)
        else:
            json_start = i
            break
    json_text = ''.join(lines[json_start:])

    # Parse JSON
    data = json.loads(json_text)

    # Replace tokens
    patched = replace_in_obj(data, repl)

    # Write out comments + patched JSON
    with open(out_path, 'w', encoding='utf-8') as f:
        for cl in comment_lines:
            f.write(cl)
        json.dump(patched, f, indent=2, ensure_ascii=False)
        f.write('\n')

if __name__ == '__main__':
    main()
