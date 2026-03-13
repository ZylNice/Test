#include <iostream>
#include <cstdint>
#include <bitset>
#include <string>

// 严格按照源码和计算位宽定义的结构体
struct FFreeBlock {
    // --- 第一个 32位桶 ---
    uint32_t BinSizeShifted : 15; // 预期占据桶0：Bit 0  ~ 14
    uint32_t PoolIndex : 7;  // 预期占据桶0：Bit 15 ~ 21
    uint32_t Canary : 8;  // 预期占据桶0：Bit 22 ~ 29
    // 桶0 剩余 Bit 30~31 (2位)

// --- 第二个 32位桶 ---
    uint32_t NumFreeBins : 12; // 预期占据桶1：Bit 0  ~ 11
    uint32_t NextFreeBlockIndex : 13; // 预期占据桶1：Bit 12 ~ 24
    // 桶1 剩余 Bit 25~31 (7位)
};

// 使用 union 强行透视 8 字节物理内存
union MemoryInspector {
    FFreeBlock block;
    uint32_t raw_memory[2]; // 刚好是 2 个 uint32_t，共 8 字节
};

int main() {
    MemoryInspector inspector;

    std::cout << "=== FFreeBlock 8字节内存物理排布透视 ===\n";
    std::cout << "总大小: " << sizeof(FFreeBlock) << " 字节\n\n";

    // ---------------------------------------------------------
    // 测试 1-3：验证第一个存储桶 (raw_memory[0])
    // ---------------------------------------------------------
    std::cout << "--- 验证第一个存储桶 (Bucket 0) ---\n";

    // 1. BinSizeShifted (15位)
    inspector.raw_memory[0] = 0; inspector.raw_memory[1] = 0;
    inspector.block.BinSizeShifted = 0x7FFF; // 填满 15 个 1
    std::string b0_1 = std::bitset<32>(inspector.raw_memory[0]).to_string();
    std::cout << "BinSizeShifted占据:\n";
    std::cout << b0_1.substr(0, 2) << " " << b0_1.substr(2, 8) << " " << b0_1.substr(10, 7) << " " << b0_1.substr(17, 15) << "\n\n";

    // 2. PoolIndex (7位)
    inspector.raw_memory[0] = 0; inspector.raw_memory[1] = 0;
    inspector.block.PoolIndex = 0x7F; // 填满 7 个 1
    std::string b0_2 = std::bitset<32>(inspector.raw_memory[0]).to_string();
    std::cout << "PoolIndex占据:\n";
    std::cout << b0_2.substr(0, 2) << " " << b0_2.substr(2, 8) << " " << b0_2.substr(10, 7) << " " << b0_2.substr(17, 15) << "\n\n";

    // 3. Canary (8位)
    inspector.raw_memory[0] = 0; inspector.raw_memory[1] = 0;
    inspector.block.Canary = 0xFF; // 填满 8 个 1
    std::string b0_3 = std::bitset<32>(inspector.raw_memory[0]).to_string();
    std::cout << "Canary占据:\n";
    std::cout << b0_3.substr(0, 2) << " " << b0_3.substr(2, 8) << " " << b0_3.substr(10, 7) << " " << b0_3.substr(17, 15) << "\n\n";


    // ---------------------------------------------------------
    // 测试 4-5：验证第二个存储桶 (raw_memory[1])
    // ---------------------------------------------------------
    std::cout << "--- 验证第二个存储桶 (Bucket 1) ---\n";

    // 4. NumFreeBins (12位)
    inspector.raw_memory[0] = 0; inspector.raw_memory[1] = 0;
    inspector.block.NumFreeBins = 0xFFF; // 填满 12 个 1
    std::string b1_1 = std::bitset<32>(inspector.raw_memory[1]).to_string();
    std::cout << "NumFreeBins占据:\n";
    std::cout << b1_1.substr(0, 7) << " " << b1_1.substr(7, 13) << " " << b1_1.substr(20, 12) << "\n\n";

    // 5. NextFreeBlockIndex (13位)
    inspector.raw_memory[0] = 0; inspector.raw_memory[1] = 0;
    inspector.block.NextFreeBlockIndex = 0x1FFF; // 填满 13 个 1
    std::string b1_2 = std::bitset<32>(inspector.raw_memory[1]).to_string();
    std::cout << "NextFreeBlockIndex占据:\n";
    std::cout << b1_2.substr(0, 7) << " " << b1_2.substr(7, 13) << " " << b1_2.substr(20, 12) << "\n\n";

    return 0;
}