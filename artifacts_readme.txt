Installation instructions:

1. Install a Remix release from https://github.com/NVIDIAGameWorks/rtx-remix
(Generally, the last release that came out before the build you are installing)

2. Copy and paste the files from this package into the `.trex/` folder
from that release.

Notes:

- You should be prompted to overwrite existing files - if you aren't, you're
probably putting the files in the wrong place.

- For x86 games, the files here should NOT be put next to the game's executable.
The d3d9.dll file next to the executable is the Remix Bridge, not DXVK-Remix.