/**
 * AWS Lambda function for AWS IoT MQTT Proxy
 * 
 * This Lambda function provides signed WebSocket URLs for AWS IoT MQTT connections.
 * It uses the AWS credentials from environment variables (set via GitHub Secrets).
 * 
 * API Gateway Integration:
 * - POST /connect - Returns signed WebSocket URL for MQTT connection
 */

const AWS = require('aws-sdk');
const crypto = require('crypto');

// Initialize AWS SDK - uses IAM role credentials automatically
// Region from environment variable (set by Lambda) or default
const region = process.env.AWS_REGION || process.env.AWS_DEFAULT_REGION || 'ap-south-1';
AWS.config.update({ region: region });

/**
 * Generate signed WebSocket URL for AWS IoT MQTT connection
 */
async function generateSignedWebSocketUrl(endpoint, region) {
    // Get credentials - try environment variables first (custom names), then IAM role
    let accessKeyId, secretAccessKey;
    
    if (process.env.IOT_ACCESS_KEY_ID && process.env.IOT_SECRET_ACCESS_KEY) {
        // Use custom environment variables (from GitHub Secrets)
        accessKeyId = process.env.IOT_ACCESS_KEY_ID;
        secretAccessKey = process.env.IOT_SECRET_ACCESS_KEY;
    } else {
        // Fallback to IAM role credentials
        // Use default credential chain (Lambda execution role)
        const credentials = await new Promise((resolve, reject) => {
            const chain = new AWS.CredentialProviderChain();
            chain.resolve((err, creds) => {
                if (err) reject(err);
                else resolve(creds);
            });
        });
        accessKeyId = credentials.accessKeyId;
        secretAccessKey = credentials.secretAccessKey;
    }
    
    const timestamp = new Date().toISOString().replace(/[:\-]|\.\d{3}/g, '');
    const dateStamp = timestamp.substr(0, 8);
    const credentialScope = `${dateStamp}/${region}/iotdevicegateway/aws4_request`;
    
    // Build canonical request
    const canonicalUri = '/mqtt';
    const canonicalQuerystring = `X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=${encodeURIComponent(`${accessKeyId}/${credentialScope}`)}&X-Amz-Date=${timestamp}&X-Amz-SignedHeaders=host`;
    
    const canonicalHeaders = `host:${endpoint}\n`;
    const signedHeaders = 'host';
    const payloadHash = crypto.createHash('sha256').update('').digest('hex');
    
    const canonicalRequest = `GET\n${canonicalUri}\n${canonicalQuerystring}\n${canonicalHeaders}\n${signedHeaders}\n${payloadHash}`;
    
    // Create string to sign
    const algorithm = 'AWS4-HMAC-SHA256';
    const stringToSign = `${algorithm}\n${timestamp}\n${credentialScope}\n${crypto.createHash('sha256').update(canonicalRequest).digest('hex')}`;
    
    // Calculate signature
    const kDate = crypto.createHmac('sha256', `AWS4${secretAccessKey}`).update(dateStamp).digest();
    const kRegion = crypto.createHmac('sha256', kDate).update(region).digest();
    const kService = crypto.createHmac('sha256', kRegion).update('iotdevicegateway').digest();
    const kSigning = crypto.createHmac('sha256', kService).update('aws4_request').digest();
    const signature = crypto.createHmac('sha256', kSigning).update(stringToSign).digest('hex');
    
    // Build final signed URL
    const signedQuerystring = `${canonicalQuerystring}&X-Amz-Signature=${signature}`;
    return `wss://${endpoint}${canonicalUri}?${signedQuerystring}`;
}

/**
 * Lambda handler
 */
exports.handler = async (event) => {
    console.log('Event received:', JSON.stringify(event));
    
    // Handle CORS preflight
    if (event.httpMethod === 'OPTIONS') {
        return {
            statusCode: 200,
            headers: {
                'Access-Control-Allow-Origin': '*',
                'Access-Control-Allow-Headers': 'Content-Type',
                'Access-Control-Allow-Methods': 'POST, OPTIONS'
            },
            body: ''
        };
    }
    
    try {
        // Parse request body
        let body;
        try {
            body = typeof event.body === 'string' ? JSON.parse(event.body) : event.body;
        } catch (e) {
            console.error('Body parse error:', e);
            return {
                statusCode: 400,
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ 
                    error: 'Invalid JSON in request body',
                    details: e.message
                })
            };
        }
        
        console.log('Parsed body:', JSON.stringify(body));
        
        const { endpoint, region, device, topic } = body;
        
        if (!endpoint || !region || !device || !topic) {
            return {
                statusCode: 400,
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ 
                    error: 'Missing required parameters: endpoint, region, device, topic',
                    received: { endpoint: !!endpoint, region: !!region, device: !!device, topic: !!topic }
                })
            };
        }
        
        console.log('Generating signed URL for:', endpoint, region);
        
        // Generate signed WebSocket URL
        const websocketUrl = await generateSignedWebSocketUrl(endpoint, region);
        
        console.log('Generated URL (first 100 chars):', websocketUrl.substring(0, 100));
        
        return {
            statusCode: 200,
            headers: {
                'Access-Control-Allow-Origin': '*',
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                websocketUrl: websocketUrl,
                topic: topic,
                device: device,
                endpoint: endpoint,
                region: region
            })
        };
    } catch (error) {
        console.error('Handler error:', error);
        console.error('Stack:', error.stack);
        return {
            statusCode: 500,
            headers: {
                'Access-Control-Allow-Origin': '*',
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ 
                error: error.message,
                stack: error.stack
            })
        };
    }
};
