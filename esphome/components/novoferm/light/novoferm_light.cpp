#include "esphome/core/log.h"
#include "novoferm_light.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace novoferm {

static const char *const TAG = "novoferm.light";

void NovofermLight::setup() {
  this->parent_->set_light_state_listener([this](const LightStatus &status) {
    if (this->current_status_ == status) {
      return;
    }

    if (this->state_->current_values != this->state_->remote_values) {
      ESP_LOGD(TAG, "Light is transitioning, change ignored");
      return;
    }

    ESP_LOGI(TAG, "Status changed from %s to %s", light_status_to_str(this->current_status_),
             light_status_to_str(status));
    this->current_status_ = status;

    auto call = this->state_->make_call();
    call.set_state(status == LightStatus::ON);
    call.perform();
  });
}

void NovofermLight::dump_config() { ESP_LOGCONFIG(TAG, "Novoferm Light"); }

light::LightTraits NovofermLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  return traits;
}

void NovofermLight::setup_state(light::LightState *state) { state_ = state; }

void NovofermLight::write_state(light::LightState *state) {
  if (!state->current_values.is_on()) {
    this->parent_->perform_light_action(LightStatus::OFF);
    return;
  }

  this->parent_->perform_light_action(LightStatus::ON);
}

}  // namespace novoferm
}  // namespace esphome
