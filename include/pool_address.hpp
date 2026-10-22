#pragma once
#include <cstring>
namespace advanced {
// The TCP client needs a DNS host, not a Stratum URI. Only strip the explicit
// plaintext scheme; TLS and other protocols must never be silently downgraded.
inline const char* tcp_pool_host(const char* address) {
    static const char prefix[] = "stratum+tcp://";
    if (!address) return "";
    return std::strncmp(address, prefix, sizeof(prefix) - 1) == 0
        ? address + sizeof(prefix) - 1 : address;
}
}
