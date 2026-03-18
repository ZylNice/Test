#include <iostream>
#include <thread>
#include <atomic>

#define PRINT_TITLE(msg) std::cout << "\n========== " << msg << " ==========\n"

// ============================================================================
// 实验 1：证明“本线程先生效，其他线程晚生效（延迟）” 
// (双重自旋对齐 + Cache Line 隔离，逼出真实的 Store Buffer 延迟)
// ============================================================================

// 1. 核心原子变量 (强制 64 字节对齐，彻底干掉 CPU 伪共享串行排队！)
alignas(64) std::atomic<int> X{ 0 };
alignas(64) std::atomic<int> Y{ 0 };
alignas(64) int r1 = 0;
alignas(64) int r2 = 0;

// 2. 极致同步信号 (同样 64 字节对齐，防止互相干扰)
alignas(64) std::atomic<int> iter_main{ 0 };
alignas(64) std::atomic<int> ready_t1{ 0 };
alignas(64) std::atomic<int> ready_t2{ 0 };
alignas(64) std::atomic<int> done_t1{ 0 };
alignas(64) std::atomic<int> done_t2{ 0 };

void Lab1_TestDelay() {
    PRINT_TITLE("实验 1: 验证 acq_rel/release 的延迟生效 (Store Buffer 现象)");

    int delay_caught_count = 0;
    const int TEST_ITERATIONS = 1000000; // 跑 100 万次

    // 常驻线程 1：负责写 X 看 Y
    std::thread t1([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_main.load(std::memory_order_acquire);
            if (current == -1) break; // 收到下班信号

            if (current == expected_iter) {
                // 【绝杀机制】：走到起跑线，互相确认眼神
                ready_t1.store(expected_iter, std::memory_order_release);
                while (ready_t2.load(std::memory_order_acquire) != expected_iter) {}
                // --- 此时，两个线程在纳秒级别处于绝对同步状态 ---

                // 核心测试逻辑
                X.store(1, std::memory_order_release);
                r1 = Y.load(std::memory_order_acquire);

                // 汇报完工
                done_t1.store(expected_iter, std::memory_order_release);
                expected_iter++;
            }
        }
    });

    // 常驻线程 2：负责写 Y 看 X
    std::thread t2([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_main.load(std::memory_order_acquire);
            if (current == -1) break; // 收到下班信号

            if (current == expected_iter) {
                // 【绝杀机制】：走到起跑线，互相确认眼神
                ready_t2.store(expected_iter, std::memory_order_release);
                while (ready_t1.load(std::memory_order_acquire) != expected_iter) {}
                // --- 此时，两个线程在纳秒级别处于绝对同步状态 ---

                // 核心测试逻辑
                Y.store(1, std::memory_order_release);
                r2 = X.load(std::memory_order_acquire);

                // 汇报完工
                done_t2.store(expected_iter, std::memory_order_release);
                expected_iter++;
            }
        }
    });

    // 裁判员（主线程）
    for (int i = 1; i <= TEST_ITERATIONS; ++i) {
        // 1. 重置战场
        X.store(0, std::memory_order_relaxed);
        Y.store(0, std::memory_order_relaxed);
        r1 = 0;
        r2 = 0;

        // 2. 打响总发令枪，唤醒两个线程去起跑线就位
        iter_main.store(i, std::memory_order_release);

        // 3. 阻塞等待双方冲过终点线
        while (done_t1.load(std::memory_order_acquire) != i ||
               done_t2.load(std::memory_order_acquire) != i) {
        }

        // 4. 致命核对：如果双方互没看到对方挂旗，抓到一次纯物理延迟！
        if (r1 == 0 && r2 == 0) {
            delay_caught_count++;
        }
    }

    // 发送下班信号并回收
    iter_main.store(-1, std::memory_order_release);
    t1.join();
    t2.join();

    std::cout << "总测试次数: " << TEST_ITERATIONS << "\n";
    std::cout << "发生【延迟生效 (双方互没看到)】的次数: " << delay_caught_count << "\n";
    std::cout << "-> 验证：只要不用 seq_cst，本线程修改对外延迟生效是客观存在的物理现象！\n";
}


// ============================================================================
// 实验 2：证明“任务切换导致身份复用时，单用 acquire 会漏水”
// ============================================================================
alignas(64) int PayloadData = 0;
alignas(64) std::atomic<int> TaskState{ 0 };
alignas(64) int observed_payload = 0;

std::atomic<int> iter_lab2{ 0 };
std::atomic<int> done_lab2{ 0 };

void Lab2_RoleSwitchLeak() {
    PRINT_TITLE("实验 2: 验证任务切换时，单用 acquire 导致的【指令漏水】");

    int leak_caught_count = 0;
    const int TEST_ITERATIONS = 1000000;

    // 工作线程 (模拟先做生产者，后做消费者)
    std::thread worker([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_lab2.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                // 【身份 A：生产者】写下业务数据
                PayloadData = 42;

                // 【身份 B：消费者】执行状态更新
                // 错误用法演示：这里只用 acquire，防不住上面的 PayloadData 往下漏！
                // (只有换成 memory_order_acq_rel 才能修复这个 Bug)
                TaskState.exchange(1, std::memory_order_acquire);

                done_lab2.fetch_add(1, std::memory_order_acq_rel);
                expected_iter++;
            }
        }
    });

    // 观察者线程
    std::thread observer([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_lab2.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                // 自旋等待 TaskState 变成 1
                while (TaskState.load(std::memory_order_acquire) != 1) {}

                // 此时状态已经是 1 了，读取业务数据
                observed_payload = PayloadData;

                done_lab2.fetch_add(1, std::memory_order_acq_rel);
                expected_iter++;
            }
        }
    });

    // 裁判员（主线程）
    for (int i = 1; i <= TEST_ITERATIONS; ++i) {
        PayloadData = 0;
        observed_payload = 0;
        TaskState.store(0, std::memory_order_relaxed);
        done_lab2.store(0, std::memory_order_relaxed);

        iter_lab2.store(i, std::memory_order_release);

        while (done_lab2.load(std::memory_order_acquire) != 2) {}

        // 核对：状态虽然变成 1 了，但拿到的 PayloadData 如果还是 0，说明被 CPU 乱序执行了！
        if (observed_payload != 42) {
            leak_caught_count++;
        }
    }

    iter_lab2.store(-1, std::memory_order_release);
    worker.join();
    observer.join();

    std::cout << "总测试次数: " << TEST_ITERATIONS << "\n";
    std::cout << "发生【状态已更新，但业务数据仍是 0 (漏水)】的次数: " << leak_caught_count << "\n";
    std::cout << "-> 验证：单用 acquire，编译期/CPU可以把上面的生产代码重排下来，必须用 acq_rel！\n";
    std::cout << "  (注：在强一致性的 x86 上此处通常为 0，但在 ARM 手机/Mac 芯片，或开启 O3 优化时极易触发)\n";
}

int main() {
    std::cout << "开始 C++ 工业级并发压测（常驻线程 + 极限重合屏障 + Cache Line 隔离）...\n";
    Lab1_TestDelay();
    Lab2_RoleSwitchLeak();
    std::cout << "\n压测结束！\n";
    return 0;
}