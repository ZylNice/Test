// Test_AoS_SoA_AoSoA.cpp
// =============================================================================
// 数据排布性能对比：AoS  vs  SoA  vs  AoSoA
// 参考文章：胡渊鸣《优化数据排布，让你的程序加速 4 倍！》
//
// 本测试复现文章中的核心实验，覆盖以下场景：
//   [小结构体: 4 属性]
//   1) assign_all          ——  顺序访问，填充全部分量 (x, y, z, w)
//   2) assign_all_unrolled ——  同上，手工展开循环，放大访存瓶颈
//   3) assign_single       ——  顺序访问，仅填充第 0 个分量 (SoA 优势场景)
//   4) assign_all_random   ——  随机访问，填充全部分量   (AoS 优势场景)
//   5) compute_all         ——  读-算-写，模拟真实物理仿真负载
//
//   [大结构体: 24 属性 / 96 B，跨 2 条 cacheline] —— 极端场景
//   6) big_assign_single   ——  只写 1 个属性   (AoS 浪费 23/24 带宽, SoA 完美)
//   7) big_assign_few      ——  只写 3 个属性   (典型"只处理位置"的游戏更新)
//   8) big_assign_all      ——  写入全部 24 属性
//   9) big_assign_random   ——  随机写 1 个属性 (AoS 与 SoA 的 cacheline 较量)
//
// 结论预期（CPU 上）：
//   * 顺序 + 全分量        : AoS ≈ SoA ≈ AoSoA
//   * 顺序 + 单分量        : SoA 明显快 (不浪费 cacheline)
//   * 随机 + 全分量        : AoS 明显快 (一次 cacheline 拿全所有分量)
//   * AoSoA 是两种布局的折中方案，在多种场景下都能有较好表现
//   * 大结构体单分量       : SoA 相对 AoS 可达近 10~20x 差距 (cacheline 利用率比值)
// =============================================================================

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <cstdint>
#include <cstdlib>

#if defined(_MSC_VER)
#include <malloc.h>
#define ALIGNED_ALLOC(align, size)  _aligned_malloc((size), (align))
#define ALIGNED_FREE(ptr)           _aligned_free((ptr))
#else
#include <cstdlib>
#define ALIGNED_ALLOC(align, size)  std::aligned_alloc((align), (size))
#define ALIGNED_FREE(ptr)           std::free((ptr))
#endif

using namespace std;
using Clock = chrono::high_resolution_clock;

// -----------------------------------------------------------------------------
// 全局参数
// -----------------------------------------------------------------------------
// 总粒子数：4M 个，每个粒子 4 * sizeof(float) = 16 B，总计 64 MB
// 该容量超过常见 CPU 的 LLC (一般 ≤ 32 MB)，能够真实暴露访存瓶颈
constexpr int    N       = 4 * 1024 * 1024;
constexpr int    DIM     = 4;
constexpr int    REPEAT  = 30;       // 每个 kernel 重复次数
constexpr int    UNROLL  = 8;        // 循环展开因子
constexpr int    BLOCK   = 64;        // AoSoA 的 tile 宽度 (通常 = SIMD 宽度)

static_assert(N % BLOCK == 0, "N 必须是 BLOCK 的整数倍");

// -----------------------------------------------------------------------------
// 三种数据布局定义
// -----------------------------------------------------------------------------

// --- 1) AoS: Array of Structures (C/C++ 默认风格) ---------------------------
//     内存排布: [x0 y0 z0 w0][x1 y1 z1 w1][x2 y2 z2 w2]...
//     优点: 同一粒子的四个属性聚集在一起 (随机访问友好)
//     缺点: 只用一个属性时浪费 3/4 的 cacheline 带宽
struct ParticleAoS {
    float x, y, z, w;
};

// --- 2) SoA: Structure of Arrays --------------------------------------------
//     内存排布: [x0 x1 x2 ...][y0 y1 y2 ...][z0 z1 ...][w0 w1 ...]
//     优点: 单分量访问时连续、SIMD / GPU coalescing 友好
//     缺点: 随机访问时同一粒子散落到 4 个 cacheline
struct ParticlesSoA {
    float* x;
    float* y;
    float* z;
    float* w;
};

// --- 3) AoSoA: Array of Structures of Arrays --------------------------------
//     内存排布: [x0..x7 y0..y7 z0..z7 w0..w7] [x8..x15 y8..y15 ...] ...
//     优点: 块内是 SoA 适配 SIMD；块间是 AoS 保持空间局部性；两者折中
struct ParticleTile {
    float x[BLOCK];
    float y[BLOCK];
    float z[BLOCK];
    float w[BLOCK];
};

// -----------------------------------------------------------------------------
// 辅助工具
// -----------------------------------------------------------------------------
struct Stat {
    double best_ms = 1e18;
    double total_ms = 0.0;
    int    runs = 0;

    void add(double ms) {
        total_ms += ms;
        best_ms = (ms < best_ms) ? ms : best_ms;
        ++runs;
    }
    double avg() const { return runs ? total_ms / runs : 0.0; }
};

template <typename Fn>
Stat bench(const char* name, Fn fn) {
    Stat s;
    // 先跑一次预热，避免首轮的冷启动/分页开销污染数据
    fn();
    for (int r = 0; r < REPEAT; ++r) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        double ms = chrono::duration<double, milli>(t1 - t0).count();
        s.add(ms);
    }
    cout << "  " << left << setw(32) << name
         << "avg = " << fixed << setprecision(3) << setw(8) << s.avg() << " ms"
         << "   best = " << setw(8) << s.best_ms << " ms" << endl;
    return s;
}

// 打印一行对比结果
void print_row(const char* title,
               double aos_ms, double soa_ms, double aosoa_ms) {
    double base = aos_ms;
    cout << "| " << left << setw(26) << title << "| "
         << right << setw(9) << fixed << setprecision(3) << aos_ms   << " | "
         << right << setw(9) << soa_ms                               << " (x"
         << setprecision(2) << setw(5) << (base / soa_ms)   << ") | "
         << setprecision(3) << right << setw(9) << aosoa_ms << " (x"
         << setprecision(2) << setw(5) << (base / aosoa_ms) << ") |"
         << endl;
}

// =============================================================================
// 各个 kernel 实现（AoS / SoA / AoSoA × 5 个场景）
// =============================================================================

// ---------- AoS ----------
static inline void aos_assign_all(ParticleAoS* a) {
    for (int i = 0; i < N; ++i) {
        a[i].x = float(i + 0);
        a[i].y = float(i + 1);
        a[i].z = float(i + 2);
        a[i].w = float(i + 3);
    }
}
static inline void aos_assign_all_unrolled(ParticleAoS* a) {
    const int steps = N / UNROLL;
    for (int i_ = 0; i_ < steps; ++i_) {
        const int base = i_ * UNROLL;
        for (int j = 0; j < UNROLL; ++j) {
            int i = base + j;
            a[i].x = float(i + 0);
            a[i].y = float(i + 1);
            a[i].z = float(i + 2);
            a[i].w = float(i + 3);
        }
    }
}
static inline void aos_assign_single(ParticleAoS* a) {
    for (int i = 0; i < N; ++i) {
        a[i].x = float(i);
        // 仅写 1/4 的数据量，但每次访问都拉一整条 cacheline -> 浪费 3/4 带宽
    }
}
static inline void aos_assign_all_random(ParticleAoS* a) {
    constexpr uint32_t MASK = uint32_t(N - 1);  // N 为 2 的幂
    for (int i_ = 0; i_ < N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        a[i].x = float(i + 0);
        a[i].y = float(i + 1);
        a[i].z = float(i + 2);
        a[i].w = float(i + 3);
    }
}
static inline void aos_compute_all(ParticleAoS* a) {
    // 读-算-写：模拟简单的物理推进
    for (int i = 0; i < N; ++i) {
        a[i].x = a[i].x + a[i].w * 0.5f;
        a[i].y = a[i].y + a[i].w * 0.5f;
        a[i].z = a[i].z + a[i].w * 0.5f;
    }
}

// ---------- SoA ----------
static inline void soa_assign_all(ParticlesSoA& a) {
    for (int i = 0; i < N; ++i) {
        a.x[i] = float(i + 0);
        a.y[i] = float(i + 1);
        a.z[i] = float(i + 2);
        a.w[i] = float(i + 3);
    }
}
static inline void soa_assign_all_unrolled(ParticlesSoA& a) {
    const int steps = N / UNROLL;
    for (int i_ = 0; i_ < steps; ++i_) {
        const int base = i_ * UNROLL;
        for (int j = 0; j < UNROLL; ++j) {
            int i = base + j;
            a.x[i] = float(i + 0);
            a.y[i] = float(i + 1);
            a.z[i] = float(i + 2);
            a.w[i] = float(i + 3);
        }
    }
}
static inline void soa_assign_single(ParticlesSoA& a) {
    // 只访问 x[] 这一整块连续内存 -> cacheline 利用率 100%
    for (int i = 0; i < N; ++i) {
        a.x[i] = float(i);
    }
}
static inline void soa_assign_all_random(ParticlesSoA& a) {
    constexpr uint32_t MASK = uint32_t(N - 1);
    for (int i_ = 0; i_ < N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        // 4 个分量分别位于 4 条完全不同的 cacheline，随机访问最坏情况
        a.x[i] = float(i + 0);
        a.y[i] = float(i + 1);
        a.z[i] = float(i + 2);
        a.w[i] = float(i + 3);
    }
}
static inline void soa_compute_all(ParticlesSoA& a) {
    for (int i = 0; i < N; ++i) {
        float w = a.w[i];
        a.x[i] = a.x[i] + w * 0.5f;
        a.y[i] = a.y[i] + w * 0.5f;
        a.z[i] = a.z[i] + w * 0.5f;
    }
}

// ---------- AoSoA ----------
static inline void aosoa_assign_all(ParticleTile* a) {
    const int tiles = N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        for (int j = 0; j < BLOCK; ++j) {
            int i = t * BLOCK + j;
            a[t].x[j] = float(i + 0);
            a[t].y[j] = float(i + 1);
            a[t].z[j] = float(i + 2);
            a[t].w[j] = float(i + 3);
        }
    }
}
static inline void aosoa_assign_all_unrolled(ParticleTile* a) {
    const int tiles = N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        // tile 内的 BLOCK 次迭代天然就相当于“展开”，且是 SoA 风格，对 SIMD 友好
        for (int j = 0; j < BLOCK; ++j) {
            int i = t * BLOCK + j;
            a[t].x[j] = float(i + 0);
            a[t].y[j] = float(i + 1);
            a[t].z[j] = float(i + 2);
            a[t].w[j] = float(i + 3);
        }
    }
}
static inline void aosoa_assign_single(ParticleTile* a) {
    const int tiles = N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        for (int j = 0; j < BLOCK; ++j) {
            a[t].x[j] = float(t * BLOCK + j);
        }
        // 只访问了每个 tile 前 BLOCK*4=32 B 的 x 段
        // 虽然不像 SoA 那样完全连续，但也不会像 AoS 那样 "每 16B 跳一次"
    }
}
static inline void aosoa_assign_all_random(ParticleTile* a) {
    constexpr uint32_t MASK = uint32_t(N - 1);
    for (int i_ = 0; i_ < N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        int t = i / BLOCK;
        int j = i % BLOCK;
        // 一个 tile = 128 B，恰好跨 2 条 cacheline，比纯 SoA 的 4 条要少
        a[t].x[j] = float(i + 0);
        a[t].y[j] = float(i + 1);
        a[t].z[j] = float(i + 2);
        a[t].w[j] = float(i + 3);
    }
}
static inline void aosoa_compute_all(ParticleTile* a) {
    const int tiles = N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        for (int j = 0; j < BLOCK; ++j) {
            float w = a[t].w[j];
            a[t].x[j] = a[t].x[j] + w * 0.5f;
            a[t].y[j] = a[t].y[j] + w * 0.5f;
            a[t].z[j] = a[t].z[j] + w * 0.5f;
        }
    }
}

// =============================================================================
// 极端场景：大结构体 (24 个属性 / 96 B，跨 2 条 cacheline)
// -----------------------------------------------------------------------------
// 模拟真实游戏/仿真中的"Entity"——位置、速度、加速度、旋转四元数、颜色、UV、
// 骨骼权重、生命值、阵营、掩码 …… 这类结构体动辄几十个字段非常常见。
// 在 AoS 布局下，只要你只读写少量字段，就会大量浪费 cacheline 带宽：
//   - 若只访问 1 个 float 属性 (4 B)，一次 cacheline 读入 64 B 中只用了 4/64
//   - 而 SoA 布局下这 4 B 在单独的连续数组里，cacheline 利用率 100%
// 这正是 Data-Oriented Design / ECS 架构大行其道的原因。
// =============================================================================

constexpr int BIG_N      = 1 * 1024 * 1024;  // 1M 个对象
constexpr int BIG_FIELDS = 24;               // 24 个 float 属性
// 96 B/个, 总共 96 MB, 远超 LLC, 明显暴露访存瓶颈
static_assert(BIG_N % BLOCK == 0, "BIG_N 必须是 BLOCK 的整数倍");

// --- 1) 大结构体 AoS ---------------------------------------------------------
// 24 个 float 内联字段（与 float f[24] 等价的内存布局，这里写成具名字段更"真实"）
struct BigAoS {
    // 位置 / 速度 / 加速度 (3 组 × 3 = 9)
    float px, py, pz;
    float vx, vy, vz;
    float ax, ay, az;
    // 旋转四元数 + 缩放 (4 + 3 = 7)
    float qx, qy, qz, qw;
    float sx, sy, sz;
    // 颜色 RGBA (4)
    float r, g, b, a;
    // 其它 (4) —— 质量/生命/寿命/阻力
    float mass, hp, lifetime, drag;
};
static_assert(sizeof(BigAoS) == BIG_FIELDS * sizeof(float),
              "BigAoS 应恰好 96 B，无 padding");

// --- 2) 大结构体 SoA ---------------------------------------------------------
struct BigSoA {
    float* f[BIG_FIELDS]; // 24 条独立的连续数组
};

// --- 3) 大结构体 AoSoA (块宽 = BLOCK) ---------------------------------------
struct BigTile {
    float f[BIG_FIELDS][BLOCK]; // 每个 tile 内部是 24 × BLOCK 的 SoA
};

// -----------------------------------------------------------------------------
// kernels for Big
// -----------------------------------------------------------------------------
// 6) big_assign_single —— 只写第 0 个属性（如仅更新位置的 x 分量）
static inline void big_aos_assign_single(BigAoS* a) {
    for (int i = 0; i < BIG_N; ++i) {
        a[i].px = float(i);
        // 每写 4 B 就要拉 64 B 的 cacheline —— 极端浪费
    }
}
static inline void big_soa_assign_single(BigSoA& a) {
    float* p = a.f[0];
    for (int i = 0; i < BIG_N; ++i) {
        p[i] = float(i); // 完全连续，cacheline 100% 被用上
    }
}
static inline void big_aosoa_assign_single(BigTile* a) {
    const int tiles = BIG_N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        float* p = a[t].f[0]; // tile 内该字段连续
        for (int j = 0; j < BLOCK; ++j) {
            p[j] = float(t * BLOCK + j);
        }
    }
}

// 7) big_assign_few —— 只写 3 个属性 (px, py, pz)，典型"位置更新"负载
static inline void big_aos_assign_few(BigAoS* a) {
    for (int i = 0; i < BIG_N; ++i) {
        a[i].px = float(i);
        a[i].py = float(i + 1);
        a[i].pz = float(i + 2);
    }
}
static inline void big_soa_assign_few(BigSoA& a) {
    float* px = a.f[0];
    float* py = a.f[1];
    float* pz = a.f[2];
    for (int i = 0; i < BIG_N; ++i) {
        px[i] = float(i);
        py[i] = float(i + 1);
        pz[i] = float(i + 2);
    }
}
static inline void big_aosoa_assign_few(BigTile* a) {
    const int tiles = BIG_N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        float* px = a[t].f[0];
        float* py = a[t].f[1];
        float* pz = a[t].f[2];
        for (int j = 0; j < BLOCK; ++j) {
            int i = t * BLOCK + j;
            px[j] = float(i);
            py[j] = float(i + 1);
            pz[j] = float(i + 2);
        }
    }
}

// 8) big_assign_all —— 写入全部 24 个字段
static inline void big_aos_assign_all(BigAoS* a) {
    auto* base = reinterpret_cast<float*>(a);
    for (int i = 0; i < BIG_N; ++i) {
        float* row = base + i * BIG_FIELDS;
        for (int k = 0; k < BIG_FIELDS; ++k)
            row[k] = float(i + k);
    }
}
static inline void big_soa_assign_all(BigSoA& a) {
    // 逐字段整段写：LLC 反而更友好 (一次性流式写，不会反复驱逐)
    for (int k = 0; k < BIG_FIELDS; ++k) {
        float* p = a.f[k];
        for (int i = 0; i < BIG_N; ++i)
            p[i] = float(i + k);
    }
}
static inline void big_aosoa_assign_all(BigTile* a) {
    const int tiles = BIG_N / BLOCK;
    for (int t = 0; t < tiles; ++t) {
        for (int k = 0; k < BIG_FIELDS; ++k) {
            for (int j = 0; j < BLOCK; ++j) {
                a[t].f[k][j] = float(t * BLOCK + j + k);
            }
        }
    }
}

// 9) big_assign_random —— 随机索引，只写 1 个字段
//    这是个有趣的对比：AoS 依然浪费 cacheline，但 SoA 的随机访问也一样浪费；
//    因此两者差距不大，甚至 SoA 不一定占优 —— 验证"随机+小访问量" 并非 SoA 的主场
static inline void big_aos_assign_random(BigAoS* a) {
    constexpr uint32_t MASK = uint32_t(BIG_N - 1);
    for (int i_ = 0; i_ < BIG_N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        a[i].px = float(i);
    }
}
static inline void big_soa_assign_random(BigSoA& a) {
    constexpr uint32_t MASK = uint32_t(BIG_N - 1);
    float* p = a.f[0];
    for (int i_ = 0; i_ < BIG_N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        p[i] = float(i);
    }
}
static inline void big_aosoa_assign_random(BigTile* a) {
    constexpr uint32_t MASK = uint32_t(BIG_N - 1);
    for (int i_ = 0; i_ < BIG_N; ++i_) {
        int i = int((uint32_t(i_) * 10007u) & MASK);
        int t = i / BLOCK;
        int j = i % BLOCK;
        a[t].f[0][j] = float(i);
    }
}



// =============================================================================
// main
// =============================================================================
int main() {
    cout << "=====================================================" << endl;
    cout << "  AoS / SoA / AoSoA  Memory Layout Benchmark" << endl;
    cout << "-----------------------------------------------------" << endl;
    cout << "  Particles (N) : " << N
         << "  (" << (double(N) * DIM * sizeof(float) / (1024.0 * 1024.0))
         << " MB)" << endl;
    cout << "  DIM           : " << DIM << "  (x, y, z, w as float)" << endl;
    cout << "  BLOCK (AoSoA) : " << BLOCK << endl;
    cout << "  UNROLL        : " << UNROLL << endl;
    cout << "  REPEAT        : " << REPEAT << " (+1 warmup)" << endl;
    cout << "=====================================================" << endl;

    // ----- 分配内存 (64 B 对齐) --------------------------------------------
    auto* aos = static_cast<ParticleAoS*>(
        ALIGNED_ALLOC(64, sizeof(ParticleAoS) * N));

    ParticlesSoA soa;
    soa.x = static_cast<float*>(ALIGNED_ALLOC(64, sizeof(float) * N));
    soa.y = static_cast<float*>(ALIGNED_ALLOC(64, sizeof(float) * N));
    soa.z = static_cast<float*>(ALIGNED_ALLOC(64, sizeof(float) * N));
    soa.w = static_cast<float*>(ALIGNED_ALLOC(64, sizeof(float) * N));

    auto* aosoa = static_cast<ParticleTile*>(
        ALIGNED_ALLOC(64, sizeof(ParticleTile) * (N / BLOCK)));

    // Big (24 属性) 相关内存
    auto* big_aos = static_cast<BigAoS*>(
        ALIGNED_ALLOC(64, sizeof(BigAoS) * BIG_N));
    BigSoA big_soa;
    for (int k = 0; k < BIG_FIELDS; ++k) {
        big_soa.f[k] = static_cast<float*>(ALIGNED_ALLOC(64, sizeof(float) * BIG_N));
    }
    auto* big_aosoa = static_cast<BigTile*>(
        ALIGNED_ALLOC(64, sizeof(BigTile) * (BIG_N / BLOCK)));

    if (!aos || !soa.x || !soa.y || !soa.z || !soa.w || !aosoa
        || !big_aos || !big_aosoa) {
        cerr << "内存分配失败!" << endl;
        return -1;
    }
    for (int k = 0; k < BIG_FIELDS; ++k) {
        if (!big_soa.f[k]) { cerr << "BigSoA 分配失败!" << endl; return -1; }
    }

    // 先 touch 一遍内存，避免首次分页导致的巨大抖动
    for (int i = 0; i < N; ++i) {
        aos[i] = { 0.f, 0.f, 0.f, 0.f };
        soa.x[i] = soa.y[i] = soa.z[i] = soa.w[i] = 0.f;
    }
    for (int t = 0; t < N / BLOCK; ++t) {
        for (int j = 0; j < BLOCK; ++j) {
            aosoa[t].x[j] = aosoa[t].y[j] = aosoa[t].z[j] = aosoa[t].w[j] = 0.f;
        }
    }
    // touch Big
    {
        auto* p = reinterpret_cast<float*>(big_aos);
        for (int i = 0; i < BIG_N * BIG_FIELDS; ++i) p[i] = 0.f;
    }
    for (int k = 0; k < BIG_FIELDS; ++k) {
        for (int i = 0; i < BIG_N; ++i) big_soa.f[k][i] = 0.f;
    }
    for (int t = 0; t < BIG_N / BLOCK; ++t) {
        for (int k = 0; k < BIG_FIELDS; ++k)
            for (int j = 0; j < BLOCK; ++j)
                big_aosoa[t].f[k][j] = 0.f;
    }

    // ----- 跑测试 ----------------------------------------------------------
    cout << "\n[AoS]" << endl;
    Stat aos_all     = bench("assign_all",          [&] { aos_assign_all(aos); });
    Stat aos_all_u   = bench("assign_all_unrolled", [&] { aos_assign_all_unrolled(aos); });
    Stat aos_sin     = bench("assign_single",       [&] { aos_assign_single(aos); });
    Stat aos_rand    = bench("assign_all_random",   [&] { aos_assign_all_random(aos); });
    Stat aos_cmp     = bench("compute_all",         [&] { aos_compute_all(aos); });

    cout << "\n[SoA]" << endl;
    Stat soa_all     = bench("assign_all",          [&] { soa_assign_all(soa); });
    Stat soa_all_u   = bench("assign_all_unrolled", [&] { soa_assign_all_unrolled(soa); });
    Stat soa_sin     = bench("assign_single",       [&] { soa_assign_single(soa); });
    Stat soa_rand    = bench("assign_all_random",   [&] { soa_assign_all_random(soa); });
    Stat soa_cmp     = bench("compute_all",         [&] { soa_compute_all(soa); });

    cout << "\n[AoSoA  block=" << BLOCK << "]" << endl;
    Stat aa_all      = bench("assign_all",          [&] { aosoa_assign_all(aosoa); });
    Stat aa_all_u    = bench("assign_all_unrolled", [&] { aosoa_assign_all_unrolled(aosoa); });
    Stat aa_sin      = bench("assign_single",       [&] { aosoa_assign_single(aosoa); });
    Stat aa_rand     = bench("assign_all_random",   [&] { aosoa_assign_all_random(aosoa); });
    Stat aa_cmp      = bench("compute_all",         [&] { aosoa_compute_all(aosoa); });

    // ----- Big (24 属性) 极端场景 ------------------------------------------
    cout << "\n=====================================================" << endl;
    cout << "  大结构体场景：" << BIG_FIELDS << " 个 float 属性 / "
         << sizeof(BigAoS) << " B, N=" << BIG_N
         << " (" << (double(BIG_N) * sizeof(BigAoS) / (1024.0 * 1024.0))
         << " MB)" << endl;
    cout << "=====================================================" << endl;

    cout << "\n[Big-AoS]" << endl;
    Stat b_aos_sin   = bench("assign_single (1/24)",   [&] { big_aos_assign_single(big_aos); });
    Stat b_aos_few   = bench("assign_few    (3/24)",   [&] { big_aos_assign_few(big_aos); });
    Stat b_aos_all   = bench("assign_all    (24/24)",  [&] { big_aos_assign_all(big_aos); });
    Stat b_aos_rnd   = bench("assign_random (1/24)",   [&] { big_aos_assign_random(big_aos); });

    cout << "\n[Big-SoA]" << endl;
    Stat b_soa_sin   = bench("assign_single (1/24)",   [&] { big_soa_assign_single(big_soa); });
    Stat b_soa_few   = bench("assign_few    (3/24)",   [&] { big_soa_assign_few(big_soa); });
    Stat b_soa_all   = bench("assign_all    (24/24)",  [&] { big_soa_assign_all(big_soa); });
    Stat b_soa_rnd   = bench("assign_random (1/24)",   [&] { big_soa_assign_random(big_soa); });

    cout << "\n[Big-AoSoA  block=" << BLOCK << "]" << endl;
    Stat b_aa_sin    = bench("assign_single (1/24)",   [&] { big_aosoa_assign_single(big_aosoa); });
    Stat b_aa_few    = bench("assign_few    (3/24)",   [&] { big_aosoa_assign_few(big_aosoa); });
    Stat b_aa_all    = bench("assign_all    (24/24)",  [&] { big_aosoa_assign_all(big_aosoa); });
    Stat b_aa_rnd    = bench("assign_random (1/24)",   [&] { big_aosoa_assign_random(big_aosoa); });

    // ----- 汇总表格 --------------------------------------------------------
    cout << "\n=====================================================" << endl;
    cout << "  汇总对比 (取 avg, 单位 ms; 括号内为相对 AoS 的加速比)" << endl;
    cout << "=====================================================" << endl;
    cout << "| " << left << setw(26) << "Scenario"
         << "| " << right << setw(9) << "AoS"
         << " | " << setw(9) << "SoA" << "        "
         << " | " << setw(9) << "AoSoA" << "      " << " |" << endl;
    cout << "|---------------------------"
         << "|-----------|--------------------|---------------------|" << endl;
    print_row("assign_all",          aos_all.avg(),   soa_all.avg(),   aa_all.avg());
    print_row("assign_all_unrolled", aos_all_u.avg(), soa_all_u.avg(), aa_all_u.avg());
    print_row("assign_single",       aos_sin.avg(),   soa_sin.avg(),   aa_sin.avg());
    print_row("assign_all_random",   aos_rand.avg(),  soa_rand.avg(),  aa_rand.avg());
    print_row("compute_all",         aos_cmp.avg(),   soa_cmp.avg(),   aa_cmp.avg());
    cout << "=====================================================" << endl;

    // ----- Big 结构体汇总表格 ---------------------------------------------
    cout << "\n=====================================================" << endl;
    cout << "  大结构体 (24 属性) 汇总对比" << endl;
    cout << "=====================================================" << endl;
    cout << "| " << left << setw(26) << "Scenario"
         << "| " << right << setw(9) << "AoS"
         << " | " << setw(9) << "SoA" << "        "
         << " | " << setw(9) << "AoSoA" << "      " << " |" << endl;
    cout << "|---------------------------"
         << "|-----------|--------------------|---------------------|" << endl;
    print_row("big assign_single 1/24", b_aos_sin.avg(), b_soa_sin.avg(), b_aa_sin.avg());
    print_row("big assign_few    3/24", b_aos_few.avg(), b_soa_few.avg(), b_aa_few.avg());
    print_row("big assign_all   24/24", b_aos_all.avg(), b_soa_all.avg(), b_aa_all.avg());
    print_row("big assign_random 1/24", b_aos_rnd.avg(), b_soa_rnd.avg(), b_aa_rnd.avg());
    cout << "=====================================================" << endl;

    cout << "\n观察要点：" << endl;
    cout << "  * assign_single       : SoA 应显著胜出 (cacheline 利用率 100% vs 25%)" << endl;
    cout << "  * assign_all_random   : AoS 应显著胜出 (随机访问 1 条 cacheline vs 4 条)" << endl;
    cout << "  * assign_all/compute  : 三者接近, AoSoA 通常是稳定的中间值" << endl;
    cout << "  * big assign_single   : SoA 可达 ~10x+ (AoS 浪费 23/24 的 cacheline)" << endl;
    cout << "  * big assign_all      : 三者接近 (都要把所有字节写一遍)" << endl;
    cout << "  * big assign_random   : 若只访问 1 个字段, AoS 的 '抢占 cacheline' 优势失效" << endl;

    // ----- 清理 ------------------------------------------------------------
    ALIGNED_FREE(aos);
    ALIGNED_FREE(soa.x);
    ALIGNED_FREE(soa.y);
    ALIGNED_FREE(soa.z);
    ALIGNED_FREE(soa.w);
    ALIGNED_FREE(aosoa);
    ALIGNED_FREE(big_aos);
    for (int k = 0; k < BIG_FIELDS; ++k) ALIGNED_FREE(big_soa.f[k]);
    ALIGNED_FREE(big_aosoa);
    return 0;
}
