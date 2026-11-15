#pragma once

#include "realscript/bindgen/BindingTypes.h"
#include "realscript/game/DeterministicMath.h"

namespace realscript::game {

namespace detail = ::realscript::bindgen::detail;

template <typename T>
using TypedAwaitable = ::realscript::bindgen::TypedAwaitable<T>;

} // namespace realscript::game
