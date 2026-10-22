#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace advanced {
template<class T> inline T add(T a, T b) {
    const T limit = std::numeric_limits<T>::max();
    return b > limit - a ? limit : static_cast<T>(a + b);
}
inline double best(double value) {
    return std::isfinite(value) && value >= 0 ? value : 0;
}
struct Counters {
    uint64_t hashes = 0;
    uint32_t submitted = 0, accepted = 0, rejected = 0, blocks = 0;
    uint32_t uptime_s = 0;
    double best_difficulty = 0;
};
// Only pool replies belong in the denominator; submitted/pending shares do not.
inline double success_percent(const Counters& counts) {
    uint64_t replies = static_cast<uint64_t>(counts.accepted) + counts.rejected;
    if (!replies) return -1.0;
    // Round once in integer basis points so display and browser agree.
    uint64_t basis_points = (10000ULL * counts.accepted + replies / 2) / replies;
    return static_cast<double>(basis_points) / 100.0;
}
inline void success_json(const Counters& counts, char* out, size_t capacity) {
    double value = success_percent(counts);
    if (value < 0) std::snprintf(out, capacity, "null");
    else std::snprintf(out, capacity, "%.2f", value);
}
class Ledger {
    Counters boot_;
public:
    explicit Ledger(const Counters& boot = Counters()) : boot_(boot) {}
    Counters total(const Counters& session) const {
        Counters t;
        t.hashes = add(boot_.hashes, session.hashes);
        t.submitted = add(boot_.submitted, session.submitted);
        t.accepted = add(boot_.accepted, session.accepted);
        t.rejected = add(boot_.rejected, session.rejected);
        t.blocks = add(boot_.blocks, session.blocks);
        t.uptime_s = add(boot_.uptime_s, session.uptime_s);
        t.best_difficulty = best(boot_.best_difficulty) > best(session.best_difficulty)
            ? best(boot_.best_difficulty) : best(session.best_difficulty);
        return t;
    }
    // Save absolute totals. Neither a successful save nor a failed one changes boot_.
    template<class Save> bool checkpoint(const Counters& session, Save save) const {
        return save(total(session));
    }
};
class SessionClock {
    uint32_t last_ms_ = 0;
    uint64_t elapsed_ms_ = 0;
public:
    void start(uint32_t now) { last_ms_ = now; elapsed_ms_ = 0; }
    uint64_t tick(uint32_t now) {
        elapsed_ms_ += static_cast<uint32_t>(now - last_ms_);
        last_ms_ = now;
        return elapsed_ms_ / 1000;
    }
};
class SavePolicy {
    bool attempted_ = false, first_share_saved_ = false, early_done_ = false;
    uint64_t last_attempt_ = 0, last_saved_ = 0;
public:
    bool due(uint64_t seconds, uint32_t accepted) const {
        if (attempted_ && seconds - last_attempt_ < 30) return false;
        return (!first_share_saved_ && accepted > 0) ||
            (!early_done_ && seconds >= 300) ||
            (early_done_ && seconds - last_saved_ >= 3600);
    }
    void result(uint64_t seconds, uint32_t accepted, bool success) {
        attempted_ = true; last_attempt_ = seconds;
        if (!success) return;
        if (accepted) first_share_saved_ = true;
        if (seconds >= 300) early_done_ = true;
        last_saved_ = seconds;
    }
};
class Rate {
    bool ready_ = false, have_rate_ = false;
    uint64_t hashes_ = 0, ms_ = 0;
    double value_ = 0;
public:
    double sample(uint64_t hashes, uint64_t ms) {
        if (ready_ && ms > ms_ && hashes >= hashes_) {
            double instant = static_cast<double>(hashes - hashes_) * 1000.0 / (ms - ms_);
            value_ = have_rate_ ? 0.15 * instant + 0.85 * value_ : instant;
            have_rate_ = true;
        } else if (ready_ && hashes < hashes_) {
            value_ = 0; have_rate_ = false;
        }
        hashes_ = hashes; ms_ = ms; ready_ = true;
        return value_;
    }
};
struct Snapshot {
    Counters session, lifetime;
    uint64_t sampled_ms = 0, sequence = 0;
    uint32_t boot_id = 0;
    double hashrate_hs = 0;
    bool pool_connected = false, wifi_connected = false;
    bool last_save_ok = true;
    bool save_attempted = false;
};
inline bool limited(const Counters& c) {
    return c.hashes == UINT64_MAX || c.submitted == UINT32_MAX ||
        c.accepted == UINT32_MAX || c.rejected == UINT32_MAX ||
        c.blocks == UINT32_MAX || c.uptime_s == UINT32_MAX;
}
inline bool json(const Snapshot& s, uint64_t now_ms, char* out, size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = '\0';
    if (!std::isfinite(s.hashrate_hs) || s.hashrate_hs < 0 || now_ms < s.sampled_ms) return false;
    const auto& a = s.session; const auto& t = s.lifetime;
    char session_success[16], lifetime_success[16];
    success_json(a, session_success, sizeof session_success);
    success_json(t, lifetime_success, sizeof lifetime_success);
    int n = std::snprintf(out, capacity,
        "{\"schema\":1,\"boot_id\":\"%u\",\"sequence\":\"%llu\",\"sample_age_ms\":\"%llu\",\"hashrate_hs\":%.3f,"
        "\"pool_connected\":%s,\"wifi_connected\":%s,\"save_attempted\":%s,\"last_save_ok\":%s,"
        "\"counter_limit_reached\":%s,"
        "\"session\":{\"hashes\":\"%llu\",\"submitted\":\"%u\",\"accepted\":\"%u\",\"rejected\":\"%u\",\"blocks\":\"%u\",\"uptime_s\":\"%u\",\"success_percent\":%s},"
        "\"lifetime\":{\"hashes\":\"%llu\",\"submitted\":\"%u\",\"accepted\":\"%u\",\"rejected\":\"%u\",\"blocks\":\"%u\",\"uptime_s\":\"%u\",\"best_difficulty\":%.9g,\"success_percent\":%s}}",
        (unsigned)s.boot_id, (unsigned long long)s.sequence, (unsigned long long)(now_ms-s.sampled_ms), s.hashrate_hs,
        s.pool_connected?"true":"false", s.wifi_connected?"true":"false",
        s.save_attempted?"true":"false", s.last_save_ok?"true":"false", limited(t)?"true":"false",
        (unsigned long long)a.hashes, (unsigned)a.submitted, (unsigned)a.accepted, (unsigned)a.rejected, (unsigned)a.blocks, (unsigned)a.uptime_s, session_success,
        (unsigned long long)t.hashes, (unsigned)t.submitted, (unsigned)t.accepted, (unsigned)t.rejected, (unsigned)t.blocks, (unsigned)t.uptime_s, best(t.best_difficulty), lifetime_success);
    if (n < 0 || static_cast<size_t>(n) >= capacity) { out[0] = '\0'; return false; }
    return true;
}
} // namespace advanced
