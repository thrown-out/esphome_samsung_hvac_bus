#pragma once
#include <cstdint>
#include <cstddef>

namespace esphome {
namespace samsung_ac {

/**
 * TcpMirror – spiegelt empfangene UART-Bytes roh an einen TCP-Client.
 *
 * - max. 1 gleichzeitiger Client
 * - vollständig non-blocking, blockiert nie den Hauptloop
 * - kein Client: write_bytes() ist ein No-op
 * - port=0: komplett deaktiviert
 */
class TcpMirror {
 public:
  /// Einmalig in setup() aufrufen. port=0 deaktiviert den Mirror.
  void begin(uint16_t port);

  /// In loop() aufrufen – nimmt ankommende TCP-Verbindungen entgegen.
  void loop();

  /// Bytes an den verbundenen Client senden. Kein Client → verwirft die Daten.
  void write_bytes(const uint8_t *data, size_t len);

 private:
  int server_fd_{-1};
  int client_fd_{-1};

  void try_accept_();
  void check_client_();   // erkennt saubere Client-Disconnects
  void close_client_();
};

}  // namespace samsung_ac
}  // namespace esphome
