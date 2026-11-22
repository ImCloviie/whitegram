param(
    [string]$DesktopRoot = (Resolve-Path "$PSScriptRoot/../../..").Path,
    [string]$AndroidResources = "$PSScriptRoot/android-res"
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -Path "$PSScriptRoot/ExportIcons.cs" -ReferencedAssemblies System.Drawing
$utf8 = New-Object Text.UTF8Encoding($false)
foreach ($variant in @('light', 'dark')) {
    $dark = $variant -eq 'dark'
    # Use one silhouette for both desktop polarities so their W and scale match.
    $mark = [WhitegramIconExport]::LoadMark("$PSScriptRoot/desktop-master.png", $dark, $false)
    try {
        $art = "$DesktopRoot/Telegram/Resources/art"
        [WhitegramIconExport]::Png($mark, 1024, $dark, 'desktop', "$art/white/whitegram_$variant/app.png")
        [WhitegramIconExport]::Ico($mark, $dark, "$art/white/whitegram_$variant/app_icon.ico")
        if (!$dark) {
            Copy-Item "$art/white/whitegram_light/app_icon.ico" "$art/icon256.ico" -Force
            foreach ($size in @(16, 32, 48, 64, 128, 256, 512)) {
                [WhitegramIconExport]::Png($mark, $size, $false, 'desktop', "$art/icon$size.png")
                [WhitegramIconExport]::Png($mark, $size * 2, $false, 'desktop', "$art/icon$size@2x.png")
            }
            Copy-Item "$art/white/whitegram_light/app.png" "$art/icon_round512@2x.png" -Force
        }
        # Keep the existing mobile artwork and placement independent of desktop.
        $mark.Dispose()
        $mark = [WhitegramIconExport]::LoadMark("$PSScriptRoot/$variant-master.png", $dark, $dark)
        $densities = @{mdpi=1; hdpi=1.5; xhdpi=2; xxhdpi=3; xxxhdpi=4}
        foreach ($density in $densities.GetEnumerator()) {
            $folder = "$AndroidResources/mipmap-$($density.Key)"
            $size = [int](48 * $density.Value)
            [WhitegramIconExport]::Png($mark, $size, $dark, 'rounded', "$folder/whitegram_${variant}.png")
            [WhitegramIconExport]::Png($mark, $size, $dark, 'circle', "$folder/whitegram_${variant}_round.png")
            [WhitegramIconExport]::Png($mark, [int](108 * $density.Value), $dark, 'foreground', "$folder/whitegram_${variant}_foreground.png")
        }
        $adaptive = @"
<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@drawable/whitegram_${variant}_background" />
    <foreground android:drawable="@mipmap/whitegram_${variant}_foreground" />
    <monochrome android:drawable="@mipmap/whitegram_light_foreground" />
</adaptive-icon>
"@
        $folder = "$AndroidResources/mipmap-anydpi-v26"
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        foreach ($suffix in @('', '_round')) {
            [IO.File]::WriteAllText("$folder/whitegram_$variant$suffix.xml", $adaptive, $utf8)
        }
        $color = if ($dark) { '#FF000000' } else { '#FFFFFFFF' }
        $background = @"
<?xml version="1.0" encoding="utf-8"?>
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="$color" />
</shape>
"@
        New-Item -ItemType Directory -Path "$AndroidResources/drawable" -Force | Out-Null
        [IO.File]::WriteAllText("$AndroidResources/drawable/whitegram_${variant}_background.xml", $background, $utf8)
    } finally { $mark.Dispose() }
}
Write-Output 'Exported light/dark desktop icons and Android resources.'
