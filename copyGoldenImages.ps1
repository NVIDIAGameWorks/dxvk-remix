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

# relative path from this script to the destination
$destDir = "tests/rtx/dxvk_rt_testing/goldens"

$destImages = Get-ChildItem $destDir -Recurse -Include "*_rtxImagePostTonemapping.png", "*_rtxImageDebugView.png", "*_rtxImageDxvkView.png"
$srcImages = Get-ChildItem $sourceDir -Recurse -Include "*_rtxImagePostTonemapping.png", "*_rtxImageDebugView.png", "*_rtxImageDxvkView.png"

ForEach ($destImage in $destImages) {
    $srcImage = $srcImages | where Name -CEQ $destImage.Name

    if ($srcImage) {
        Write-Host "Copying,"$srcImage.Name"---> $destImage"
        Copy-Item $srcImage -Destination $destImage

        $srcImages = $srcImages | Where-Object {$_.FullName -ne $srcImage.FullName}
    } else {
        Write-Warning ("Image " + $destImage.Name + " is not found in the artifacts!")
    }
}

if ($srcImages) {
    [console]::beep(500, 500)

    Write-Warning "New images discovered in the artifacts:"

    ForEach ($srcImage in $srcImages) {
        write-host "`t"$srcImage.FullName
    }

    Write-Warning "You may need to manually initialize these images in the repo."
}
