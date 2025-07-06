#pragma once

#include "esphome/core/component.h"
#include "esphome/components/novoferm/novoferm.h"
#include "esphome/components/light/light_output.h"

namespace esphome {
namespace novoferm {

class NovofermLight : public Component, public light::LightOutput {
 public:
  void setup() override;
  void dump_config() override;

  void set_novoferm_parent(Novoferm *parent) { this->parent_ = parent; }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;
  LightStatus current_status_{OFF};

 protected:
  Novoferm *parent_;
  light::LightState *state_{nullptr};
};

}  // namespace novoferm
}  // namespace esphome
