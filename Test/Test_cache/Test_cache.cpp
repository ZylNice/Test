#include <iostream>
#include <vector>
#include <chrono>
#include <random>

using namespace std;

const int NUM_BINS = 64;

// =====================================================================
// 模拟虚幻引擎中的内存池控制块 / 线程局部缓存 (TLC)
// alignas(64) 强制结构体对齐到缓存行边界，模拟真实引擎的内存对齐规范
// =====================================================================

// 情况 A：使用 uint32_t 
struct alignas(64) PoolBlock32 {
    uint64_t LockState;                     // 8 bytes (偏移 0)
    uint32_t ShiftedBins[NUM_BINS];         // 256 bytes (占据 4 个缓存行，偏移 8-263)

    // 致命点：热点数据被庞大的数组推到了距离起点 264 字节的地方
    volatile uint64_t FreeListHead;
    volatile uint64_t AllocCount;
};

// 情况 B：使用 uint16_t (虚幻优化的版本)
struct alignas(64) PoolBlock16 {
    uint64_t LockState;                     // 8 bytes (偏移 0)
    uint16_t ShiftedBins[NUM_BINS];         // 128 bytes (占据 2 个缓存行，偏移 8-135)

    // 优势：热点数据大幅前移，距离起点仅 136 字节，极易被硬件预取器覆盖
    volatile uint64_t FreeListHead;
    volatile uint64_t AllocCount;
};

int main() {
    // 模拟引擎中大量的内存池或对象池 (10万个，足以导致 Cache Miss)
    const int NUM_POOLS = 100000;
    const int NUM_ALLOCS = 20000000; // 模拟 2000 万次分配请求

    cout << "正在初始化内存池阵列..." << endl;
    vector<PoolBlock32> pools32(NUM_POOLS);
    vector<PoolBlock16> pools16(NUM_POOLS);

    // 预生成随机的 [内存池索引] 和 [阶梯容量索引]
    // 这样避免了运行时 rand() 的开销干扰测速
    mt19937 rng(1337);
    uniform_int_distribution<int> distPool(0, NUM_POOLS - 1);
    uniform_int_distribution<int> distBin(0, NUM_BINS - 1);

    vector<int> randomPools(NUM_ALLOCS);
    vector<int> randomBins(NUM_ALLOCS);
    for (int i = 0; i < NUM_ALLOCS; ++i) {
        randomPools[i] = distPool(rng);
        randomBins[i] = distBin(rng);
    }

    uint64_t dummyResult32 = 0;
    uint64_t dummyResult16 = 0;

    cout << "开始测试 (2000万次分配热路径寻址):" << endl;
    cout << "---------------------------------------" << endl;

    // =========================================================
    // 测试 A：uint32_t (庞大结构体，引发两次 Cache Miss)
    // =========================================================
    auto start32 = chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_ALLOCS; ++i) {
        int pIdx = randomPools[i];
        int bIdx = randomBins[i];

        // 步骤 1：读取数组（大概率触发 Cache Miss，从主存拉取某个 64 字节块）
        uint32_t binVal = pools32[pIdx].ShiftedBins[bIdx];

        // 步骤 2：立即读取尾部的 FreeListHead
        // 由于相隔超过 3 个缓存行，硬件预取器可能没有将其抓入 L1
        // 这里极易触发【第二次 Cache Miss】，CPU 再次被迫等待
        dummyResult32 += binVal + pools32[pIdx].FreeListHead;
        pools32[pIdx].AllocCount++;
    }
    auto end32 = chrono::high_resolution_clock::now();
    chrono::duration<double, milli> time32 = end32 - start32;

    // =========================================================
    // 测试 B：uint16_t (紧凑结构体，硬件预取命中)
    // =========================================================
    auto start16 = chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_ALLOCS; ++i) {
        int pIdx = randomPools[i];
        int bIdx = randomBins[i];

        // 步骤 1：读取数组（触发 Cache Miss，拉取数据）
        uint16_t binVal = pools16[pIdx].ShiftedBins[bIdx];

        // 步骤 2：立即读取尾部的 FreeListHead
        // 因为相距较近（仅差 1~2 个缓存行），现代 CPU 的空间预取器
        // 几乎肯定在拉取数组时，顺道把 FreeListHead 所在的缓存行拉进来了
        // 这里是【Cache Hit】，直接从 L1 零延迟读取
        dummyResult16 += binVal + pools16[pIdx].FreeListHead;
        pools16[pIdx].AllocCount++;
    }
    auto end16 = chrono::high_resolution_clock::now();
    chrono::duration<double, milli> time16 = end16 - start16;

    cout << "uint32_t 结构体总耗时: " << time32.count() << " ms" << endl;
    cout << "uint16_t 结构体总耗时: " << time16.count() << " ms" << endl;
    cout << "---------------------------------------" << endl;
    cout << "紧凑化带来的性能提升: " << (time32.count() / time16.count() - 1) * 100 << " %" << endl;

    return 0;
}