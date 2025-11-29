/**
 * Tiny HTTP Webserver for M5 Atom Debugging and Control
 * 
 * Provides:
 * - Device status dashboard
 * - LED/scene control
 * - Rule store viewer
 * - Sensor data
 * - REST API endpoints
 */

#include "webserver.h"
#include "device_state.h"
#include "rule_store.h"
#include "scene_controller.h"
#include "sensor_reader.h"
#include "sensor_integration.h"
#include "somnus_ble.h"
#include "wifi_manager.h"
#include "ota_updater.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "webserver"
#define DEFAULT_PORT 80

struct webserver {
    httpd_handle_t server;
    webserver_config_t config;
    bool running;
};

// Forward declarations
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_rules_handler(httpd_req_t *req);
static esp_err_t api_led_handler(httpd_req_t *req);
static esp_err_t api_sensors_handler(httpd_req_t *req);
static esp_err_t api_control_handler(httpd_req_t *req);
static esp_err_t api_ota_status_handler(httpd_req_t *req);
static esp_err_t api_ota_check_handler(httpd_req_t *req);
static esp_err_t api_ota_install_handler(httpd_req_t *req);
static esp_err_t api_ble_logs_handler(httpd_req_t *req);
static esp_err_t api_system_handler(httpd_req_t *req);
static const char* get_content_type(const char *path);

// HTML dashboard
static const char* html_dashboard = 
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>M5 Atom Debug Dashboard</title>"
"<style>"
"* { margin: 0; padding: 0; box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #1a1a1a; color: #e0e0e0; padding: 20px; }"
".container { max-width: 1200px; margin: 0 auto; }"
"h1 { color: #4CAF50; margin-bottom: 20px; }"
".card { background: #2a2a2a; border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
".card h2 { color: #64B5F6; margin-bottom: 15px; font-size: 1.2em; }"
".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
".status-item { padding: 10px; background: #333; border-radius: 4px; }"
".status-item label { display: block; font-size: 0.9em; color: #aaa; margin-bottom: 5px; }"
".status-item .value { font-size: 1.1em; font-weight: bold; }"
".status-on { color: #4CAF50; }"
".status-off { color: #f44336; }"
".button { background: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 1em; margin: 5px; }"
".button:hover { background: #45a049; }"
".button-danger { background: #f44336; }"
".button-danger:hover { background: #da190b; }"
"input[type='text'], input[type='number'], select { padding: 8px; border: 1px solid #555; border-radius: 4px; background: #333; color: #e0e0e0; width: 100%; margin: 5px 0; }"
".led-control { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }"
".led-control input[type='range'] { flex: 1; min-width: 150px; }"
".json-viewer { background: #1e1e1e; padding: 15px; border-radius: 4px; overflow-x: auto; font-family: 'Courier New', monospace; font-size: 0.9em; white-space: pre-wrap; }"
".refresh-btn { position: fixed; bottom: 20px; right: 20px; background: #2196F3; padding: 15px; border-radius: 50%; width: 60px; height: 60px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }"
".ota-section { margin-top: 15px; }"
".ota-status { padding: 10px; background: #333; border-radius: 4px; margin: 10px 0; }"
".ota-progress { width: 100%; height: 20px; background: #1e1e1e; border-radius: 10px; overflow: hidden; margin: 10px 0; }"
".ota-progress-bar { height: 100%; background: linear-gradient(90deg, #4CAF50, #8BC34A); transition: width 0.3s; display: flex; align-items: center; justify-content: center; color: white; font-size: 0.8em; }"
".ota-info { font-size: 0.9em; color: #aaa; margin: 5px 0; }"
".button-primary { background: #2196F3; }"
".button-primary:hover { background: #1976D2; }"
".button:disabled { opacity: 0.5; cursor: not-allowed; }"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>🤖 M5 Atom Echo Debug Dashboard</h1>"
"<div class='card'><h2>Device Status</h2>"
"<div class='status-grid' id='status-grid'></div>"
"</div>"
"<div class='card'><h2>LED Control</h2>"
"<div class='led-control'>"
"<button class='button' onclick='setScene(\"warm_dim\")'>Warm Dim</button>"
"<button class='button' onclick='setScene(\"cool_bright\")'>Cool Bright</button>"
"<button class='button' onclick='setScene(\"off\")'>Off</button>"
"<br>"
"<label>Color: <input type='color' id='led-color' value='#ff8800'></label>"
"<label>Brightness: <input type='range' id='led-brightness' min='0' max='100' value='50'></label>"
"<button class='button' onclick='setColor()'>Set Color</button>"
"</div>"
"</div>"
"<div class='card'><h2>Rules Store</h2>"
"<button class='button' onclick='loadRules()'>Refresh Rules</button>"
"<div class='json-viewer' id='rules-viewer'>Loading...</div>"
"</div>"
"<div class='card'><h2>Sensor Data</h2>"
"<button class='button' onclick='loadSensors()'>Refresh Sensors</button>"
"<div class='sensor-grid' id='sensor-grid' style='display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-top: 15px;'></div>"
"<div class='json-viewer' id='sensors-viewer' style='margin-top: 15px; display: none;'></div>"
"<button class='button' onclick='toggleSensorView()' style='margin-top: 10px;'>Toggle JSON View</button>"
"</div>"
"<div class='card'><h2>BLE Logs</h2>"
"<button class='button' onclick='loadBLELogs()'>Refresh Logs</button>"
"<button class='button' onclick='clearBLELogs()' style='margin-left: 10px;'>Clear View</button>"
"<div class='ble-log-viewer' id='ble-log-viewer' style='margin-top: 15px; max-height: 400px; overflow-y: auto; background: #1e1e1e; padding: 15px; border-radius: 4px; font-family: monospace; font-size: 0.85em;'></div>"
"</div>"
"<div class='card'><h2>💾 Memory & Tasks</h2>"
"<button class='button' onclick='loadSystemInfo()'>Refresh System Info</button>"
"<div id='system-info' style='margin-top: 15px;'>"
"<div class='status-grid' id='memory-grid' style='margin-bottom: 20px;'></div>"
"<h3 style='color: #64B5F6; margin-top: 20px; margin-bottom: 10px;'>FreeRTOS Tasks</h3>"
"<div id='tasks-table' style='overflow-x: auto;'></div>"
"</div>"
"</div>"
"<div class='card'><h2>🔄 Firmware Update (OTA)</h2>"
"<div class='ota-section'>"
"<div class='ota-status' id='ota-status'>Loading status...</div>"
"<div class='ota-progress' id='ota-progress-container' style='display:none;'>"
"<div class='ota-progress-bar' id='ota-progress-bar' style='width:0%;'>0%</div>"
"</div>"
"<div class='ota-info' id='ota-info'></div>"
"<button class='button button-primary' onclick='checkOTA()' id='ota-check-btn'>Check for Updates</button>"
"<button class='button' onclick='installOTA()' id='ota-install-btn' style='display:none;'>Install Update</button>"
"</div>"
"</div>"
"</div>"
"<button class='button refresh-btn' onclick='refreshAll()' title='Refresh All'>🔄</button>"
"<script>"
"function refreshAll() { loadStatus(); loadRules(); loadSensors(); loadOTAStatus(); }"
"function loadStatus() {"
"  fetch('/api/status').then(r=>r.json()).then(data=>{"
"    const grid = document.getElementById('status-grid');"
"    grid.innerHTML = '';"
"    const items = ["
"      {label:'WiFi', value: data.wifi_connected ? 'Connected: ' + (data.wifi_ssid || 'N/A') : 'Disconnected', status: data.wifi_connected},"
"      {label:'IP Address', value: data.ip_address || 'N/A'},"
"      {label:'AWS IoT', value: data.aws_connected ? 'Connected' : 'Disconnected', status: data.aws_connected},"
"      {label:'Spotify', value: data.spotify_ready ? 'Ready' : 'Not Ready', status: data.spotify_ready},"
"      {label:'Gemini', value: data.gemini_ready ? 'Ready' : 'Not Ready', status: data.gemini_ready},"
"      {label:'Lights', value: data.lights_available ? 'Available' : 'Not Available', status: data.lights_available},"
"      {label:'Firmware', value: data.firmware_version || '0.1'},"
"      {label:'Free Heap', value: (data.free_heap / 1024).toFixed(1) + ' KB'},"
"      {label:'Uptime', value: (data.uptime_seconds / 60).toFixed(1) + ' min'}"
"    ];"
"    items.forEach(item=>{"
"      const div = document.createElement('div');"
"      div.className = 'status-item';"
"      div.innerHTML = `<label>${item.label}</label><div class='value ${item.status !== undefined ? (item.status ? 'status-on' : 'status-off') : ''}'>${item.value}</div>`;"
"      grid.appendChild(div);"
"    });"
"  }).catch(e=>console.error('Status error:', e));"
"}"
"function setScene(scene) {"
"  fetch('/api/led', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({scene})})"
"    .then(r=>r.json()).then(d=>alert(d.success ? 'Scene set!' : 'Error: ' + d.error))"
"    .catch(e=>alert('Error: ' + e));"
"}"
"function setColor() {"
"  const color = document.getElementById('led-color').value;"
"  const brightness = parseInt(document.getElementById('led-brightness').value) / 100;"
"  const r = parseInt(color.substr(1,2), 16);"
"  const g = parseInt(color.substr(3,2), 16);"
"  const b = parseInt(color.substr(5,2), 16);"
"  fetch('/api/led', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({r,g,b,brightness})})"
"    .then(r=>r.json()).then(d=>alert(d.success ? 'Color set!' : 'Error: ' + d.error))"
"    .catch(e=>alert('Error: ' + e));"
"}"
"function loadRules() {"
"  fetch('/api/rules').then(r=>r.json()).then(data=>{"
"    document.getElementById('rules-viewer').textContent = JSON.stringify(data, null, 2);"
"  }).catch(e=>document.getElementById('rules-viewer').textContent = 'Error: ' + e);"
"}"
"let showSensorJson = false;"
"function toggleSensorView() {"
"  showSensorJson = !showSensorJson;"
"  const grid = document.getElementById('sensor-grid');"
"  const viewer = document.getElementById('sensors-viewer');"
"  if (showSensorJson) {"
"    grid.style.display = 'none';"
"    viewer.style.display = 'block';"
"  } else {"
"    grid.style.display = 'grid';"
"    viewer.style.display = 'none';"
"  }"
"}"
"function loadSensors() {"
"  fetch('/api/sensors').then(r=>r.json()).then(data=>{"
"    const grid = document.getElementById('sensor-grid');"
"    const viewer = document.getElementById('sensors-viewer');"
"    grid.innerHTML = '';"
"    viewer.textContent = JSON.stringify(data, null, 2);"
"    "
"    if (data.sensors) {"
"      const s = data.sensors;"
"      const items = [];"
"      "
"      if (s.temperature_c !== undefined) {"
"        const synth = s.temperature_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'Temperature', value: s.temperature_c.toFixed(1) + '°C' + synth, icon: '🌡️'});"
"      }"
"      if (s.humidity_rh !== undefined) {"
"        const synth = s.humidity_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'Humidity', value: s.humidity_rh.toFixed(1) + '%' + synth, icon: '💧'});"
"      }"
"      if (s.co2_ppm !== undefined) {"
"        const synth = s.co2_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'CO₂', value: s.co2_ppm.toFixed(0) + ' ppm' + synth, icon: '🌬️'});"
"      }"
"      if (s.voc_index !== undefined) {"
"        const synth = s.voc_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'VOC Index', value: s.voc_index + synth, icon: '💨'});"
"      }"
"      if (s.ambient_lux !== undefined) {"
"        const synth = s.ambient_lux_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'Ambient Light', value: s.ambient_lux + ' lux' + synth, icon: '💡'});"
"      }"
"      if (s.proximity !== undefined) {"
"        const synth = s.proximity_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'Proximity', value: s.proximity + synth, icon: '👋'});"
"      }"
"      if (s.pm2_5_ug_m3 !== undefined) {"
"        const synth = s.pm2_5_synthetic ? ' (synthetic)' : '';"
"        items.push({label: 'PM2.5', value: s.pm2_5_ug_m3.toFixed(1) + ' μg/m³' + synth, icon: '🌫️'});"
"      }"
"      "
"      items.forEach(item=>{"
"        const div = document.createElement('div');"
"        div.className = 'status-item';"
"        div.innerHTML = `<label>${item.icon} ${item.label}</label><div class='value'>${item.value}</div>`;"
"        grid.appendChild(div);"
"      });"
"      "
"      if (items.length === 0) {"
"        grid.innerHTML = '<div class=\"status-item\"><label>No sensor data available</label></div>';"
"      }"
"    }"
"  }).catch(e=>{"
"    document.getElementById('sensor-grid').innerHTML = '<div class=\"status-item\"><label>Error</label><div class=\"value status-off\">' + e + '</div></div>';"
"    document.getElementById('sensors-viewer').textContent = 'Error: ' + e;"
"  });"
"}"
"let otaUpdateAvailable = false;"
"let otaDownloadUrl = null;"
"let otaLatestVersion = null;"
"function loadOTAStatus() {"
"  fetch('/api/ota/status').then(r=>r.json()).then(data=>{"
"    const statusEl = document.getElementById('ota-status');"
"    const progressContainer = document.getElementById('ota-progress-container');"
"    const progressBar = document.getElementById('ota-progress-bar');"
"    const infoEl = document.getElementById('ota-info');"
"    const installBtn = document.getElementById('ota-install-btn');"
"    "
"    const statusNames = ['Idle', 'Checking', 'Update Available', 'Downloading', 'Installing', 'Success', 'Failed'];"
"    const status = data.status !== undefined ? statusNames[data.status] || 'Unknown' : 'Unknown';"
"    const message = data.status_message || 'Unknown';"
"    const progress = data.progress || 0;"
"    "
"    statusEl.innerHTML = `<strong>Status:</strong> ${status}<br><small>${message}</small>`;"
"    "
"    if (data.status === 3 || data.status === 4) {"
"      progressContainer.style.display = 'block';"
"      progressBar.style.width = progress + '%';"
"      progressBar.textContent = progress + '%';"
"    } else {"
"      progressContainer.style.display = 'none';"
"    }"
"    "
"    if (data.status === 2) {"
"      installBtn.style.display = 'inline-block';"
"      infoEl.textContent = 'Update available! Click Install to update firmware.';"
"    } else if (data.status === 5) {"
"      infoEl.textContent = 'Update successful! Device will reboot shortly.';"
"      installBtn.style.display = 'none';"
"    } else if (data.status === 6) {"
"      infoEl.textContent = 'Update failed. Check logs for details.';"
"      installBtn.style.display = 'none';"
"    } else {"
"      installBtn.style.display = 'none';"
"      infoEl.textContent = '';"
"    }"
"  }).catch(e=>{"
"    document.getElementById('ota-status').textContent = 'OTA not available: ' + e;"
"  });"
"}"
"function checkOTA() {"
"  const btn = document.getElementById('ota-check-btn');"
"  btn.disabled = true;"
"  btn.textContent = 'Checking...';"
"  document.getElementById('ota-status').textContent = 'Checking for updates...';"
"  "
"  fetch('/api/ota/check', {method:'POST'}).then(r=>r.json()).then(data=>{"
"    btn.disabled = false;"
"    btn.textContent = 'Check for Updates';"
"    "
"    if (data.success) {"
"      if (data.available) {"
"        otaUpdateAvailable = true;"
"        otaDownloadUrl = data.download_url;"
"        otaLatestVersion = data.latest_version;"
"        document.getElementById('ota-status').innerHTML = `<strong>Update Available!</strong><br><small>Latest version: ${data.latest_version}</small>`;"
"        document.getElementById('ota-info').textContent = `Version ${data.latest_version} is available. Click Install to update.`;"
"        document.getElementById('ota-install-btn').style.display = 'inline-block';"
"      } else {"
"        document.getElementById('ota-status').innerHTML = '<strong>Up to Date</strong><br><small>No updates available</small>';"
"        document.getElementById('ota-info').textContent = 'Your firmware is up to date.';"
"        document.getElementById('ota-install-btn').style.display = 'none';"
"      }"
"    } else {"
"      document.getElementById('ota-status').innerHTML = `<strong>Error</strong><br><small>${data.error || 'Failed to check for updates'}</small>`;"
"      document.getElementById('ota-info').textContent = '';"
"    }"
"    loadOTAStatus();"
"  }).catch(e=>{"
"    btn.disabled = false;"
"    btn.textContent = 'Check for Updates';"
"    document.getElementById('ota-status').innerHTML = `<strong>Error</strong><br><small>${e.message}</small>`;"
"  });"
"}"
"function installOTA() {"
"  if (!confirm('This will install the firmware update and reboot the device. Continue?')) return;"
"  "
"  const btn = document.getElementById('ota-install-btn');"
"  btn.disabled = true;"
"  btn.textContent = 'Installing...';"
"  document.getElementById('ota-status').textContent = 'Starting update installation...';"
"  document.getElementById('ota-progress-container').style.display = 'block';"
"  "
"  const payload = otaDownloadUrl ? {url: otaDownloadUrl} : {};"
"  "
"  fetch('/api/ota/install', {"
"    method:'POST',"
"    headers:{'Content-Type':'application/json'},"
"    body:JSON.stringify(payload)"
"  }).then(r=>r.json()).then(data=>{"
"    if (data.success) {"
"      document.getElementById('ota-status').innerHTML = '<strong>Installing...</strong><br><small>Update in progress, device will reboot when complete</small>';"
"      document.getElementById('ota-info').textContent = data.message || 'Update started successfully.';"
"      "
"      let progress = 0;"
"      const progressInterval = setInterval(()=>{"
"        progress = Math.min(progress + 2, 90);"
"        const progressBar = document.getElementById('ota-progress-bar');"
"        progressBar.style.width = progress + '%';"
"        progressBar.textContent = progress + '%';"
"        "
"        loadOTAStatus();"
"        "
"        if (progress >= 90) clearInterval(progressInterval);"
"      }, 500);"
"    } else {"
"      btn.disabled = false;"
"      btn.textContent = 'Install Update';"
"      document.getElementById('ota-status').innerHTML = `<strong>Error</strong><br><small>${data.error || 'Installation failed'}</small>`;"
"      document.getElementById('ota-info').textContent = '';"
"    }"
"  }).catch(e=>{"
"    btn.disabled = false;"
"    btn.textContent = 'Install Update';"
"    document.getElementById('ota-status').innerHTML = `<strong>Error</strong><br><small>${e.message}</small>`;"
"  });"
"}"
"setInterval(()=>{ loadOTAStatus(); }, 2000);"
"setInterval(refreshAll, 5000);"
"function loadBLELogs() {"
"  fetch('/api/ble/logs').then(r=>r.json()).then(data=>{"
"    const viewer = document.getElementById('ble-log-viewer');"
"    if (data.logs && data.logs.length > 0) {"
"      let html = '';"
"      data.logs.forEach((log, idx) => {"
"        const time = new Date(log.timestamp_ms);"
"        const timeStr = time.toLocaleTimeString();"
"        const typeColors = {"
"          'CONNECT': '#4CAF50',"
"          'DISCONNECT': '#f44336',"
"          'RX': '#2196F3',"
"          'TX': '#FF9800',"
"          'SUBSCRIBE': '#9C27B0'"
"        };"
"        const color = typeColors[log.type] || '#aaa';"
"        html += `<div style='margin: 5px 0; padding: 5px; border-left: 3px solid ${color}; background: #2a2a2a;'>`;"
"        html += `<span style='color: ${color}; font-weight: bold;'>[${log.type}]</span> `;"
"        html += `<span style='color: #888;'>${timeStr}</span> `;"
"        html += `<span style='color: #e0e0e0;'>${log.message}</span>`;"
"        html += `</div>`;"
"      });"
"      viewer.innerHTML = html;"
"      viewer.scrollTop = viewer.scrollHeight;"
"    } else {"
"      viewer.innerHTML = '<div style=\"color: #888; padding: 10px;\">No BLE logs yet</div>';"
"    }"
"  }).catch(e=>{"
"    document.getElementById('ble-log-viewer').innerHTML = '<div style=\"color: #f44336;\">Error loading logs: ' + e + '</div>';"
"  });"
"}"
"function clearBLELogs() {"
"  document.getElementById('ble-log-viewer').innerHTML = '';"
"}"
"function loadSystemInfo() {"
"  fetch('/api/system').then(r=>r.json()).then(data=>{"
"    const memGrid = document.getElementById('memory-grid');"
"    if (data.memory) {"
"      const mem = data.memory;"
"      const freeMB = (mem.free_bytes / 1024 / 1024).toFixed(2);"
"      const minMB = (mem.min_free_bytes / 1024 / 1024).toFixed(2);"
"      const largestMB = (mem.largest_free_block_bytes / 1024 / 1024).toFixed(2);"
"      memGrid.innerHTML = '';"
"      memGrid.innerHTML += '<div class=\"status-item\"><label>Free Heap</label><div class=\"value\">' + freeMB + ' MB</div></div>';"
"      memGrid.innerHTML += '<div class=\"status-item\"><label>Min Free</label><div class=\"value\">' + minMB + ' MB</div></div>';"
"      memGrid.innerHTML += '<div class=\"status-item\"><label>Largest Block</label><div class=\"value\">' + largestMB + ' MB</div></div>';"
"      memGrid.innerHTML += '<div class=\"status-item\"><label>Free %</label><div class=\"value\">' + mem.free_percent + '%</div></div>';"
"    }"
"    const tasksTable = document.getElementById('tasks-table');"
"    if (data.tasks && data.tasks.length > 0) {"
"      let html = '<table style=\"width:100%; border-collapse: collapse; font-size: 0.9em;\">';"
"      html += '<thead><tr style=\"background: #333; color: #64B5F6;\"><th style=\"padding: 8px; text-align: left; border-bottom: 2px solid #555;\">Task Name</th>';"
"      html += '<th style=\"padding: 8px; text-align: left; border-bottom: 2px solid #555;\">State</th>';"
"      html += '<th style=\"padding: 8px; text-align: right; border-bottom: 2px solid #555;\">Priority</th>';"
"      html += '<th style=\"padding: 8px; text-align: right; border-bottom: 2px solid #555;\">Stack High Water (words)</th>';"
"      html += '<th style=\"padding: 8px; text-align: right; border-bottom: 2px solid #555;\">Runtime</th></tr></thead><tbody>';"
"      const stateNames = ['Running', 'Ready', 'Blocked', 'Suspended', 'Deleted'];"
"      data.tasks.forEach(task => {"
"        const stateName = stateNames[task.state] || 'Unknown';"
"        const stackUsed = task.stack_high_water_mark || 0;"
"        html += '<tr style=\"border-bottom: 1px solid #444;\">';"
"        html += '<td style=\"padding: 8px; font-family: monospace;\">' + task.name + '</td>';"
"        html += '<td style=\"padding: 8px;\">' + stateName + '</td>';"
"        html += '<td style=\"padding: 8px; text-align: right;\">' + task.priority + '</td>';"
"        html += '<td style=\"padding: 8px; text-align: right; color: ' + (stackUsed < 100 ? '#f44336' : stackUsed < 500 ? '#ff9800' : '#4CAF50') + ';\">' + stackUsed + '</td>';"
"        html += '<td style=\"padding: 8px; text-align: right;\">' + (task.runtime || 0) + '</td>';"
"        html += '</tr>';"
"      });"
"      html += '</tbody></table>';"
"      tasksTable.innerHTML = html;"
"    } else {"
"      tasksTable.innerHTML = '<p style=\"color: #aaa;\">No task information available</p>';"
"    }"
"  }).catch(err=>{"
"    console.error('Failed to load system info:', err);"
"  });"
"}"
"function refreshAll() { loadStatus(); loadRules(); loadSensors(); loadOTAStatus(); loadBLELogs(); loadSystemInfo(); }"
"setInterval(()=>{ loadOTAStatus(); }, 2000);"
"setInterval(refreshAll, 5000);"
"refreshAll();"
"</script>"
"</body></html>";

// Helper to get content type (unused for now, kept for future use)
__attribute__((unused)) static const char* get_content_type(const char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    return "text/plain";
}

// Root handler - serve dashboard
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_dashboard, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API: Get device status
static esp_err_t api_status_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    
    // Get device state JSON and merge it into our response
    char *device_json = device_state_to_json();
    if (device_json) {
        cJSON *device_obj = cJSON_Parse(device_json);
        if (device_obj) {
            // Merge device state fields into root object by iterating child items
            cJSON *item = device_obj->child;
            while (item) {
                cJSON *next = item->next;
                cJSON_AddItemToObject(json, item->string, cJSON_Duplicate(item, 1));
                item = next;
            }
            cJSON_Delete(device_obj);
        }
        free(device_json);
    }
    
    // Add/override system info
    cJSON_AddBoolToObject(json, "wifi_connected", wifi_manager_is_connected());
    cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "uptime_seconds", esp_timer_get_time() / 1000000);
    
    // Get IP address
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            cJSON_AddStringToObject(json, "ip_address", ip_str);
        }
    }
    
    // Get WiFi SSID (override if device_state didn't have it)
    // Note: esp_wifi_sta_get_ap_info may not be available in all ESP-IDF versions
    // Using device_state data instead which already has WiFi info
    
    // Add lights_available from config
    cJSON_AddBoolToObject(json, "lights_available", ws->config.led_handle != NULL);
    
    // Add firmware version if OTA updater is available
    if (ws->config.ota_updater) {
        ota_updater_t *ota = (ota_updater_t *)ws->config.ota_updater;
        const char *version = ota_updater_get_current_version(ota);
        cJSON_AddStringToObject(json, "firmware_version", version ? version : "0.1");
    } else {
        cJSON_AddStringToObject(json, "firmware_version", "0.1");
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get rules
static esp_err_t api_rules_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    
    if (ws->config.rule_store) {
        rule_store_snapshot_t snapshot = {0};
        if (rule_store_get_snapshot((rule_store_t *)ws->config.rule_store, &snapshot) == ESP_OK) {
            cJSON_AddStringToObject(json, "sha256", snapshot.sha256);
            if (snapshot.json) {
                cJSON *rules_obj = cJSON_Parse(snapshot.json);
                if (rules_obj) {
                    cJSON_AddItemToObject(json, "rules", rules_obj);
                } else {
                    cJSON_AddStringToObject(json, "rules", snapshot.json);
                }
            }
            rule_store_release_snapshot(&snapshot);
        } else {
            cJSON_AddStringToObject(json, "error", "Failed to get rule snapshot");
        }
    } else {
        cJSON_AddStringToObject(json, "error", "Rule store not available");
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: LED control
static esp_err_t api_led_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    
    if (!ws->config.led_handle) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "LED controller not available");
    } else {
        // Read request body
        char content[256];
        int ret = httpd_req_recv(req, content, sizeof(content) - 1);
        if (ret <= 0) {
            cJSON_AddBoolToObject(json, "success", false);
            cJSON_AddStringToObject(json, "error", "Failed to read request");
        } else {
            content[ret] = '\0';
            cJSON *req_json = cJSON_Parse(content);
            if (req_json) {
                scene_controller_t *scene = (scene_controller_t *)ws->config.led_handle;
                
                // Check for scene command
                cJSON *scene_item = cJSON_GetObjectItem(req_json, "scene");
                if (scene_item && cJSON_IsString(scene_item)) {
                    const char *scene_id = cJSON_GetStringValue(scene_item);
                    if (strcmp(scene_id, "off") == 0) {
                        err = scene_controller_set_light_color(scene, 0, 0, 0, 0.0f, 0);
                    } else {
                        err = scene_controller_apply_light_scene(scene, scene_id, 500);
                    }
                }
                // Check for color command
                else {
                    cJSON *r_item = cJSON_GetObjectItem(req_json, "r");
                    cJSON *g_item = cJSON_GetObjectItem(req_json, "g");
                    cJSON *b_item = cJSON_GetObjectItem(req_json, "b");
                    cJSON *brightness_item = cJSON_GetObjectItem(req_json, "brightness");
                    
                    if (r_item && g_item && b_item) {
                        uint8_t r = (uint8_t)cJSON_GetNumberValue(r_item);
                        uint8_t g = (uint8_t)cJSON_GetNumberValue(g_item);
                        uint8_t b = (uint8_t)cJSON_GetNumberValue(b_item);
                        float brightness = brightness_item ? (float)cJSON_GetNumberValue(brightness_item) : 0.5f;
                        err = scene_controller_set_light_color(scene, r, g, b, brightness, 500);
                    } else {
                        err = ESP_ERR_INVALID_ARG;
                    }
                }
                
                cJSON_Delete(req_json);
                cJSON_AddBoolToObject(json, "success", err == ESP_OK);
                if (err != ESP_OK) {
                    cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
                }
            } else {
                cJSON_AddBoolToObject(json, "success", false);
                cJSON_AddStringToObject(json, "error", "Invalid JSON");
            }
        }
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get sensor data
static esp_err_t api_sensors_handler(httpd_req_t *req) {
    (void)req;
    cJSON *json = cJSON_CreateObject();
    
    // Get sensor data from sensor integration
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    
    // Add timestamp
    cJSON_AddNumberToObject(json, "timestamp_ms", sensor_data.last_update_ms);
    
    // Create sensors object
    cJSON *sensors = cJSON_CreateObject();
    
    // SHT45 Temperature & Humidity
    if (sensor_data.sht45_available) {
        cJSON_AddBoolToObject(sensors, "sht45_available", true);
        cJSON_AddNumberToObject(sensors, "temperature_c", sensor_data.temperature_c);
        cJSON_AddNumberToObject(sensors, "humidity_rh", sensor_data.humidity_rh);
    } else {
        cJSON_AddBoolToObject(sensors, "sht45_available", false);
        // Synthetic data for demo
        cJSON_AddNumberToObject(sensors, "temperature_c", 22.5);
        cJSON_AddNumberToObject(sensors, "humidity_rh", 45.0);
        cJSON_AddBoolToObject(sensors, "temperature_synthetic", true);
        cJSON_AddBoolToObject(sensors, "humidity_synthetic", true);
    }
    
    // SGP40 VOC
    if (sensor_data.sgp40_available) {
        cJSON_AddBoolToObject(sensors, "sgp40_available", true);
        cJSON_AddNumberToObject(sensors, "voc_index", sensor_data.voc_index);
    } else {
        cJSON_AddBoolToObject(sensors, "sgp40_available", false);
        // Synthetic data
        cJSON_AddNumberToObject(sensors, "voc_index", 150);
        cJSON_AddBoolToObject(sensors, "voc_synthetic", true);
    }
    
    // SCD40 CO2, Temperature, Humidity
    if (sensor_data.scd40_available) {
        cJSON_AddBoolToObject(sensors, "scd40_available", true);
        cJSON_AddNumberToObject(sensors, "co2_ppm", sensor_data.co2_ppm);
        cJSON_AddNumberToObject(sensors, "temperature_co2_c", sensor_data.temperature_co2_c);
        cJSON_AddNumberToObject(sensors, "humidity_co2_rh", sensor_data.humidity_co2_rh);
    } else {
        cJSON_AddBoolToObject(sensors, "scd40_available", false);
        // Synthetic data
        cJSON_AddNumberToObject(sensors, "co2_ppm", 450);
        cJSON_AddNumberToObject(sensors, "temperature_co2_c", 22.3);
        cJSON_AddNumberToObject(sensors, "humidity_co2_rh", 44.5);
        cJSON_AddBoolToObject(sensors, "co2_synthetic", true);
    }
    
    // VCNL4040 Ambient Light & Proximity
    if (sensor_data.vcnl4040_available) {
        cJSON_AddBoolToObject(sensors, "vcnl4040_available", true);
        cJSON_AddNumberToObject(sensors, "ambient_lux", sensor_data.ambient_lux);
        cJSON_AddNumberToObject(sensors, "proximity", sensor_data.proximity);
    } else {
        cJSON_AddBoolToObject(sensors, "vcnl4040_available", false);
        // Synthetic data
        cJSON_AddNumberToObject(sensors, "ambient_lux", 250);
        cJSON_AddNumberToObject(sensors, "proximity", 0);
        cJSON_AddBoolToObject(sensors, "ambient_lux_synthetic", true);
        cJSON_AddBoolToObject(sensors, "proximity_synthetic", true);
    }
    
    // EC10 PM2.5 (stored in ec_ms_per_cm field)
    if (sensor_data.ec10_available) {
        cJSON_AddBoolToObject(sensors, "ec10_available", true);
        cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", sensor_data.ec_ms_per_cm);
    } else {
        cJSON_AddBoolToObject(sensors, "ec10_available", false);
        // Synthetic data
        cJSON_AddNumberToObject(sensors, "pm2_5_ug_m3", 12.5);
        cJSON_AddBoolToObject(sensors, "pm2_5_synthetic", true);
    }
    
    cJSON_AddItemToObject(json, "sensors", sensors);
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: General control endpoint
static esp_err_t api_control_handler(httpd_req_t *req) {
    (void)req;  // Unused for now
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Control endpoint - extend as needed");
    cJSON_AddStringToObject(json, "available", "LED control via /api/led");
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get BLE logs
static esp_err_t api_ble_logs_handler(httpd_req_t *req) {
    (void)req;
    char *json_str = NULL;
    esp_err_t err = somnus_ble_get_logs(&json_str, 0);  // 0 = all entries
    
    if (err != ESP_OK || !json_str) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", "Failed to get BLE logs");
        cJSON_AddNumberToObject(json, "error_code", err);
        json_str = cJSON_Print(json);
        cJSON_Delete(json);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    return ESP_OK;
}

// API: Get system info (memory and tasks)
static esp_err_t api_system_handler(httpd_req_t *req) {
    (void)req;
    cJSON *json = cJSON_CreateObject();
    
    // Memory information
    cJSON *memory = cJSON_CreateObject();
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    
    // Get total heap size (approximate - ESP32-S3 has ~512KB internal SRAM)
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_DEFAULT);
    size_t total_heap = heap_info.total_free_bytes + heap_info.total_allocated_bytes;
    int free_percent = total_heap > 0 ? (int)((free_heap * 100) / total_heap) : 0;
    
    cJSON_AddNumberToObject(memory, "free_bytes", free_heap);
    cJSON_AddNumberToObject(memory, "min_free_bytes", min_free_heap);
    cJSON_AddNumberToObject(memory, "largest_free_block_bytes", largest_free_block);
    cJSON_AddNumberToObject(memory, "total_bytes", total_heap);
    cJSON_AddNumberToObject(memory, "allocated_bytes", heap_info.total_allocated_bytes);
    cJSON_AddNumberToObject(memory, "free_percent", free_percent);
    cJSON_AddItemToObject(json, "memory", memory);
    
    // FreeRTOS task information
    // Note: Detailed task info requires configUSE_TRACE_FACILITY=1 in FreeRTOS config
    cJSON *tasks = cJSON_CreateArray();
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    cJSON_AddNumberToObject(json, "num_tasks", num_tasks);
    
    // Try to get detailed task info if trace facility is enabled
    #if configUSE_TRACE_FACILITY == 1
    TaskStatus_t *task_status_array = malloc(num_tasks * sizeof(TaskStatus_t));
    if (task_status_array) {
        UBaseType_t num_tasks_running = uxTaskGetSystemState(task_status_array, num_tasks, NULL);
        
        for (UBaseType_t i = 0; i < num_tasks_running; i++) {
            cJSON *task = cJSON_CreateObject();
            cJSON_AddStringToObject(task, "name", task_status_array[i].pcTaskName);
            cJSON_AddNumberToObject(task, "state", task_status_array[i].eCurrentState);
            cJSON_AddNumberToObject(task, "priority", task_status_array[i].uxCurrentPriority);
            cJSON_AddNumberToObject(task, "stack_high_water_mark", task_status_array[i].usStackHighWaterMark);
            #if configGENERATE_RUN_TIME_STATS == 1
            cJSON_AddNumberToObject(task, "runtime", task_status_array[i].ulRunTimeCounter);
            #else
            cJSON_AddNumberToObject(task, "runtime", 0);
            #endif
            cJSON_AddItemToArray(tasks, task);
        }
        free(task_status_array);
    }
    #else
    // Trace facility not enabled - just report task count
    cJSON_AddStringToObject(json, "task_info_note", "Detailed task info requires configUSE_TRACE_FACILITY=1");
    #endif
    cJSON_AddItemToObject(json, "tasks", tasks);
    
    // System uptime
    cJSON_AddNumberToObject(json, "uptime_ms", esp_timer_get_time() / 1000);
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get OTA status
static esp_err_t api_ota_status_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    
    if (ws->config.ota_updater) {
        ota_updater_t *ota = (ota_updater_t *)ws->config.ota_updater;
        ota_status_t status = ota_updater_get_status(ota);
        const char *status_msg = ota_updater_get_status_message(ota);
        int progress = ota_updater_get_progress(ota);
        
        cJSON_AddNumberToObject(json, "status", status);
        cJSON_AddStringToObject(json, "status_message", status_msg);
        cJSON_AddNumberToObject(json, "progress", progress);
    } else {
        cJSON_AddStringToObject(json, "error", "OTA updater not available");
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Check for OTA updates
static esp_err_t api_ota_check_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    
    if (!ws->config.ota_updater) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "OTA updater not available");
    } else {
        ota_updater_t *ota = (ota_updater_t *)ws->config.ota_updater;
        bool available = false;
        char *version = NULL;
        char *url = NULL;
        
        esp_err_t err = ota_updater_check(ota, &available, &version, &url);
        cJSON_AddBoolToObject(json, "success", err == ESP_OK);
        if (err == ESP_OK) {
            cJSON_AddBoolToObject(json, "available", available);
            if (version) {
                cJSON_AddStringToObject(json, "latest_version", version);
                free(version);
            }
            if (url) {
                cJSON_AddStringToObject(json, "download_url", url);
                free(url);
            }
        } else {
            cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
        }
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Install OTA update
static esp_err_t api_ota_install_handler(httpd_req_t *req) {
    webserver_t *ws = (webserver_t *)req->user_ctx;
    cJSON *json = cJSON_CreateObject();
    
    if (!ws->config.ota_updater) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "OTA updater not available");
    } else {
        // Read request body for optional URL
        char content[512];
        int ret = httpd_req_recv(req, content, sizeof(content) - 1);
        const char *url = NULL;
        
        if (ret > 0) {
            content[ret] = '\0';
            cJSON *req_json = cJSON_Parse(content);
            if (req_json) {
                cJSON *url_item = cJSON_GetObjectItem(req_json, "url");
                if (url_item && cJSON_IsString(url_item)) {
                    url = cJSON_GetStringValue(url_item);
                }
                cJSON_Delete(req_json);
            }
        }
        
        ota_updater_t *ota = (ota_updater_t *)ws->config.ota_updater;
        esp_err_t err = ota_updater_install(ota, url);
        cJSON_AddBoolToObject(json, "success", err == ESP_OK);
        if (err != ESP_OK) {
            cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
        } else {
            cJSON_AddStringToObject(json, "message", "Update started, device will reboot");
        }
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t webserver_start(webserver_t **out_server, const webserver_config_t *cfg) {
    if (!out_server || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    webserver_t *ws = calloc(1, sizeof(webserver_t));
    if (!ws) {
        return ESP_ERR_NO_MEM;
    }
    
    ws->config = *cfg;
    if (ws->config.port <= 0) {
        ws->config.port = DEFAULT_PORT;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = ws->config.port;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    
    esp_err_t err = httpd_start(&ws->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        free(ws);
        return err;
    }
    
    // Register handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &root_uri);
    
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &status_uri);
    
    httpd_uri_t rules_uri = {
        .uri = "/api/rules",
        .method = HTTP_GET,
        .handler = api_rules_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &rules_uri);
    
    httpd_uri_t led_uri = {
        .uri = "/api/led",
        .method = HTTP_POST,
        .handler = api_led_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &led_uri);
    
    httpd_uri_t sensors_uri = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = api_sensors_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &sensors_uri);
    
    httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_GET,
        .handler = api_control_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &control_uri);
    
    httpd_uri_t ota_status_uri = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = api_ota_status_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &ota_status_uri);
    
    httpd_uri_t ota_check_uri = {
        .uri = "/api/ota/check",
        .method = HTTP_POST,
        .handler = api_ota_check_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &ota_check_uri);
    
    httpd_uri_t ota_install_uri = {
        .uri = "/api/ota/install",
        .method = HTTP_POST,
        .handler = api_ota_install_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &ota_install_uri);
    
    httpd_uri_t ble_logs_uri = {
        .uri = "/api/ble/logs",
        .method = HTTP_GET,
        .handler = api_ble_logs_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &ble_logs_uri);
    
    httpd_uri_t system_uri = {
        .uri = "/api/system",
        .method = HTTP_GET,
        .handler = api_system_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &system_uri);
    
    ws->running = true;
    *out_server = ws;
    
    ESP_LOGI(TAG, "HTTP server started on port %d", ws->config.port);
    ESP_LOGI(TAG, "Access dashboard at http://nap.local/ or http://<device-ip>/");
    return ESP_OK;
}

void webserver_stop(webserver_t *server) {
    if (!server || !server->running) {
        return;
    }
    
    httpd_stop(server->server);
    server->running = false;
    free(server);
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool webserver_is_running(webserver_t *server) {
    return server && server->running;
}
