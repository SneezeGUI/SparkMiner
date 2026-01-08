/*
 * SparkMiner - Stratum Protocol Implementation
 * Stratum v1 client for pool communication
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <utility>  // For std::swap
#include <board_config.h>
#include "stratum.h"
#include "../mining/miner.h"

// ============================================================
// Constants
// ============================================================
#define STRATUM_MSG_BUFFER  512
#define RESPONSE_TIMEOUT_MS 3000
#define KEEPALIVE_MS        120000
#define INACTIVITY_MS       700000

// ============================================================
// Global State
// ============================================================
static QueueHandle_t s_submitQueue = NULL;
static submit_entry_t s_pendingResponses[MAX_PENDING_SUBMISSIONS];
static uint16_t s_pendingIndex = 0;

static pool_config_t s_primaryPool;
static pool_config_t s_backupPool;
static bool s_hasBackupPool = false;

static volatile bool s_isConnected = false;
static volatile bool s_reconnectRequested = false;
static char s_currentPoolUrl[MAX_POOL_URL_LEN] = {0};

static uint32_t s_messageId = 1;
static uint32_t s_lastActivity = 0;
static uint32_t s_lastSubmit = 0;

// Extra nonce from subscription
static char s_extraNonce1[32] = {0};
static int s_extraNonce2Size = 4;

// JSON document for parsing
static StaticJsonDocument<4096> s_doc;

// ============================================================
// Utility Functions
// ============================================================

static uint32_t getNextId() {
    if (s_messageId == UINT32_MAX) {
        s_messageId = 1;
    }
    return s_messageId++;
}

// Safe string copy with null termination
static void safeStrCpy(char *dest, const char *src, size_t maxLen) {
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
}

// Format hex string with zero padding (big-endian - value as hex)
static void formatHex8(char *dest, uint32_t value) {
    static const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        dest[i] = hex[value & 0xF];
        value >>= 4;
    }
    dest[8] = '\0';
}

// Bounded read to prevent stack overflow/OOM from malicious packets
static String readBoundedLine(WiFiClient& client, size_t maxLen = 4096) {
    String line;
    line.reserve(256);  // Initial allocation
    unsigned long start = millis();
    
    while (client.connected() && (millis() - start < 5000)) {
        if (client.available()) {
            char c = client.read();
            if (c == '\n') {
                return line;
            }
            if (line.length() < maxLen) {
                line += c;
            } else {
                // Line too long - read until newline and discard
                while (client.available()) {
                     if (client.read() == '\n') break;
                     if (millis() - start > 5000) break;
                }
                Serial.println("[STRATUM] WARNING: Line exceeded max length, discarded");
                return "";  // Return empty to signal error
            }
        } else {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }
    return line;
}

// ============================================================
// Protocol Functions
// ============================================================

static bool sendMessage(WiFiClient &client, const char *msg) {
    if (!client.connected()) return false;

    // Send message with newline as single write (like NerdMiner)
    // This avoids TCP packet fragmentation issues
    String fullMsg = String(msg) + "\n";
    client.print(fullMsg);

    dbg("[STRATUM] TX: %s\n", msg);
    return true;
}

static bool waitForResponse(WiFiClient &client, int timeoutMs) {
    int elapsed = 0;
    while (!client.available() && elapsed < timeoutMs) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        elapsed += 10;
    }
    return client.available() > 0;
}

static bool parseSubscribeResponse(const String &line) {
    s_doc.clear();
    DeserializationError err = deserializeJson(s_doc, line);

    if (err) {
        Serial.printf("[STRATUM] JSON parse error: %s\nRAW: %s\n", err.c_str(), line.c_str());
        return false;
    }

    if (s_doc.containsKey("error") && !s_doc["error"].isNull()) {
        const char *errMsg = s_doc["error"][1];
        Serial.printf("[STRATUM] Subscribe error: %s\n", errMsg ? errMsg : "unknown");
        return false;
    }

    if (!s_doc.containsKey("result") || !s_doc["result"].is<JsonArray>()) {
        Serial.println("[STRATUM] Invalid subscribe response (no result)");
        return false;
    }

    // Extract extra nonce
    const char *en1 = s_doc["result"][1];
    if (en1) {
        safeStrCpy(s_extraNonce1, en1, sizeof(s_extraNonce1));
    }

    s_extraNonce2Size = s_doc["result"][2] | 4;

    // Pass to miner
    miner_set_extranonce(s_extraNonce1, s_extraNonce2Size);

    dbg("[STRATUM] Subscribed: extraNonce1=%s, extraNonce2Size=%d\n",
        s_extraNonce1, s_extraNonce2Size);

    return true;
}

static bool parseAuthorizeResponse(const String &line) {
    s_doc.clear();
    DeserializationError err = deserializeJson(s_doc, line);

    if (err) return false;

    if (s_doc.containsKey("error") && !s_doc["error"].isNull()) {
        const char *errMsg = s_doc["error"][1];
        Serial.printf("[STRATUM] Auth error: %s\n", errMsg ? errMsg : "unknown");
        return false;
    }

    bool result = s_doc["result"] | false;
    return result;
}

static void parseMiningNotify(const String &line) {
    if (!s_doc.containsKey("params")) return;

    JsonArray params = s_doc["params"];

    // Use static job to avoid stack allocation of large struct each time
    static stratum_job_t job;
    memset(&job, 0, sizeof(job));

    // Copy strings to fixed char arrays (no heap allocation!)
    const char *p0 = params[0];
    const char *p1 = params[1];
    const char *p2 = params[2];
    const char *p3 = params[3];
    const char *p5 = params[5];
    const char *p6 = params[6];
    const char *p7 = params[7];

    if (p0) strncpy(job.jobId, p0, STRATUM_JOB_ID_LEN - 1);
    if (p1) strncpy(job.prevHash, p1, STRATUM_PREVHASH_LEN - 1);
    if (p2) strncpy(job.coinBase1, p2, STRATUM_COINBASE1_LEN - 1);
    if (p3) strncpy(job.coinBase2, p3, STRATUM_COINBASE2_LEN - 1);
    if (p5) strncpy(job.version, p5, STRATUM_FIELD_LEN - 1);
    if (p6) strncpy(job.nbits, p6, STRATUM_FIELD_LEN - 1);
    if (p7) strncpy(job.ntime, p7, STRATUM_FIELD_LEN - 1);

    // Copy merkle branches to fixed array
    JsonArray merkle = params[4];
    job.merkleBranchCount = 0;
    for (size_t i = 0; i < merkle.size() && i < STRATUM_MAX_MERKLE; i++) {
        const char *branch = merkle[i];
        if (branch) {
            strncpy(job.merkleBranches[i], branch, 67);
            job.merkleBranches[i][67] = '\0';
            job.merkleBranchCount++;
        }
    }

    job.cleanJobs = params[8] | false;
    strncpy(job.extraNonce1, s_extraNonce1, STRATUM_EXTRANONCE_LEN - 1);
    job.extraNonce2Size = s_extraNonce2Size;

    s_lastActivity = millis();
    miner_start_job(&job);
}

static void parseSetDifficulty(const String &line) {
    if (!s_doc.containsKey("params")) return;

    double diff = s_doc["params"][0] | 1.0;

    if (!isnan(diff) && diff > 0) {
        miner_set_difficulty(diff);
        dbg("[STRATUM] Pool difficulty: %.4f\n", diff);
    }
}

static void handleServerMessage(WiFiClient &client) {
    String line = readBoundedLine(client);
    line.trim();

    if (line.length() == 0) return;

    dbg("[STRATUM] RX: %s\n", line.c_str());

    s_doc.clear();
    DeserializationError err = deserializeJson(s_doc, line);
    if (err) {
        dbg("[STRATUM] Parse error: %s\n", err.c_str());
        return;
    }

    // Check for submission responses
    if (s_doc.containsKey("id") && s_doc.containsKey("result")) {
        uint32_t msgId = s_doc["id"];
        bool accepted = s_doc["result"] | false;

        // Find matching pending submission
        for (int i = 0; i < MAX_PENDING_SUBMISSIONS; i++) {
            if (s_pendingResponses[i].msgId == msgId) {
                mining_stats_t *stats = miner_get_stats();

                uint32_t latency = millis() - s_pendingResponses[i].sentTime;
                stats->lastLatency = latency;
                stats->avgLatency = (stats->avgLatency == 0) ? latency : ((stats->avgLatency * 9 + latency) / 10);

                if (accepted) {
                    stats->accepted++;
                    dbg("[STRATUM] Share accepted!\n");
                } else {
                    stats->rejected++;
                    const char *reason = s_doc["error"][1] | "unknown";
                    dbg("[STRATUM] Share rejected: %s\n", reason);
                    Serial.printf("[STRATUM] Share rejected: %s\n", reason);
                }

                // Call callback if set
                if (s_pendingResponses[i].callback) {
                    const char *reason = accepted ? NULL : (const char *)s_doc["error"][1];
                    s_pendingResponses[i].callback(
                        s_pendingResponses[i].sessionId,
                        s_pendingResponses[i].msgId,
                        accepted,
                        reason
                    );
                }

                s_pendingResponses[i].msgId = 0;  // Clear slot
                break;
            }
        }
    }

    // Check for method calls
    if (s_doc.containsKey("method")) {
        const char *method = s_doc["method"];

        if (strcmp(method, "mining.notify") == 0) {
            parseMiningNotify(line);
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            parseSetDifficulty(line);
        } else {
            dbg("[STRATUM] Unknown method: %s\n", method);
        }
    }
}

// Helper: Read lines until we get a response with matching ID (or timeout)
// Handles method calls (set_difficulty, notify) that arrive before the response
static bool waitForResponseById(WiFiClient &client, uint32_t expectedId, String &outResponse, int maxAttempts = 10) {
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        String line = readBoundedLine(client);  // Use bounded read to prevent OOM
        line.trim();

        if (line.length() == 0) {
            Serial.println("[STRATUM] Response timeout");
            return false;
        }

        // Parse to check if this is our response or a method call
        s_doc.clear();
        DeserializationError err = deserializeJson(s_doc, line);
        if (err) {
            Serial.printf("[STRATUM] JSON parse error: %s\n", err.c_str());
            continue;
        }

        // Check if this is a method call (id is null or missing, has "method" field)
        if (s_doc.containsKey("method")) {
            const char *method = s_doc["method"];

            // Handle set_difficulty immediately since it's important
            if (strcmp(method, "mining.set_difficulty") == 0) {
                double diff = s_doc["params"][0] | 1.0;
                if (!isnan(diff) && diff > 0) {
                    miner_set_difficulty(diff);
                }
            }
            // Continue reading for our actual response
            continue;
        }

        // Check if this response matches our expected ID
        if (s_doc.containsKey("id")) {
            uint32_t respId = s_doc["id"] | 0;
            if (respId == expectedId) {
                outResponse = line;
                return true;
            }
            Serial.printf("[STRATUM] Got response for different id: %lu (expected %lu)\n", respId, expectedId);
        }
    }

    Serial.println("[STRATUM] Max attempts reached waiting for response");
    return false;
}

static bool subscribe(WiFiClient &client, const char *wallet, const char *password) {
    char msg[STRATUM_MSG_BUFFER];

    // Set client timeout for blocking reads
    client.setTimeout(5000);

    // Mining.subscribe
    uint32_t subId = getNextId();
    snprintf(msg, sizeof(msg),
        "{\"id\":%lu,\"method\":\"mining.subscribe\",\"params\":[\"%s/%s\"]}",
        subId, MINER_NAME, AUTO_VERSION);

    uint32_t startSub = millis();
    if (!sendMessage(client, msg)) return false;

    // Small delay to allow server to process (like NerdMiner)
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // Wait for subscribe response (handle any method calls that arrive first)
    String resp;
    if (!waitForResponseById(client, subId, resp)) {
        Serial.println("[STRATUM] No subscribe response");
        return false;
    }

    // Record subscribe latency
    uint32_t subLatency = millis() - startSub;
    mining_stats_t *stats = miner_get_stats();
    stats->lastLatency = subLatency;
    stats->avgLatency = (stats->avgLatency == 0) ? subLatency : ((stats->avgLatency * 9 + subLatency) / 10);

    if (!parseSubscribeResponse(resp)) {
        Serial.println("[STRATUM] Subscribe failed");
        return false;
    }

    // Suggest difficulty
    uint32_t diffId = getNextId();
    snprintf(msg, sizeof(msg),
        "{\"id\":%lu,\"method\":\"mining.suggest_difficulty\",\"params\":[%.10g]}",
        diffId, DESIRED_DIFFICULTY);
    sendMessage(client, msg);

    // Mining.authorize - append worker name if set
    char fullUsername[MAX_WALLET_LEN + 34];
    if (s_primaryPool.workerName[0]) {
        snprintf(fullUsername, sizeof(fullUsername), "%s.%s", wallet, s_primaryPool.workerName);
    } else {
        safeStrCpy(fullUsername, wallet, sizeof(fullUsername));
    }

    uint32_t authId = getNextId();
    snprintf(msg, sizeof(msg),
        "{\"id\":%lu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}",
        authId, fullUsername, password);

    uint32_t startAuth = millis();
    if (!sendMessage(client, msg)) return false;

    // Small delay before reading
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // Wait for authorize response (handle set_difficulty/notify that may arrive first)
    if (!waitForResponseById(client, authId, resp)) {
        Serial.println("[STRATUM] No authorize response");
        return false;
    }

    // Record authorize latency
    uint32_t authLatency = millis() - startAuth;
    stats->lastLatency = authLatency;
    stats->avgLatency = (stats->avgLatency * 9 + authLatency) / 10;

    if (!parseAuthorizeResponse(resp)) {
        Serial.println("[STRATUM] Authorization failed");
        return false;
    }

    Serial.printf("[STRATUM] Authorized as %s\n", wallet);
    return true;
}

static void submitShare(WiFiClient &client, const submit_entry_t *entry) {
    char msg[STRATUM_MSG_BUFFER];
    char timestamp[9], nonce[9];

    // Format as 8-char hex (value as hex, zero-padded)
    formatHex8(timestamp, entry->timestamp);
    formatHex8(nonce, entry->nonce);

    uint32_t msgId = getNextId();

    // Standard Stratum v1 submit (5 params, no version rolling)
    snprintf(msg, sizeof(msg),
        "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}",
        msgId,
        s_primaryPool.wallet,
        entry->jobId,
        entry->extraNonce2,
        timestamp,
        nonce);

    Serial.printf("[STRATUM] Submit: job=%s en2=%s time=%s nonce=%s\n",
        entry->jobId, entry->extraNonce2, timestamp, nonce);

    if (sendMessage(client, msg)) {
        // Store in pending responses for latency tracking
        submit_entry_t pending = *entry;
        pending.msgId = msgId;
        pending.sentTime = millis();

        s_pendingResponses[s_pendingIndex] = pending;
        s_pendingIndex = (s_pendingIndex + 1) % MAX_PENDING_SUBMISSIONS;

        s_lastSubmit = millis();
        miner_get_stats()->shares++;
    }
}

// ============================================================
// Public API
// ============================================================

void stratum_init() {
    // Create submission queue
    s_submitQueue = xQueueCreate(MAX_PENDING_SUBMISSIONS, sizeof(submit_entry_t));

    // Initialize pending responses
    memset(s_pendingResponses, 0, sizeof(s_pendingResponses));

    // Set default pool
    safeStrCpy(s_primaryPool.url, DEFAULT_POOL_URL, MAX_POOL_URL_LEN);
    s_primaryPool.port = DEFAULT_POOL_PORT;
    safeStrCpy(s_primaryPool.password, DEFAULT_POOL_PASS, MAX_PASSWORD_LEN);

    dbg("[STRATUM] Initialized\n");
}

void stratum_task(void *param) {
    WiFiClient client;
    bool usingBackup = false;
    uint32_t lastConnectAttempt = 0;
    uint32_t backupConnectTime = 0;

    Serial.printf("[STRATUM] Task started on core %d\n", xPortGetCoreID());

    while (true) {
        // Wait for WiFi
        if (WiFi.status() != WL_CONNECTED) {
            if (s_isConnected) {
                miner_stop();
                client.stop();
                s_isConnected = false;
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        // Check pool configuration
        if (!s_primaryPool.url[0] || !s_primaryPool.port) {
            dbg("[STRATUM] No pool configured\n");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!s_primaryPool.wallet[0]) {
            dbg("[STRATUM] No wallet configured\n");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        // Handle reconnect request
        if (s_reconnectRequested) {
            miner_stop();
            client.stop();
            s_isConnected = false;
            s_reconnectRequested = false;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Connect if needed
        if (!client.connected()) {
            if (s_isConnected) {
                miner_stop();
                s_isConnected = false;
            }

            usingBackup = false;

            Serial.printf("[STRATUM] Connecting to %s:%d...\n",
                s_primaryPool.url, s_primaryPool.port);

            // STABILITY FIX: Use connect timeout (10s) to prevent long blocks
            if (client.connect(s_primaryPool.url, s_primaryPool.port, 10000)) {
                if (subscribe(client, s_primaryPool.wallet, s_primaryPool.password)) {
                    s_isConnected = true;
                    s_lastActivity = millis();
                    safeStrCpy(s_currentPoolUrl, s_primaryPool.url, MAX_POOL_URL_LEN);
                    Serial.println("[STRATUM] Connected to primary pool");
                } else {
                    client.stop();
                }
            } else {
                Serial.println("[STRATUM] Connection failed");

                // Try backup pool after 30s of failures
                if (s_hasBackupPool && (millis() - lastConnectAttempt > POOL_FAILOVER_MS)) {
                    Serial.printf("[STRATUM] Trying backup: %s:%d\n",
                        s_backupPool.url, s_backupPool.port);

                    // STABILITY FIX: Use connect timeout (10s)
                    if (client.connect(s_backupPool.url, s_backupPool.port, 10000)) {
                        if (subscribe(client, s_backupPool.wallet, s_backupPool.password)) {
                            s_isConnected = true;
                            usingBackup = true;
                            backupConnectTime = millis();
                            s_lastActivity = millis();
                            safeStrCpy(s_currentPoolUrl, s_backupPool.url, MAX_POOL_URL_LEN);
                            Serial.println("[STRATUM] Connected to backup pool");
                        } else {
                            client.stop();
                        }
                    }
                }
            }

            lastConnectAttempt = millis();

            if (!s_isConnected) {
                vTaskDelay(10000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        // Try to switch back from backup after 2 minutes
        if (usingBackup && (millis() - backupConnectTime > 120000)) {
            // STABILITY FIX: Use connect timeout and avoid shallow copy of WiFiClient
            // Test connection to primary pool first
            WiFiClient testClient;
            if (testClient.connect(s_primaryPool.url, s_primaryPool.port, 10000)) {
                if (subscribe(testClient, s_primaryPool.wallet, s_primaryPool.password)) {
                    // Successfully connected to primary - switch over
                    miner_stop();
                    client.stop();
                    // Use swap to safely transfer the connection instead of shallow copy
                    std::swap(client, testClient);
                    testClient.stop();  // Clean up the old (now empty) client
                    usingBackup = false;
                    safeStrCpy(s_currentPoolUrl, s_primaryPool.url, MAX_POOL_URL_LEN);
                    Serial.println("[STRATUM] Switched back to primary pool");
                    continue;
                } else {
                    testClient.stop();
                }
            }
            backupConnectTime = millis();  // Try again later
        }

        // Handle incoming messages
        while (client.available() > 0) {
            handleServerMessage(client);
        }

        // Process submission queue
        submit_entry_t entry;
        while (xQueueReceive(s_submitQueue, &entry, 0) == pdTRUE) {
            submitShare(client, &entry);
        }

        // Send keepalive if idle
        if (millis() - s_lastSubmit > KEEPALIVE_MS) {
            char msg[STRATUM_MSG_BUFFER];
            uint32_t keepId = getNextId();
            snprintf(msg, sizeof(msg),
                "{\"id\":%lu,\"method\":\"mining.suggest_difficulty\",\"params\":[%.10g]}",
                keepId, DESIRED_DIFFICULTY);
            sendMessage(client, msg);
            s_lastSubmit = millis();
        }

        // Check for inactivity
        if (millis() - s_lastActivity > INACTIVITY_MS) {
            Serial.println("[STRATUM] Pool inactive, disconnecting");
            miner_stop();
            client.stop();
            s_isConnected = false;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

bool stratum_submit_share(const submit_entry_t *entry) {
    if (!s_submitQueue) return false;
    return xQueueSend(s_submitQueue, entry, pdMS_TO_TICKS(100)) == pdTRUE;
}

void stratum_reconnect() {
    s_reconnectRequested = true;
}

bool stratum_is_connected() {
    return s_isConnected;
}

const char* stratum_get_pool() {
    return s_currentPoolUrl;
}

void stratum_set_pool(const char *url, int port, const char *wallet, const char *password, const char *workerName) {
    safeStrCpy(s_primaryPool.url, url, MAX_POOL_URL_LEN);
    s_primaryPool.port = port;
    safeStrCpy(s_primaryPool.wallet, wallet, MAX_WALLET_LEN);
    safeStrCpy(s_primaryPool.password, password, MAX_PASSWORD_LEN);
    if (workerName) {
        safeStrCpy(s_primaryPool.workerName, workerName, 32);
    } else {
        s_primaryPool.workerName[0] = '\0';
    }
}

void stratum_set_backup_pool(const char *url, int port, const char *wallet, const char *password, const char *workerName) {
    safeStrCpy(s_backupPool.url, url, MAX_POOL_URL_LEN);
    s_backupPool.port = port;
    safeStrCpy(s_backupPool.wallet, wallet, MAX_WALLET_LEN);
    safeStrCpy(s_backupPool.password, password, MAX_PASSWORD_LEN);
    if (workerName) {
        safeStrCpy(s_backupPool.workerName, workerName, 32);
    } else {
        s_backupPool.workerName[0] = '\0';
    }
    s_hasBackupPool = (url[0] && port > 0 && wallet[0]);
}
