#pragma once

#include <cinttypes>
#include <vector>
#include <deque>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/application.h"
namespace esphome {
namespace novoferm {

// PROTOCOL_DEFINITIONS_START

enum MessageType : uint16_t {
  STATUS = 0x0104,
  COMMAND = 0x0106,
};

inline const char *message_type_to_str(MessageType t) {
  switch (t) {
    case STATUS:
      return "Status";
    case COMMAND:
      return "Command";
    default:
      return "Unknown";
  }
}

// Serialize the given object to a new byte vector.
template<typename T> std::vector<uint8_t> serialize(T obj) {
  std::vector<uint8_t> out(sizeof(T));
  memcpy(out.data(), &obj, sizeof(T));
  return out;
}

// StatusType denotes which 'page' of information needs to be retrieved.
// Novoferm 423, only supports GATE status
// Novoport IV, supports GATE and LIGHT with UNKNOWN being 0x0C
enum StatusType : uint16_t {
  GATE = 0x0A,
  LIGHT = 0x0B,
  UNKNOWN = 0x0C,
};

enum GateStatus : uint8_t {
  PAUSED,
  CLOSED,
  VENTILATING,
  OPENED,
  OPENING,
  CLOSING,
};

inline const char *gate_status_to_str(GateStatus s) {
  switch (s) {
    case PAUSED:
      return "Paused";
    case CLOSED:
      return "Closed";
    case VENTILATING:
      return "Ventilating";
    case OPENED:
      return "Opened";
    case OPENING:
      return "Opening";
    case CLOSING:
      return "Closing";
    default:
      return "Unknown";
  }
}

enum GateAction : uint8_t {
  PAUSE,
  CLOSE,
  VENTILATE,
  OPEN,
};

enum LightStatus : uint8_t { OFF, ON };

inline const char *light_status_to_str(LightStatus s) {
  switch (s) {
    case OFF:
      return "OFF";
    case ON:
      return "ON";
    default:
      return "Unknown";
  }
}

// MessageHeader appears at the start of every message, both requests and replies.
struct MessageHeader {
  uint16_t seq;
  uint32_t len;
  MessageType type;

  MessageHeader() = default;

  MessageHeader(MessageType type, uint16_t seq, uint32_t payload_size) {
    this->type = convert_big_endian(type);
    this->seq = convert_big_endian(seq);
    // len includes the length of the type field
    this->len = convert_big_endian(payload_size + sizeof(this->type));
  }

  std::string print() const {
    return str_sprintf("MessageHeader: seq %d, len %d, type %s", this->seq, this->len,
                       message_type_to_str(static_cast<MessageType>(this->type)));
  }

  // payload_size returns the amount of payload bytes to be read from the uart
  // buffer after reading the header.
  uint32_t payload_size() const { return this->len - sizeof(this->type); }
} __attribute__((packed));

template<typename StatusEnum> struct CommandRequestReplyTemplate {
  StatusType type;
  uint8_t pad = 0x0;
  StatusEnum state;

  CommandRequestReplyTemplate() = default;
  CommandRequestReplyTemplate(StatusEnum state) : state(state) {}

  void byteswap() { this->type = convert_big_endian(this->type); }

  std::string print() {
    if constexpr (std::is_same_v<StatusEnum, GateStatus>) {
      return str_sprintf("CommandRequestReply: state %s", gate_status_to_str(this->state));
    } else if constexpr (std::is_same_v<StatusEnum, LightStatus>) {
      return str_sprintf("CommandRequestReply: state %s", this->state == LightStatus::ON ? "ON" : "OFF");
    } else {
      return "CommandRequestReply: unknown state";
    }
  }
} __attribute__((packed));

struct StatusReply {
  uint8_t ack = 0x2;
  // Only use when gate was requested, else garbage data
  GateStatus gateState;
  // Only use when light was requested, else garbage data
  LightStatus lightState;

  std::string print(StatusType expected_type) const {
    switch (expected_type) {
      case StatusType::GATE:
        return str_sprintf("StatusReply: gate state %s", gate_status_to_str(this->gateState));
      case StatusType::LIGHT:
        return str_sprintf("StatusReply: light state %s", this->lightState == LightStatus::ON ? "ON" : "OFF");
      default:
        return "StatusReply: unknown status type";
    }
  }

} __attribute__((packed));

// PROTOCOL_DEFINITIONS_END

struct NovofermStatusListener {
  StatusType type;
  std::function<void()> on_data;
};

// Expected response storing type and sequence number
struct NovofermExpectedResponse {
  uint16_t seq;
  MessageType type;

  // Status Type is optional but can be used to check if the proper reply was provided
  optional<StatusType> statusType;
  uint32_t timestamp;

  NovofermExpectedResponse() {
    statusType = nullopt;
    timestamp = App.get_loop_component_start_time();
  }

  NovofermExpectedResponse(uint16_t seq, MessageType type, optional<StatusType> statusType = nullopt)
      : seq(seq), type(type), timestamp(App.get_loop_component_start_time()), statusType(statusType) {}
};

// Just for easier use
enum NovofermCommandType {
  GATE_CMD,
  LIGHT_CMD,
  GATE_STATUS_REQUEST,
  LIGHT_STATUS_REQUEST,
};

struct NovofermCommand {
  NovofermCommandType type;
  std::vector<uint8_t> payload;
};

class Novoferm : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_cover_state_listener(const std::function<void(GateStatus)> &func) { cover_state_callback_ = func; }

  void set_light_state_listener(const std::function<void(LightStatus)> &func) { light_state_callback_ = func; }

  void add_on_initialized_callback(std::function<void()> callback) {
    this->initialized_callback_.add(std::move(callback));
  }

  void perform_gate_action(GateAction action);
  void perform_light_action(LightStatus action);

 protected:
  void handle_char_(uint8_t c);
  bool validate_message_();

  void handle_message_(uint16_t seq, uint16_t type, const uint8_t *buffer, uint32_t len);
  void process_command_queue_();

  void send_raw_command_(NovofermCommand command);
  void send_command_(const NovofermCommand &command);

  void request_gate_status();
  void request_light_status();
  void request_status(NovofermCommandType type);
  void request_full_status();

  uint16_t next_sequence() {
    // avoid 0 as sequence number
    if (++seq_tx_ == 0)
      seq_tx_ = 1;
    return seq_tx_;
  }
  uint16_t seq_tx_{0};

  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  std::vector<uint8_t> rx_message_;
  std::vector<NovofermCommand> command_queue_;
  std::deque<NovofermExpectedResponse> expected_responses_;

  CallbackManager<void()> initialized_callback_{};
  std::function<void(GateStatus)> cover_state_callback_ = nullptr;
  std::function<void(LightStatus)> light_state_callback_ = nullptr;
};

}  // namespace novoferm
}  // namespace esphome
