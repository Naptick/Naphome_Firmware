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
    // Get credentials from IAM role (Lambda execution role)
    // Use default credential chain which automatically uses the execution role
    const chain = new AWS.CredentialProviderChain();
    const credentials = await chain.resolvePromise();
    
    const timestamp = new Date().toISOString().replace(/[:\-]|\.\d{3}/g, '');
    const dateStamp = timestamp.substr(0, 8);
    const credentialScope = `${dateStamp}/${region}/iotdevicegateway/aws4_request`;
    
    const accessKeyId = credentials.accessKeyId;
    const secretAccessKey = credentials.secretAccessKey;
    
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
        const body = typeof event.body === 'string' ? JSON.parse(event.body) : event.body;
        const { endpoint, region, device, topic } = body;
        
        if (!endpoint || !region || !device || !topic) {
            return {
                statusCode: 400,
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ 
                    error: 'Missing required parameters: endpoint, region, device, topic' 
                })
            };
        }
        
        // Generate signed WebSocket URL
        const websocketUrl = await generateSignedWebSocketUrl(endpoint, region);
        
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
        console.error('Error:', error);
        return {
            statusCode: 500,
            headers: {
                'Access-Control-Allow-Origin': '*',
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ 
                error: error.message 
            })
        };
    }
};
