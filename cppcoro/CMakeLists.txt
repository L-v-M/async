set(includes
	awaitable_traits.hpp
	broken_promise.hpp
	coroutine.hpp
	is_awaitable.hpp
	sync_wait.hpp
	task.hpp
	when_all_ready.hpp
)
list(TRANSFORM includes PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/include/cppcoro/")

set(detailIncludes
	any.hpp
	get_awaiter.hpp
	is_awaiter.hpp
	lightweight_manual_reset_event.hpp
	remove_rvalue_reference.hpp
	sync_wait_task.hpp
	unwrap_reference.hpp
	void_value.hpp
	when_all_counter.hpp
	when_all_ready_awaitable.hpp
	when_all_task.hpp
)
list(TRANSFORM detailIncludes PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/include/cppcoro/detail/")

set(sources
    lib/lightweight_manual_reset_event.cpp
)

add_library(cppcoro
	${includes}
	${detailIncludes}
	${sources}
)

target_include_directories(cppcoro PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)