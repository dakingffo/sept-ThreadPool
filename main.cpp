#include <iostream>
#include <cmath>
#include <vector>
#include "thread_pool.h"
void print(int idx) {					//test1:no return function 
	std::cout << "hello world from task" << idx << std::endl;
}

unsigned long long fib(int idx) {				//test1:make fibonacci from idx = 0
	unsigned long long res[100] = { 0,1 };
	if (idx == 0) return 0;
	if (idx == 1) return 1;
	for (int j = 2; j <= idx; j++)
		res[j] = res[j - 1] + res[j - 2];
	return res[idx];
}

int mul(int x, int y) {
	return x * y;
}

int main() {
	//fixed mode test
	{									
		sept::ThreadPool thread_pool;
		std::cout << "----------------------test1----------------------" << std::endl;
		thread_pool.run();
		for (int i = 0; i < 100; i++) {
			thread_pool.submit(print)(i);
		}
		thread_pool.shut_down();		//shut_down after all tasks done
		std::cout << "----------------------test2----------------------" << std::endl;
		thread_pool.run();
		std::vector<std::future<unsigned long long>> res(100);
		for (int i = 0; i < 100; i++) {
			res[i] = thread_pool.submit(fib)(i);
		}
		for (int i = 0; i < 100; i++) {
			std::cout << res[i].get() << ' ';
		}
		std::cout << std::endl;
	}									//shut_down in ~ThreadPool()
	//cached mode test
	{									
		sept::ThreadPool thread_pool(8, 1024, sept::ThreadPool::Mode::cached, 16);
		std::cout << "----------------------test3----------------------" << std::endl;
		thread_pool.run();
		for (int i = 0; i < 32; i++) {
			thread_pool.submit(print)(i);
		}
		std::cout << "Now number of threads " << thread_pool.size() << std::endl;
		thread_pool.shut_down();		
		std::cout << "----------------------test4----------------------" << std::endl;
		thread_pool.run();
		std::vector<std::future<int>> res(100);
		for (int i = 0, j = 1; i < 100; i++, j++) {
			res[i] = thread_pool.submit(mul)(i, j);
		}
		std::this_thread::sleep_for(std::chrono::seconds(32));
										//after extra threads destroyed
		std::cout << "Now number of threads " << thread_pool.size() << std::endl;
		for (int i = 0; i < 100; i++) {
			std::cout << res[i].get() << ' ';
		}
		std::cout << std::endl;
	}
	return 0;
}