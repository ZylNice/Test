#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

// 灾难版：a 和 b 紧紧挨着，必定挤在同一个 64 字节缓存行里
struct BadStruct {
    std::atomic<long long> a{0};
    std::atomic<long long> b{0};
};

// UE 拯救版：a 和 b 都加上对齐，强制物理隔离到不同的缓存行
struct GoodStruct {
    alignas(64) std::atomic<long long> a{0};
    alignas(64) std::atomic<long long> b{0};
};

const int ITERATIONS = 100'000'000; // 1 亿次运算

// 测试核心逻辑
template <typename T>
void test_performance(const char* name) {
    T data;
    auto start = std::chrono::high_resolution_clock::now();

    // 线程 1 绑定给 CPU 核心 A：疯狂修改 a
    std::thread t1([&]() { for (int i = 0; i < ITERATIONS; ++i) data.a.fetch_add(1, std::memory_order_relaxed); });
    // 线程 2 绑定给 CPU 核心 B：疯狂修改 b
    std::thread t2([&]() { for (int i = 0; i < ITERATIONS; ++i) data.b.fetch_add(1, std::memory_order_relaxed); });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = end - start;
    std::cout << name << " 耗时: " << ms.count() << " ms\n";
}

int main() {
    test_performance<BadStruct>("【灾难版】伪共享");
    test_performance<GoodStruct>("【拯救版】完美隔离");
    return 0;
}