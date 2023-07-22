New-Item ".\_release" -Type Directory
Get-ChildItem -Path ".\_output" -Recurse -Filter *.pdb | Copy-Item
Get-ChildItem -Path ".\_output" -Recurse -Filter *.dll | Copy-Item
Get-ChildItem -Path ".\_output" -Recurse -Filter *.txt | Copy-Item
