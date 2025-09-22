#include "mcp_sandtimer/TimerClient.h"

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif

#include "mcp_sandtimer/Json.h"

namespace mcp_sandtimer {
// TimerClient 负责和 sandtimer 程序的 TCP端口通信
// 下面处理跨平台 sokect 适配
namespace {
#ifdef _WIN32
struct WinsockSession {
    WinsockSession() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            std::ostringstream oss;
            oss << "WSAStartup failed with error " << result;
            throw TimerClientError(oss.str());
        }
    }
    ~WinsockSession() { WSACleanup(); }
};
using socket_handle = SOCKET;
constexpr socket_handle kInvalidSocket = INVALID_SOCKET;
inline void close_socket(socket_handle socket) {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}
inline std::string last_error_message(const std::string& prefix) {
    const int code = WSAGetLastError();
    std::ostringstream oss;
    oss << prefix << " (code " << code << ")";
    return oss.str();
}
#else
using socket_handle = int;
constexpr socket_handle kInvalidSocket = -1;
inline void close_socket(socket_handle socket) {
    if (socket != kInvalidSocket) {
        close(socket);
    }
}
inline std::string last_error_message(const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << ": " << std::strerror(errno);
    return oss.str();
}
#endif
}  // namespace

TimerClientError::TimerClientError(const std::string& message) : std::runtime_error(message) {}

TimerClient::TimerClient() : TimerClient("127.0.0.1", 61420) {}

TimerClient::TimerClient(std::string host, std::uint16_t port, milliseconds timeout)
    : host_(std::move(host)), port_(port), timeout_(timeout) {}

void TimerClient::start_timer(const std::string& label, int seconds) const {
    json::Value payload = json::make_object({
        {"cmd", json::Value("start")},
        {"label", json::Value(label.c_str())},
        {"time", json::Value(seconds)}
    });
    send_payload(payload);
}

void TimerClient::reset_timer(const std::string& label) const {
    json::Value payload = json::make_object({
        {"cmd", json::Value("reset")},
        {"label", json::Value(label.c_str())}
    });
    send_payload(payload);
}

void TimerClient::cancel_timer(const std::string& label) const {
    json::Value payload = json::make_object({
        {"cmd", json::Value("cancel")},
        {"label", json::Value(label.c_str())}
    });
    send_payload(payload);
}
// 发送消息给sandtimer，每次都建立新连接，发送完毕后关闭连接，所以接收端不readAll就能拿到完整消息。
void TimerClient::send_payload(const json::Value& payload) const {
    const std::string message = payload.dump();

#ifdef _WIN32
    WinsockSession session;
#endif

    struct AddrInfoDeleter {
        void operator()(addrinfo* ptr) const { if (ptr) { freeaddrinfo(ptr); } }
    };

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::unique_ptr<addrinfo, AddrInfoDeleter> info;
    std::string port_string = std::to_string(port_);

    addrinfo* raw_info = nullptr;
    // DNS/地址解析
    int status = getaddrinfo(host_.c_str(), port_string.c_str(), &hints, &raw_info);
    if (status != 0) {
#ifdef _WIN32
        std::ostringstream oss;
        oss << "getaddrinfo failed with error " << status;
        throw TimerClientError(oss.str());
#else
        throw TimerClientError(std::string("getaddrinfo failed: ") + gai_strerror(status));
#endif
    }
    info.reset(raw_info);

    bool sent = false;
    std::string error_message;
    // 遍历所有解析到的地址，尝试连接。连接后分块发送数据，直到所有字节发送完毕。
    for (addrinfo* entry = info.get(); entry != nullptr; entry = entry->ai_next) {
        socket_handle socket = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (socket == kInvalidSocket) {
            error_message = last_error_message("Failed to create socket");
            continue;
        }

#ifdef _WIN32
        DWORD timeout_ms = static_cast<DWORD>(timeout_.count());
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = static_cast<long>(timeout_.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout_.count() % 1000) * 1000);
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        if (::connect(socket, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) != 0) {
            error_message = last_error_message("Failed to connect to sandtimer");
            close_socket(socket);
            continue;
        }

        const char* data = message.data();
        std::size_t remaining = message.size();
        while (remaining > 0) {
            int chunk = ::send(socket, data, static_cast<int>(remaining), 0);
#ifdef _WIN32
            if (chunk == SOCKET_ERROR) {
                error_message = last_error_message("Failed to send payload");
                break;
            }
#else
            if (chunk < 0) {
                error_message = last_error_message("Failed to send payload");
                break;
            }
#endif
            data += chunk;
            remaining -= static_cast<std::size_t>(chunk);
        }

        close_socket(socket);

        if (remaining == 0) {
            sent = true;
            break;
        }
    }

    if (!sent) {
        if (error_message.empty()) {
            error_message = "Unable to deliver payload to sandtimer";
        }
        throw TimerClientError(error_message);
    }
}

}  // namespace mcp_sandtimer
