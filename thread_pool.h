#pragma once
#ifndef SEPT_THREAD_POOL_H
#define SEPT_THREAD_POOL_H
#include <list>
#include <deque>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>

namespace sept {
	class ThreadPool {
	public:
		enum class Mode {
			fixed, cached
		};

	public:
		explicit ThreadPool(std::size_t count1 = std::thread::hardware_concurrency(),	//set default thread size
			std::size_t count2 = default_queue_size_threshold,							//set default task size threshold
			Mode mode = Mode::fixed,													//set mode
			std::size_t count3 = std::thread::hardware_concurrency() * 2)				//set default thread size threshold(when cached)
			: basic_thread_size(count1), thread_list(count1)
			, queue_size_threshold(count2), mode(mode), thread_size_threshold(count3){
		}

		~ThreadPool() {
			shut_down();
		};

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool& operator=(ThreadPool&&) = delete;

		void run() {
			if (is_running)
				return;
			is_running = true;
			for (std::thread& t : thread_list) {
				t = std::thread(std::bind(&ThreadPool::Get_task, this));
				thread_size++;
			}
		}

		void shut_down() {
			if (!is_running)
				return;
			is_running = false;
			queue_ready.notify_all();
			for (std::thread& t : thread_list)
				t.join();
			if (mode == Mode::cached)
				thread_list.resize(basic_thread_size);
			thread_size = basic_thread_size;
			running_thread_count = 0;
		}

		template <typename Func>
		auto submit(Func&& func) {
			return [&](auto&&...args) -> std::future<decltype(func(args...))> {
				std::unique_lock<std::mutex> lock(queue_mtx);
				bool wait_result = queue_not_full.wait_for(lock, std::chrono::seconds(30), [this]() -> bool {
					return queue_size < queue_size_threshold;
					});
				auto result_ptr = std::make_shared<std::packaged_task<decltype(func(args...))()>>(
					std::bind(std::forward<Func>(func), std::forward<decltype(args)>(args)...)
				);
				if (wait_result) {
					task_queue.emplace_back([result_ptr]() -> void {
						(*result_ptr)();
						});
					queue_size++;
					queue_ready.notify_one();
					if (mode == Mode::cached 
						&& queue_size > thread_size - running_thread_count 
						&& thread_size < thread_size_threshold) {
						thread_list.emplace_back(std::thread(std::bind(&ThreadPool::Get_task, this)));
						thread_size++;
					}
				}
				else Submit_error();
				return result_ptr->get_future();
				};
		}

	private:
		void Get_task() {
			std::function<void()> task{};
			auto last_time = std::chrono::high_resolution_clock().now();
			while (true) {
				{
					std::unique_lock<std::mutex> lock(queue_mtx);
					while (!queue_size) {
						if (!is_running)
							return;
						switch (mode) {
						case Mode::fixed: {
							queue_ready.wait(lock, [this]() -> bool {
								return queue_size || !is_running;
								});
							break;
						}
						case Mode::cached: {
							if (!queue_ready.wait_for(lock, std::chrono::seconds(1), [this]() -> bool {
								return queue_size || !is_running;
								})) {
								auto now_time = std::chrono::high_resolution_clock().now();
								auto during = std::chrono::duration_cast<std::chrono::seconds>(now_time - last_time);
								if (thread_size == basic_thread_size || during.count() <= max_thread_idle_time)
									break;
								for (auto it = thread_list.begin(); it != thread_list.end(); it++)
									if ((*it).get_id() == std::this_thread::get_id()) {
										(*it).detach();
										thread_list.erase(it);
										thread_size--;
										return;
									}
							}
							break;
						}
						}
					}
					if (queue_size) {
						task = std::move(task_queue.front());
						task_queue.pop_front();
						if(--queue_size)
							queue_ready.notify_all();
					}
				}
				queue_not_full.notify_all();
				if (task) {
					running_thread_count++;
					task();
					running_thread_count--;
				}
				last_time = std::chrono::high_resolution_clock().now();
			}
		}

		[[noreturn]] static void Submit_error() {
			std::runtime_error err("Thread_pool can't take on a new task");
		}

	public:
		bool set_basic_thread_size(std::size_t count) {
			if (is_running)
				return false;
			basic_thread_size = count;
			return true;
		}

		bool set_mode(Mode mode) {
			if (is_running)
				return false;
			this->mode = mode;
			return true;
		}

		bool set_thread_size_threshold(std::size_t count) {
			if (is_running && mode != Mode::cached)
				return false;
			thread_size_threshold = count;
			return true;
		}

		std::size_t size() const noexcept {
			return static_cast<std::size_t>(thread_size);
		}

	private:
		Mode mode;
		std::list<std::thread> thread_list;
		std::size_t basic_thread_size;
		std::size_t thread_size_threshold;
		std::atomic_uint thread_size = 0;
		std::atomic_uint running_thread_count = 0;
		std::atomic_bool is_running = false;
		static constexpr std::size_t max_thread_idle_time = 30;

	public:
		bool set_task_threshold(std::size_t count) {
			if (is_running)
				return false;
			queue_size_threshold = count;
			return true;
		}

	private:
		std::deque<std::function<void()>> task_queue;
		std::size_t queue_size_threshold;
		std::mutex queue_mtx;
		std::condition_variable queue_not_full;
		std::condition_variable queue_ready;
		std::atomic_uint queue_size = 0;
		static constexpr std::size_t default_queue_size_threshold = 1024;
	};
}
#endif
