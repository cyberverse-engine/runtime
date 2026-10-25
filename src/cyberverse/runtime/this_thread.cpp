/**
 * @file this_thread.cpp
 * @author simon2077s (simon2077s@gmail.com)
 * @brief This thread implementation
 * @version 0.1
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "cyberverse/runtime/this_thread.hpp"

namespace cyberverse::runtime {
namespace {

thread_local bool render_thread = false;

} // namespace

bool is_this_render_thread() noexcept { return render_thread; }

void set_this_as_render_thread() noexcept { render_thread = true; }

} // namespace cyberverse::runtime
