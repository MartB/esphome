#include "novoferm.h"
#include "esphome/components/network/util.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

namespace esphome {
namespace novoferm {

static const char *const TAG = "novoferm";
static const int COMMAND_DELAY = 10;
static const int RECEIVE_TIMEOUT = 200;

void Novoferm::dump_config() {
  ESP_LOGCONFIG(TAG, "Novoferm:");
  this->check_uart_settings(9600, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

// Poll every second
void Novoferm::setup() {
  this->set_interval("status_polling", 1000, [this] { this->request_full_status(); });
}

void Novoferm::request_status(NovofermCommandType type) {}

void Novoferm::request_gate_status() {
  NovofermCommand cmd;
  cmd.type = NovofermCommandType::GATE_STATUS_REQUEST;
  cmd.payload = {0x00, 0x0A, 0x00, 0x01};  // GATE + Trailer
  this->send_command_(cmd);
}

void Novoferm::request_light_status() {
  NovofermCommand cmd;
  cmd.type = NovofermCommandType::LIGHT_STATUS_REQUEST;
  cmd.payload = {0x00, 0x0B, 0x00, 0x01};  // LIGHT + Trailer
  this->send_command_(cmd);
}

// Request gate and light status
void Novoferm::request_full_status() {
  request_gate_status();
  request_light_status();
}

void Novoferm::perform_gate_action(GateAction action) {
  NovofermCommand cmd;
  cmd.type = NovofermCommandType::GATE_CMD;
  cmd.payload = {0x00, 0x0A, 0x00, action};  // pad + action
  this->send_command_(cmd);
}

void Novoferm::perform_light_action(LightStatus action) {
  NovofermCommand cmd;
  cmd.type = NovofermCommandType::LIGHT_CMD;
  cmd.payload = {0x00, 0x0B, 0x00, action};  // pad + action
  this->send_command_(cmd);
}

void Novoferm::loop() {
  while (this->available()) {
    uint8_t c;
    this->read_byte(&c);
    this->handle_char_(c);
  }
  process_command_queue_();
}

void Novoferm::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!this->validate_message_()) {
    this->rx_message_.clear();
  } else {
    this->last_rx_char_timestamp_ = millis();
  }
}

bool Novoferm::validate_message_() {
  uint32_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  // Bytes 0-1: SEQ (any) 2–5: LEN (any), Bytes 6-7 Type (validate once 7th is received)
  if (at <= 6)
    return true;

  uint16_t seq = (uint16_t(data[0]) << 8) | uint16_t(data[1]);
  uint32_t len = (uint32_t(data[2]) << 24) | (uint32_t(data[3]) << 16) | (uint32_t(data[4]) << 8) | data[5];
  uint16_t type = (uint16_t(data[6]) << 8) | uint16_t(data[7]);

  // Only validate possible types once
  if (at == 7 && !(type == MessageType::STATUS || type == MessageType::COMMAND)) {
    ESP_LOGW(TAG, "Unexpected TYPE 0x%04X — discarding", type);
    return false;  // reset buffer
  }

  // Wait until all payload bytes have arrived
  uint32_t payloadSize = len - sizeof(type);
  if (at - 7 < payloadSize) {
    return true;
  }

  // For now just log the message
  const uint8_t *payload = data + 8;

  ESP_LOGV(TAG, "Received message: SEQ=%u TYPE=0x%04X PAYLOAD_SIZE=%u PAYLOAD=[%s]", seq, type, payloadSize,
           format_hex_pretty(payload, payloadSize).c_str());

  this->handle_message_(seq, type, payload, payloadSize);

  // Returning false here means: reset buffer after processing.
  return false;
}

void Novoferm::handle_message_(uint16_t seq, uint16_t type, const uint8_t *buffer, uint32_t len) {
  novoferm::MessageType message_type = (novoferm::MessageType) type;

  optional<StatusType> expectedStatusType;

  // Find matching expected response by seq and type
  auto it = std::find_if(expected_responses_.begin(), expected_responses_.end(),
                         [&](const NovofermExpectedResponse &er) { return er.seq == seq && er.type == message_type; });

  if (it != expected_responses_.end()) {
    expectedStatusType = it->statusType;
    expected_responses_.erase(it);
    if (!command_queue_.empty())
      command_queue_.erase(command_queue_.begin());
  }

  switch (message_type) {
    case novoferm::MessageType::COMMAND:
      ESP_LOGD(TAG, "Received command (echo) reply");
      break;

    case novoferm::MessageType::STATUS: {
      if (!expectedStatusType.has_value()) {
        ESP_LOGE(TAG, "Received STATUS message but expectedStatusType is missing!");
        break;
      }

      const StatusReply *reply = reinterpret_cast<const StatusReply *>(buffer);
      switch (expectedStatusType.value()) {
        case StatusType::GATE:
          ESP_LOGD(TAG, "Received gate status: %s", reply->print(StatusType::GATE).c_str());
          if (this->cover_state_callback_ != nullptr) {
            this->cover_state_callback_(reply->gateState);
          }
          break;

        case StatusType::LIGHT:
          ESP_LOGD(TAG, "Received light status: %s", reply->print(StatusType::LIGHT).c_str());
          if (this->light_state_callback_ != nullptr) {
            this->light_state_callback_(reply->lightState);
          }
          break;

        default:
          ESP_LOGE(TAG, "Received STATUS with unknown StatusType, are we alone?");
          break;
      }
      break;
    }

    default:
      ESP_LOGE(TAG, "Invalid command received");
  }
}

void Novoferm::send_raw_command_(NovofermCommand command) {
  this->last_command_timestamp_ = millis();

  uint16_t seq = this->next_sequence();  // increment sequence for each command sent
  uint16_t len = command.payload.size();
  novoferm::MessageType type = novoferm::MessageType::COMMAND;

  // Set message type and expected response based on easier to use command.type
  switch (command.type) {
    case NovofermCommandType::GATE_CMD:
    case NovofermCommandType::LIGHT_CMD:
      type = novoferm::MessageType::COMMAND;
      expected_responses_.push_back({seq, type, nullopt});
      break;
    case NovofermCommandType::GATE_STATUS_REQUEST:
      type = novoferm::MessageType::STATUS;
      expected_responses_.push_back({seq, type, StatusType::GATE});
      break;
    case NovofermCommandType::LIGHT_STATUS_REQUEST:
      type = novoferm::MessageType::STATUS;
      expected_responses_.push_back({seq, type, StatusType::LIGHT});
      break;
    default:
      break;
  }

  ESP_LOGV(TAG, "Sending Novoferm: SEQ=%u CMD=0x%02X LEN=%u DATA=[%s]", seq, type, len,
           format_hex_pretty(command.payload).c_str());

  // Serialize header
  MessageHeader hdr(type, seq, len);
  std::vector<uint8_t> message = serialize(hdr);

  // Append payload if present
  if (!command.payload.empty())
    message.insert(message.end(), command.payload.begin(), command.payload.end());

  // Write all at once
  this->write_array(message);
}

// Put a command into the queue
void Novoferm::send_command_(const NovofermCommand &command) {
  command_queue_.push_back(command);
  process_command_queue_();
}

void Novoferm::process_command_queue_() {
  uint32_t now = App.get_loop_component_start_time();

  // Clear RX buffer if no chars received recently
  if (now - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {
    this->rx_message_.clear();
  }

  // Remove timed out expected responses and corresponding commands
  while (!expected_responses_.empty()) {
    const auto &resp = expected_responses_.front();
    if (now - resp.timestamp > RECEIVE_TIMEOUT) {
      ESP_LOGW(TAG, "Timeout waiting for response SEQ=%u TYPE=0x%04X - dropping response and command", resp.seq,
               resp.type);
      expected_responses_.pop_front();

      if (!command_queue_.empty())
        command_queue_.erase(command_queue_.begin());
    } else {
      // The earliest expected response is still valid
      break;
    }
  }

  // Send next command only if:
  // - enough delay since last command sent
  // - command queue not empty
  // - no partial RX message in progress
  // - no expected responses pending
  uint32_t delay_since_last_command = now - this->last_command_timestamp_;
  if (delay_since_last_command > COMMAND_DELAY && !command_queue_.empty() && this->rx_message_.empty() &&
      expected_responses_.empty()) {
    const auto &cmd = command_queue_.front();

    ESP_LOGV(TAG, "Sending next command from queue");

    this->send_raw_command_(cmd);

    // If no expected response was registered, remove the command immediately
    if (expected_responses_.empty())
      command_queue_.erase(command_queue_.begin());
  }
}

}  // namespace novoferm
}  // namespace esphome
