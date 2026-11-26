#include "skeet/network/user_message_runtime.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/esp/user_message_handlers.hpp"
#include "skeet/rage/feedback_owner.hpp"
#include "skeet/script/say_text_message.hpp"
#include <stdexcept>

namespace skeet::network {
namespace {
UserMessageRuntime* active = nullptr;
}
struct UserMessageRuntime::Data {
    client::ClientRuntimeStorage& storage;
    esp::UserMessageHandlers esp;
    rage::FeedbackPositionRuntime feedback;
    script::SayTextMessageRuntime chat;
    bool bound = false;
    Data(client::ClientRuntimeStorage& s, const players::State& players, script::Bridge& bridge)
        : storage(s),
          esp(s.data_owner(), {s.data().native_cell<esp::EspOwnerRuntime*>(0x434758DCu),
                               s.data().native_cell<std::uint8_t>(0x4346754Cu), players}),
          feedback{s.owner_registries().at(0x43475930u)},
          chat{bridge, script::SayTextMessageNames(s.event_names())} {}
};
UserMessageRuntime::UserMessageRuntime(client::ClientRuntimeStorage& storage,
                                       const players::State& players, script::Bridge& bridge)
    : data_(std::make_unique<Data>(storage, players, bridge)) {}
UserMessageRuntime::~UserMessageRuntime() {
    if (!data_->bound)
        return;
    esp::unbind_user_message_handlers(data_->esp);
    rage::unbind_feedback_position_runtime(data_->feedback);
    script::unbind_say_text_message_runtime(data_->chat);
    if (active == this)
        active = nullptr;
}
void UserMessageRuntime::bind() {
    if (active || data_->bound)
        throw std::logic_error("SDK user-message contexts are already bound");
    esp::bind_user_message_handlers(data_->esp);
    rage::bind_feedback_position_runtime(data_->feedback);
    script::bind_say_text_message_runtime(data_->chat);
    data_->bound = true;
    active = this;
}
bool UserMessageRuntime::bound_to(const engine::NativeDataImage& image) const noexcept {
    return data_->bound && &data_->storage.data() == &image;
}
bool user_message_contexts_ready(const engine::NativeDataImage& image) noexcept {
    return active && active->bound_to(image);
}
}
