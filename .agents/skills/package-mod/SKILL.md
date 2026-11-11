---
name: package-mod
description: Packages a USD mod to USDG format. Use when user asks to package a mod.
argument-hint: "[usd-file-path]"
---

# Package mod

All commands run in PowerShell from the repo root.

## Prerequisites

The `_output\usdg-converter.exe` tool must exist, if it does not - invoke `build-remix` skill and build `release` configuration first.

## Tool commands

'pack' - package a USD into USDG.

Pack command options:

`-c` - remove all USD geometry attributes that have been processed and optimized for GPU.
`-i N` - process images and compress using N compression level. 0 for store, 12 for maximum compression, -1 to skip image processing.
`-g N` - process geometry and compress using N compression level. 0 for store, 12 for maximum compression, -1 to skip image processing.
`-ip MB` - place the images into separate packages split to MB size.
`-gp MB` - place the geometry into separate packages split to MB size.
`-b N` - Maximum bone influence count per vertex. Default - 4.
`-binary` - force convert the output USD to binary form for faster loading.

## Package config

If the skill argument is empty, ask user to specify a `.usd` or `.usda` file to package. Check if the provided file is located in the root of a mod - the folder where the file is located must also contain `mod.usd` or `mod.usda` file.
Ask if user wants to use the default configuration or not. If not, ask the questions below.
Ask if user wants to package the images. If yes, ask if the images must NOT be packaged separately into `.pkg` files.
Ask user if he wants to set advanced config options. If yes, ask if user wants to: set compression levels for geometry and images, retain the original geometry data, retain the original USD formats, set image package split threshold in MB.

## Packaging

### Default configuration

```powershell
_output\usdg-converter.exe pack -c -i 8 -g 3 -ip 4096 -gp 1024 -binary <path_to_usd_file_provided>
```

### User configuration

Display the configuration.

If user does not want images to be packages separately, remove `-ip N` option from the default configuration.
If user set compression level for images, modify the number after the `-i` option in the default config.
If user set compression level for geometry, modify the number after the `-g` option in the default config.
If user wants to retain the original geometry, remove `-c` option from the default config.
If user wants to retain the original formats, remove `-binary` option from the default config.

Display the resulting command line and ask if user wants to execute it.

### Output error checking

Do not be concerned with the messages from USD runtime, only analyze the log lines tagged with `err:` and `warn:`.
Do do not display any information about the "Could not load sublayer" messages when `-binary` option was specified.

## Deploy package

After a successeful packaging ask if user wants to deploy the resulting package. If yes, ask for destination folder.
If the package filename is not `mod.usdg`, check if the destination folder already has `mod.usda` file and. If it does not, copy the `mod.usda` from source folder to destination.
Move all packaged output files (do not miss the `.usdg.XY` files!) from the source folder to the destination folder with replacing. If the package filename is not `mod.usdg`, modify the text `mod.usda` file in the destination so that it references the newly create `.usdg` package instead of `.usd` or `.usda` files. Show the text difference and ask user for confirmation.
If the destination folder has `.usd` or `.usda` file with the USDG package name, tell the user and then set the file extension to `.not_used`.
