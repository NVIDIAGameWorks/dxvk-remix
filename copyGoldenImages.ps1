function Usage {
  write-host "Usage:"
  write-host "  copyGoldenImages.ps1 <path_to_source_tests_folder>"
  write-host "  <path_to_source_tests_folder> - path must end with the word 'tests', i.e. e:\data\tests'"
}

$sourceDirectory=$args[0]

if ($sourceDirectory.substring($sourceDirectory.length - 5, 5) -ne "tests")
{
  Usage
  exit
}

write-host "Renaming \Results to \Golden folders in $sourceDirectory..."
Get-ChildItem -Directory -Filter Results -Recurse $sourceDirectory |
  Rename-Item -NewName { $_.name -replace 'Results', 'Golden' }  > $null

write-host "Copying -rtxImagePostTonemapping.pngs from input path to local folder preserving relative folder tree..."
robocopy $sourceDirectory ./tests *rtxImagePostTonemapping.png /S > $null

write-host "Copying -rtxImageDebugView.pngs from input path to local folder preserving relative folder tree..."
robocopy $sourceDirectory ./tests *rtxImageDebugView.png /S > $null

write-host "Copying -rtxImageDxvkView.pngs from input path to local folder preserving relative folder tree..."
robocopy $sourceDirectory ./tests *rtxImageDxvkView.png /S > $null

write-host "Renaming \Golden to \Results folders in $sourceDirectory..."
Get-ChildItem -Directory -Filter Golden -Recurse $sourceDirectory |
  Rename-Item -NewName { $_.name -replace 'Golden', 'Results' }  > $null

write-host "Done"

