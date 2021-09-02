///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#include <cppcoro/detail/lightweight_manual_reset_event.hpp>

#include <system_error>

#include <cassert>
#include <cerrno>
#include <climits>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

namespace
{
	namespace local
	{
		// No futex() function provided by libc.
		// Wrap the syscall ourselves here.
		int futex(
			int* UserAddress,
			int FutexOperation,
			int Value,
			const struct timespec* timeout,
			int* UserAddress2,
			int Value3)
		{
			return syscall(
				SYS_futex, UserAddress, FutexOperation, Value, timeout, UserAddress2, Value3);
		}
	}  // namespace local
}  // namespace

cppcoro::detail::lightweight_manual_reset_event::lightweight_manual_reset_event(bool initiallySet)
	: m_value(initiallySet ? 1 : 0)
{
}

cppcoro::detail::lightweight_manual_reset_event::~lightweight_manual_reset_event()
{
}

void cppcoro::detail::lightweight_manual_reset_event::set() noexcept
{
	m_value.store(1, std::memory_order_release);

	constexpr int numberOfWaitersToWakeUp = INT_MAX;

	[[maybe_unused]] int numberOfWaitersWokenUp = local::futex(
		reinterpret_cast<int*>(&m_value),
		FUTEX_WAKE_PRIVATE,
		numberOfWaitersToWakeUp,
		nullptr,
		nullptr,
		0);

	// There are no errors expected here unless this class (or the caller)
	// has done something wrong.
	assert(numberOfWaitersWokenUp != -1);
}

void cppcoro::detail::lightweight_manual_reset_event::reset() noexcept
{
	m_value.store(0, std::memory_order_relaxed);
}

void cppcoro::detail::lightweight_manual_reset_event::wait() noexcept
{
	// Wait in a loop as futex() can have spurious wake-ups.
	int oldValue = m_value.load(std::memory_order_acquire);
	while (oldValue == 0)
	{
		int result = local::futex(
			reinterpret_cast<int*>(&m_value), FUTEX_WAIT_PRIVATE, oldValue, nullptr, nullptr, 0);
		if (result == -1)
		{
			if (errno == EAGAIN)
			{
				// The state was changed from zero before we could wait.
				// Must have been changed to 1.
				return;
			}

			// Other errors we'll treat as transient and just read the
			// value and go around the loop again.
		}

		oldValue = m_value.load(std::memory_order_acquire);
	}
}
