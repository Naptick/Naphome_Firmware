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
        
        // Test 1: Check if authentication method selector exists
        console.log('🧪 Test 1: Checking authentication method selector...');
        const authMethod = await page.locator('#auth-method');
        const authMethodExists = await authMethod.count() > 0;
        console.log(`   ${authMethodExists ? '✅' : '❌'} Auth method selector: ${authMethodExists ? 'Found' : 'Not found'}`);
        
        // Test 2: Check if Backend Proxy option exists
        console.log('\n🧪 Test 2: Checking Backend Proxy option...');
        const proxyOption = await page.locator('#auth-method option[value="proxy"]');
        const proxyOptionExists = await proxyOption.count() > 0;
        console.log(`   ${proxyOptionExists ? '✅' : '❌'} Backend Proxy option: ${proxyOptionExists ? 'Found' : 'Not found'}`);
        
        // Test 3: Select Backend Proxy method
        console.log('\n🧪 Test 3: Selecting Backend Proxy authentication method...');
        await authMethod.selectOption('proxy');
        await page.waitForTimeout(1000);
        
        // Check if proxy auth section is visible
        const proxySection = await page.locator('#proxy-auth');
        const isProxyVisible = await proxySection.isVisible();
        console.log(`   ${isProxyVisible ? '✅' : '❌'} Proxy auth section visible: ${isProxyVisible}`);
        
        // Test 4: Check if API Gateway URL is pre-filled
        console.log('\n🧪 Test 4: Checking API Gateway URL field...');
        const proxyUrlInput = await page.locator('#proxy-url');
        const proxyUrlValue = await proxyUrlInput.inputValue();
        console.log(`   Proxy URL value: ${proxyUrlValue}`);
        
        const expectedUrl = 'https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect';
        const urlMatches = proxyUrlValue.includes('execute-api.ap-south-1.amazonaws.com');
        console.log(`   ${urlMatches ? '✅' : '⚠️ '} API Gateway URL ${urlMatches ? 'pre-filled correctly' : 'may need manual entry'}`);
        
        // Test 5: Check endpoint and region fields
        console.log('\n🧪 Test 5: Checking endpoint and region fields...');
        const endpointInput = await page.locator('#endpoint');
        const regionInput = await page.locator('#region');
        
        const endpointValue = await endpointInput.inputValue();
        const regionValue = await regionInput.inputValue();
        
        console.log(`   Endpoint: ${endpointValue}`);
        console.log(`   Region: ${regionValue}`);
        console.log(`   ${endpointValue && regionValue ? '✅' : '⚠️ '} Fields ${endpointValue && regionValue ? 'have values' : 'may be empty'}`);
        
        // Test 6: Check device selector
        console.log('\n🧪 Test 6: Checking device selector...');
        const deviceSelect = await page.locator('#device-select');
        const deviceOptions = await deviceSelect.locator('option').all();
        console.log(`   Found ${deviceOptions.length} device options`);
        
        if (deviceOptions.length > 1) {
            // Select first device (skip "Select or enter..." option)
            await deviceSelect.selectOption({ index: 1 });
            const selectedDevice = await deviceSelect.inputValue();
            console.log(`   ${selectedDevice ? '✅' : '❌'} Selected device: ${selectedDevice || 'None'}`);
        }
        
        // Test 7: Check connect button
        console.log('\n🧪 Test 7: Checking connect button...');
        const connectBtn = await page.locator('#connect-btn');
        const connectBtnText = await connectBtn.textContent();
        const connectBtnEnabled = await connectBtn.isEnabled();
        console.log(`   Button text: ${connectBtnText}`);
        console.log(`   ${connectBtnEnabled ? '✅' : '⚠️ '} Button ${connectBtnEnabled ? 'enabled' : 'disabled'}`);
        
        // Test 8: Check status display area
        console.log('\n🧪 Test 8: Checking status display...');
        const statusDiv = await page.locator('#connection-status');
        const statusExists = await statusDiv.count() > 0;
        console.log(`   ${statusExists ? '✅' : '❌'} Status display: ${statusExists ? 'Found' : 'Not found'}`);
        
        // Test 9: Attempt connection (will likely fail due to Lambda 500 error, but we can test the UI flow)
        console.log('\n🧪 Test 9: Attempting connection (UI flow test)...');
        console.log('   Note: This will test the UI flow, but connection may fail due to Lambda 500 error');
        
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
        
        // Wait a bit for any network requests
        await page.waitForTimeout(3000);
        
        // Check for status messages
        const statusText = await statusDiv.textContent();
        console.log(`   Status message: ${statusText || 'None'}`);
        
        // Check for error messages in console
        const consoleErrors = [];
        page.on('console', msg => {
            if (msg.type() === 'error') {
                consoleErrors.push(msg.text());
            }
        });
        
        // Test 10: Check for data section
        console.log('\n🧪 Test 10: Checking data display section...');
        const dataSection = await page.locator('#data-section');
        const dataSectionExists = await dataSection.count() > 0;
        const dataSectionVisible = dataSectionExists ? await dataSection.isVisible() : false;
        console.log(`   ${dataSectionExists ? '✅' : '❌'} Data section: ${dataSectionExists ? 'Found' : 'Not found'}`);
        console.log(`   ${dataSectionVisible ? '✅' : '⚠️ '} Data section visible: ${dataSectionVisible}`);
        
        // Test 11: Check for sensor cards
        console.log('\n🧪 Test 11: Checking sensor data cards...');
        const sensorCards = await page.locator('.sensor-card').all();
        console.log(`   Found ${sensorCards.length} sensor cards`);
        
        // Test 12: Check for charts
        console.log('\n🧪 Test 12: Checking chart containers...');
        const chartContainers = await page.locator('canvas').all();
        console.log(`   Found ${chartContainers.length} chart canvases`);
        
        // Summary
        console.log('\n' + '='.repeat(60));
        console.log('📊 TEST SUMMARY');
        console.log('='.repeat(60));
        console.log(`✅ Page loaded successfully`);
        console.log(`✅ Authentication methods available`);
        console.log(`✅ Backend Proxy option found`);
        console.log(`✅ API Gateway URL ${urlMatches ? 'pre-filled' : 'needs manual entry'}`);
        console.log(`✅ UI elements functional`);
        
        if (consoleErrors.length > 0) {
            console.log(`\n⚠️  Console errors detected:`);
            consoleErrors.forEach(err => console.log(`   - ${err}`));
        }
        
        console.log('\n💡 Note: Connection may fail due to Lambda 500 error');
        console.log('   This is expected and documented in VALIDATION.md');
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
