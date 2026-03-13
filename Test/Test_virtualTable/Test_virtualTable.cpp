#include <iostream>
#include <typeinfo> // 用于解析 RTTI
#include <cstdint>
using namespace std;

// 为了安全调用类成员函数，定义带 this 指针参数的函数指针
typedef void (*FuncPtr)(void*);

class Base1 {
public:
    int b1_data = 0x11;
    virtual void f1() { cout << "  [执行] Base1::f1()" << endl; }
};

class Base2 {
public:
    int b2_data = 0x22;
    virtual void f2() { cout << "  [执行] Base2::f2()" << endl; }
};

// 多重继承：Derived 继承自 Base1 和 Base2
class Derived : public Base1, public Base2 {
public:
    int d_data = 0x33;

    // 重写两个父类的虚函数
    void f1() override { cout << "  [执行] Derived::f1() [重写 Base1]" << endl; }
    void f2() override { cout << "  [执行] Derived::f2() [重写 Base2]" << endl; }

    // 子类独有的新虚函数
    virtual void d_func() { cout << "  [执行] Derived::d_func() [子类新增]" << endl; }
};

int main() {
    Derived d;

    // ==========================================
    // 1. 抓取第一个虚表指针 (主虚表，继承自 Base1)
    // ==========================================
    intptr_t* vptr1 = *(intptr_t**)(&d);

    cout << "====== 第一张虚表 (主虚表 / Base1 视角) ======\n";
    cout << "vptr1 地址: " << vptr1 << "\n";

    // 解析 vtable1[-2]: Offset to top
    // Base1 是第一个基类，它的起点就是 Derived 的起点，偏移量为 0
    intptr_t offset1 = vptr1[-2];
    cout << "vtable1[-2] [Offset to top]: " << offset1 << " (字节)\n";

    // 解析 vtable1[-1]: RTTI Type Info
    const type_info* rtti1 = reinterpret_cast<const type_info*>(vptr1[-1]);
    cout << "vtable1[-1] [RTTI Type Name]: " << rtti1->name() << "\n";

    cout << "--- 虚函数映射 ---\n";
    cout << "vtable1[0] (应为 Derived::f1): " << (void*)vptr1[0] << "\n";
    ((FuncPtr)vptr1[0])(&d); // 传入 &d 作为 this 指针

    cout << "vtable1[1] (应为 Derived::d_func): " << (void*)vptr1[1] << "\n";
    ((FuncPtr)vptr1[1])(&d);

    cout << "\n";


    // ==========================================
    // 2. 抓取第二个虚表指针 (次虚表，继承自 Base2)
    // ==========================================
    // 在内存模型中，Base2 的子对象紧挨在 Base1 子对象之后。
    // 我们将 Derived 指针安全转为 Base2 指针，编译器会自动加上偏移量。
    Base2* ptr_b2 = &d;
    intptr_t* vptr2 = *(intptr_t**)(ptr_b2);

    cout << "====== 第二张虚表 (次虚表 / Base2 视角) ======\n";
    cout << "Base2 子对象内存地址: " << ptr_b2 << " (比 Derived 起点多了 "
        << (intptr_t)ptr_b2 - (intptr_t)&d << " 字节)\n";
    cout << "vptr2 地址: " << vptr2 << "\n";

    // 解析 vtable2[-2]: Offset to top （见证奇迹的时刻）
    // Base2 子对象的 vptr 距离 Derived 对象顶部的负偏移量！
    intptr_t offset2 = vptr2[-2];
    cout << "vtable2[-2] [Offset to top]: " << offset2 << " (字节) <-- 注意这个负数！\n";

    // 解析 vtable2[-1]: RTTI Type Info
    // 即使是次虚表，RTTI 依然精准指向了实际的最底层子类 Derived
    const type_info* rtti2 = reinterpret_cast<const type_info*>(vptr2[-1]);
    cout << "vtable2[-1] [RTTI Type Name]: " << rtti2->name() << "\n";

    cout << "--- 虚函数映射 ---\n";
    cout << "vtable2[0] (应为 Derived::f2 的 Thunk): " << (void*)vptr2[0] << "\n";
    // 注意：这里传入的是 Base2 的起始地址作为 this 指针，底层 Thunk 会自动减去偏移量修正回 Derived
    ((FuncPtr)vptr2[0])(ptr_b2);

    return 0;
}