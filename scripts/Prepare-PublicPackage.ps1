<#
Creates publishable copies from the read-only source mirror. It does not edit
sources/. Inspect all generated files before committing.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'sources'
$firmwareDir = Join-Path $root 'firmware'
$webDir = Join-Path $root 'web'

if (-not (Test-Path -LiteralPath (Join-Path $source 'smarthomegatesystem.txt'))) {
  throw 'Expected source firmware was not found under sources/.'
}

New-Item -ItemType Directory -Force -Path $firmwareDir, $webDir | Out-Null

# Firmware: replace credentials and local addresses by declaration shape,
# rather than embedding any source value in this script.
$firmware = Get-Content -Raw -LiteralPath (Join-Path $source 'smarthomegatesystem.txt')
$firmware = $firmware -replace '(?m)^const char\* FIREBASE_HOST = ".*";', 'const char* FIREBASE_HOST = FIREBASE_DATABASE_URL;'
$firmware = $firmware -replace '(?m)^const char\* FIREBASE_AUTH = ".*";', 'const char* FIREBASE_AUTH = FIREBASE_DATABASE_AUTH;'
$firmware = $firmware -replace '(?m)^const char\* ssid = ".*";', 'const char* ssid = WIFI_SSID;'
$firmware = $firmware -replace '(?m)^const char\* password = ".*";', 'const char* password = WIFI_PASSWORD;'
$firmware = $firmware -replace '(?m)^#define BOT_TOKEN ".*"', '#define BOT_TOKEN TELEGRAM_BOT_TOKEN'
$firmware = $firmware -replace '(?m)^String CHAT_ID_1 = ".*";', 'String CHAT_ID_1 = TELEGRAM_CHAT_ID_PRIMARY;'
$firmware = $firmware -replace '(?m)^String CHAT_ID_2 = ".*";', 'String CHAT_ID_2 = TELEGRAM_CHAT_ID_SECONDARY;'
$firmware = $firmware -replace 'String camUrl = ".*";', 'String camUrl = CAMERA_SNAPSHOT_URL;'
$firmware = $firmware -replace 'String credentials = ".*";', 'String credentials = String(CAMERA_USERNAME) + ":" + CAMERA_PASSWORD;'
$firmware = $firmware -replace 'var streamUrl = ".*";', 'var streamUrl = "http://LOCAL_STREAM_HOST:8888/cam/index.m3u8";'
$firmware = $firmware -replace '#include <Espalexa.h>', "#include <Espalexa.h>`r`n#include `"include/Config.h`""
Set-Content -LiteralPath (Join-Path $firmwareDir 'SmartGateAutomation.ino') -Value $firmware -NoNewline

# PWA: replace all project-specific web, database, and VAPID values by their
# published placeholders. Do not rely on this as a secret scanner.
$web = Get-Content -Raw -LiteralPath (Join-Path $source 'index.html')
$web = $web -replace '(?m)^\s*const FIREBASE_HOST = ".*";', ('  const runtimeConfig = window.SMART_GATE_CONFIG || {};' + [Environment]::NewLine + '  const FIREBASE_HOST = runtimeConfig.databaseUrl;')
$web = $web -replace '(?m)^\s*const FIREBASE_AUTH = ".*";', '  const FIREBASE_AUTH = runtimeConfig.databaseAuth;'
$web = $web -replace '(?s)const firebaseConfig = \{.*?\n  \};', 'const firebaseConfig = runtimeConfig.firebase;'
$web = $web -replace '(?m)^\s*const VAPID_KEY = ".*";', '  const VAPID_KEY = runtimeConfig.vapidKey;'
$web = $web -replace '<script src="https://www.gstatic.com/firebasejs/10.13.0/firebase-app-compat.js"></script>', ('<script src="config.local.js"></script>' + [Environment]::NewLine + '<script src="https://www.gstatic.com/firebasejs/10.13.0/firebase-app-compat.js"></script>')
Set-Content -LiteralPath (Join-Path $webDir 'index.html') -Value $web -NoNewline

foreach ($asset in 'manifest.json', 'icon-192.png', 'icon-512.png', '404.html') {
  $from = Join-Path $source $asset
  if (Test-Path -LiteralPath $from) { Copy-Item -LiteralPath $from -Destination (Join-Path $webDir $asset) -Force }
}
Copy-Item -LiteralPath (Join-Path $source 'package.json') -Destination (Join-Path $webDir 'package.json') -Force
Copy-Item -LiteralPath (Join-Path $source 'package-lock.json') -Destination (Join-Path $webDir 'package-lock.json') -Force

Write-Host 'Sanitized public package created in firmware/ and web/. Review before committing.'
