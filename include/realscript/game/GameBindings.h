#pragma once

#include "realscript/bindgen/BindGen.h"
#include "realscript/game/BindingMarshal.h"

#include <memory>
#include <utility>

namespace realscript::game {

using GameApi = ::realscript::bindgen::HostApi;

template <typename T>
using NativeTypeBuilder = ::realscript::bindgen::NativeTypeBuilder<T>;

template <typename S>
using NativeStructBuilder = ::realscript::bindgen::NativeStructBuilder<S>;

[[nodiscard]] inline std::shared_ptr<runtime::ManagedHeap> createIsolatedHeap(
    runtime::HeapConfig config = {}) {
    if (!config.valuePoolDomain) {
        config.valuePoolDomain = runtime::ValuePoolDomain::create();
    }
    return std::make_shared<runtime::ManagedHeap>(config);
}

[[nodiscard]] inline GameApi createIsolatedGameApi(
    runtime::HeapConfig config = {},
    std::shared_ptr<runtime::NativeHandleRegistry> nativeHandles = nullptr) {
    return GameApi(createIsolatedHeap(config), std::move(nativeHandles));
}

} // namespace realscript::game
