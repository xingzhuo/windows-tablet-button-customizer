Add-Type -AssemblyName System.Windows.Forms
$key='HKCU:\Software\SurfaceButtonBindings'
New-Item $key -Force | Out-Null
$form=New-Object Windows.Forms.Form
$form.Text='平板设备配置'
$form.Width=900
$form.Height=450
$list=New-Object Windows.Forms.ListBox
$list.SetBounds(15,45,850,270)
$form.Controls.Add($list)
$label=New-Object Windows.Forms.Label
$label.Text='选择经测试可暂停机身音量的 Consumer 子设备。点击扫描读取当前设备。'
$label.SetBounds(15,15,850,25)
$form.Controls.Add($label)
$scan=New-Object Windows.Forms.Button
$scan.Text='扫描设备'
$scan.SetBounds(15,330,120,35)
$form.Controls.Add($scan)
$save=New-Object Windows.Forms.Button
$save.Text='确认并保存'
$save.SetBounds(150,330,140,35)
$form.Controls.Add($save)
$script:items=@()
$scan.Add_Click({
 try {
  $script:items=@(Get-PnpDevice -PresentOnly -Class HIDClass | Where-Object {
   $ids=Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue
   ($ids.Data -join ' ') -match 'UP:000C_U:0001|HID_DEVICE_SYSTEM_CONSUMER'
  })
  $list.Items.Clear()
  foreach($device in $script:items) { [void]$list.Items.Add($device.FriendlyName+' | '+$device.InstanceId) }
  $label.Text='找到 '+$script:items.Count+' 个候选。保存前请用已验证的禁用/恢复流程确认目标。'
 } catch { [Windows.Forms.MessageBox]::Show($_.Exception.Message) }
})
$save.Add_Click({
 if($list.SelectedIndex -lt 0) { return }
 $device=$script:items[$list.SelectedIndex]
 $answer=[Windows.Forms.MessageBox]::Show('是否已确认此实例禁用后机身音量停止、键盘音量和绑定仍可用？','验证确认','YesNo')
 if($answer -ne 'Yes') { return }
 Set-ItemProperty $key VolumeDevice $device.InstanceId
 Set-ItemProperty $key DeviceName $device.FriendlyName
 try {
  $machine=Get-CimInstance Win32_ComputerSystem
  Set-ItemProperty $key TabletModel ($machine.Manufacturer+' '+$machine.Model)
 } catch {}
 Set-ItemProperty $key ProfileVersion 1
 $form.Close()
})
[void]$form.ShowDialog()
