/*
 * ESP32-CAM CORS Proxy Server
 * Run: node server.js
 * Then access: http://localhost:3000
 */

const express = require('express');
const cors = require('cors');
const { createProxyMiddleware } = require('http-proxy-middleware');
const axios = require('axios');

const app = express();
const PORT = 3000;

// CORS Middleware
app.use(cors({
    origin: '*',
    methods: ['GET', 'POST', 'OPTIONS'],
    allowedHeaders: ['*'],
    credentials: true
}));

// Middleware
app.use(express.json());

// Test endpoint
app.get('/api/test', (req, res) => {
    res.json({ 
        status: 'online', 
        server: 'ESP32-CAM Proxy',
        version: '1.0.0'
    });
});

// Direct camera proxy (FIX CORS ISSUE)
app.use('/proxy/camera', async (req, res) => {
    try {
        const { url } = req.query;
        
        if (!url) {
            return res.status(400).json({ error: 'Missing URL parameter' });
        }

        // Decode URL
        const decodedUrl = decodeURIComponent(url);
        
        // Fetch from camera
        const response = await axios({
            method: req.method,
            url: decodedUrl,
            responseType: 'stream',
            timeout: 10000,
            headers: {
                'Accept': req.headers.accept || '*/*',
                'User-Agent': 'ESP32-CAM-Proxy/1.0'
            }
        });

        // Forward headers
        res.set({
            'Content-Type': response.headers['content-type'] || 'application/octet-stream',
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, OPTIONS',
            'Access-Control-Allow-Headers': '*',
            'Cache-Control': 'no-cache, no-store, must-revalidate',
            'Pragma': 'no-cache',
            'Expires': '0'
        });

        // Stream the response
        response.data.pipe(res);

    } catch (error) {
        console.error('Proxy error:', error.message);
        res.status(500).json({ 
            error: 'Proxy failed', 
            message: error.message,
            tip: 'Check camera URL and network connection'
        });
    }
});

// Stream proxy for MJPEG
app.get('/proxy/stream', async (req, res) => {
    try {
        const { url } = req.query;
        
        if (!url) {
            return res.status(400).json({ error: 'Missing camera URL' });
        }

        const cameraUrl = decodeURIComponent(url);
        
        // Set MJPEG headers
        res.set({
            'Content-Type': 'multipart/x-mixed-replace; boundary=frame',
            'Access-Control-Allow-Origin': '*',
            'Cache-Control': 'no-cache',
            'Connection': 'keep-alive'
        });

        // Forward stream
        const response = await axios({
            method: 'GET',
            url: cameraUrl,
            responseType: 'stream',
            timeout: 0
        });

        response.data.pipe(res);

    } catch (error) {
        console.error('Stream proxy error:', error.message);
        res.status(500).json({ error: 'Stream proxy failed' });
    }
});

// Ngrok auto-detection endpoint
app.get('/api/ngrok/status', async (req, res) => {
    try {
        const response = await axios.get('http://localhost:4040/api/tunnels', {
            timeout: 3000
        });
        
        const tunnels = response.data.tunnels || [];
        const publicUrl = tunnels[0]?.public_url || null;
        
        res.json({
            running: true,
            tunnels: tunnels.length,
            publicUrl: publicUrl,
            status: 'active'
        });
        
    } catch (error) {
        res.json({
            running: false,
            tunnels: 0,
            publicUrl: null,
            status: 'not_running',
            error: error.message
        });
    }
});

// ESP32-CAM auto-discovery
app.get('/api/discover', async (req, res) => {
    const discoveredCameras = [];
    
    // Common ESP32-CAM mDNS addresses
    const commonAddresses = [
        'http://esp32-cam.local',
        'http://esp32.local',
        'http://192.168.1.100',
        'http://192.168.1.101',
        'http://192.168.1.102',
        'http://192.168.4.1'  // AP mode
    ];

    const promises = commonAddresses.map(async (address) => {
        try {
            const response = await axios.get(`${address}/status`, {
                timeout: 2000
            });
            
            if (response.status === 200) {
                discoveredCameras.push({
                    url: address,
                    status: 'online',
                    data: response.data
                });
            }
        } catch (error) {
            // Camera not found at this address
        }
    });

    await Promise.all(promises);
    res.json({ cameras: discoveredCameras });
});

// Health check
app.get('/health', (req, res) => {
    res.json({ 
        status: 'healthy',
        timestamp: new Date().toISOString(),
        service: 'ESP32-CAM CORS Proxy'
    });
});

// Static files (for serving HTML)
app.use(express.static(__dirname));

// Start server
app.listen(PORT, () => {
    console.log('════════════════════════════════════════');
    console.log('   ESP32-CAM CORS PROXY SERVER v1.0');
    console.log('════════════════════════════════════════');
    console.log(`🚀 Server running on: http://localhost:${PORT}`);
    console.log(`📡 Proxy endpoint: http://localhost:${PORT}/proxy/camera?url=`);
    console.log(`🎥 Stream proxy: http://localhost:${PORT}/proxy/stream?url=`);
    console.log(`🌐 Ngrok detection: http://localhost:${PORT}/api/ngrok/status`);
    console.log(`🔍 Camera discovery: http://localhost:${PORT}/api/discover`);
    console.log('════════════════════════════════════════');
    console.log('📝 How to use:');
    console.log('1. Run ESP32-CAM on local network');
    console.log('2. Access viewer at: http://localhost:3000');
    console.log('3. Camera will be auto-proxied (NO CORS!)');
    console.log('════════════════════════════════════════');
});
