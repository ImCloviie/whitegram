param([string]$AndroidRoot = 'D:/Whitegram4A')
$ErrorActionPreference = 'Stop'
$desktop = (Resolve-Path "$PSScriptRoot/../../..").Path
Add-Type -AssemblyName System.Drawing
function Assert($condition, $message) { if (!$condition) { throw $message } }
function Check-Png($path, $size) {
    $image = [Drawing.Bitmap]::FromFile($path)
    try {
        Assert ($image.Width -eq $size -and $image.Height -eq $size) "Wrong dimensions: $path"
        Assert ($image.GetPixel(0, 0).A -eq 0) "Icon corners must be transparent: $path"
    } finally { $image.Dispose() }
}
foreach ($variant in @('light', 'dark')) {
    $dir = "$desktop/Telegram/Resources/art/white/whitegram_$variant"
    Check-Png "$dir/app.png" 1024
    $bytes = [IO.File]::ReadAllBytes("$dir/app_icon.ico")
    Assert ([BitConverter]::ToUInt16($bytes, 2) -eq 1) 'Not an ICO'
    $count = [BitConverter]::ToUInt16($bytes, 4)
    Assert ($count -eq 10) 'Missing Windows DPI sizes'
    $sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
    for ($i = 0; $i -lt $count; $i++) {
        $entry = 6 + 16 * $i
        $length = [BitConverter]::ToInt32($bytes, $entry + 8)
        $offset = [BitConverter]::ToInt32($bytes, $entry + 12)
        Assert ($offset + $length -le $bytes.Length) 'ICO frame extends past EOF'
        $stream = New-Object IO.MemoryStream(,$bytes[$offset..($offset + $length - 1)])
        try {
            $frame = [Drawing.Bitmap]::FromStream($stream)
            try { Assert ($frame.Width -eq $sizes[$i] -and $frame.Height -eq $sizes[$i]) 'Wrong ICO frame size' }
            finally { $frame.Dispose() }
        } finally { $stream.Dispose() }
    }

    $densities = @{mdpi=1; hdpi=1.5; xhdpi=2; xxhdpi=3; xxxhdpi=4}
    foreach ($density in $densities.GetEnumerator()) {
        $dir = "$AndroidRoot/TMessagesProj/src/main/res/mipmap-$($density.Key)"
        Check-Png "$dir/whitegram_$variant.png" ([int](48 * $density.Value))
        Check-Png "$dir/whitegram_${variant}_round.png" ([int](48 * $density.Value))
        Check-Png "$dir/whitegram_${variant}_foreground.png" ([int](108 * $density.Value))
    }
}
$legacy = "$desktop/Telegram/Resources/art/white/whitegram"
Assert ((Get-FileHash "$legacy/app.png").Hash -eq '0DC07414BDED7BC8A7D4D1909DD32794B914EA567D8AAB861BA4C19404336239') 'Legacy PNG changed'
Assert ((Get-FileHash "$legacy/app_icon.ico").Hash -eq 'C74F1B34F39FEF8419831CFC084CDD253C82BEBD209CEB42D5739102D5C3A67A') 'Legacy ICO changed'
$qrcPath = "$desktop/Telegram/Resources/qrc/white/white.qrc"
[xml]$qrc = [IO.File]::ReadAllText($qrcPath)
foreach ($file in $qrc.RCC.qresource.file) {
    Assert (Test-Path (Join-Path (Split-Path $qrcPath) $file.InnerText)) "Missing Qt resource: $($file.InnerText)"
}
[xml]$manifest = [IO.File]::ReadAllText("$AndroidRoot/TMessagesProj/src/main/AndroidManifest.xml")
$ns = 'http://schemas.android.com/apk/res/android'
$aliases = @($manifest.manifest.application.'activity-alias' | Where-Object {
    $_.'intent-filter'.category | Where-Object { $_.GetAttribute('name', $ns) -eq 'android.intent.category.LAUNCHER' }
})
$enabled = @($aliases | Where-Object { $_.GetAttribute('enabled', $ns) -eq 'true' })
Assert ($enabled.Count -eq 1) 'Exactly one launcher alias must be enabled by default'
Assert ($enabled[0].GetAttribute('name', $ns) -eq 'org.telegram.messenger.DefaultIcon') 'Default alias changed'
Assert ($enabled[0].GetAttribute('icon', $ns) -eq '@mipmap/whitegram_light') 'Default is not light'
$dark = @($aliases | Where-Object { $_.GetAttribute('name', $ns) -eq 'org.telegram.messenger.WhitegramDarkIcon' })
Assert ($dark.Count -eq 1 -and $dark[0].GetAttribute('enabled', $ns) -eq 'false') 'Dark alias must start disabled'
foreach ($path in Get-ChildItem "$AndroidRoot/TMessagesProj/config" -Recurse -Filter '*Manifest*.xml') {
    [xml]$flavor = [IO.File]::ReadAllText($path.FullName)
    $icon = $flavor.manifest.application.GetAttribute('icon', $ns)
    if ($icon) { Assert ($icon -eq '@mipmap/whitegram_light') "Wrong application icon in $($path.FullName)" }
}
Write-Output 'PASS: PNG sizes/alpha, all 20 ICO frames, unchanged legacy assets, Qt resources, Android default/dark aliases and build flavors.'
