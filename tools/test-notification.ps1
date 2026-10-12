# Test tool: sends a Windows notification 3 seconds after the button is clicked
# (time to put the headset back on, or to switch tab in the SteamVR menu).
# Run with test-notification.cmd.
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
[Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null

$aumid = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
$photo = 'file:///C:/ProgramData/Microsoft/User%20Account%20Pictures/user-192.png'
$hero = 'file:///C:/Windows/Web/Wallpaper/Windows/img0.jpg'
$script:count = 0
$script:progressTimer = $null

$kinds = [ordered]@{
    'Simple' = { param($n) "<toast><visual><binding template='ToastGeneric'><text>Notification de test $n</text><text>Bonjour depuis Windows ! Elle flotte devant toi, en bas.</text></binding></visual></toast>" }
    'Message avec photo' = { param($n) "<toast><visual><binding template='ToastGeneric'><text>Lucas</text><text>t'es dispo pour une session VRChat ce soir ? On se retrouve vers 21 h ($n)</text><image placement='appLogoOverride' hint-crop='circle' src='$photo'/></binding></visual></toast>" }
    'Grande image + bouton' = { param($n) "<toast><visual><binding template='ToastGeneric'><text>Capture d'écran copiée ($n)</text><text>Enregistrement automatique dans le dossier des captures d'écran.</text><image placement='hero' src='$hero'/></binding></visual><actions><action content='Balisage et partage' arguments='x'/></actions></toast>" }
    'Barre de progression' = { param($n) "<toast><visual><binding template='ToastGeneric'><text>Téléchargement $n</text><progress title='Mise à jour' value='{v}' valueStringOverride='{s}' status='Téléchargement…'/></binding></visual></toast>" }
    'Rappel (reste affiché)' = { param($n) "<toast scenario='reminder'><visual><binding template='ToastGeneric'><text>Réunion d'équipe $n</text><text>Dans 5 minutes</text></binding></visual><actions><action activationType='system' arguments='snooze' content=''/><action activationType='system' arguments='dismiss' content=''/></actions></toast>" }
    'Texte long' = { param($n) "<toast><visual><binding template='ToastGeneric'><text>Résumé de Microsoft Defender ($n)</text><text>L'Antivirus Microsoft Defender a réussi à effectuer une action sur 1 menace depuis le dernier récapitulatif. Aucune action supplémentaire n'est nécessaire de votre part pour le moment, tout va bien.</text></binding></visual></toast>" }
}

function Send-Toast([string]$kind) {
    $script:count++
    $xml = New-Object Windows.Data.Xml.Dom.XmlDocument
    $xml.LoadXml((& $kinds[$kind] $script:count))
    $toast = [Windows.UI.Notifications.ToastNotification]::new($xml)
    $notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid)
    if ($kind -eq 'Barre de progression') {
        # Data-bound progress, updated every second like a real download.
        $tag = "progress$($script:count)"
        $toast.Tag = $tag
        $data = New-Object 'System.Collections.Generic.Dictionary[string,string]'
        $data['v'] = '0'; $data['s'] = '0 %'
        $toast.Data = [Windows.UI.Notifications.NotificationData]::new($data, 1)
        $notifier.Show($toast)
        $script:progress = 0
        $script:progressTag = $tag
        $script:progressTimer = New-Object System.Windows.Forms.Timer
        $script:progressTimer.Interval = 1000
        $script:progressTimer.Add_Tick({
            $script:progress += 10
            $d = New-Object 'System.Collections.Generic.Dictionary[string,string]'
            $d['v'] = ($script:progress / 100).ToString([Globalization.CultureInfo]::InvariantCulture)
            $d['s'] = "$($script:progress) %"
            $nd = [Windows.UI.Notifications.NotificationData]::new($d, [uint32]($script:progress / 10 + 1))
            [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid).Update($nd, $script:progressTag) | Out-Null
            if ($script:progress -ge 100) { $script:progressTimer.Stop() }
        })
        $script:progressTimer.Start()
    } else {
        $notifier.Show($toast)
    }
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Test notification VR'
$form.Size = New-Object System.Drawing.Size(340, 190)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.TopMost = $true
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object System.Drawing.Font('Segoe UI', 11)

$combo = New-Object System.Windows.Forms.ComboBox
$combo.DropDownStyle = 'DropDownList'
$combo.Location = New-Object System.Drawing.Point(20, 18)
$combo.Size = New-Object System.Drawing.Size(285, 30)
$kinds.Keys | ForEach-Object { [void]$combo.Items.Add($_) }
$combo.SelectedIndex = 0
$form.Controls.Add($combo)

$button = New-Object System.Windows.Forms.Button
$button.Text = 'Envoyer dans 3 s'
$button.Location = New-Object System.Drawing.Point(20, 62)
$button.Size = New-Object System.Drawing.Size(285, 60)
$form.Controls.Add($button)

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    $script:left--
    if ($script:left -le 0) {
        $timer.Stop()
        Send-Toast $script:kind
        $button.Text = 'Envoyer dans 3 s'
        $button.Enabled = $true
    } else {
        $button.Text = "Envoi dans $($script:left) s…"
    }
})
$button.Add_Click({
    $script:kind = $combo.SelectedItem
    $script:left = 3
    $button.Enabled = $false
    $button.Text = 'Envoi dans 3 s…'
    $timer.Start()
})

[void]$form.ShowDialog()
