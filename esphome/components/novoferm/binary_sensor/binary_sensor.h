#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/novoferm/novoferm.h"

namespace esphome {
namespace novoferm {

static const char *const TAG = "novofermventilationsensor";

class NovofermBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:

  void setup() override {
    this->parent_->set_ventilation_state_listener([this](const bool &onOff) {
        ESP_LOGV(TAG, "Novoferm ventilation state is: %s", ONOFF(onOff));
        this->publish_state(onOff);
    });
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Novoferm Ventilation Binary Sensor");
  };

  void set_novoferm_parent(Novoferm *parent) { this->parent_ = parent; }

 protected:
  Novoferm *parent_{nullptr};
};

}  // namespace novoferm
}  // namespace esphome