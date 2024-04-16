function Usage {
  Write-Host "Usage:"  -ForegroundColor Green
  Write-Host "  copyGoldenImages.ps1 <path_to_artifacts_folder>"
  Write-Host "  All goldens within <path_to_artifacts_folder> subtree"
  Write-Host "  will be copied over to a relative golden directory specified in golden path logs."
}

if ($args.count -ne 1) {
  Usage
  exit
}

$srcDir=$args[0]
Write-Host "Copying goldens from $srcDir" -ForegroundColor Yellow

# Iterate through all found golden path log files in the source directory
# - Copy golden from a log file location into a relative path in the working directory from the log
$goldenPathFiles = Get-ChildItem $srcDir -Recurse -Include "golden_paths.txt"
ForEach ($goldenLogPath in $goldenPathFiles) {

  # Read the golden description from the first tw lines
  $goldenDesc = Get-Content $goldenLogPath
  
  # Both and only a golden git path and source golden name must be present in the file
  if ($goldenDesc.length -ne 2) {
    Write-Host "Found a golden_paths.txt with invalid number of entries." -ForegroundColor Red
    Write-Host "The file must only contain 2 entries: golden's git path and a source golden file name."  -ForegroundColor Red
    Write-Host "Terminating." -ForegroundColor Red
    Write-Host "File: $goldenLogPath" -ForegroundColor Red
    exit
  }
  
  # Set up source and destination path strings
  $srcGoldenName = $goldenDesc[0]
  $gitRelativeGoldenPath = $goldenDesc[1]
  $goldenParentFolder = Split-Path -Parent $goldenLogPath
  $srcGoldenPath = $goldenParentFolder + "\" + $srcGoldenName
  $destGoldenPath = ".\" + $gitRelativeGoldenPath | Resolve-Path 

  # Copy the golden
  Write-Host "$srcGoldenName --> $destGoldenPath"
  Copy-Item $srcGoldenPath -Destination $destGoldenPath -Force  -errorVariable copyErrors
  if ($copyErrors.count -ge 1) {    
    Write-Host "Failed to copy the golden:" -ForegroundColor Red
    Write-Host "  Source: $srcGoldenPath" -ForegroundColor Red
    Write-Host "  Destination: $destGoldenPath" -ForegroundColor Red
    if ($errors.length -ge 1) {
      Write-Host "  Error(s):" -ForegroundColor Red
      foreach($error in $errors) {
        Write-Host "   - $($error.Exception)" -ForegroundColor Red 
      }
    }
    exit
  }
}
    
Write-Host "Total files copied: $($goldenPathFiles.count)" -ForegroundColor Yellow
