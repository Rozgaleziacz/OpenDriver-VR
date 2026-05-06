#include "android_hmd_plugin.h"

// Minimal JSON parser dependency — zakładamy nlohmann/json dostępne przez opendriver
#include <nlohmann/json.hpp>

#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  define sock_errno  WSAGetLastError()
#  define EWOULDBLOCK WSAEWOULDBLOCK
#  define EAGAIN      WSAEWOULDBLOCK
#else
#  define sock_errno  errno
#endif

namespace android_hmd {

// ============================================================================
// UdpTrackingReceiver
// ============================================================================

bool UdpTrackingReceiver::Start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif

    m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock == INVALID_SOCK) return false;

    // Non-blocking żeby ReceiveLoop nie blokowała wiecznie przy Stop()
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(m_sock, FIONBIO, &mode);
#else
    int flags = fcntl(m_sock, F_GETFL, 0);
    fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
#endif

    // Dołącz do portu
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(m_sock, (sockaddr*)&addr, sizeof(addr)) == SOCK_ERR) {
        sock_close(m_sock);
        m_sock = INVALID_SOCK;
        return false;
    }

    m_running = true;
    m_thread  = std::thread(&UdpTrackingReceiver::ReceiveLoop, this);
    return true;
}

void UdpTrackingReceiver::Stop() {
    m_running = false;
    if (m_sock != INVALID_SOCK) {
        sock_close(m_sock);
        m_sock = INVALID_SOCK;
    }
    if (m_thread.joinable()) m_thread.join();
}

bool UdpTrackingReceiver::GetLatestPose(TrackingPose& out) {
    std::lock_guard<std::mutex> lock(m_pose_mutex);
    if (!m_has_new_pose) return false;
    out              = m_latest_pose;
    m_has_new_pose   = false;
    return true;
}

std::string UdpTrackingReceiver::GetLastSenderIp() const {
    std::lock_guard<std::mutex> lock(m_pose_mutex);
    return m_last_sender_ip;
}

void UdpTrackingReceiver::ReceiveLoop() {
    // Bufor na pakiet UDP (JSON ~200 bajtów)
    static constexpr int BUF_SIZE = 1024;
    char buf[BUF_SIZE];

    auto last_hz_time    = std::chrono::steady_clock::now();
    uint64_t hz_counter  = 0;

    while (m_running) {
        sockaddr_in sender{};
        socklen_t   sender_len = sizeof(sender);

        int received = ::recvfrom(m_sock, buf, BUF_SIZE - 1, 0,
                                  (sockaddr*)&sender, &sender_len);

        if (received <= 0) {
            // EAGAIN/EWOULDBLOCK = brak danych, poczekaj chwilę
            int err = sock_errno;
            if (err == EWOULDBLOCK || err == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }
            // Inny błąd — socket prawdopodobnie zamknięty
            break;
        }

        buf[received] = '\0';

        // Parsuj JSON
        try {
            auto j = nlohmann::json::parse(buf, buf + received);

            TrackingPose pose;
            pose.qw = j.value("qw", 1.0);
            pose.qx = j.value("qx", 0.0);
            pose.qy = j.value("qy", 0.0);
            pose.qz = j.value("qz", 0.0);
            pose.x  = j.value("x",  0.0);
            pose.y  = j.value("y",  1.6);
            pose.z  = j.value("z",  0.0);
            // Prędkości opcjonalne — jeśli Android je wyśle, to dobrze
            // Jeśli nie, PoseProcessor i tak wylicza je sam
            pose.linVelX = j.value("lvx", 0.0);
            pose.linVelY = j.value("lvy", 0.0);
            pose.linVelZ = j.value("lvz", 0.0);
            pose.angVelX = j.value("avx", 0.0);
            pose.angVelY = j.value("avy", 0.0);
            pose.angVelZ = j.value("avz", 0.0);

            {
                std::lock_guard<std::mutex> lock(m_pose_mutex);
                m_latest_pose  = pose;
                m_has_new_pose = true;
                m_last_sender_ip = inet_ntoa(sender.sin_addr);
            }

            m_packet_count++;
            hz_counter++;

        } catch (...) {
            m_dropped_count++;
        }

        // Aktualizacja Hz co sekundę
        auto now     = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - last_hz_time).count();
        if (elapsed >= 1.0f) {
            m_hz          = hz_counter / elapsed;
            hz_counter    = 0;
            last_hz_time  = now;
        }
    }
}

} // namespace android_hmd
