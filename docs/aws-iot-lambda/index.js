/**
 * AWS Lambda function for AWS IoT MQTT Proxy
 * 
 * This Lambda function provides signed WebSocket URLs for AWS IoT MQTT connections.
 * It uses the AWS credentials from environment variables (set via GitHub Secrets).
 * 
 * API Gateway Integration:
 * - POST /connect - Returns signed WebSocket URL for MQTT connection
 */

const crypto = require('crypto');

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
        // Fallback: Try to get from Lambda execution role via STS
        // For now, throw error if credentials not in environment
        throw new Error('IOT_ACCESS_KEY_ID and IOT_SECRET_ACCESS_KEY environment variables are required');
    }
    
    // Generate timestamp in format: YYYYMMDDTHHmmssZ
    const now = new Date();
    const year = now.getUTCFullYear();
    const month = String(now.getUTCMonth() + 1).padStart(2, '0');
    const day = String(now.getUTCDate()).padStart(2, '0');
    const hours = String(now.getUTCHours()).padStart(2, '0');
    const minutes = String(now.getUTCMinutes()).padStart(2, '0');
    const seconds = String(now.getUTCSeconds()).padStart(2, '0');
    const timestamp = `${year}${month}${day}T${hours}${minutes}${seconds}Z`;
    const dateStamp = timestamp.substring(0, 8);
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
    console.log('=== Lambda handler invoked ===');
    console.log('Event:', JSON.stringify(event, null, 2));
    
    try {
        // Handle CORS preflight
        if (event && event.httpMethod === 'OPTIONS') {
            console.log('Handling OPTIONS request');
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
        
        // Parse request body
        let body;
        try {
            if (typeof event.body === 'string') {
                body = JSON.parse(event.body);
            } else if (event.body) {
                body = event.body;
            } else {
                body = event; // API Gateway might pass event directly
            }
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
        
        console.log('Parsed body:', JSON.stringify(body, null, 2));
        
        // Extract parameters
        const endpoint = body.endpoint;
        const region = body.region || 'ap-south-1';
        const device = body.device;
        const topic = body.topic;
        
        if (!endpoint || !device || !topic) {
            console.error('Missing required parameters:', { endpoint: !!endpoint, device: !!device, topic: !!topic });
            return {
                statusCode: 400,
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ 
                    error: 'Missing required parameters: endpoint, device, topic',
                    received: { endpoint: !!endpoint, device: !!device, topic: !!topic }
                })
            };
        }
        
        console.log('Generating signed URL for:', { endpoint, region, device, topic });
        
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
        console.error('Error stack:', error.stack);
        return {
            statusCode: 500,
            headers: {
                'Access-Control-Allow-Origin': '*',
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ 
                error: error.message,
                stack: process.env.NODE_ENV === 'development' ? error.stack : undefined
            })
        };
    }
};
