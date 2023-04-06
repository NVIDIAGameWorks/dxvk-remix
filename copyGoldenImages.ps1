function Usage {
  write-host "Usage:"
  write-host "  copyGoldenImages.ps1 <path_to_source_tests_folder>"
  write-host "  <path_to_source_tests_folder> - path must end with the word 'tests', i.e. e:\data\tests'"
}

$sourceDir=$args[0]

if ($sourceDir.substring($sourceDir.length - 5, 5) -ne "tests")
{
  Usage
  exit
}

$sourceDir = [IO.Path]::Combine($sourceDir, "rtx\\dxvk_rt_testing")

# relative path from this script to the destination
$destDir = "tests/rtx/dxvk_rt_testing"

Get-ChildItem $sourceDir -Recurse -Include "*_rtxImagePostTonemapping.png", "*_rtxImageDebugView.png", "*_rtxImageDxvkView.png" | ForEach-Object {
    $sourcePath = $_.FullName
    $relPath = $_.FullName.Substring($sourceDir.Length) -Replace "\\results\\", "\"
	$relPath = $relPath.replace("apics", "goldens")
    $destPath = Join-Path $destDir $relPath
    $destFolder = Split-Path $destPath -Parent
  
    if (!(Test-Path $destFolder)) {
		Write-Host "$destFolder dir didn't exist, creating"
        New-Item -ItemType Directory -Path $destFolder | Out-Null
    }

    Write-Host "Copying, $sourcePath ---> $destPath"
    Copy-Item $sourcePath -Destination $destPath
}