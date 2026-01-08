/**
 * SparkMiner Stats Proxy - Cloudflare Worker
 *
 * This worker acts as an HTTP-to-HTTPS proxy for the ESP32 miner stats APIs.
 * It offloads SSL/TLS handshakes from the ESP32, improving stability.
 *
 * Deploy to Cloudflare Workers (free tier includes 100k requests/day)
 *
 * Deployment:
 *   1. Create a Cloudflare account and go to Workers & Pages
 *   2. Create a new Worker
 *   3. Paste this code and deploy
 *   4. Configure your SparkMiner with: http://your-worker.workers.dev:80
 *
 * Supported Request Formats:
 *
 *   1. Traditional HTTP Proxy (SparkMiner default):
 *      GET https://api.coingecko.com/api/v3/simple/price?... HTTP/1.1
 *      Host: api.coingecko.com
 *
 *   2. URL Parameter (browser/testing):
 *      GET /?url=https://api.coingecko.com/api/v3/simple/price?...
 *
 *   3. Path Forwarding:
 *      GET /https://api.coingecko.com/api/v3/simple/price?...
 *
 * The worker fetches the target URL over HTTPS and returns the response over HTTP.
 *
 * Security Notes:
 *   - Only allows requests to whitelisted domains (crypto APIs)
 *   - Rate limited by Cloudflare's built-in protections
 *   - No authentication required (public price/stats data only)
 */

// Allowed domains for proxying (crypto/stats APIs only)
const ALLOWED_DOMAINS = [
  'api.coingecko.com',
  'public-pool.io',
  'mempool.space',
  'blockchain.info',
  'api.blockchain.info',
  'api.blockcypher.com'
];

// CORS headers for browser compatibility (if needed)
const CORS_HEADERS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type'
};

/**
 * Check if the target URL's domain is in our whitelist
 */
function isAllowedDomain(targetUrl) {
  try {
    const url = new URL(targetUrl);
    return ALLOWED_DOMAINS.some(domain =>
      url.hostname === domain || url.hostname.endsWith('.' + domain)
    );
  } catch (e) {
    return false;
  }
}

/**
 * Extract target URL from various request formats
 */
function extractTargetUrl(request) {
  const requestUrl = new URL(request.url);

  // Method 1: Check ?url= query parameter
  const urlParam = requestUrl.searchParams.get('url');
  if (urlParam && urlParam.startsWith('https://')) {
    return urlParam;
  }

  // Method 2: Check if path starts with /https:// (path forwarding)
  const path = requestUrl.pathname;
  if (path.startsWith('/https://') || path.startsWith('/http://')) {
    // Reconstruct the full URL from path + query string
    return path.substring(1) + requestUrl.search;
  }

  // Method 3: Check X-Target-URL header (custom header method)
  const targetHeader = request.headers.get('X-Target-URL');
  if (targetHeader && targetHeader.startsWith('https://')) {
    return targetHeader;
  }

  // Method 4: Check if the original request URL is the full target (traditional proxy)
  // When ESP32 sends: GET https://api.coingecko.com/... HTTP/1.1
  // Cloudflare may provide this in the request URL
  if (requestUrl.hostname !== request.headers.get('host')?.split(':')[0]) {
    // The request URL hostname differs from Host header - could be proxy request
    return request.url;
  }

  return null;
}

/**
 * Handle incoming requests
 */
async function handleRequest(request) {
  // Handle CORS preflight
  if (request.method === 'OPTIONS') {
    return new Response(null, { headers: CORS_HEADERS });
  }

  // Only allow GET requests
  if (request.method !== 'GET') {
    return new Response(JSON.stringify({ error: 'Method not allowed' }), {
      status: 405,
      headers: { 'Content-Type': 'application/json', ...CORS_HEADERS }
    });
  }

  // Extract target URL from request
  const targetUrl = extractTargetUrl(request);

  if (!targetUrl) {
    return new Response(JSON.stringify({
      error: 'Missing target URL',
      usage: [
        'GET /?url=https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd',
        'GET /https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd',
        'Configure SparkMiner proxy: http://your-worker.workers.dev:80'
      ],
      note: 'This proxy offloads HTTPS for ESP32 devices'
    }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...CORS_HEADERS }
    });
  }

  // Validate the target URL
  if (!targetUrl.startsWith('https://')) {
    return new Response(JSON.stringify({ error: 'Only HTTPS URLs are allowed' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...CORS_HEADERS }
    });
  }

  // Check domain whitelist
  if (!isAllowedDomain(targetUrl)) {
    return new Response(JSON.stringify({
      error: 'Domain not allowed',
      allowed: ALLOWED_DOMAINS
    }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...CORS_HEADERS }
    });
  }

  try {
    // Fetch the target URL over HTTPS
    const response = await fetch(targetUrl, {
      method: 'GET',
      headers: {
        'User-Agent': 'SparkMiner-StatsProxy/1.0',
        'Accept': 'application/json'
      }
    });

    // Get the response body
    const body = await response.text();

    // Return the response with original status and CORS headers
    return new Response(body, {
      status: response.status,
      headers: {
        'Content-Type': response.headers.get('Content-Type') || 'application/json',
        'X-Proxied-From': new URL(targetUrl).hostname,
        ...CORS_HEADERS
      }
    });

  } catch (error) {
    return new Response(JSON.stringify({
      error: 'Proxy fetch failed',
      message: error.message
    }), {
      status: 502,
      headers: { 'Content-Type': 'application/json', ...CORS_HEADERS }
    });
  }
}

// Cloudflare Workers entry point
export default {
  async fetch(request, env, ctx) {
    return handleRequest(request);
  }
};

// Alternative: addEventListener for older Worker format
// addEventListener('fetch', event => {
//   event.respondWith(handleRequest(event.request));
// });
