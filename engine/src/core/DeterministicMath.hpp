#pragma once

namespace engine::core::det {

// Trig that gives bit-identical results on every platform and compiler
// (Jolt's own implementations), for code inside rollback simulations.
// The C library's sin/cos/atan2 can differ in the last bit between systems.
[[nodiscard]] float sin(float radians);
[[nodiscard]] float cos(float radians);
[[nodiscard]] float atan2(float y, float x);

} // namespace engine::core::det
