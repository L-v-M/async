#ifndef CPPCORO_COROUTINE_HPP_INCLUDED
#define CPPCORO_COROUTINE_HPP_INCLUDED

#include <coroutine>

namespace cppcoro
{
	using std::coroutine_handle;
	using std::noop_coroutine;
	using std::suspend_always;
	using std::suspend_never;
}  // namespace cppcoro

#endif
