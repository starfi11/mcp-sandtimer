#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "mcp_sandtimer/Json.h"

namespace mcp_sandtimer {

class TimerClientError : public std::runtime_error {
public:
    explicit TimerClientError(const std::string& message);
};

class TimerClient {
public:
    using milliseconds = std::chrono::milliseconds;

    TimerClient();
    TimerClient(std::string host, std::uint16_t port, milliseconds timeout = milliseconds{5000});

    const std::string& host() const noexcept { return host_; }
    std::uint16_t port() const noexcept { return port_; }
    milliseconds timeout() const noexcept { return timeout_; }

    void set_host(std::string host) { host_ = std::move(host); }
    void set_port(std::uint16_t port) noexcept { port_ = port; }
    void set_timeout(milliseconds timeout) noexcept { timeout_ = timeout; }

    void start_timer(const std::string& label, int seconds) const;
    void reset_timer(const std::string& label) const;
    void cancel_timer(const std::string& label) const;

private:
    std::string host_;
    std::uint16_t port_;
    milliseconds timeout_;

    void send_payload(const json::Value& payload) const;
};

}  // namespace mcp_sandtimer
