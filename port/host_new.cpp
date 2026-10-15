/*
 * OpenJazz CD32X - PC build: C++ new/delete through malloc/free, so the heap count in
 * plat_host.c sees them. (--wrap=malloc only reaches calls made from our own objects;
 * libstdc++'s operator new calls the real malloc, so every `new` was invisible.)
 */
#include <cstdlib>
#include <new>

void *operator new(std::size_t n) { void *p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void *operator new[](std::size_t n) { return operator new(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
