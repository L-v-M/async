///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_DETAIL_WHEN_ALL_TASK_HPP_INCLUDED
#define CPPCORO_DETAIL_WHEN_ALL_TASK_HPP_INCLUDED

#include <cppcoro/awaitable_traits.hpp>

#include <cppcoro/detail/void_value.hpp>
#include <cppcoro/detail/when_all_counter.hpp>

#include <cppcoro/coroutine.hpp>
#include <cassert>
#include <exception>

namespace cppcoro
{
	namespace detail
	{
		template<typename TASK_CONTAINER>
		class when_all_ready_awaitable;

		template<typename RESULT>
		class when_all_task;

		template<typename RESULT>
		class when_all_task_promise final
		{
		public:
			using coroutine_handle_t = cppcoro::coroutine_handle<when_all_task_promise<RESULT>>;

			when_all_task_promise() noexcept {}

			auto get_return_object() noexcept { return coroutine_handle_t::from_promise(*this); }

			cppcoro::suspend_always initial_suspend() noexcept { return {}; }

			auto final_suspend() noexcept
			{
				class completion_notifier
				{
				public:
					bool await_ready() const noexcept { return false; }

					void await_suspend(coroutine_handle_t coro) const noexcept
					{
						coro.promise().m_counter->notify_awaitable_completed();
					}

					void await_resume() const noexcept {}
				};

				return completion_notifier{};
			}

			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			void return_void() noexcept
			{
				// We should have either suspended at co_yield point or
				// an exception was thrown before running off the end of
				// the coroutine.
				assert(false);
			}

			auto yield_value(RESULT&& result) noexcept
			{
				m_result = std::addressof(result);
				return final_suspend();
			}

			void start(when_all_counter& counter) noexcept
			{
				m_counter = &counter;
				coroutine_handle_t::from_promise(*this).resume();
			}

			RESULT& result() &
			{
				rethrow_if_exception();
				return *m_result;
			}

			RESULT&& result() &&
			{
				rethrow_if_exception();
				return std::forward<RESULT>(*m_result);
			}

		private:
			void rethrow_if_exception()
			{
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
			}

			when_all_counter* m_counter;
			std::exception_ptr m_exception;
			std::add_pointer_t<RESULT> m_result;
		};

		template<>
		class when_all_task_promise<void> final
		{
		public:
			using coroutine_handle_t = cppcoro::coroutine_handle<when_all_task_promise<void>>;

			when_all_task_promise() noexcept {}

			auto get_return_object() noexcept { return coroutine_handle_t::from_promise(*this); }

			cppcoro::suspend_always initial_suspend() noexcept { return {}; }

			auto final_suspend() noexcept
			{
				class completion_notifier
				{
				public:
					bool await_ready() const noexcept { return false; }

					void await_suspend(coroutine_handle_t coro) const noexcept
					{
						coro.promise().m_counter->notify_awaitable_completed();
					}

					void await_resume() const noexcept {}
				};

				return completion_notifier{};
			}

			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			void return_void() noexcept {}

			void start(when_all_counter& counter) noexcept
			{
				m_counter = &counter;
				coroutine_handle_t::from_promise(*this).resume();
			}

			void result()
			{
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
			}

		private:
			when_all_counter* m_counter;
			std::exception_ptr m_exception;
		};

		template<typename RESULT>
		class when_all_task final
		{
		public:
			using promise_type = when_all_task_promise<RESULT>;

			using coroutine_handle_t = typename promise_type::coroutine_handle_t;

			when_all_task(coroutine_handle_t coroutine) noexcept
				: m_coroutine(coroutine)
			{
			}

			when_all_task(when_all_task&& other) noexcept
				: m_coroutine(std::exchange(other.m_coroutine, coroutine_handle_t{}))
			{
			}

			~when_all_task()
			{
				if (m_coroutine)
					m_coroutine.destroy();
			}

			when_all_task(const when_all_task&) = delete;
			when_all_task& operator=(const when_all_task&) = delete;

			decltype(auto) result() & { return m_coroutine.promise().result(); }

			decltype(auto) result() && { return std::move(m_coroutine.promise()).result(); }

			decltype(auto) non_void_result() &
			{
				if constexpr (std::is_void_v<decltype(this->result())>)
				{
					this->result();
					return void_value{};
				}
				else
				{
					return this->result();
				}
			}

			decltype(auto) non_void_result() &&
			{
				if constexpr (std::is_void_v<decltype(this->result())>)
				{
					std::move(*this).result();
					return void_value{};
				}
				else
				{
					return std::move(*this).result();
				}
			}

		private:
			template<typename TASK_CONTAINER>
			friend class when_all_ready_awaitable;

			void start(when_all_counter& counter) noexcept { m_coroutine.promise().start(counter); }

			coroutine_handle_t m_coroutine;
		};

		template<
			typename AWAITABLE,
			typename RESULT = typename cppcoro::awaitable_traits<AWAITABLE&&>::await_result_t,
			std::enable_if_t<!std::is_void_v<RESULT>, int> = 0>
		when_all_task<RESULT> make_when_all_task(AWAITABLE awaitable)
		{
			co_yield co_await static_cast<AWAITABLE&&>(awaitable);
		}

		template<
			typename AWAITABLE,
			typename RESULT = typename cppcoro::awaitable_traits<AWAITABLE&&>::await_result_t,
			std::enable_if_t<std::is_void_v<RESULT>, int> = 0>
		when_all_task<void> make_when_all_task(AWAITABLE awaitable)
		{
			co_await static_cast<AWAITABLE&&>(awaitable);
		}

		template<
			typename AWAITABLE,
			typename RESULT = typename cppcoro::awaitable_traits<AWAITABLE&>::await_result_t,
			std::enable_if_t<!std::is_void_v<RESULT>, int> = 0>
		when_all_task<RESULT> make_when_all_task(std::reference_wrapper<AWAITABLE> awaitable)
		{
			co_yield co_await awaitable.get();
		}

		template<
			typename AWAITABLE,
			typename RESULT = typename cppcoro::awaitable_traits<AWAITABLE&>::await_result_t,
			std::enable_if_t<std::is_void_v<RESULT>, int> = 0>
		when_all_task<void> make_when_all_task(std::reference_wrapper<AWAITABLE> awaitable)
		{
			co_await awaitable.get();
		}
	}  // namespace detail
}  // namespace cppcoro

#endif
