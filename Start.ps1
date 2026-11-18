$ErrorActionPreference='Stop'
$key='HKCU:\Software\SurfaceButtonBindings'
if(-not (Get-ItemProperty $key -Name VolumeDevice -ErrorAction SilentlyContinue)) { & "$PSScriptRoot\DeviceSetup.ps1" }
$profile=Get-ItemProperty $key -ErrorAction SilentlyContinue
if(-not $profile.VolumeDevice) { throw '请先完成设备设置。' }
if(-not (Get-PnpDevice -InstanceId $profile.VolumeDevice -PresentOnly -ErrorAction SilentlyContinue)) {
 & "$PSScriptRoot\DeviceSetup.ps1"
}
$arch=if($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {'ARM64'} else {'x64'}
Start-Process "$PSScriptRoot\SurfaceButtonBindings-$arch.exe"
