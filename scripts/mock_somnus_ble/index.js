#!/usr/bin/env node

/**
 * Somnus BLE mock peripheral for local mobile-app testing.
 *
 * This script emulates the ESP32 BLE UART service implemented in
 * components/somnus_ble by exposing the same service / characteristic UUIDs and
 * notification behaviour. It runs on macOS using @abandonware/bleno, making it
 * possible to exercise the Somnus onboarding flow without hardware.
 */

const bleno = require('@abandonware/bleno');
const { execFile } = require('child_process');

const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
const TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

const DEVICE_NAME = process.env.SOMNUS_BLE_NAME || 'rpi-gatt-server';
const DEVICE_ID = process.env.SOMNUS_DEVICE_ID || 'SOMNUS_LOCAL_TEST';
const ALWAYS_FAIL_CONNECT = process.env.SOMNUS_BLE_FAIL_CONNECT === '1';
const AIRPORT = '/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport';

let txCharacteristic = null;

function log(...args) {
    console.log('[somnus-ble-mock]', ...args);
}

function chunkAndNotify(message) {
    if (!txCharacteristic || !txCharacteristic.updateValueCallback) {
        log('No subscriber for notification, dropping message:', message);
        return;
    }

    const buffer = Buffer.from(message, 'utf8');
    const chunkSize = 20;
    for (let offset = 0; offset < buffer.length; offset += chunkSize) {
        const chunk = buffer.slice(offset, offset + chunkSize);
        txCharacteristic.updateValueCallback(chunk);
    }
}

function sendStatus(message) {
    log('Notify ->', message);
    chunkAndNotify(message);
}

async function performWifiScan() {
    return new Promise((resolve, reject) => {
        execFile(AIRPORT, ['-s'], { timeout: 5000 }, (error, stdout) => {
            if (error) {
                reject(error);
                return;
            }

            const lines = stdout
                .split('\n')
                .map((line) => line.trimEnd())
                .filter((line) => line.length > 0);
            if (lines.length <= 1) {
                resolve([]);
                return;
            }

            lines.shift(); // drop header
            const networks = [];
            const seen = new Set();

            const macRegex = /([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})/;
            for (const rawLine of lines) {
                const match = rawLine.match(macRegex);
                if (!match) {
                    continue;
                }
                const mac = match[1].toUpperCase();
                if (seen.has(mac)) {
                    continue;
                }
                seen.add(mac);

                const ssid = rawLine.slice(0, rawLine.indexOf(mac)).trim();
                if (!ssid) {
                    continue;
                }

                networks.push({
                    ssid,
                    mac,
                });
            }

            resolve(networks);
        });
    });
}

async function handleScan() {
    sendStatus('WIFI_LIST_START');
    try {
        let wifiList = await performWifiScan();
        if (wifiList.length === 0) {
            wifiList = [
                { ssid: 'Naphome-2G', mac: '02:00:00:00:00:01' },
                { ssid: 'Naphome-5G', mac: '02:00:00:00:00:02' },
            ];
            log('Wi-Fi scan returned no results, using fallback list.');
        }

        chunkAndNotify(JSON.stringify(wifiList));
    } catch (err) {
        log('Wi-Fi scan failed:', err.message);
        sendStatus('WIFI_LIST_ERROR');
    } finally {
        sendStatus('WIFI_LIST_END');
    }
}

function handleConnect(request) {
    const ssid = request.ssid || '';
    const password = request.password || '';
    const token = request.user_token || request.token || '';
    const isProduction = Boolean(request.is_production);

    log('CONNECT_WIFI request', {
        ssid,
        password: password ? '<redacted>' : '',
        token,
        isProduction,
    });

    if (!ssid || !password) {
        sendStatus('Missing ssid/password/token');
        return;
    }

    sendStatus(`Connecting to ${ssid}...`);

    setTimeout(() => {
        if (ALWAYS_FAIL_CONNECT) {
            sendStatus('Wi-Fi connection failed');
            return;
        }
        sendStatus(`Connected to ${ssid}`);
    }, 1500);
}

function handleIncomingPayload(payload) {
    const message = payload.toString('utf8').trim();
    if (!message) {
        return;
    }
    log('RX <-', message);

    let json = null;
    try {
        json = JSON.parse(message);
    } catch (err) {
        sendStatus('Bad JSON format');
        return;
    }

    const action = (json.action || '').toString().toUpperCase();
    switch (action) {
    case 'SCAN':
        handleScan();
        break;
    case 'CONNECT_WIFI':
        handleConnect(json);
        break;
    default:
        sendStatus('Unknown action');
        break;
    }
}

class TxCharacteristic extends bleno.Characteristic {
    constructor() {
        super({
            uuid: TX_UUID,
            properties: ['notify'],
            descriptors: [
                new bleno.Descriptor({
                    uuid: '2901',
                    value: 'Somnus TX (notifications)',
                }),
            ],
        });
        this.updateValueCallback = null;
    }

    onSubscribe(maxValueSize, updateValueCallback) {
        this.updateValueCallback = updateValueCallback;
        log(`Client subscribed (maxValueSize=${maxValueSize})`);
        sendStatus(`DeviceId:${DEVICE_ID}`);
    }

    onUnsubscribe() {
        log('Client unsubscribed from notifications');
        this.updateValueCallback = null;
    }
}

class RxCharacteristic extends bleno.Characteristic {
    constructor() {
        super({
            uuid: RX_UUID,
            properties: ['write'],
            descriptors: [
                new bleno.Descriptor({
                    uuid: '2901',
                    value: 'Somnus RX (JSON commands)',
                }),
            ],
        });
    }

    onWriteRequest(data, offset, withoutResponse, callback) {
        if (offset !== 0) {
            callback(this.RESULT_ATTR_NOT_LONG);
            return;
        }
        handleIncomingPayload(data);
        callback(this.RESULT_SUCCESS);
    }
}

txCharacteristic = new TxCharacteristic();
const rxCharacteristic = new RxCharacteristic();

bleno.on('stateChange', (state) => {
    log('BLE state ->', state);
    if (state === 'poweredOn') {
        bleno.setAdvertisingName(DEVICE_NAME);
        bleno.startAdvertising(DEVICE_NAME, [SERVICE_UUID], (err) => {
            if (err) {
                log('startAdvertising error:', err.message);
            } else {
                log(`Advertising as "${DEVICE_NAME}" with service ${SERVICE_UUID}`);
            }
        });
    } else {
        bleno.stopAdvertising();
    }
});

bleno.on('advertisingStart', (error) => {
    if (error) {
        log('Advertising start error:', error.message);
        return;
    }

    bleno.setServices(
        [
            new bleno.PrimaryService({
                uuid: SERVICE_UUID,
                characteristics: [txCharacteristic, rxCharacteristic],
            }),
        ],
        (err) => {
            if (err) {
                log('setServices error:', err.message);
                return;
            }
            log('Somnus BLE service ready.');
        }
    );
});

bleno.on('accept', (clientAddress) => {
    log('Client connected:', clientAddress);
});

bleno.on('disconnect', (clientAddress) => {
    log('Client disconnected:', clientAddress);
});

process.on('SIGINT', () => {
    log('Gracefully shutting down...');
    bleno.stopAdvertising(() => {
        bleno.disconnect();
        process.exit(0);
    });
});

log('Starting Somnus BLE mock peripheral...');
log(`Device ID: ${DEVICE_ID}`);
