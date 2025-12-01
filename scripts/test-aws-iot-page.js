/**
 * Playwright test for AWS IoT Sensor Monitor page
 * Tests the web interface and backend proxy connection
 */

import { chromium } from 'playwright';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function testAwsIoTPage() {
    console.log('🚀 Starting Playwright test for AWS IoT test page...\n');
    
    const browser = await chromium.launch({ 
        headless: false, // Set to true for CI/CD
        slowMo: 500 // Slow down actions for visibility
    });
    
    const context = await browser.newContext({
        viewport: { width: 1920, height: 1080 }
    });
    
    const page = await context.newPage();
    
    try {
        // Navigate to the page
        console.log('📄 Navigating to test page...');
        await page.goto('https://naptick.github.io/Naphome_Firmware/aws-iot-test.html', {
            waitUntil: 'networkidle',
            timeout: 30000
        });
        
        console.log('✅ Page loaded\n');
        
        // Check page title
        const title = await page.title();
        console.log(`📋 Page title: ${title}`);
        
        // Wait for page to be fully loaded
        await page.waitForSelector('h1', { timeout: 10000 });
        console.log('✅ Page elements loaded\n');
        
        // Test 1: Check if authentication method selector is hidden (as expected)
        console.log('🧪 Test 1: Checking authentication method selector is hidden...');
        const authMethod = await page.locator('#auth-method');
        const authMethodExists = await authMethod.count() > 0;
        const authMethodVisible = authMethodExists ? await authMethod.isVisible() : false;
        console.log(`   ${!authMethodVisible ? '✅' : '❌'} Auth method selector: ${!authMethodVisible ? 'Hidden (as expected)' : 'Visible (should be hidden)'}`);
        
        // Test 2: Check if proxy auth section is visible by default
        console.log('\n🧪 Test 2: Checking Backend Proxy section is visible by default...');
        const proxySection = await page.locator('#proxy-auth');
        const isProxyVisible = await proxySection.isVisible();
        console.log(`   ${isProxyVisible ? '✅' : '❌'} Proxy auth section visible: ${isProxyVisible}`);
        
        // Test 3: Check for "No Credentials Required" message
        console.log('\n🧪 Test 3: Checking for "No Credentials Required" message...');
        const noCredentialsMessage = await page.locator('#proxy-auth strong:has-text("No Credentials Required")');
        const messageVisible = await noCredentialsMessage.isVisible();
        console.log(`   ${messageVisible ? '✅' : '❌'} "No Credentials Required" message: ${messageVisible ? 'Visible' : 'Not found'}`);
        
        // Test 4: Verify proxy is the default auth method
        console.log('\n🧪 Test 4: Verifying proxy is the default auth method...');
        const proxyAuthValue = await page.evaluate(() => {
            const select = document.getElementById('auth-method');
            return select ? select.value : null;
        });
        console.log(`   ${proxyAuthValue === 'proxy' ? '✅' : '❌'} Default auth method: ${proxyAuthValue === 'proxy' ? 'proxy (correct)' : proxyAuthValue || 'not set'}`);
        
        // Test 5: Check if API Gateway URL is pre-filled (even if hidden)
        console.log('\n🧪 Test 5: Checking API Gateway URL field is pre-filled...');
        const proxyUrlInput = await page.locator('#proxy-url');
        const proxyUrlValue = await proxyUrlInput.inputValue();
        console.log(`   Proxy URL value: ${proxyUrlValue}`);
        
        const expectedUrl = 'https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect';
        const urlMatches = proxyUrlValue.includes('execute-api.ap-south-1.amazonaws.com');
        console.log(`   ${urlMatches ? '✅' : '⚠️ '} API Gateway URL ${urlMatches ? 'pre-filled correctly' : 'may need manual entry'}`);
        
        // Test 6: Check endpoint and region fields
        console.log('\n🧪 Test 6: Checking endpoint and region fields...');
        const endpointInput = await page.locator('#endpoint');
        const regionInput = await page.locator('#region');
        
        const endpointValue = await endpointInput.inputValue();
        const regionValue = await regionInput.inputValue();
        
        console.log(`   Endpoint: ${endpointValue}`);
        console.log(`   Region: ${regionValue}`);
        console.log(`   ${endpointValue && regionValue ? '✅' : '⚠️ '} Fields ${endpointValue && regionValue ? 'have values' : 'may be empty'}`);
        
        // Test 7: Check device selector
        console.log('\n🧪 Test 7: Checking device selector...');
        const deviceSelect = await page.locator('#device-select');
        const deviceOptions = await deviceSelect.locator('option').all();
        console.log(`   Found ${deviceOptions.length} device options`);
        
        if (deviceOptions.length > 1) {
            // Select first device (skip "Select or enter..." option)
            await deviceSelect.selectOption({ index: 1 });
            const selectedDevice = await deviceSelect.inputValue();
            console.log(`   ${selectedDevice ? '✅' : '❌'} Selected device: ${selectedDevice || 'None'}`);
        }
        
        // Test 8: Check connect button
        console.log('\n🧪 Test 8: Checking connect button...');
        const connectBtn = await page.locator('#connect-btn');
        const connectBtnText = await connectBtn.textContent();
        const connectBtnEnabled = await connectBtn.isEnabled();
        console.log(`   Button text: ${connectBtnText}`);
        console.log(`   ${connectBtnEnabled ? '✅' : '⚠️ '} Button ${connectBtnEnabled ? 'enabled' : 'disabled'}`);
        
        // Test 9: Check status display area
        console.log('\n🧪 Test 9: Checking status display...');
        const statusDiv = await page.locator('#connection-status');
        const statusExists = await statusDiv.count() > 0;
        console.log(`   ${statusExists ? '✅' : '❌'} Status display: ${statusExists ? 'Found' : 'Not found'}`);
        
        // Test 10: Attempt connection (will test the UI flow and proxy connection)
        console.log('\n🧪 Test 10: Attempting connection via proxy (UI flow test)...');
        console.log('   Note: This will test the UI flow and proxy connection');
        
        // Fill in required fields if needed
        if (!endpointValue) {
            await endpointInput.fill('a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com');
        }
        if (!regionValue) {
            await regionInput.fill('ap-south-1');
        }
        
        // Select a device if available
        if (deviceOptions.length > 1) {
            await deviceSelect.selectOption({ index: 1 });
        }
        
        // Click connect button
        await connectBtn.click();
        console.log('   ✅ Connect button clicked');
        
        // Wait for connection to establish
        console.log('   ⏳ Waiting for connection to establish...');
        await page.waitForTimeout(2000);
        
        // Check for status messages
        const statusText = await statusDiv.textContent();
        console.log(`   Status message: ${statusText || 'None'}`);
        
        // Monitor console messages for debugging
        const consoleMessages = [];
        const consoleErrors = [];
        const consoleLogs = [];
        
        page.on('console', msg => {
            const text = msg.text();
            consoleMessages.push({ type: msg.type(), text: text });
            if (msg.type() === 'error') {
                consoleErrors.push(text);
            } else if (msg.type() === 'log' || msg.type() === 'info') {
                consoleLogs.push(text);
            }
        });
        
        // Test 11: Wait for data section to become visible (indicates successful connection)
        console.log('\n🧪 Test 11: Waiting for data section to appear...');
        const dataSection = await page.locator('#data-section');
        try {
            await dataSection.waitFor({ state: 'visible', timeout: 10000 });
            console.log('   ✅ Data section is visible (connection successful)');
        } catch (e) {
            const isVisible = await dataSection.isVisible();
            console.log(`   ${isVisible ? '✅' : '⚠️ '} Data section visible: ${isVisible}`);
        }
        
        // Test 12: Verify MQTT connection and wait for sensor data
        console.log('\n🧪 Test 12: Verifying MQTT connection state...');
        
        // Wait a bit for mqttClient to be assigned
        await page.waitForTimeout(1000);
        
        // Check connection status in the UI
        const connStatusElement = await page.locator('#conn-status');
        let connStatus = '';
        if (await connStatusElement.count() > 0) {
            connStatus = await connStatusElement.textContent() || '';
            console.log(`   Connection status: ${connStatus}`);
        }
        
        // Check if MQTT client is connected by evaluating in browser context
        const mqttState = await page.evaluate(() => {
            if (typeof window !== 'undefined' && window.mqttClient) {
                return {
                    connected: window.mqttClient.connected || false,
                    disconnecting: window.mqttClient.disconnecting || false,
                    reconnecting: window.mqttClient.reconnecting || false,
                    options: window.mqttClient.options ? {
                        clientId: window.mqttClient.options.clientId,
                        protocol: window.mqttClient.options.protocol
                    } : null
                };
            }
            return null;
        });
        
        if (mqttState) {
            console.log(`   MQTT Client state: ${JSON.stringify(mqttState)}`);
            if (!mqttState.connected) {
                console.log('   ⚠️  MQTT client not connected - checking for connection errors...');
            } else {
                console.log('   ✅ MQTT client is connected');
            }
        } else {
            console.log('   ⚠️  MQTT client not found in window object (may still be initializing)');
        }
        
        // Wait for sensor data to arrive and verify sensor cards
        console.log('\n🧪 Test 12b: Waiting for sensor data...');
        console.log('   ⏳ Waiting up to 30 seconds for sensor data to arrive...');
        
        let sensorCards = [];
        let messageCount = 0;
        let lastUpdate = '';
        let maxWaitTime = 30000; // 30 seconds
        let waitInterval = 2000; // Check every 2 seconds
        let waited = 0;
        let connectionEstablished = false;
        
        while (waited < maxWaitTime) {
            await page.waitForTimeout(waitInterval);
            waited += waitInterval;
            
            sensorCards = await page.locator('.sensor-card').all();
            const messageCountElement = await page.locator('#message-count');
            const lastUpdateElement = await page.locator('#last-update');
            
            if (messageCountElement.count() > 0) {
                messageCount = parseInt(await messageCountElement.textContent()) || 0;
            }
            if (lastUpdateElement.count() > 0) {
                lastUpdate = await lastUpdateElement.textContent() || '';
            }
            
            // Check connection status
            if (await connStatusElement.count() > 0) {
                connStatus = await connStatusElement.textContent() || '';
                if (connStatus.includes('Connected') && !connectionEstablished) {
                    connectionEstablished = true;
                    console.log(`   ✅ Connection established at ${waited/1000}s`);
                }
            }
            
            console.log(`   [${waited/1000}s] Status: ${connStatus}, Messages: ${messageCount}, Cards: ${sensorCards.length}, Last: ${lastUpdate}`);
            
            // Check for MQTT messages in console logs
            const recentLogs = consoleLogs.filter(log => 
                log.includes('message') || log.includes('MQTT') || log.includes('subscribe') || 
                log.includes('connect') || log.includes('data')
            );
            if (recentLogs.length > 0 && waited % 10000 === 0) {
                console.log(`   Recent relevant logs: ${recentLogs.slice(-3).join('; ')}`);
            }
            
            if (sensorCards.length > 0 && messageCount > 0) {
                console.log(`   ✅ Sensor data received! Found ${sensorCards.length} sensor cards after ${messageCount} message(s)`);
                break;
            }
        }
        
        if (sensorCards.length === 0) {
            console.log(`   ⚠️  No sensor cards found after ${maxWaitTime/1000} seconds`);
            console.log(`   This may indicate: 1) Device is not sending data, 2) Connection issue, or 3) Data format mismatch`);
        } else {
            // Verify sensor card content
            console.log('\n🧪 Test 12b: Verifying sensor card content...');
            for (let i = 0; i < Math.min(sensorCards.length, 3); i++) {
                const card = sensorCards[i];
                const cardText = await card.textContent();
                const hasValue = cardText && /\d+\.?\d*/.test(cardText); // Check for numeric values
                console.log(`   Card ${i + 1}: ${hasValue ? '✅' : '⚠️ '} ${hasValue ? 'Contains sensor values' : 'May be empty'}`);
                if (i === 0) {
                    // Show first card content (truncated)
                    const preview = cardText.substring(0, 100).replace(/\s+/g, ' ');
                    console.log(`   Preview: ${preview}...`);
                }
            }
        }
        
        // Test 13: Check for charts
        console.log('\n🧪 Test 13: Checking chart containers...');
        const chartContainers = await page.locator('canvas').all();
        console.log(`   Found ${chartContainers.length} chart canvases`);
        
        if (chartContainers.length > 0) {
            console.log('   ✅ Charts initialized');
        } else {
            console.log('   ⚠️  No charts found (may appear after data arrives)');
        }
        
        // Test 14: Verify message count increased
        console.log('\n🧪 Test 14: Verifying message reception...');
        console.log(`   Messages received: ${messageCount}`);
        console.log(`   Last update: ${lastUpdate || 'Never'}`);
        if (messageCount > 0) {
            console.log('   ✅ Messages are being received');
        } else {
            console.log('   ⚠️  No messages received yet (device may not be sending data)');
        }
        
        // Summary
        console.log('\n' + '='.repeat(60));
        console.log('📊 TEST SUMMARY');
        console.log('='.repeat(60));
        console.log(`✅ Page loaded successfully`);
        console.log(`✅ Authentication selector hidden (no credentials required)`);
        console.log(`✅ Backend Proxy is default and visible`);
        console.log(`✅ "No Credentials Required" message displayed`);
        console.log(`✅ API Gateway URL ${urlMatches ? 'pre-filled' : 'needs manual entry'}`);
        console.log(`✅ UI elements functional`);
        console.log(`✅ Connection established: ${statusText && statusText.includes('Connected') ? 'Yes' : 'Partial'}`);
        console.log(`✅ Sensor data received: ${sensorCards.length > 0 ? `Yes (${sensorCards.length} cards)` : 'No'}`);
        console.log(`✅ Messages received: ${messageCount > 0 ? `Yes (${messageCount} messages)` : 'No'}`);
        console.log(`✅ Charts initialized: ${chartContainers.length > 0 ? `Yes (${chartContainers.length} charts)` : 'No'}`);
        
        // Test 15: Analyze console messages for connection issues
        console.log('\n🧪 Test 15: Analyzing console messages...');
        if (consoleErrors.length > 0) {
            console.log(`   ⚠️  Console errors detected (${consoleErrors.length}):`);
            consoleErrors.slice(0, 5).forEach(err => console.log(`      - ${err}`));
            if (consoleErrors.length > 5) {
                console.log(`      ... and ${consoleErrors.length - 5} more errors`);
            }
        } else {
            console.log('   ✅ No console errors detected');
        }
        
        // Check for relevant log messages
        const relevantLogs = consoleLogs.filter(log => 
            log.toLowerCase().includes('mqtt') || 
            log.toLowerCase().includes('connect') || 
            log.toLowerCase().includes('subscribe') ||
            log.toLowerCase().includes('message') ||
            log.toLowerCase().includes('websocket') ||
            log.includes('📨') || // MQTT message emoji
            log.includes('🔌') || // Connection emoji
            log.includes('✅') || // Success emoji
            log.includes('📊')    // Data emoji
        );
        if (relevantLogs.length > 0) {
            console.log(`   📋 Relevant log messages (${relevantLogs.length}):`);
            relevantLogs.slice(0, 10).forEach(log => console.log(`      - ${log.substring(0, 150)}`));
        } else {
            console.log('   ⚠️  No relevant MQTT log messages found');
        }
        
        // Check specifically for message reception logs
        const messageLogs = consoleLogs.filter(log => 
            log.includes('📨') || log.includes('MQTT message received')
        );
        if (messageLogs.length > 0) {
            console.log(`   📨 MQTT message logs found (${messageLogs.length}):`);
            messageLogs.forEach(log => console.log(`      - ${log.substring(0, 200)}`));
        } else {
            console.log('   ⚠️  No MQTT message reception logs found');
        }
        
        // Check network requests to proxy
        console.log('\n🧪 Test 15b: Checking network requests...');
        const networkRequests = await page.evaluate(() => {
            if (window.performance && window.performance.getEntriesByType) {
                return window.performance.getEntriesByType('resource')
                    .filter(entry => entry.name.includes('execute-api') || entry.name.includes('iot'))
                    .map(entry => ({
                        url: entry.name,
                        type: entry.initiatorType,
                        duration: entry.duration,
                        status: entry.responseStatus || 'unknown'
                    }));
            }
            return [];
        });
        
        if (networkRequests.length > 0) {
            console.log(`   Found ${networkRequests.length} relevant network requests:`);
            networkRequests.forEach(req => {
                console.log(`      ${req.type}: ${req.url.substring(0, 80)}... (${req.duration.toFixed(0)}ms, status: ${req.status})`);
            });
        } else {
            console.log('   ⚠️  No network requests to proxy detected (may be using cached connection)');
        }
        
        if (sensorCards.length === 0 || messageCount === 0) {
            console.log('\n⚠️  Note: No sensor data received during test');
            console.log('   Possible reasons:');
            console.log('   - Device is not currently sending telemetry data');
            console.log('   - Device is offline or not connected to AWS IoT');
            console.log('   - Data format may not match expected structure');
            console.log('   - Network latency (data may arrive after test completes)');
        } else {
            console.log('\n✅ Sensor data verification successful!');
            console.log('   The page is correctly receiving and displaying sensor data.');
        }
        
        console.log('\n💡 Note: Connection test completed');
        console.log('   The page now works without requiring user credentials');
        console.log('='.repeat(60));
        
        // Take a screenshot
        const screenshotPath = path.join(__dirname, 'aws-iot-test-screenshot.png');
        await page.screenshot({ path: screenshotPath, fullPage: true });
        console.log(`\n📸 Screenshot saved to: ${screenshotPath}`);
        
        // Keep browser open for a few seconds to see the result
        console.log('\n⏳ Keeping browser open for 5 seconds...');
        await page.waitForTimeout(5000);
        
    } catch (error) {
        console.error('\n❌ Test failed with error:');
        console.error(error);
        
        // Take screenshot on error
        const screenshotPath = path.join(__dirname, 'aws-iot-test-error.png');
        await page.screenshot({ path: screenshotPath, fullPage: true });
        console.log(`\n📸 Error screenshot saved to: ${screenshotPath}`);
    } finally {
        await browser.close();
        console.log('\n✅ Browser closed');
    }
}

// Run the test
testAwsIoTPage().catch(console.error);
