param([string]$AndroidRoot = 'D:/Whitegram4A')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $AndroidRoot).Path
if ($root -ne 'D:\Whitegram4A') { throw 'This integration targets D:\Whitegram4A only.' }
$utf8 = New-Object Text.UTF8Encoding($false)
function Edit-Text([string]$RelativePath, [scriptblock]$Change) {
    $path = Join-Path $root $RelativePath
    $before = [IO.File]::ReadAllText($path)
    $after = & $Change $before
    if ($after -eq $before) { throw "Expected a change in $RelativePath" }
    [IO.File]::WriteAllText($path, $after, $utf8)
}
Edit-Text 'TMessagesProj/src/main/java/org/telegram/ui/LauncherIconController.java' {
    param($text)
    $old = 'DEFAULT("DefaultIcon", R.drawable.icon_background_sa, R.mipmap.icon_foreground_sa, R.string.AppIconDefault),'
    if (!$text.Contains($old)) { throw 'Unexpected DEFAULT launcher icon definition.' }
    $text.Replace($old, 'DEFAULT("DefaultIcon", R.drawable.whitegram_light_background, R.mipmap.whitegram_light_foreground, R.string.WhitegramIconLight),' + "`n" +
        '        WHITEGRAM_DARK("WhitegramDarkIcon", R.drawable.whitegram_dark_background, R.mipmap.whitegram_dark_foreground, R.string.WhitegramIconDark),')
}
Edit-Text 'TMessagesProj/src/main/AndroidManifest.xml' {
    param($text)
    $marker = '        <activity-alias' + "`n" + '            android:enabled="false"' + "`n" + '            android:name="org.telegram.messenger.VintageIcon"'
    $newline = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $marker = $marker.Replace("`n", $newline)
    if (!$text.Contains($marker)) { throw 'Missing VintageIcon insertion point.' }
    $alias = @'
        <activity-alias
            android:enabled="false"
            android:name="org.telegram.messenger.WhitegramDarkIcon"
            android:targetActivity="org.telegram.ui.LaunchActivity"
            android:icon="@mipmap/whitegram_dark"
            android:roundIcon="@mipmap/whitegram_dark_round"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
                <category android:name="android.intent.category.MULTIWINDOW_LAUNCHER" />
            </intent-filter>
            <meta-data android:name="android.app.shortcuts" android:resource="@xml/shortcuts" />
        </activity-alias>

'@
    $alias = $alias.Replace("`r`n", "`n").Replace("`n", $newline)
    $text = $text.Replace($marker, $alias + $newline + $marker)
    $default = 'android:name="org.telegram.messenger.DefaultIcon"'
    $text.Replace($default, $default + $newline + '            android:icon="@mipmap/whitegram_light"' + $newline + '            android:roundIcon="@mipmap/whitegram_light_round"')
}
# Application metadata and the default alias use the light icon in every build flavor.
foreach ($manifest in @(
    'TMessagesProj/config/debug/AndroidManifest.xml',
    'TMessagesProj/config/debug/AndroidManifest_SDK23.xml',
    'TMessagesProj/config/release/AndroidManifest.xml',
    'TMessagesProj/config/release/AndroidManifest_SDK23.xml',
    'TMessagesProj/config/release/AndroidManifest_standalone.xml'
)) {
    Edit-Text $manifest {
        param($text)
        $text = [regex]::Replace($text, 'android:icon="@mipmap/ic_launcher(?:_sa)?"', 'android:icon="@mipmap/whitegram_light"')
        [regex]::Replace($text, 'android:roundIcon="@mipmap/ic_launcher(?:_round|_sa)?"', 'android:roundIcon="@mipmap/whitegram_light_round"')
    }
}
foreach ($locale in @('values', 'values-ru')) {
    Edit-Text "TMessagesProj/src/main/res/$locale/ayu.xml" {
        param($text)
        if ($text.Contains('name="WhitegramIconLight"')) { throw 'Whitegram icon strings already installed.' }
        $light = if ($locale -eq 'values-ru') { 'Светлая' } else { 'Light' }
        $dark = if ($locale -eq 'values-ru') { 'Тёмная' } else { 'Dark' }
        $text.Replace('</resources>', "    <string name=`"WhitegramIconLight`">$light</string>`n    <string name=`"WhitegramIconDark`">$dark</string>`n</resources>")
    }
}
$sourceRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/android-res").Path
$resRoot = Join-Path $root 'TMessagesProj/src/main/res'
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
    $relative = $file.FullName.Substring($sourceRoot.Length + 1)
    $destination = Join-Path $resRoot $relative
    if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite new resource $destination" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
Write-Output 'Installed light/dark Whitegram icons; existing launcher aliases retained.'
