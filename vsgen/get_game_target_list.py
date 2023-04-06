#!/usr/bin/python3

import os
from vsutil import *

os.chdir(os.path.dirname(os.path.realpath(__file__)))

targets = load_game_targets()
for g in targets:
    print(g + "," + targets[g]['outputdir'])
