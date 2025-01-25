# sept::ThreadPool
## Insturction 
>**sept::ThreadPool**是一个在我在现代C++程序设计和多线程学习过程中实现的项目。

线程池是一种运用并发机制的技术，可以多线程执行多个任务。

有许多优良的线程池项目设计，而我综合了几种设计实现了这份200行代码的轻量级线程池。
如你所见，它定义在**sept**命名空间中，是一个具备fixed/cached模式的线程池，这意味着它可以是动态的，关于如何使用请见下文。
<sub>命名空间叫做sept仅仅是因为这是对我2024年九月份的一个简易线程池的重构</sub>。

**注意：使用C++14标准，这只是一个用于学习的轻量化项目，可能带有未知bug，请不要在生产实践中使用它。**
 
>**sept::ThreadPool** is a project I implemented during my learning process of modern C++ programming and multithreading.
 
Thread pool is a technology that uses concurrency mechanism to execute multiple tasks in multiple threads.

There are many excellent thread pool project designs, and I have synthesized several designs to implement this lightweight thread pool with 200 lines of code.

As you can see, it is defined in the **sept** namespace and is a thread pool with a fixed/cached mode, which means it can be dynamic. For more information on how to use it, see below.
<sub>The namespace called sept is only because it is a refactoring of my simple thread pool in September 2024</sub>.

**Note:This is a lightweight project for learning using the C++14 standard, and may contain unknown bugs. Please do not use it in production practice.**
## Install
>开发环境：Windows IDE：visual studio 2022 标准：C++14。测试环境同。

这是一个单头文件的库，这意味着你仅仅需要下载.h文件，配置包含路径后`#include "ThreadPool.h"`，然后就可以正常工作。

用于测试的example代码位于对应的文件夹中，后续将提供CMake文件以构建项目代码。

>Development environment: Windows IDE: visual studio 2022 Standard: C++14. The testing environment is the same.

This is a single-header file library, which means you only need to download the .h file, configure the include path with `#include "ThreadPool.h"`, and then it will work normally.

The example code for testing is located in the corresponding folder, and a CMake file will be provided later to build the project code.
## About ThreadPool
### constructor/destructor
```
explicit ThreadPool(
  std::size_t count1 = std::thread::hardware_concurrency(),	        //set basic thread size
	std::size_t count2 = default_queue_size_threshold,              //set task size threshold
	Mode mode = Mode::fixed,				        //set mode
	std::size_t count3 = std::thread::hardware_concurrency() * 2)	//set thread size threshold(when cached)
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
```
构造函数可以设置基本线程数量，任务队列大小上限，模式(fixed/cached)，以及在cached模式下的线程数量阈值。**在对应的set方法中进行可以进行修改**。
默认基本线程数量为你的硬件核心数目，任务队列大小上限为1024，模式为fixed，线程数量阈值为基本线程数量的两倍。
析构函数会调用`shut_down()`，如果已经调用`shut_down()`，那么不会做任何事。

### run()
```
void run() {
	if (is_running)
		return;
	is_running = true;
	for (int i = 0; i < basic_thread_size; i++) {
		thread_list.emplace_back(std::thread(std::bind(&ThreadPool::Get_task, this)));
		thread_size++;
	}
}
```
`run()`会创建线程，添加到线程列表中，每个线程都会执行`ThreadPool::Get_task()`。
```
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
					if (thread_size <= basic_thread_size || during.count() <= max_thread_idle_time)
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
```
`Get_task()`是一个私有的函数，包含一个`while(true)`，用于维持创建出的线程不断工作。循环会不断从任务队列里获得任务，使用互斥锁来保证对任务队列的操作时线程安全的。

在cached模式下，如果某线程30秒未工作，则会停止执行。
### submit()
```
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
```
不觉得很酷吗？`submit()`返回一个接收形参包的lambda，这意味着代码应该是这样的`thread_pool.submit(func)(args...)`。

接收一个可执行对象func，在第二个括号中输入他的参数，线程池的停止或运行过程中都可以submit。

`auto res = thread_pool.submit(func)(args...);`以得到他的结果。res的类型是std::future。通过`res.get()`获取函数返回值。
关于这部分，详见并发支持库关于异步的内容:[std::future](https://en.cppreference.com/w/cpp/thread/future)

在cached模式下，当任务数量多于空余线程数量时，会动态增长thread_list，多于线程的回收由`Get_task()`执行。

`submit()`返回的lambda函数体会按引用捕获可执行对象func，获取任务队列的互斥锁，调用std::bind()，以`std::forward<Func>`,`std::forward<decltype(args)>`保证完美转发。
进而包装成一个位于堆上的std::packaged_task，以确保这个任务的生命周期。std::shared_ptr会获得任务的地址，这是一个RAII机制的智能指针，因此对堆上对象的内存管理是安全的。
最后lambda按值捕获并执行这个指针，这个lambda会被放入任务队列(这是因为任务队列储存的是function<void()>类型)。

等待30秒后提交任务的线程仍被阻塞则会导致异常：
[[noreturn]] static void Submit_error() {
	std::runtime_error err("Thread_pool can't take on a new task");
}

### shut_down()
```
void shut_down() {
	if (!is_running)
		return;
	is_running = false;
	queue_ready.notify_all();
	for (std::thread& t : thread_list)
		if(t.joinable())
			t.join();
	thread_list.clear();
	thread_size = basic_thread_size;
	running_thread_count = 0;
}
```
`shut_down()`会同步所有的线程，并结束线程池的一次运行。
**保证所有任务都结束后线程池终止运行。**
### fixed mode
在初始化或线程池停止运行时`set_mode(sept::ThreadPool::Mode::fixed)`，使得线程池size不会动态变化。
如果`is_running == true`，那么返回false，不做任何事；否则返回true。
### cached mode
在初始化或线程池停止运行时`set_mode(sept::ThreadPool::Mode::fixed)`，使得线程池size能够动态变化。
如果`is_running == true`，那么返回false，不做任何事；否则返回true。
## Test
1. output (fixed)
2. return unsigned long long (fixed && submit before run)
3. output (cached)
4. return int (cached)

refer to example/main.cpp for details

## Future plan

- More safer
- Update to C++20 (maybe)
  
请告知我所有相关bug的案例。
Please inform me of all relevant bug cases.
