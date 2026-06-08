// web.h

const char HTML_LOGIN_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>Login</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; background-color: #f0f2f5; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .login-container { background-color: #fff; padding: 2rem; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); width: 300px; }
        h1 { text-align: center; color: #333; }
        .input-group { margin-bottom: 1rem; }
        label { display: block; margin-bottom: 5px; color: #555; }
        input[type="text"], input[type="password"] { width: 100%; padding: 10px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background-color: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background-color: #0056b3; }
        .error { color: #dc3545; text-align: center; margin-top: 1rem; }
    </style>
</head>
<body>
    <div class="login-container">
        <h1>Device Login</h1>
        <form action="/login" method="post">
            <div class="input-group">
                <label for="username">Username</label>
                <input type="text" id="username" name="username" required>
            </div>
            <div class="input-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" required>
            </div>
            <button type="submit">Login</button>
        </form>
        <div class="error">%ERROR%</div>
    </div>
</body>
</html>
)=====";

const char HTML_HEADER[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>Flash Light Settings</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 0; background-color: #f4f4f4; }
        .navbar { background-color: #333; overflow: hidden; }
        .navbar a { float: left; display: block; color: white; text-align: center; padding: 14px 20px; text-decoration: none; }
        .navbar a:hover { background-color: #ddd; color: black; }
        .container { padding: 20px; }
        h1, h2, h3 { color: #333; }
        form { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }
        input[type=text], input[type=password], input[type=number], textarea { width: 100%; padding: 12px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; margin-top: 6px; margin-bottom: 16px; }
        button { background-color: #007bff; color: white; padding: 14px 20px; margin: 8px 0; border: none; cursor: pointer; width: 100%; border-radius: 4px; }
        button:hover { background-color: #0056b3; }
    </style>
</head>
<body>
    <div class="navbar">
        <a href="/setting">Settings</a>
        <a href="/fwupload">Firmware Update</a>
        <a href="/hw_test">Hardware Test</a>
        <a href="/logout" style="float:right;">Logout</a>
    </div>
    <div class="container">
)=====";

const char HTML_FOOTER[] PROGMEM = R"=====(
    </div>
</body>
</html>
)=====";

const char HTML_FIRMWARE_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>Firmware Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 0; background-color: #f4f4f4; }
        .navbar { background-color: #333; overflow: hidden; }
        .navbar a { float: left; display: block; color: white; text-align: center; padding: 14px 20px; text-decoration: none; }
        .navbar a:hover { background-color: #ddd; color: black; }
        .container { padding: 20px; }
        .upload-form { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); max-width: 500px; margin: auto; }
        h1 { color: #333; text-align: center; }
        input[type=file] { width: 100%; padding: 12px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; margin-bottom: 16px; }
        button { background-color: #28a745; color: white; padding: 14px 20px; margin: 8px 0; border: none; cursor: pointer; width: 100%; border-radius: 4px; }
        button:hover { background-color: #218838; }
        .progress-bar { width: 100%; background-color: #f3f3f3; border-radius: 4px; }
        .progress { width: 0%; height: 30px; background-color: #4caf50; text-align: center; line-height: 30px; color: white; border-radius: 4px; }
    </style>
</head>
<body>
    <div class="navbar">
        <a href="/setting">Settings</a>
        <a href="/fwupload">Firmware Update</a>
        <a href="/hw_test">Hardware Test</a>
        <a href="/logout" style="float:right;">Logout</a>
    </div>
    <div class="container">
        <div class="upload-form">
            <h1>Firmware Update</h1>
            <form id="upload_form" action="/update" method="POST" enctype="multipart/form-data">
                <input type="file" name="update" id="file" required>
                <button type="submit">Update</button>
            </form>
            <div class="progress-bar">
                <div class="progress" id="progressBar">0%</div>
            </div>
        </div>
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
                alert('Upload finished. Device will now reboot.');
                setTimeout(function(){ window.location.href = '/'; }, 3000);
            };
            xhr.send(data);
        });
    </script>
</body>
</html>
)=====";

const char HTML_HW_TEST_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <title>Hardware Test</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 20px; }
        .container { max-width: 600px; margin: auto; background: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .test-group { border: 1px solid #ddd; padding: 15px; margin-top: 20px; border-radius: 5px; }
        h2 { color: #555; border-bottom: 1px solid #eee; padding-bottom: 10px; margin-top: 0; }
        .gpio-control { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .gpio-control label { font-weight: bold; }
        .btn { padding: 10px 15px; border: none; border-radius: 5px; color: white; cursor: pointer; font-size: 14px; }
        .btn-on { background-color: #28a745; }
        .btn-off { background-color: #dc3545; }
        .btn-back { display: inline-block; margin-top: 20px; background-color: #007bff; text-decoration: none; text-align: center; width: 100%; box-sizing: border-box; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Hardware Test</h1>
        <div class="test-group">
            <h2>Flash Light & Buzzer</h2>
            <div class="gpio-control">
                <label>Red Light (GPIO 9)</label>
                <div>
                    <button class="btn btn-on" onclick="controlGPIO(9, 'on')">ON</button>
                    <button class="btn btn-off" onclick="controlGPIO(9, 'off')">OFF</button>
                </div>
            </div>
            <div class="gpio-control">
                <label>Green Light (GPIO 20)</label>
                <div>
                    <button class="btn btn-on" onclick="controlGPIO(20, 'on')">ON</button>
                    <button class="btn btn-off" onclick="controlGPIO(20, 'off')">OFF</button>
                </div>
            </div>
            <div class="gpio-control">
                <label>Blue Light (GPIO 19)</label>
                <div>
                    <button class="btn btn-on" onclick="controlGPIO(19, 'on')">ON</button>
                    <button class="btn btn-off" onclick="controlGPIO(19, 'off')">OFF</button>
                </div>
            </div>
            <div class="gpio-control">
                <label>Buzzer (GPIO 48)</label>
                <div>
                    <button class="btn btn-on" onclick="controlGPIO(48, 'on')">ON</button>
                    <button class="btn btn-off" onclick="controlGPIO(48, 'off')">OFF</button>
                </div>
            </div>
        </div>
        <div class="test-group">
            <h2>Beacon Bridge</h2>
             <div class="gpio-control">
                <label>Beacon Status LED (GPIO 2)</label>
                <div>
                    <button class="btn btn-on" onclick="controlBeaconLED('on')">ON</button>
                    <button class="btn btn-off" onclick="controlBeaconLED('off')">OFF</button>
                </div>
            </div>
        </div>
        <a href="/logout" class="btn btn-back">Exit Test Mode & Logout</a>
    </div>
    <script>
        function controlGPIO(gpio, action) {
            fetch(`/hw_test?gpio=${gpio}&action=${action}`);
        }
        function controlBeaconLED(action) {
            fetch(`/hw_test?beacon_led=${action}`);
        }
    </script>
</body>
</html>
)=====";