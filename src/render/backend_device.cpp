#include "skeet/render/backend.hpp"
#include "skeet/render/font_runtime.hpp"
#include <cstring>
#include <intrin.h>

namespace skeet::ui {

void RendererBackend::release_device_objects() { // 433C9479
    auto release_font = [&](auto& record) {
        if (record.texture) {
            if (record.link && record.link->cache && record.link->cache->runtime)
                record.link->cache->runtime->mark_dirty();
            record.texture->Release();
            record.texture = nullptr;
        }
    };
    if (fonts) {
        // The native context walks fixed font slots, irrespective of which
        // cache first became dirty and acquired an atlas texture.
        for (auto* set : {&fonts->default_fonts, &fonts->scaled_fonts})
            for (auto& link : *set)
                for (auto& record : font_textures_)
                    if (record.link == &link)
                        release_font(record);
    }
    for (auto& record : font_textures_)
        release_font(record);
    for (auto& record : textures_) {
        if (record.texture)
            record.texture->Release();
        record.texture = nullptr;
    }
    if (buffer_) {
        if (mapped_) {
            buffer_->Unlock();
            mapped_ = nullptr;
        }
        buffer_->Release();
        buffer_ = nullptr;
    }
    if (saved_texture_) {
        saved_texture_->Release();
        saved_texture_ = nullptr;
    }
}

// 4336AFEE: upload through a SYSTEMMEM texture, then UpdateTexture into DEFAULT.
// The original copies the supplied byte count in one memcpy, ignoring Pitch.
HRESULT RendererBackend::upload_texture(std::int32_t width, std::int32_t height,
                                        std::span<const std::byte> pixels,
                                        IDirect3DTexture9** output) {
    if (!output)
        return E_INVALIDARG;
    IDirect3DTexture9* staging = nullptr;
    auto result = device_->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                         &staging, nullptr);
    if (FAILED(result))
        return result;
    IDirect3DTexture9* target = nullptr;
    result = device_->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target,
                                    nullptr);
    if (FAILED(result)) {
        staging->Release();
        return result;
    }
    D3DLOCKED_RECT locked{};
    result = staging->LockRect(0, &locked, nullptr, 0);
    if (SUCCEEDED(result)) {
        if (!pixels.empty())
            std::memcpy(locked.pBits, pixels.data(), pixels.size());
        result = staging->UnlockRect(0);
        if (SUCCEEDED(result))
            result = device_->UpdateTexture(staging, target);
    }
    if (SUCCEEDED(result))
        *output = target;
    else
        target->Release();
    staging->Release();
    return result;
}
std::uint32_t RendererBackend::create_texture(std::uint32_t key, std::int32_t width,
                                              std::int32_t height,
                                              std::span<const std::byte> pixels) { // 4336B39C
    TextureRecord record;
    record.key = key;
    record.width = width;
    record.height = height;
    record.pixels.assign(pixels.begin(), pixels.end());
    record.timestamp = static_cast<std::uint32_t>(__rdtsc());
    textures_.push_back(std::move(record));
    return static_cast<std::uint32_t>(textures_.size() - 1);
}
void RendererBackend::update_texture(std::uint32_t index, std::int32_t width, std::int32_t height,
                                     std::span<const std::byte> pixels) { // 4335BC40
    if (index == UINT32_MAX || index >= textures_.size())
        return;
    auto& record = textures_[index];
    record.width = width;
    record.height = height;
    record.pixels.assign(pixels.begin(), pixels.end());
    record.timestamp = static_cast<std::uint32_t>(__rdtsc());
    record.dirty = true;
}
std::int32_t RendererBackend::find_texture(std::uint32_t key) const noexcept { // 4331E088
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i].key == key)
            return static_cast<std::int32_t>(i);
    return -1;
}
void RendererBackend::texture_size(std::uint32_t index, std::int32_t* width,
                                   std::int32_t* height) const noexcept { // 433D7503
    if (!width || !height)
        return;
    *width = 0;
    *height = 0;
    if (index != UINT32_MAX && index < textures_.size()) {
        *width = textures_[index].width;
        *height = textures_[index].height;
    }
}
void RendererBackend::upload_textures() { // 433C973D..433C97BA
    for (auto& record : textures_) {
        if (record.dirty) {
            record.dirty = false;
            if (record.texture)
                record.texture->Release();
            record.texture = nullptr;
        } else if (record.texture)
            continue;
        if (!record.pixels.empty()) {
            IDirect3DTexture9* next = nullptr;
            if (SUCCEEDED(upload_texture(record.width, record.height, record.pixels, &next)))
                record.texture = next;
        }
    }
}

bool RendererBackend::begin_frame() { // 433C95CE, D3D setup after cache uploads
    upload_fonts();
    upload_textures();
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (SUCCEEDED(device_->GetCreationParameters(&creation))) {
        RECT rectangle{};
        if (GetClientRect(creation.hFocusWindow, &rectangle)) {
            screen_width = rectangle.right - rectangle.left;
            screen_height = rectangle.bottom - rectangle.top;
        }
    }
    if (!screen_width || !screen_height)
        return false;
    rendering_.store(true, std::memory_order_release);
    D3DCAPS9 caps{};
    device_->GetDeviceCaps(&caps);
    if (caps.AlphaCmpCaps & 0x10) {
        device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27), 1);
        device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(24), 1);
        device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(15), 1);
        device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(25), 5);
    }
    constexpr DWORD actual_states[][2]{
        {7, 1},    {8, 3},   {9, 2},   {14, 1},  {19, 5},  {20, 6},  {22, 1},  {23, 4},  {28, 0},
        {29, 0},   {48, 0},  {52, 0},  {128, 0}, {136, 1}, {137, 0}, {145, 1}, {151, 0}, {161, 1},
        {168, 15}, {171, 1}, {176, 1}, {174, 1}, {184, 0}, {206, 1}, {207, 2}, {208, 6}, {209, 5}};
    for (const auto& state : actual_states)
        device_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state[0]), state[1]);
    constexpr DWORD texture_states[][3]{{0, 4, 4}, {0, 5, 2},  {0, 6, 0},  {0, 1, 4}, {0, 2, 2},
                                        {0, 3, 0}, {0, 11, 0}, {0, 24, 0}, {1, 1, 1}, {1, 4, 1}};
    for (const auto& state : texture_states)
        device_->SetTextureStageState(state[0], static_cast<D3DTEXTURESTAGESTATETYPE>(state[1]),
                                      state[2]);
    const DWORD sampler_states[][3]{
        {0, 6, 2}, {0, 5, 2}, {0, 9, 0}, {0, 8, 0}, {0, 10, caps.MaxAnisotropy},
        {0, 1, 1}, {0, 2, 1}, {1, 1, 1}, {1, 2, 1}};
    for (const auto& state : sampler_states)
        device_->SetSamplerState(state[0], static_cast<D3DSAMPLERSTATETYPE>(state[1]), state[2]);
    if (SUCCEEDED(device_->GetFVF(&saved_fvf_)) && saved_fvf_ != 0x142)
        device_->SetFVF(0x142);
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    device_->SetTransform(D3DTS_WORLD, &identity);
    device_->SetTransform(D3DTS_VIEW, &identity);
    D3DVIEWPORT9 viewport{};
    device_->GetViewport(&viewport);
    const auto left = float(viewport.X) + 0.5f, right = float(viewport.X + viewport.Width) + 0.5f;
    const auto top = float(viewport.Y) + 0.5f, bottom = float(viewport.Y + viewport.Height) + 0.5f;
    const auto x = 1.0f / (right - left), y = 1.0f / (top - bottom);
    D3DMATRIX projection{};
    projection._11 = x + x;
    projection._22 = y + y;
    projection._33 = 1.0f;
    projection._41 = -(left + right) * x;
    projection._42 = -(bottom + top) * y;
    projection._43 = -0.0f;
    projection._44 = 1.0f;
    device_->SetTransform(D3DTS_PROJECTION, &projection);
    set_depth(0.5f);
    if (FAILED(device_->GetTexture(0, &saved_texture_)))
        saved_texture_ = nullptr;
    device_->SetTexture(0, nullptr);
    texture_ = nullptr;
    return true;
}
void RendererBackend::end_frame() { // 4337EF22
    flush(true);
    if (saved_fvf_ != 0x142)
        device_->SetFVF(saved_fvf_);
    device_->SetTexture(0, saved_texture_);
    texture_ = saved_texture_;
    if (saved_texture_) {
        saved_texture_->Release();
        saved_texture_ = nullptr;
    }
    rendering_.store(false, std::memory_order_release);
}

} // namespace skeet::ui
