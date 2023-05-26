function Convert-Junction-To-SymLink { 
	param(
		[Parameter(Mandatory)]
		[string]$RootPath,

		[Parameter(Mandatory)]
		[bool]$Recursive
	)
    
    $bIsAdmin = (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    if(!$bIsAdmin) {
        Write-Output("Convert-Junction-To-SymLink not run with admin privileges, so impossible.")
        return
    }

    function _Convert-Junction-To-SymLink { 
        param(
            [Parameter(Mandatory)]
            [string]$Path,

            [Parameter(Mandatory)]
            [bool]$Recursive
        )

        Get-ChildItem $Path |
        Foreach-Object {
            if($_ -is [System.IO.DirectoryInfo]) {
                if($_.LinkType -eq "Junction") {
                    $linkPath = "" + $_.FullName
                    $targetPath = "" + $_.Target
                    $_.Delete()
                    New-Item -ItemType SymbolicLink -Path $linkPath -Target $targetPath
                } elseif(!$_.LinkType) {
                    if($Recursive) {
                        _Convert-Junction-To-SymLink -Path $_.FullName -Recursive $Recursive
                    }
                }
            }
        }
    }
    _Convert-Junction-To-SymLink -Path $RootPath -Recursive $Recursive
}
