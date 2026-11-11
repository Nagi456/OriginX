// MyClient's own hooks (client/OwnHook.h) on real functions: built with MinGW and run under Wine
// (x86_64-w64-mingw32-g++ ... client/OwnHook.cpp client/vendor/minhook/src/*.c client/vendor/minhook/src/hde/hde64.c).
#ifndef MYCLIENT_HOOK_NO_LEVILAMINA
#define MYCLIENT_HOOK_NO_LEVILAMINA
#endif
#include "../client/OwnHook.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

struct Counter
{
    int base = 10;
    __attribute__((noinline)) int Add(int a, int b) { volatile int r = base; r = r + a + b; return r; }
    __attribute__((noinline)) std::string Name(int n) { return "counter " + std::to_string(n + base); }
};
// long enough for a 5-byte jump (a game function always is; a 4-byte one is refused, tested below)
__attribute__((noinline)) int Twice(int x) { volatile int r = x; r = r * 2; return r; }
__attribute__((noinline)) int Thrice(int x) { volatile int r = x; r = r * 3; return r; }

int (Counter::*addPtr)(int, int) = &Counter::Add;
std::string (Counter::*namePtr)(int) = &Counter::Name;
uintptr_t AddAddress() { return reinterpret_cast<uintptr_t>(MyClientHook::ToPointer(addPtr)); }
uintptr_t NameAddress() { return reinterpret_cast<uintptr_t>(MyClientHook::ToPointer(namePtr)); }

MYCLIENT_TYPE_INSTANCE_HOOK(AddHook, 0, Counter, AddAddress(), int, int a, int b)
{
    return origin(a, b) + 1000 + base;   // `this` is the game's object
}
MYCLIENT_INSTANCE_HOOK(NameHook, 0, NameAddress(), std::string, int n)
{
    return "[" + origin(n) + "]";        // returned object through the hidden pointer
}
MYCLIENT_STATIC_HOOK(TwiceHook, 0, reinterpret_cast<void*>(&Twice), int, int x)
{
    return origin(x) + 1;
}
MYCLIENT_STATIC_HOOK(MissingHook, 0, uintptr_t{0}, int, int x)
{
    return origin(x);
}
MYCLIENT_AUTO_TYPE_INSTANCE_HOOK(ThriceHook, 0, MyClientHook::Empty, reinterpret_cast<void*>(&Thrice), int, int x)
{
    return origin(x) - 1;
}

int main()
{
    // installed at load (static initialisation), like LL_AUTO_*
    assert(ThriceHook::_AutoHookCount == 1);
    int (*volatile thrice)(int) = &Thrice;
    assert(thrice(5) == 14);

    Counter counter;
    int (Counter::*volatile add)(int, int) = &Counter::Add;
    assert((counter.*add)(1, 2) == 13);
    assert(AddHook::hook() == 0);
    assert((counter.*add)(1, 2) == 13 + 1000 + 10);
    counter.base = 20;
    assert((counter.*add)(1, 2) == 23 + 1000 + 20);
    // a second hook on the same function is refused, the first stays
    assert(AddHook::hook() != 0);
    assert((counter.*add)(1, 2) == 23 + 1000 + 20);

    std::string (Counter::*volatile name)(int) = &Counter::Name;
    assert(NameHook::hook() == 0);
    assert((counter.*name)(1) == "[counter 21]");

    int (*volatile twice)(int) = &Twice;
    assert(TwiceHook::hook() == 0);
    assert(twice(4) == 9);

    assert(MissingHook::hook() == -1);
    assert(MyClientHook::Installed() == 4);
    std::printf("own_hook ok (last refusal: %s)\n", MyClientHook::LastError());
    return 0;
}
