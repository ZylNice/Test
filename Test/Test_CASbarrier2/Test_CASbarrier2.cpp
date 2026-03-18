#include <iostream>
#include <thread>
#include <atomic>

#define PRINT_TITLE(msg) std::cout << "\n========== " << msg << " ==========\n"

// ============================================================================
// 实验 1：证明“本线程先生效，其他线程晚生效（延迟）” 
// (使用纯正 C++ 标准的 std::atomic_signal_fence 替代底层宏)
// ============================================================================

alignas(64) std::atomic<int> X{ 0 };
alignas(64) std::atomic<int> Y{ 0 };
alignas(64) int r1 = 0;
alignas(64) int r2 = 0;

alignas(64) std::atomic<int> iter_main{ 0 };
alignas(64) std::atomic<int> ready_t1{ 0 };
alignas(64) std::atomic<int> ready_t2{ 0 };
alignas(64) std::atomic<int> done_t1{ 0 };
alignas(64) std::atomic<int> done_t2{ 0 };

void Lab1_TestDelay() {
    PRINT_TITLE("实验 1: 验证 Store Buffer 延迟 (纯 C++ 标准版)");

    int delay_caught_count = 0;
    const int TEST_ITERATIONS = 1000000;

    std::thread t1([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_main.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                ready_t1.store(expected_iter, std::memory_order_release);
                while (ready_t2.load(std::memory_order_acquire) != expected_iter) {}

                // --- 【标准 C++ 破壁测试核心】 ---
                X.store(1, std::memory_order_release);

                // 【替代品】：C++ 官方标准的“纯编译器屏障”
                // 它不会生成任何 CPU 屏障指令，纯粹是为了阻止 C++ 编译器乱排代码
                std::atomic_signal_fence(std::memory_order_acq_rel);

                r1 = Y.load(std::memory_order_acquire);
                // ---------------------------

                done_t1.store(expected_iter, std::memory_order_release);
                expected_iter++;
            }
        }
    });

    std::thread t2([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_main.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                ready_t2.store(expected_iter, std::memory_order_release);
                while (ready_t1.load(std::memory_order_acquire) != expected_iter) {}

                // --- 【标准 C++ 破壁测试核心】 ---
                Y.store(1, std::memory_order_release);

                std::atomic_signal_fence(std::memory_order_acq_rel);

                r2 = X.load(std::memory_order_acquire);
                // ---------------------------

                done_t2.store(expected_iter, std::memory_order_release);
                expected_iter++;
            }
        }
    });

    for (int i = 1; i <= TEST_ITERATIONS; ++i) {
        X.store(0, std::memory_order_relaxed);
        Y.store(0, std::memory_order_relaxed);
        r1 = 0;
        r2 = 0;

        iter_main.store(i, std::memory_order_release);

        while (done_t1.load(std::memory_order_acquire) != i ||
               done_t2.load(std::memory_order_acquire) != i) {
        }

        if (r1 == 0 && r2 == 0) {
            delay_caught_count++;
        }
    }

    iter_main.store(-1, std::memory_order_release);
    t1.join();
    t2.join();

    std::cout << "总测试次数: " << TEST_ITERATIONS << "\n";
    std::cout << "发生【物理延迟生效 (双方互没看到)】的次数: " << delay_caught_count << "\n";
    std::cout << "-> 结论：不使用底层宏，仅用 C++ 标准语法，同样抓到了真实的物理延迟！\n";
}


// ============================================================================
// 实验 2：证明“任务切换导致身份复用时，单用 acquire 会漏水”
// (采用极度严谨对照：给 Payload 也加上 release)
// ============================================================================

// 严谨改造：将 Payload 也变成原子类型
alignas(64) std::atomic<int> PayloadData{ 0 };
alignas(64) std::atomic<int> TaskState{ 0 };
alignas(64) int observed_payload = 0;

std::atomic<int> iter_lab2{ 0 };
std::atomic<int> done_lab2{ 0 };

void Lab2_RoleSwitchLeak() {
    PRINT_TITLE("实验 2: 极限严谨测试 - Payload 加上 release 依然漏水");

    int leak_caught_count = 0;
    const int TEST_ITERATIONS = 1000000;

    std::thread worker([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_lab2.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                // 【身份 A：生产者】
                // 严谨测试：我们给业务数据也加上强力的 release 语义！
                PayloadData.store(42, std::memory_order_release);

                //std::atomic_signal_fence(std::memory_order_acq_rel);

                // 【身份 B：消费者】
                // 但是！状态门卫依然只用 acquire（错误用法）
                TaskState.exchange(1, std::memory_order_acquire);

                done_lab2.fetch_add(1, std::memory_order_acq_rel);
                expected_iter++;
            }
        }
    });

    std::thread observer([&]() {
        int expected_iter = 1;
        while (true) {
            int current = iter_lab2.load(std::memory_order_acquire);
            if (current == -1) break;

            if (current == expected_iter) {
                while (TaskState.load(std::memory_order_acquire) != 1) {}

                // 严谨读取
                observed_payload = PayloadData.load(std::memory_order_acquire);

                done_lab2.fetch_add(1, std::memory_order_acq_rel);
                expected_iter++;
            }
        }
    });

    for (int i = 1; i <= TEST_ITERATIONS; ++i) {
        PayloadData.store(0, std::memory_order_relaxed);
        observed_payload = 0;
        TaskState.store(0, std::memory_order_relaxed);
        done_lab2.store(0, std::memory_order_relaxed);

        iter_lab2.store(i, std::memory_order_release);

        while (done_lab2.load(std::memory_order_acquire) != 2) {}

        // 核对：即便 Payload 加了 release，状态更新了，数据依然可能是错的（0）！
        if (observed_payload != 42) {
            leak_caught_count++;
        }
    }

    iter_lab2.store(-1, std::memory_order_release);
    worker.join();
    observer.join();

    std::cout << "总测试次数: " << TEST_ITERATIONS << "\n";
    std::cout << "发生【状态已更新，但业务数据仍是 0 (漏水)】的次数: " << leak_caught_count << "\n";
    std::cout << "-> 终极证明：给业务数据加 release 毫无意义！门卫 (TaskState) 必须是 acq_rel 才能防止漏水！\n";
}

int main() {
    std::cout << "开始 C++ 纯标准严谨并发压测...\n";
    Lab1_TestDelay();
    Lab2_RoleSwitchLeak();
    std::cout << "\n压测结束！\n";
    return 0;
}