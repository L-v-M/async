///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_TASK_HPP_INCLUDED
#define CPPCORO_TASK_HPP_INCLUDED

#include <cppcoro/allocator.hpp>
#include <cppcoro/awaitable_traits.hpp>
#include <cppcoro/broken_promise.hpp>

#include <cppcoro/detail/remove_rvalue_reference.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <type_traits>
#include <utility>

#include <cppcoro/coroutine.hpp>

namespace cppcoro
{
	template<typename T>
	class task;

	namespace detail
	{
		inline thread_local Allocator* allocator{ nullptr };

		class task_promise_base
		{
			friend struct final_awaitable;

			struct final_awaitable
			{
				bool await_ready() const noexcept { return false; }

				template<typename PROMISE>
				cppcoro::coroutine_handle<>
				await_suspend(cppcoro::coroutine_handle<PROMISE> coro) noexcept
				{
					return coro.promise().m_continuation;
				}

				void await_resume() noexcept {}
			};

		public:
			task_promise_base() noexcept {}

			static void* operator new(size_t sz)
			{
				if (allocator)
				{
					return allocator->allocate(sz);
				}
				else
				{
					return ::operator new(sz);
				}
			}

			static void operator delete(void* p, size_t sz)
			{
				if (allocator)
				{
					allocator->deallocate(p, sz);
				}
				else
				{
					::operator delete(p, sz);
				}
			}

			// template<typename... Args>
			// static void*
			// operator new(std::size_t sz, std::allocator_arg_t, async::Allocator& alloc, Args&...)
			// {
			// 	// Store a pointer to the allocator in the allocated memory
			// 	// region so that the allocator can be accessed in operator delete
			// 	std::size_t allocator_offset = (sz + alignof(void*) - 1u) & ~(alignof(void*) - 1u);
			// 	void* ptr = alloc.Allocate(allocator_offset + sizeof(void*));
			// 	new (static_cast<unsigned char*>(ptr) + allocator_offset)
			// 		async::Allocator* { &alloc };
			// 	return ptr;
			// }

			// static void operator delete(void* p, std::size_t sz)
			// {
			// 	std::size_t allocator_offset = (sz + alignof(void*) - 1u) & ~(alignof(void*) - 1u);
			// 	async::Allocator* alloc = *reinterpret_cast<async::Allocator**>(
			// 		static_cast<unsigned char*>(p) + allocator_offset);
			// 	if (alloc)
			// 	{
			// 		alloc->Deallocate(p, allocator_offset + sizeof(void*));
			// 	}
			// 	else
			// 	{
			// 		::operator delete(p);
			// 	}
			// }

			auto initial_suspend() noexcept { return cppcoro::suspend_always{}; }

			auto final_suspend() noexcept { return final_awaitable{}; }

			void set_continuation(cppcoro::coroutine_handle<> continuation) noexcept
			{
				m_continuation = continuation;
			}

		private:
			cppcoro::coroutine_handle<> m_continuation;
		};

		template<typename T>
		class task_promise final : public task_promise_base
		{
		public:
			task_promise() noexcept {}

			~task_promise()
			{
				switch (m_resultType)
				{
					case result_type::value:
						m_value.~T();
						break;
					case result_type::exception:
						m_exception.~exception_ptr();
						break;
					default:
						break;
				}
			}

			task<T> get_return_object() noexcept;

			void unhandled_exception() noexcept
			{
				::new (static_cast<void*>(std::addressof(m_exception)))
					std::exception_ptr(std::current_exception());
				m_resultType = result_type::exception;
			}

			template<typename VALUE, typename = std::enable_if_t<std::is_convertible_v<VALUE&&, T>>>
			void return_value(VALUE&& value) noexcept(std::is_nothrow_constructible_v<T, VALUE&&>)
			{
				::new (static_cast<void*>(std::addressof(m_value))) T(std::forward<VALUE>(value));
				m_resultType = result_type::value;
			}

			T& result() &
			{
				if (m_resultType == result_type::exception)
				{
					std::rethrow_exception(m_exception);
				}

				assert(m_resultType == result_type::value);

				return m_value;
			}

			// HACK: Need to have co_await of task<int> return prvalue rather than
			// rvalue-reference to work around an issue with MSVC where returning
			// rvalue reference of a fundamental type from await_resume() will
			// cause the value to be copied to a temporary. This breaks the
			// sync_wait() implementation.
			// See https://github.com/lewissbaker/cppcoro/issues/40#issuecomment-326864107
			using rvalue_type =
				std::conditional_t<std::is_arithmetic_v<T> || std::is_pointer_v<T>, T, T&&>;

			rvalue_type result() &&
			{
				if (m_resultType == result_type::exception)
				{
					std::rethrow_exception(m_exception);
				}

				assert(m_resultType == result_type::value);

				return std::move(m_value);
			}

		private:
			enum class result_type
			{
				empty,
				value,
				exception
			};

			result_type m_resultType = result_type::empty;

			union
			{
				T m_value;
				std::exception_ptr m_exception;
			};
		};

		template<>
		class task_promise<void> : public task_promise_base
		{
		public:
			task_promise() noexcept = default;

			task<void> get_return_object() noexcept;

			void return_void() noexcept {}

			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			void result()
			{
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
			}

		private:
			std::exception_ptr m_exception;
		};

		template<typename T>
		class task_promise<T&> : public task_promise_base
		{
		public:
			task_promise() noexcept = default;

			task<T&> get_return_object() noexcept;

			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			void return_value(T& value) noexcept { m_value = std::addressof(value); }

			T& result()
			{
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}

				return *m_value;
			}

		private:
			T* m_value = nullptr;
			std::exception_ptr m_exception;
		};
	}  // namespace detail

	/// \brief
	/// A task represents an operation that produces a result both lazily
	/// and asynchronously.
	///
	/// When you call a coroutine that returns a task, the coroutine
	/// simply captures any passed parameters and returns exeuction to the
	/// caller. Execution of the coroutine body does not start until the
	/// coroutine is first co_await'ed.
	template<typename T = void>
	class [[nodiscard]] task
	{
	public:
		using promise_type = detail::task_promise<T>;

		using value_type = T;

	private:
		struct awaitable_base
		{
			cppcoro::coroutine_handle<promise_type> m_coroutine;

			awaitable_base(cppcoro::coroutine_handle<promise_type> coroutine) noexcept
				: m_coroutine(coroutine)
			{
			}

			bool await_ready() const noexcept { return !m_coroutine || m_coroutine.done(); }

			cppcoro::coroutine_handle<>
			await_suspend(cppcoro::coroutine_handle<> awaitingCoroutine) noexcept
			{
				m_coroutine.promise().set_continuation(awaitingCoroutine);
				return m_coroutine;
			}
		};

	public:
		task() noexcept
			: m_coroutine(nullptr)
		{
		}

		explicit task(cppcoro::coroutine_handle<promise_type> coroutine)
			: m_coroutine(coroutine)
		{
		}

		task(task&& t) noexcept
			: m_coroutine(t.m_coroutine)
		{
			t.m_coroutine = nullptr;
		}

		/// Disable copy construction/assignment.
		task(const task&) = delete;
		task& operator=(const task&) = delete;

		/// Frees resources used by this task.
		~task()
		{
			if (m_coroutine)
			{
				m_coroutine.destroy();
			}
		}

		task& operator=(task&& other) noexcept
		{
			if (std::addressof(other) != this)
			{
				if (m_coroutine)
				{
					m_coroutine.destroy();
				}

				m_coroutine = other.m_coroutine;
				other.m_coroutine = nullptr;
			}

			return *this;
		}

		/// \brief
		/// Query if the task result is complete.
		///
		/// Awaiting a task that is ready is guaranteed not to block/suspend.
		bool is_ready() const noexcept { return !m_coroutine || m_coroutine.done(); }

		auto operator co_await() const& noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				decltype(auto) await_resume()
				{
					if (!this->m_coroutine)
					{
						throw broken_promise{};
					}

					return this->m_coroutine.promise().result();
				}
			};

			return awaitable{ m_coroutine };
		}

		auto operator co_await() const&& noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				decltype(auto) await_resume()
				{
					if (!this->m_coroutine)
					{
						throw broken_promise{};
					}

					return std::move(this->m_coroutine.promise()).result();
				}
			};

			return awaitable{ m_coroutine };
		}

		/// \brief
		/// Returns an awaitable that will await completion of the task without
		/// attempting to retrieve the result.
		auto when_ready() const noexcept
		{
			struct awaitable : awaitable_base
			{
				using awaitable_base::awaitable_base;

				void await_resume() const noexcept {}
			};

			return awaitable{ m_coroutine };
		}

	private:
		cppcoro::coroutine_handle<promise_type> m_coroutine;
	};

	namespace detail
	{
		template<typename T>
		task<T> task_promise<T>::get_return_object() noexcept
		{
			return task<T>{ cppcoro::coroutine_handle<task_promise>::from_promise(*this) };
		}

		inline task<void> task_promise<void>::get_return_object() noexcept
		{
			return task<void>{ cppcoro::coroutine_handle<task_promise>::from_promise(*this) };
		}

		template<typename T>
		task<T&> task_promise<T&>::get_return_object() noexcept
		{
			return task<T&>{ cppcoro::coroutine_handle<task_promise>::from_promise(*this) };
		}
	}  // namespace detail

	template<typename AWAITABLE>
	auto make_task(AWAITABLE awaitable) -> task<
		detail::remove_rvalue_reference_t<typename awaitable_traits<AWAITABLE>::await_result_t>>
	{
		co_return co_await static_cast<AWAITABLE&&>(awaitable);
	}
}  // namespace cppcoro

#endif
