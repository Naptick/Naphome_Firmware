/**
 * AWS IoT MQTT Proxy Server Example
 * 
 * This is a simple Node.js/Express server that acts as a proxy between
 * the web client and AWS IoT Core, using GitHub Secrets for authentication.
 * 
 * Deploy this to a service like:
 * - AWS Lambda + API Gateway
 * - Heroku
 * - Railway
 * - Render
 * - Vercel (serverless functions)
 * 
 * Environment Variables (set via GitHub Secrets or deployment platform):
 * - AWS_ACCESS_KEY_ID
 * - AWS_SECRET_ACCESS_KEY
 * - AWS_REGION
 * - AWS_IOT_ENDPOINT
 */

const express = require('express');
const AWS = require('aws-sdk');
const mqtt = require('mqtt');
const cors = require('cors');

const app = express();
app.use(cors());
app.use(express.json());

// Configure AWS
AWS.config.update({
    accessKeyId: process.env.AWS_ACCESS_KEY_ID,
    secretAccessKey: process.env.AWS_SECRET_ACCESS_KEY,
    region: process.env.AWS_REGION || 'ap-south-1'
});

const iotEndpoint = process.env.AWS_IOT_ENDPOINT || 'a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com';

// Store active MQTT connections
const connections = new Map();

/**
 * POST /connect
 * Creates a signed WebSocket URL for AWS IoT MQTT connection
 */
app.post('/connect', async (req, res) => {
    try {
        const { endpoint, region, device, topic } = req.body;
        
        if (!endpoint || !region || !device || !topic) {
            return res.status(400).json({ error: 'Missing required parameters' });
        }

        // Generate signed WebSocket URL
        const signedUrl = await generateSignedWebSocketUrl(endpoint, region);
        
        res.json({
            websocketUrl: signedUrl,
            topic: topic,
            device: device
        });
    } catch (error) {
        console.error('Error generating signed URL:', error);
        res.status(500).json({ error: error.message });
    }
});

/**
 * POST /subscribe
 * Proxy MQTT subscription - server subscribes and forwards messages via WebSocket
 */
app.post('/subscribe', async (req, res) => {
    try {
        const { endpoint, region, device, topic } = req.body;
        
        // Create MQTT connection if it doesn't exist
        const connectionKey = `${endpoint}_${device}`;
        if (!connections.has(connectionKey)) {
            const mqttClient = await createMqttConnection(endpoint, region);
            connections.set(connectionKey, mqttClient);
        }
        
        const mqttClient = connections.get(connectionKey);
        mqttClient.subscribe(topic, { qos: 0 }, (err) => {
            if (err) {
                return res.status(500).json({ error: err.message });
            }
            res.json({ success: true, topic: topic });
        });
    } catch (error) {
        console.error('Error subscribing:', error);
        res.status(500).json({ error: error.message });
    }
});

/**
 * Generate signed WebSocket URL for AWS IoT
 */
async function generateSignedWebSocketUrl(endpoint, region) {
    const signer = new AWS.Signers.V4({
        service: 'iotdevicegateway',
        region: region
    }, {
        accessKeyId: AWS.config.credentials.accessKeyId,
        secretAccessKey: AWS.config.credentials.secretAccessKey
    });

    const timestamp = new Date().toISOString().replace(/[:\-]|\.\d{3}/g, '');
    const dateStamp = timestamp.substr(0, 8);
    const credentialScope = `${dateStamp}/${region}/iotdevicegateway/aws4_request`;
    
    const canonicalUri = '/mqtt';
    const canonicalQuerystring = `X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=${encodeURIComponent(`${AWS.config.credentials.accessKeyId}/${credentialScope}`)}&X-Amz-Date=${timestamp}&X-Amz-SignedHeaders=host`;
    
    const stringToSign = `GET\n${canonicalUri}\n${canonicalQuerystring}\nhost:${endpoint}\n\nhost\n${require('crypto').createHash('sha256').update('').digest('hex')}`;
    const kDate = require('crypto').createHmac('sha256', `AWS4${AWS.config.credentials.secretAccessKey}`).update(dateStamp).digest();
    const kRegion = require('crypto').createHmac('sha256', kDate).update(region).digest();
    const kService = require('crypto').createHmac('sha256', kRegion).update('iotdevicegateway').digest();
    const kSigning = require('crypto').createHmac('sha256', kService).update('aws4_request').digest();
    const signature = require('crypto').createHmac('sha256', kSigning).update(stringToSign).digest('hex');
    
    const signedQuerystring = `${canonicalQuerystring}&X-Amz-Signature=${signature}`;
    return `wss://${endpoint}${canonicalUri}?${signedQuerystring}`;
}

/**
 * Create MQTT connection to AWS IoT
 */
async function createMqttConnection(endpoint, region) {
    const signedUrl = await generateSignedWebSocketUrl(endpoint, region);
    
    return new Promise((resolve, reject) => {
        const client = mqtt.connect(signedUrl, {
            clientId: `proxy-${Date.now()}`,
            protocol: 'wss'
        });
        
        client.on('connect', () => {
            resolve(client);
        });
        
        client.on('error', (err) => {
            reject(err);
        });
    });
}

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
    console.log(`AWS IoT Proxy Server running on port ${PORT}`);
});
