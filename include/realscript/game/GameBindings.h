#pragma once

#include "realscript/bindgen/BindGen.h"
#include "realscript/game/BindingMarshal.h"

namespace realscript::game {

using GameApi = ::realscript::bindgen::HostApi;

template <typename T>
using NativeTypeBuilder = ::realscript::bindgen::NativeTypeBuilder<T>;

template <typename S>
using NativeStructBuilder = ::realscript::bindgen::NativeStructBuilder<S>;

} // namespace realscript::game
