# Testing Scripts

## AWS IoT Test Page - Playwright Test

### Overview
Automated browser testing for the AWS IoT Sensor Monitor page using Playwright.

### Prerequisites
```bash
npm install --save-dev playwright
npx playwright install chromium
```

### Run the Test
```bash
node scripts/test-aws-iot-page.js
```

### What It Tests

1. **Page Loading**
   - Verifies page loads successfully
   - Checks page title

2. **Authentication Methods**
   - Verifies authentication method selector exists
   - Checks Backend Proxy option is available
   - Tests switching to Backend Proxy method

3. **Form Fields**
   - Verifies API Gateway URL is pre-filled
   - Checks endpoint and region fields have default values
   - Tests device selector with available devices

4. **UI Elements**
   - Verifies connect button exists and is enabled
   - Checks status display area
   - Verifies data section structure

5. **Connection Flow**
   - Tests clicking connect button
   - Monitors status messages
   - Captures console errors

6. **Data Display**
   - Checks for sensor cards
   - Verifies chart containers

### Expected Results

✅ **All UI tests should pass:**
- Page loads correctly
- All form fields are present
- API Gateway URL is pre-filled
- Device selector works

⚠️ **Connection test will fail:**
- Expected error: "Failed to fetch" or "Proxy connection failed"
- This is due to Lambda 500 error (documented in VALIDATION.md)
- The UI flow itself is working correctly

### Screenshots

The test automatically captures:
- `aws-iot-test-screenshot.png` - Full page screenshot
- `aws-iot-test-error.png` - Error screenshot (if test fails)

### Test Output

The test provides detailed output for each test case:
- ✅ Pass
- ❌ Fail
- ⚠️ Warning

### Integration with CI/CD

To run in headless mode for CI/CD:
```javascript
const browser = await chromium.launch({ headless: true });
```

### Troubleshooting

**Playwright not found:**
```bash
npm install --save-dev playwright
npx playwright install chromium
```

**Browser launch fails:**
- Ensure Chromium is installed: `npx playwright install chromium`
- Check system dependencies for Playwright

**Page load timeout:**
- Increase timeout in `page.goto()` options
- Check network connectivity
