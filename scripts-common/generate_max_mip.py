# Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

import numpy as np
from PIL import Image
import os
import math
from pathlib import Path
import argparse

parser = argparse.ArgumentParser(
    description="Script to calculate mipmaps using the maximum value instead of the average value.\nOutput will be `<inputTexture>_maxmip.dds`"
)
parser.add_argument('inputTexture', help="A PNG or DDS file to be processed.  DDS files will be re-compressed, so it's better to use the original source png.")
parser.add_argument(
    '--nvttPath',
    help="If Nvtt_export is installed to a non-default location, the path to it can be passed in here.",
    default='"C:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/nvtt_export.exe"'
)
parser.add_argument(
    '-o', '--outputTexture',
    help="Output dds path.  Defaults to inputTexture + `_mipmap.dds`",
    default=""
)
args = parser.parse_args()
NVTTPath = args.nvttPath
inputPath = Path(args.inputTexture)
outputDds = args.outputTexture


# adapted from https://stackoverflow.com/questions/14549696/mipmap-of-image-in-numpy
def generate_max_mip_level(image):
    rows, cols, channel = image.shape
    if rows == 1:
        image = image.reshape(rows, cols // 2, 2, channel)
        image = image.max(axis=2)
    elif cols == 1:
        image = image.reshape(rows // 2, 2, cols, channel)
        image = image.max(axis=1)
    else:
        image = image.reshape(rows // 2, 2, cols // 2, 2, channel)
        image = image.max(axis=3).max(axis=1)
    return image.astype('uint8')


def generate_max_mip(image):
    img = image.copy()
    rows, cols, channel = image.shape
    result = np.zeros((rows, cols * 2 - 1, channel), dtype='uint8')
    result[:, :cols, :] = img
    col = cols
    while rows > 1 or cols > 1:
        img = generate_max_mip_level(img)
        rows = img.shape[0]
        cols = img.shape[1]
        result[0:rows, col:col + cols, :] = img
        col += cols
    return result


inputAsPng = inputPath
if inputPath.suffix == ".dds":
    inputAsPng = Path(inputPath).with_suffix(".png")

    ddsToPngCommand = f'{NVTTPath} --no-mips -o {inputAsPng} {inputPath}'
    print("running: " + ddsToPngCommand)
    os.system(ddsToPngCommand)

outputPng = inputPath.with_stem(inputPath.stem + "_maxmip").with_suffix(".png")

if outputDds is None or outputDds == "":
    outputDds = inputPath.with_stem(inputPath.stem + "_maxmip").with_suffix(".dds")

img = np.asarray(Image.open(inputAsPng))
print(f"processing texture with dimensions {img.shape}")
Image.fromarray(generate_max_mip(img)).save(outputPng)
rows, cols, channel = img.shape
levels = int(max(math.log2(rows), math.log2(cols)))+1

PngToDdsCommand = f'{NVTTPath} -f bc4 --no-mip-gamma-correct --mip-extract-from-atlas --no-mips --atlas-mips {levels} -o {outputDds} {outputPng}'
print("running: " + PngToDdsCommand)
os.system(PngToDdsCommand)
print(f"generated file {outputDds}")
