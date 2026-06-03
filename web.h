#ifndef WEB_H
#define WEB_H

const char HTML_LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Login</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #f0f2f5; margin: 0; }
        .login-container { background-color: white; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }
        h1 { text-align: center; color: #333; }
        label { display: block; margin-bottom: 0.5rem; font-weight: bold; }
        input[type="text"], input[type="password"] { width: 100%; padding: 0.8rem; margin-bottom: 1rem; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 0.8rem; background-color: #007bff; color: white; border: none; border-radius: 4px; font-size: 1rem; cursor: pointer; }
        button:hover { background-color: #0056b3; }
        .error { color: red; text-align: center; margin-bottom: 1rem; }
    </style>
</head>
<body>
    <div class="login-container">
        <h1>Dome Light Login</h1>
        <div class="error">%ERROR%</div>
        <form action="/login" method="post">
            <label for="username">Username:</label>
            <input type="text" id="username" name="username" required>
            <label for="password">Password:</label>
            <input type="password" id="password" name="password" required>
            <button type="submit">Login</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Dome Light Settings</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
        h1, h2, h3 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 5px; }
        form, fieldset { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; border: 1px solid #ddd; }
        legend { font-weight: bold; font-size: 1.2em; color: #0056b3; }
        label, input, select, textarea, button { display: block; width: 100%; margin-bottom: 10px; box-sizing: border-box; }
        input[type="text"], input[type="number"], select, textarea { padding: 8px; border: 1px solid #ccc; border-radius: 4px; }
        button { padding: 10px 15px; font-size: 16px; cursor: pointer; background-color: #007bff; color: white; border: none; border-radius: 4px; }
        button:hover { background-color: #0056b3; }
        .nav { position: fixed; top: 0; left: 0; width: 100%; background-color: #333; padding: 10px 0; text-align: center; box-shadow: 0 2px 5px rgba(0,0,0,0.2); z-index: 1000; }
        .nav a { color: white; padding: 14px 20px; text-decoration: none; display: inline-block; }
        .nav a:hover { background-color: #575757; }
        .container { margin-top: 80px; }
    </style>
</head>
<body>
    <div class="nav">
        <a href="/setting">Settings</a>
        <a href="/fwupload">Firmware Update</a>
        <a href="/hw_test">Hardware Test</a>
        <a href="/logout">Logout</a>
    </div>
    <div class="container">
    <h1>Dome Light Control Panel</h1>
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
    </div>
</body>
</html>
)rawliteral";

const char HTML_FIRMWARE_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Firmware Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
        h1, h2 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 5px; }
        form { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; border: 1px solid #ddd; max-width: 500px; }
        input[type="file"], input[type="submit"] { display: block; width: 100%; margin-bottom: 10px; box-sizing: border-box; padding: 10px; }
        input[type="submit"] { cursor: pointer; background-color: #28a745; color: white; border: none; border-radius: 4px; font-size: 1em; }
        input[type="submit"]:hover { background-color: #218838; }
        .nav { position: fixed; top: 0; left: 0; width: 100%; background-color: #333; padding: 10px 0; text-align: center; box-shadow: 0 2px 5px rgba(0,0,0,0.2); z-index: 1000; }
        .nav a { color: white; padding: 14px 20px; text-decoration: none; display: inline-block; }
        .nav a:hover { background-color: #575757; }
        .container { margin-top: 80px; }
        #progressContainer { width: 100%; background-color: #ddd; border-radius: 4px; margin-top: 15px; }
        #progressBar { width: 0%; height: 20px; background-color: #4CAF50; text-align: center; line-height: 20px; color: white; border-radius: 4px; transition: width 0.4s ease; }
    </style>
</head>
<body>
    <div class="nav">
        <a href="/setting">Settings</a>
        <a href="/fwupload">Firmware Update</a>
        <a href="/hw_test">Hardware Test</a>
        <a href="/logout">Logout</a>
    </div>
    <div class="container">
        <h1>Firmware Update</h1>
        <form id="upload_form" method='POST' action='/update' enctype='multipart/form-data'>
            <p>Select a .bin file to upload.</p>
            <input type='file' name='update' accept='.bin' required>
            <input type='submit' value='Upload and Update'>
            <div id="progressContainer">
                <div id="progressBar">0%</div>
            </div>
        </form>
    </div>
    <script>
        document.getElementById('upload_form').addEventListener('submit', function(e) {
            e.preventDefault();
            var form = e.target;
            var data = new FormData(form);
            var xhr = new XMLHttpRequest();
            xhr.open(form.method, form.action, true);
            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var percentComplete = (e.loaded / e.total) * 100;
                    var progressBar = document.getElementById('progressBar');
                    progressBar.style.width = percentComplete.toFixed(2) + '%';
                    progressBar.textContent = percentComplete.toFixed(2) + '%';
                }
            };
            xhr.onload = function() {
                // The response text from the server is in xhr.responseText
                alert('Upload finished: ' + xhr.responseText + ' Device will now reboot.');
                setTimeout(function(){ window.location.href = '/'; }, 3000);
            };
            xhr.onerror = function() {
                alert('An error occurred during the upload. Please try again.');
            };
            xhr.send(data);
        });
    </script>
</body>
</html>
)rawliteral";

const char HTML_HW_TEST_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Hardware Test</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
        h1, h2 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 5px; }
        table { border-collapse: collapse; width: 100%; max-width: 500px; margin-bottom: 20px; background-color: #fff; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
        th { background-color: #f2f2f2; }
        .btn { padding: 10px 15px; font-size: 16px; cursor: pointer; border-radius: 4px; text-decoration: none; display: inline-block; }
        .btn-on { background-color: #4CAF50; color: white; border: none; }
        .btn-off { background-color: #f44336; color: white; border: none; }
        .nav { position: fixed; top: 0; left: 0; width: 100%; background-color: #333; padding: 10px 0; text-align: center; box-shadow: 0 2px 5px rgba(0,0,0,0.2); z-index: 1000; }
        .nav a { color: white; padding: 14px 20px; text-decoration: none; display: inline-block; }
        .nav a:hover { background-color: #575757; }
        .container { margin-top: 80px; }
        fieldset { background-color: #fff; border: 1px solid #ddd; padding: 15px; margin-top: 20px; max-width: 470px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        legend { font-weight: bold; font-size: 1.2em; color: #0056b3; padding: 0 5px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        select { padding: 8px; font-size: 1em; margin-bottom: 15px; width: 100%; border-radius: 4px; border: 1px solid #ccc; }
        .play-btn { background-color: #008CBA; color: white; border: none; width: 100%; padding: 10px; font-size: 1em; }
        .play-btn:hover { background-color: #0077A2; }
    </style>
</head>
<body>
    <div class="nav">
        <a href="/setting">Settings</a>
        <a href="/fwupload">Firmware Update</a>
        <a href="/hw_test">Hardware Test</a>
        <a href="/logout">Logout</a>
    </div>
    <div class="container">
    <h1>Hardware Test</h1>
    <p><b>WARNING:</b> This page directly controls hardware GPIOs. Use with caution.</p>
    
    <h2>GPIO Test</h2>
    <table>
        <tr><th>Component</th><th>GPIO</th><th>Control</th></tr>
        <tr>
            <td>Buzzer</td><td>48</td>
            <td>
                <a href="/hw_test?gpio=48&action=on"><button class="btn btn-on">ON</button></a>
                <a href="/hw_test?gpio=48&action=off"><button class="btn btn-off">OFF</button></a>
            </td>
        </tr>
        <tr>
            <td>Red Light</td><td>19</td>
            <td>
                <a href="/hw_test?gpio=19&action=on"><button class="btn btn-on">ON</button></a>
                <a href="/hw_test?gpio=19&action=off"><button class="btn btn-off">OFF</button></a>
            </td>
        </tr>
        <tr>
            <td>Green Light</td><td>20</td>
            <td>
                <a href="/hw_test?gpio=20&action=on"><button class="btn btn-on">ON</button></a>
                <a href="/hw_test?gpio=20&action=off"><button class="btn btn-off">OFF</button></a>
            </td>
        </tr>
        <tr>
            <td>Blue Light</td><td>21</td>
            <td>
                <a href="/hw_test?gpio=21&action=on"><button class="btn btn-on">ON</button></a>
                <a href="/hw_test?gpio=21&action=off"><button class="btn btn-off">OFF</button></a>
            </td>
        </tr>
    </table>

    <fieldset>
        <legend>Ringtone Test</legend>
        <form action="/hw_test" method="get">
            <label for="sound_index">Melten Sound Index:</label>
            <select name="sound_index" id="sound_index">
                <option value="1">1</option><option value="2">2</option><option value="3">3</option>
                <option value="4">4</option><option value="5">5</option><option value="6">6</option>
                <option value="7">7</option><option value="8">8</option><option value="9">9</option>
                <option value="10">10</option><option value="11">11</option><option value="12">12</option>
                <option value="13">13</option><option value="14">14</option><option value="15">15</option>
                <option value="16">16</option><option value="17">17</option><option value="18">18</option>
                <option value="19">19</option><option value="20">20</option><option value="21">21</option>
            </select>

            <label for="volume_level">Volume:</label>
            <select name="volume_level" id="volume_level">
                <option value="0">Level 0 (Mute)</option>
                <option value="1">Level 1</option>
                <option value="2">Level 2</option>
                <option value="3">Level 3</option>
                <option value="4">Level 4</option>
                <option value="5">Level 5 (Max)</option>
            </select>
            
            <button type="submit" name="play_ringtone" value="1" class="btn play-btn">Play Sound</button>
        </form>
    </fieldset>

    </div>
</body>
</html>
)rawliteral";

#endif // WEB_H