const char HTML_MESH_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Flash Light MESH Settings</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        h1, h2 { color: #333; }
        form { background: #f4f4f4; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        label { display: block; margin-bottom: 8px; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 95%; padding: 8px; margin-bottom: 10px; border-radius: 4px; border: 1px solid #ddd; }
        input[type="radio"] { margin-right: 5px; }
        button { background-color: #007bff; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }
        button:hover { background-color: #0056b3; }
        .note { font-size: 0.9em; color: #666; }
        .warning { padding: 10px; border: 1px solid #ffc107; background-color: #fff3cd; color: #856404; border-radius: 4px; margin-top: 10px; }
    </style>
</head>
<body>
    <h1>Flash Light MESH Settings</h1>
    <form action="/save_mesh" method="post">
        <h2>1. MESH Mode</h2>
        <input type="radio" id="mesh_enabled" name="mesh_mode" value="1" {{MESH_ENABLED_CHECKED}}>
        <label for="mesh_enabled">Enabled</label><br>
        <input type="radio" id="mesh_disabled" name="mesh_mode" value="0" {{MESH_DISABLED_CHECKED}}>
        <label for="mesh_disabled">Disabled (Standalone Mode)</label>

        <h2>2. MESH Role</h2>
        <p class="note">This setting is only effective when MESH Mode is enabled.</p>
        <input type="radio" id="mesh_root" name="mesh_role" value="1" {{MESH_ROOT_CHECKED}}>
        <label for="mesh_root">Root Node</label>
        <div class="warning">A MESH network can only have ONE Root node. The Root node connects to your Wi-Fi AP and MQTT Broker.</div>
        <input type="radio" id="mesh_nonroot" name="mesh_role" value="0" {{MESH_NONROOT_CHECKED}}>
        <label for="mesh_nonroot">Non-Root Node</label>
        <div class="warning">A Non-Root node only connects to the MESH network and receives alarms from the Root.</div>

        <h2>3. MESH Network Credentials</h2>
        <p class="note">All nodes in the same MESH network must use the exact same SSID and Password.</p>
        <label for="mesh_ssid">MESH SSID:</label>
        <input type="text" id="mesh_ssid" name="mesh_ssid" value="{{MESH_SSID}}">
        <label for="mesh_password">MESH Password:</label>
        <input type="password" id="mesh_password" name="mesh_password" value="{{MESH_PASSWORD}}">
        
        <br><br>
        <button type="submit">Save MESH Settings & Reboot</button>
    </form>
    <a href="/marap">Go to Main Settings</a>
</body>
</html>
)rawliteral";