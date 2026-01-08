/*
 * SparkMiner - Stratum Protocol Types
 * Data structures for Stratum mining protocol
 *
 * Based on BitsyMiner by Justin Williams (GPL v3)
 */

#ifndef STRATUM_TYPES_H
#define STRATUM_TYPES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <board_config.h>

// Stratum protocol constants
#define DESIRED_DIFFICULTY      0.0014
#define STRATUM_MSG_SIZE        512
#define MAX_PENDING_SUBMISSIONS 30

// Submission flags
#define SUBMIT_FLAG_32BIT       0x02    // 32-bit share (difficulty >= 2^32)
#define SUBMIT_FLAG_BLOCK       0x04    // Full block solution

// Callback for submission response
typedef void (*SubmitCallback)(uint32_t sessionId, uint32_t msgId, bool accepted, const char* reason);

// Fixed sizes for stratum job fields (avoid heap fragmentation from String)
#define STRATUM_JOB_ID_LEN      16      // Job ID (usually 4-8 hex chars)
#define STRATUM_PREVHASH_LEN    68      // Previous block hash (64 hex + null)
#define STRATUM_COINBASE1_LEN   512     // Coinbase part 1 (variable, can be large)
#define STRATUM_COINBASE2_LEN   256     // Coinbase part 2
#define STRATUM_EXTRANONCE_LEN  32      // Extra nonce (usually 8-16 hex)
#define STRATUM_FIELD_LEN       12      // Version/nbits/ntime (8 hex + null)
#define STRATUM_MAX_MERKLE      16      // Max merkle branches (usually < 10)

/**
 * Stratum job from pool (mining.notify)
 * Uses fixed char arrays to avoid heap fragmentation
 */
typedef struct {
    char jobId[STRATUM_JOB_ID_LEN];         // Unique job identifier
    char prevHash[STRATUM_PREVHASH_LEN];    // Previous block hash (256-bit hex)
    char coinBase1[STRATUM_COINBASE1_LEN];  // Coinbase transaction part 1
    char coinBase2[STRATUM_COINBASE2_LEN];  // Coinbase transaction part 2
    char extraNonce1[STRATUM_EXTRANONCE_LEN]; // Pool-provided extra nonce
    int extraNonce2Size;                    // Size of extraNonce2 in bytes
    char merkleBranches[STRATUM_MAX_MERKLE][68]; // Merkle branches (64 hex + null each)
    int merkleBranchCount;                  // Number of merkle branches
    char version[STRATUM_FIELD_LEN];        // Block version (4 bytes hex)
    char nbits[STRATUM_FIELD_LEN];          // Difficulty target (4 bytes hex)
    char ntime[STRATUM_FIELD_LEN];          // Block timestamp (4 bytes hex)
    bool cleanJobs;                         // Clear pending jobs
} stratum_job_t;

/**
 * Share submission queue entry
 * Used for async submission tracking with response callback
 */
typedef struct {
    char jobId[MAX_JOB_ID_LEN];     // Job ID this share belongs to
    char extraNonce2[20];           // ExtraNonce2 value
    uint32_t timestamp;             // Block timestamp
    uint32_t nonce;                 // Winning nonce
    uint32_t msgId;                 // Stratum message ID
    uint32_t sessionId;             // Session ID for tracking
    uint32_t sentTime;              // Timestamp when sent to pool (ms)
    uint32_t versionBits;           // Version rolling bits (ASICBoost)
    uint32_t flags;                 // SUBMIT_FLAG_* values
    double difficulty;              // Share difficulty
    SubmitCallback callback;        // Response callback
} submit_entry_t;

/**
 * Mining statistics
 */
typedef struct {
    volatile uint64_t hashes;       // Total hashes computed
    volatile uint32_t shares;       // Shares submitted
    volatile uint32_t accepted;     // Shares accepted by pool
    volatile uint32_t rejected;     // Shares rejected by pool
    volatile uint32_t blocks;       // Full blocks found (lottery wins!)
    volatile uint32_t matches32;    // 32-bit difficulty matches
    volatile uint32_t matches16;    // 16-bit matches (for stats)
    volatile uint32_t lastLatency;  // Last round-trip latency in ms
    volatile uint32_t avgLatency;   // Moving average latency in ms (EMA)
    double bestDifficulty;          // Best difficulty found
    uint32_t startTime;             // Mining start timestamp
    uint32_t templates;             // Jobs received from pool
} mining_stats_t;

/**
 * Pool connection state
 */
typedef enum {
    POOL_DISCONNECTED = 0,
    POOL_CONNECTING,
    POOL_SUBSCRIBING,
    POOL_AUTHORIZING,
    POOL_MINING,
    POOL_ERROR
} pool_state_t;

/**
 * Pool configuration
 */
typedef struct {
    char url[MAX_POOL_URL_LEN];
    int port;
    char wallet[MAX_WALLET_LEN];
    char password[MAX_PASSWORD_LEN];
    char workerName[32];
} pool_config_t;

#endif // STRATUM_TYPES_H
