// Weather companion for Sliding Text watchface.
// Open Settings in the Pebble app to enter your OpenWeatherMap API key.
// Free keys: https://openweathermap.org/api  (Current Weather Data plan)

var OWM_API_KEY = '';

var CONDITION_MAP = {
  'Clear':        'sunny',
  'Clouds':       'cloudy',
  'Rain':         'rainy',
  'Drizzle':      'drizzle',
  'Thunderstorm': 'storm',
  'Snow':         'snowy',
  'Mist':         'misty',
  'Fog':          'foggy',
  'Haze':         'hazy',
  'Smoke':        'smoky',
  'Dust':         'dusty',
  'Sand':         'sandy',
  'Tornado':      'tornado',
};

// ===== Weather fetch =====

function xhrGet(url, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { callback(this.responseText); };
  xhr.onerror = function() { console.log('XHR error: ' + url); };
  xhr.open('GET', url);
  xhr.send();
}

function fetchAndSendWeather(coords) {
  var url = 'https://api.openweathermap.org/data/2.5/weather' +
            '?lat=' + coords.latitude +
            '&lon=' + coords.longitude +
            '&units=imperial' +
            '&appid=' + OWM_API_KEY;

  xhrGet(url, function(responseText) {
    try {
      var json = JSON.parse(responseText);
      var owmGroup  = json.weather[0].main;
      var condition = CONDITION_MAP[owmGroup] || owmGroup.toLowerCase().substring(0, 8);
      var tempHigh  = Math.round(json.main.temp_max);
      var tempLow   = Math.round(json.main.temp_min);
      var city      = (json.name || '').substring(0, 20);

      Pebble.sendAppMessage(
        {
          'KEY_WEATHER_CONDITION': condition,
          'KEY_TEMP_HIGH': tempHigh,
          'KEY_TEMP_LOW': tempLow,
          'KEY_CITY_NAME': city
        },
        function() { console.log('Weather sent: ' + condition + ' ' + tempHigh + '/' + tempLow + ' ' + city); },
        function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
      );
    } catch(err) {
      console.log('Weather parse error: ' + err);
    }
  });
}

function fetchWeatherByLocation() {
  if (!OWM_API_KEY) {
    console.log('No API key — open Settings in the Pebble app to add one.');
    return;
  }
  window.navigator.geolocation.getCurrentPosition(
    function(pos) { fetchAndSendWeather(pos.coords); },
    function(err) { console.log('Location error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
}

// ===== Settings page =====

function buildConfigPage(currentKey) {
  return '<!DOCTYPE html><html>' +
    '<head>' +
      '<meta name="viewport" content="width=device-width,initial-scale=1">' +
      '<title>Sliding Text Settings</title>' +
      '<style>' +
        '*{box-sizing:border-box;margin:0;padding:0}' +
        'body{font-family:-apple-system,sans-serif;background:#1a1a1a;color:#fff;padding:24px 20px}' +
        'h1{font-size:20px;font-weight:600;color:#cc5500;margin-bottom:20px}' +
        'label{display:block;font-size:13px;color:#aaa;margin-bottom:6px;text-transform:uppercase;letter-spacing:.5px}' +
        'input[type=text]{' +
          'display:block;width:100%;padding:11px 12px;' +
          'font-size:15px;font-family:monospace;' +
          'background:#2a2a2a;border:1px solid #444;color:#fff;border-radius:6px;' +
          'outline:none' +
        '}' +
        'input[type=text]:focus{border-color:#cc5500}' +
        '.hint{font-size:12px;color:#666;margin-top:8px;line-height:1.5}' +
        '.hint a{color:#cc5500;text-decoration:none}' +
        '.actions{display:flex;gap:10px;margin-top:24px}' +
        'button{flex:1;padding:13px;font-size:16px;font-weight:600;border:none;border-radius:6px;cursor:pointer}' +
        '.save{background:#cc5500;color:#fff}' +
        '.cancel{background:#2a2a2a;color:#aaa}' +
      '</style>' +
    '</head>' +
    '<body>' +
      '<h1>Sliding Text</h1>' +
      '<label for="key">OpenWeatherMap API Key</label>' +
      '<input type="text" id="key" spellcheck="false" autocorrect="off" autocapitalize="off"' +
             ' placeholder="Paste your API key here" value="' + currentKey + '">' +
      '<p class="hint">' +
        'Free key at <a href="https://openweathermap.org/api">openweathermap.org</a>' +
        ' &mdash; sign up and use the <em>Current Weather Data</em> plan.' +
      '</p>' +
      '<div class="actions">' +
        '<button class="cancel" onclick="cancel()">Cancel</button>' +
        '<button class="save" onclick="save()">Save</button>' +
      '</div>' +
      '<script>' +
        'function save(){' +
          'var k=document.getElementById("key").value.trim();' +
          'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({apiKey:k}));' +
        '}' +
        'function cancel(){location.href="pebblejs://close";}' +
      '<\/script>' +
    '</body></html>';
}

// ===== Pebble events =====

Pebble.addEventListener('ready', function() {
  OWM_API_KEY = localStorage.getItem('owm_api_key') || '';
  fetchWeatherByLocation();
});

Pebble.addEventListener('showConfiguration', function() {
  var currentKey = localStorage.getItem('owm_api_key') || '';
  Pebble.openURL('data:text/html,' + encodeURIComponent(buildConfigPage(currentKey)));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') return;
  try {
    var config = JSON.parse(decodeURIComponent(e.response));
    if (typeof config.apiKey === 'string') {
      localStorage.setItem('owm_api_key', config.apiKey);
      OWM_API_KEY = config.apiKey;
      fetchWeatherByLocation();
    }
  } catch(err) {
    console.log('Config parse error: ' + err);
  }
});
