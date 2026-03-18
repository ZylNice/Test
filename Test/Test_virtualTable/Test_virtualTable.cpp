#include <iostream>
#include <typeinfo>
#include <cxxabi.h> // GCC/Clang 专属：用于逆向解析编译器乱码名
#include <cstdint>
#include <iomanip>

using namespace std;

// 辅助函数：把 "7Derived" 还原成 "Derived"
string demangle(const char* name) {
    int status = -4;
    char* res = abi::__cxa_demangle(name, NULL, NULL, &status);
    string ret = (status == 0) ? res : name;
    free(res);
    return ret;
}

// 定义通用的成员函数指针，带 this 指针参数
typedef void (*FuncPtr)(void*);

// ==========================================
// 1. 定义极其工整的类结构 (方便观察内存对齐)
// ==========================================
class Base1 {
public:
    int64_t b1_data = 0x11111111; // 占 8 字节
    virtual void f1() { cout << "    [执行] Base1::f1() \n"; }
    virtual void g1() { cout << "    [执行] Base1::g1() \n"; }
}; // Base1 物理大小: 8(vptr) + 8(data) = 16 字节

class Base2 {
public:
    int64_t b2_data = 0x22222222; // 占 8 字节
    virtual void f2() { cout << "    [执行] Base2::f2() \n"; }
    virtual void g2() { cout << "    [执行] Base2::g2() \n"; }
}; // Base2 物理大小: 8(vptr) + 8(data) = 16 字节

class Derived : public Base1, public Base2 {
public:
    int64_t d_data = 0x33333333; // 占 8 字节

    // 重写两个父类的首个函数
    void f1() override { cout << "    [执行] Derived::f1() [重写 Base1::f1] \n"; }
    void f2() override { cout << "    [执行] Derived::f2() [重写 Base2::f2] <--- 注意这里会产生 Thunk!\n"; }

    // 新增子类自己的函数
    virtual void h() { cout << "    [执行] Derived::h()  [子类新增] \n"; }
}; // 预期总大小: 16(Base1) + 16(Base2) + 8(d_data) = 40 字节

// ==========================================
// 主程序：暴力拆解
// ==========================================
int main() {
    Derived d;

    // 获取各层级的指针
    void* p_obj = &d;                             // 对象绝对起点
    Base1* p_base1 = static_cast<Base1*>(&d);       // Base1 视角 (与起点重合)
    Base2* p_base2 = static_cast<Base2*>(&d);       // Base2 视角 (产生偏移)

    cout << "========================================================\n";
    cout << " 1. 对象物理内存布局 (总大小: " << sizeof(Derived) << " 字节)\n";
    cout << "========================================================\n";
    cout << "对象的绝对起点 : " << p_obj << "\n";
    cout << "Base1 子对象起点: " << p_base1 << " (偏移: " << (intptr_t)p_base1 - (intptr_t)p_obj << " 字节)\n";
    cout << "Base2 子对象起点: " << p_base2 << " (偏移: " << (intptr_t)p_base2 - (intptr_t)p_obj << " 字节) <-- 重点!\n\n";

    // 提取两张虚表指针 (vptr)
    intptr_t* vptr1 = *(intptr_t**)p_base1;
    intptr_t* vptr2 = *(intptr_t**)p_base2;

    cout << "========================================================\n";
    cout << " 2. 第一张虚表 (主虚表 / 掌控 Base1 和 Derived)\n";
    cout << "========================================================\n";
    cout << "vtable1[-2] [Offset to top]: " << vptr1[-2] << " 字节\n";

    const type_info* rtti1 = reinterpret_cast<const type_info*>(vptr1[-1]);
    cout << "vtable1[-1] [RTTI Type    ]: " << demangle(rtti1->name()) << "\n\n";

    cout << "--- 主虚函数映射表 (函数实体) ---\n";
    cout << "[0] (覆盖 Base1::f1)  地址: " << (void*)vptr1[0] << "\n";
    ((FuncPtr)vptr1[0])(p_base1); // 传 Base1 指针作为 this

    cout << "[1] (继承 Base1::g1)  地址: " << (void*)vptr1[1] << "\n";
    ((FuncPtr)vptr1[1])(p_base1);

    cout << "[2] (覆盖 Base2::f2)  地址: " << (void*)vptr1[2] << " <-- 优化：主表中存放了 f2 的真实实体代码\n";
    ((FuncPtr)vptr1[2])(p_base1);

    cout << "[3] (新增 Derived::h) 地址: " << (void*)vptr1[3] << "\n";
    ((FuncPtr)vptr1[3])(p_base1);
    cout << "\n";

    cout << "========================================================\n";
    cout << " 3. 第二张虚表 (次虚表 / 专属 Base2 视角)\n";
    cout << "========================================================\n";
    cout << "vtable2[-2] [Offset to top]: " << vptr2[-2] << " 字节 <-- 填补了刚才的 16 字节错位！\n";

    const type_info* rtti2 = reinterpret_cast<const type_info*>(vptr2[-1]);
    cout << "vtable2[-1] [RTTI Type    ]: " << demangle(rtti2->name()) << "\n\n";

    cout << "--- 次虚函数映射表 (包含 Thunk 补丁) ---\n";
    cout << "[0] (Thunk补丁 -> f2) 地址: " << (void*)vptr2[0] << " <-- 注意：地址与主表 [2] 不同！\n";

    // 终极验证：我们传入 Base2 的指针（处于二楼），Thunk 会自动将它减去 16 字节拉回一楼，再执行！
    ((FuncPtr)vptr2[0])(p_base2);

    cout << "[1] (继承 Base2::g2)  地址: " << (void*)vptr2[1] << "\n";
    ((FuncPtr)vptr2[1])(p_base2);

    return 0;
}