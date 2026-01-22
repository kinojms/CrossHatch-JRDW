$shaderc = "C:\CrossHatch-JRDW\build\bgfx.cmake\cmake\bgfx\Release\shaderc.exe"
$shadersDir = "C:\CrossHatch-JRDW\shaders"
$baseShadersDir = "C:\CrossHatch-JRDW\baseshaders"

if (!(Test-Path $shaderc)) {
    Write-Error "shaderc.exe not found"
    exit 1
}

Write-Host "Using shaderc: $shaderc" -ForegroundColor Cyan

Write-Host "
Compiling vertex shaders..." -ForegroundColor Yellow
Get-ChildItem "$shadersDir\v_*.sc" | ForEach-Object {
    $out = $_.FullName -replace '\.sc\$', '.bin'
    Write-Host "  $($_.Name)"
    & $shaderc -f $_.FullName -o $out --platform windows --type vertex --include $baseShadersDir
}

Write-Host "
Compiling fragment shaders..." -ForegroundColor Yellow
Get-ChildItem "$shadersDir\f_*.sc" | ForEach-Object {
    $out = $_.FullName -replace '\.sc\$', '.bin'
    Write-Host "  $($_.Name)"
    & $shaderc -f $_.FullName -o $out --platform windows --type fragment --include $baseShadersDir
}

Write-Host "
 Shader compilation complete!" -ForegroundColor Cyan
$binCount = @(Get-ChildItem "$shadersDir\*.bin").Count
Write-Host "Generated $binCount shader binaries"
