/**
 * @file frame_stage.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief Frame stage implementation
 * @version 0.1
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/frame_stage.hpp"

namespace cyberverse::runtime {

frame_stage::frame_stage(bool use_fixed_step) noexcept : use_fixed_step_(use_fixed_step) {}

bool frame_stage::use_fixed_step() const noexcept { return use_fixed_step_; }

void frame_stage::on_frame_begin(double) noexcept {}

void frame_stage::on_render_frame_begin() noexcept {}

void frame_stage::on_render_frame_end() noexcept {}

} // namespace cyberverse::runtime
