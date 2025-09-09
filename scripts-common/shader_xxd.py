import argparse
import os

parser = argparse.ArgumentParser(description = 'Convert SPV to C header')
parser.add_argument('--input', '-i', help = "Input SPV", required=True)
parser.add_argument('--output', '-o', help = "Output header", required=True)
args = parser.parse_args()

def xxd_c_array(data, output_filename):
    # array name is the file name, extract filename without extension from basename
    output_basename = os.path.basename(output_filename)
    array_name = output_basename.removesuffix('.h')

    # calculate padding from byte array length
    length = len(data)
    padding = (4 - (length % 4)) % 4
    data += b'\x00' * padding

    with open(output_filename, 'w', encoding='utf-8') as f:
        f.write(f"const uint32_t {array_name}[] = {{")

        for i in range(0, len(data), 4):
            if (i // 4) % 8 == 0: # 8 columns
                f.write('\n    ')
            val = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24)
            f.write(f'0x{val:08x}, ')

        f.write("\n};\n\n")
        f.write(f"const size_t {array_name}_sizeInBytes = {length};\n")

if not os.path.exists(args.input):
    print(args.input + " file not found.")
    exit(1)

f = open(args.input, 'rb')
xxd_c_array(f.read(), args.output)