// Weather companion for Sliding Text watchface.
// Requires a free OpenWeatherMap API key: https://openweathermap.org/api
// Replace the placeholder below with your own key.
var OWM_API_KEY = 'YOUR_OPENWEATHERMAP_API_KEY';

// Map OWM condition groups to short display strings
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

function xhrGet(url, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { callback(this.responseText); };
  xhr.onerror = function() { console.log('XHR error for: ' + url); };
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

      var owmGroup = json.weather[0].main;
      var condition = CONDITION_MAP[owmGroup] || owmGroup.toLowerCase().substring(0, 8);
      var tempHigh  = Math.round(json.main.temp_max);
      var tempLow   = Math.round(json.main.temp_min);

      Pebble.sendAppMessage(
        {
          'KEY_WEATHER_CONDITION': condition,
          'KEY_TEMP_HIGH': tempHigh,
          'KEY_TEMP_LOW':  tempLow
        },
        function() { console.log('Weather sent: ' + condition + ' ' + tempHigh + '/' + tempLow); },
        function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
      );
    } catch(err) {
      console.log('Weather parse error: ' + err);
    }
  });
}

Pebble.addEventListener('ready', function() {
  if (OWM_API_KEY === 'YOUR_OPENWEATHERMAP_API_KEY') {
    console.log('No API key set — skipping weather fetch.');
    return;
  }

  window.navigator.geolocation.getCurrentPosition(
    function(pos) { fetchAndSendWeather(pos.coords); },
    function(err) { console.log('Location error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
});
