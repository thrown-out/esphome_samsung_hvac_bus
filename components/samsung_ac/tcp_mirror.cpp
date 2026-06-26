#include "tcp_mirror.h"

// lwIP POSIX-Socket-API (ESP32 Arduino + IDF)
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>

#include "esphome/core/log.h"

// MSG_DONTWAIT: in lwIP definiert; Fallback für Unit-Test-Builds
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

namespace esphome {
namespace samsung_ac {

static const char *const TAG = "samsung_ac.tcp_mirror";

void TcpMirror::begin(uint16_t port) {
  if (port == 0) {
    ESP_LOGD(TAG, "disabled (port=0)");
    return;
  }

  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: %d", errno);
    return;
  }

  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));  // Port nach Neustart sofort verfügbar
  fcntl(server_fd_, F_SETFL, O_NONBLOCK);

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(port);

  if (bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "bind() failed: %d", errno);
    close(server_fd_);
    server_fd_ = -1;
    return;
  }

  listen(server_fd_, 1);
  ESP_LOGI(TAG, "listening on port %u", port);
}

void TcpMirror::loop() {
  if (server_fd_ < 0) return;
  check_client_();  // saubere Disconnects erkennen, bevor try_accept_ einen neuen aufnimmt
  try_accept_();
}

void TcpMirror::try_accept_() {
  struct sockaddr_in client_addr{};
  socklen_t len = sizeof(client_addr);
  int fd = accept(server_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &len);
  if (fd < 0) return;  // EAGAIN – keine Verbindung wartend

  // vorherigen Client ersetzen
  if (client_fd_ >= 0) {
    close(client_fd_);
    ESP_LOGD(TAG, "previous client replaced");
  }
  client_fd_ = fd;
  fcntl(client_fd_, F_SETFL, O_NONBLOCK);

  // Nagle-Algorithmus deaktivieren: Bytes werden sofort gesendet, nicht gepuffert
  int nodelay = 1;
  setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

  ESP_LOGI(TAG, "client connected");
}

void TcpMirror::check_client_() {
  if (client_fd_ < 0) return;

  uint8_t buf;
  ssize_t n = recv(client_fd_, &buf, sizeof(buf), MSG_DONTWAIT);

  if (n == 0) {
    // sauberer TCP-FIN vom Client
    close_client_();
  } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    // echter Socketfehler (z.B. ECONNRESET)
    close_client_();
  }
  // n == 1: Client hat etwas gesendet (für den Sniffer unerwünscht) → ignorieren
  // n < 0 mit EAGAIN/EWOULDBLOCK: kein Daten, Client noch verbunden → ok
}

void TcpMirror::close_client_() {
  if (client_fd_ >= 0) {
    close(client_fd_);
    client_fd_ = -1;
    ESP_LOGI(TAG, "client disconnected");
  }
}

void TcpMirror::write_bytes(const uint8_t *data, size_t len) {
  if (client_fd_ < 0 || len == 0) return;

  ssize_t sent = send(client_fd_, data, len, MSG_DONTWAIT);

  if (sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // TX-Puffer voll – Bytes werden verworfen (für Sniffer akzeptabel)
      return;
    }
    // echter Fehler → Verbindung beenden
    close_client_();
    return;
  }

  // sent < len: partieller Send – restliche Bytes werden verworfen (für Sniffer akzeptabel)
}

}  // namespace samsung_ac
}  // namespace esphome
