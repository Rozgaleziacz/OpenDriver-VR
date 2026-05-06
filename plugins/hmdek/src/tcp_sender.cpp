#include "android_hmd_plugin.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  define sock_errno WSAGetLastError()
#else
#  define sock_errno errno
#endif

namespace android_hmd {

namespace {

constexpr uint32_t kVideoMessageConfig = 1;
constexpr uint32_t kVideoMessageFrame = 2;
constexpr uint32_t kVideoCodecH264 = 1;
constexpr uint32_t kVideoFrameFlagKeyframe = 1u << 0;

void AppendU32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendI32BE(std::vector<uint8_t>& out, int32_t value) {
    AppendU32BE(out, static_cast<uint32_t>(value));
}

std::string SockErrorString() {
#ifdef _WIN32
    return std::to_string(WSAGetLastError());
#else
    return std::to_string(errno);
#endif
}

} // namespace

bool TcpVideoSender::Start(uint16_t port, uint32_t width, uint32_t height,
                           float refresh_rate) {
    m_port = port;
    m_width = width;
    m_height = height;
    m_refresh_rate = refresh_rate;
    m_running = true;
    m_send_thread = std::thread(&TcpVideoSender::SendLoop, this);
    return true;
}

void TcpVideoSender::Stop() {
    m_running = false;
    m_queue_cv.notify_all();

    {
        std::lock_guard<std::mutex> lock(m_conn_mutex);
        DisconnectLocked();
    }

    if (m_send_thread.joinable()) {
        m_send_thread.join();
    }
}

void TcpVideoSender::UpdateRemoteHost(const std::string& host) {
    if (host.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_conn_mutex);
    if (m_remote_host == host) {
        return;
    }

    m_remote_host = host;
    if (m_logger) {
        m_logger("[android_hmd] Video target host set to " + host);
    }
    DisconnectLocked();
}

void TcpVideoSender::EnqueueFrame(std::vector<uint8_t> nal_data,
                                  uint64_t frame_num, uint64_t pts,
                                  bool is_keyframe) {
    std::lock_guard<std::mutex> lock(m_queue_mutex);

    if (m_queue.size() >= MAX_QUEUE) {
        auto it = std::find_if(
            m_queue.begin(), m_queue.end(),
            [](const PendingFrame& f) { return !f.is_keyframe; });
        if (it != m_queue.end()) {
            m_queue.erase(it);
        } else {
            m_queue.pop_front();
        }
    }

    m_queue.push_back({std::move(nal_data), frame_num, pts, is_keyframe});
    m_queue_cv.notify_one();
}

void TcpVideoSender::UpdateSequenceHeader(const std::vector<uint8_t>& header) {
    if (header.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_header_mutex);
    m_sequence_header = header;
    m_sent_config = false;
}

void TcpVideoSender::SendLoop() {
    auto last_fps_time = std::chrono::steady_clock::now();
    uint64_t fps_counter = 0;

    while (m_running) {
        PendingFrame frame;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_queue_cv.wait_for(lock, std::chrono::milliseconds(250), [this] {
                return !m_queue.empty() || !m_running;
            });
            if (!m_running) {
                break;
            }
            if (m_queue.empty()) {
                continue;
            }

            while (m_queue.size() > 1) {
                auto next = std::next(m_queue.begin());
                if (next == m_queue.end()) {
                    break;
                }
                if (m_queue.front().is_keyframe && !next->is_keyframe) {
                    break;
                }
                m_queue.pop_front();
            }

            frame = std::move(m_queue.front());
            m_queue.pop_front();
        }

        bool sent = false;
        {
            std::lock_guard<std::mutex> lock(m_conn_mutex);
            if (EnsureConnectedLocked()) {
                if (!m_sent_config && !SendConfigLocked()) {
                    DisconnectLocked();
                } else {
                    sent = SendFrameLocked(frame);
                    if (!sent) {
                        DisconnectLocked();
                    }
                }
            }
        }

        if (sent) {
            fps_counter++;
        }

        auto now = std::chrono::steady_clock::now();
        const float elapsed =
            std::chrono::duration<float>(now - last_fps_time).count();
        if (elapsed >= 1.0f) {
            m_sent_fps = fps_counter / elapsed;
            fps_counter = 0;
            last_fps_time = now;
        }
    }
}

bool TcpVideoSender::EnsureConnectedLocked() {
    if (m_sock != INVALID_SOCK) {
        return true;
    }
    if (m_remote_host.empty()) {
        m_client_count = 0;
        return false;
    }

    socket_t sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) {
        m_client_count = 0;
        return false;
    }

    int flag = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&flag), sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    if (::inet_pton(AF_INET, m_remote_host.c_str(), &addr.sin_addr) != 1) {
        if (m_logger) {
            m_logger("[android_hmd] Video TCP invalid remote host: " + m_remote_host);
        }
        sock_close(sock);
        m_client_count = 0;
        return false;
    }

    if (m_logger) {
        m_logger("[android_hmd] Video TCP connecting to " + m_remote_host + ":" + std::to_string(m_port));
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
        SOCK_ERR) {
        if (m_logger) {
            m_logger("[android_hmd] Video TCP connect failed to " + m_remote_host + ":" +
                     std::to_string(m_port) + " (socket error " + SockErrorString() + ")");
        }
        sock_close(sock);
        m_client_count = 0;
        return false;
    }

    m_sock = sock;
    m_client_count = 1;
    m_sent_config = false;
    if (m_logger) {
        m_logger("[android_hmd] Video TCP connected to " + m_remote_host + ":" + std::to_string(m_port));
    }
    return true;
}

void TcpVideoSender::DisconnectLocked() {
    if (m_sock != INVALID_SOCK) {
        if (m_logger) {
            m_logger("[android_hmd] Video TCP disconnected");
        }
        sock_close(m_sock);
        m_sock = INVALID_SOCK;
    }
    m_client_count = 0;
    m_sent_config = false;
}

bool TcpVideoSender::SendConfigLocked() {
    std::vector<uint8_t> payload;
    payload.reserve(20 + m_sequence_header.size());

    const int32_t width = static_cast<int32_t>(m_width);
    const int32_t height = static_cast<int32_t>(m_height);
    const int32_t refresh_rate = static_cast<int32_t>(m_refresh_rate);
    const int32_t codec = static_cast<int32_t>(kVideoCodecH264);

    std::vector<uint8_t> sequence_header;
    {
        std::lock_guard<std::mutex> lock(m_header_mutex);
        sequence_header = m_sequence_header;
    }
    const int32_t config_size = static_cast<int32_t>(sequence_header.size());

    AppendI32BE(payload, width);
    AppendI32BE(payload, height);
    AppendI32BE(payload, refresh_rate);
    AppendI32BE(payload, codec);
    AppendI32BE(payload, config_size);
    payload.insert(payload.end(), sequence_header.begin(), sequence_header.end());

    if (!SendMessageLocked(kVideoMessageConfig, payload.data(), payload.size())) {
        if (m_logger) {
            m_logger("[android_hmd] Video TCP failed while sending config");
        }
        return false;
    }

    m_sent_config = true;
    if (m_logger) {
        m_logger("[android_hmd] Video TCP config sent: " + std::to_string(m_width) + "x" +
                 std::to_string(m_height) + " @" + std::to_string(static_cast<int>(m_refresh_rate)) +
                 "Hz, codec=H264, header=" + std::to_string(config_size) + " bytes");
    }
    return true;
}

bool TcpVideoSender::SendFrameLocked(const PendingFrame& frame) {
    std::vector<uint8_t> payload;
    payload.reserve(sizeof(uint32_t) + frame.nal.size());

    const uint32_t flags = frame.is_keyframe ? kVideoFrameFlagKeyframe : 0;
    AppendU32BE(payload, flags);
    payload.insert(payload.end(), frame.nal.begin(), frame.nal.end());

    if (!SendMessageLocked(kVideoMessageFrame, payload.data(), payload.size())) {
        if (m_logger) {
            m_logger("[android_hmd] Video TCP failed while sending frame payload");
        }
        return false;
    }

    m_sent_bytes += sizeof(VideoMessageHeader) + payload.size();
    return true;
}

bool TcpVideoSender::SendMessageLocked(uint32_t type, const void* payload,
                                       size_t len) {
    if (m_sock == INVALID_SOCK) {
        return false;
    }

    std::vector<uint8_t> header;
    header.reserve(sizeof(VideoMessageHeader));
    AppendU32BE(header, 0x4F445631u);
    AppendU32BE(header, type);
    AppendU32BE(header, static_cast<uint32_t>(len));
    AppendU32BE(header, 0u);

    if (!SendAll(m_sock, header.data(), header.size())) {
        return false;
    }
    if (len > 0 && !SendAll(m_sock, payload, len)) {
        return false;
    }

    return true;
}

bool TcpVideoSender::SendAll(socket_t sock, const void* data, size_t len) {
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = len;

    while (remaining > 0) {
        const int sent = ::send(sock, ptr, static_cast<int>(remaining), 0);
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

} // namespace android_hmd
