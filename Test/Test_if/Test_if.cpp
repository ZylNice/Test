#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cstdint>

#define UE_MBC_BIN_SIZE_SHIFT 4

// 模拟虚幻引擎带有 Dummy Slot 的底层数组
// [0] 是故意垫的垃圾数据 0
uint16_t SmallBinSizesShifted[6] = { 0, 1, 2, 3, 4, 5 };

// 模拟底层读取
inline uint32_t PoolIndexToBinSize(uint32_t PoolIndex) {
    // 依赖 32 位无符号整数的天然回环特性： 0xFFFFFFFF + 1 = 0
    return uint32_t(SmallBinSizesShifted[PoolIndex + 1]) << UE_MBC_BIN_SIZE_SHIFT;
}

// =========================================================
// 方案 A：传统带 if 分支的写法（会触发分支预测失败）
// =========================================================
inline bool CheckWithBranch(uint32_t PoolIndex, uint32_t NewSize) {
    if (PoolIndex == 0) {
        return true;
    }
    else {
        return NewSize > PoolIndexToBinSize(PoolIndex - 1);
    }
}

// =========================================================
// 方案 B：虚幻引擎无分支写法 (Branchless)
// =========================================================
inline bool CheckBranchless(uint32_t PoolIndex, uint32_t NewSize) {
    // 核心细节：这里必须是按位或 '|'，绝不能是逻辑或 '||'！
    // 逻辑或 '||' 具备短路特性，本质上依然会被编译器编译成 cmp 和 jmp (条件跳转) 指令。
    // 按位或 '|' 强迫 CPU 的 ALU 同时算出两边的结果然后做位运算，彻底消灭 jmp 指令。
    return (!PoolIndex) | (NewSize > PoolIndexToBinSize(PoolIndex - 1));
}

int main() {
    const size_t ITERATIONS = 100000000; // 1亿次模拟调用
    std::cout << "准备生成 1 亿次乱序测试数据以瘫痪 CPU 分支预测器..." << std::endl;

    std::vector<uint32_t> poolIndices(ITERATIONS);
    std::vector<uint32_t> newSizes(ITERATIONS);

    // 使用梅森旋转算法生成高质量的随机数，确保 0 和非 0 的交替完全不可预测
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> distPool(0, 4);
    std::uniform_int_distribution<uint32_t> distSize(1, 100);

    for (size_t i = 0; i < ITERATIONS; ++i) {
        poolIndices[i] = distPool(rng);
        newSizes[i] = distSize(rng);
    }

    std::cout << "数据生成完毕，开始 Benchmark...\n" << std::endl;

    // volatile 防止编译器把整个循环当作 dead code 优化掉
    volatile uint64_t dummySum1 = 0;
    volatile uint64_t dummySum2 = 0;

    // ---------------------------------------------------------
    // 测试 1：传统分支写法 (Branch)
    // ---------------------------------------------------------
    auto start1 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        dummySum1 += CheckWithBranch(poolIndices[i], newSizes[i]);
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration1 = end1 - start1;

    // ---------------------------------------------------------
    // 测试 2：虚幻无分支写法 (Branchless)
    // ---------------------------------------------------------
    auto start2 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        dummySum2 += CheckBranchless(poolIndices[i], newSizes[i]);
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration2 = end2 - start2;

    // ---------------------------------------------------------
    // 输出结果
    // ---------------------------------------------------------
    std::cout << "[传统 if 分支写法] 耗时: " << duration1.count() << " ms" << std::endl;
    std::cout << "[虚幻无分支写法]   耗时: " << duration2.count() << " ms" << std::endl;
    std::cout << "性能提升比例: " << (duration1.count() / duration2.count() - 1.0) * 100.0 << " %" << std::endl;

    // 校验计算结果的一致性，确保无分支逻辑在数学上绝对等价
    if (dummySum1 != dummySum2) {
        std::cerr << "致命错误：两种计算方式结果不一致！" << std::endl;
    }

    return 0;
}