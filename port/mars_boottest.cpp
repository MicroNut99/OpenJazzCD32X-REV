/*
 * OpenJazz CD32X - boot test: the C++ features OpenJazz relies on, checked on the 32X
 * before the game starts (port/plat_mars.c boot_test shows the results).
 */
#include <new>

namespace {
	volatile int ctor_mark;
	struct CtorCheck { CtorCheck() { ctor_mark = 0x0C70; } };
	CtorCheck ctor_check;  // a global object: its constructor must have run before main

	__attribute__((noinline)) int thrower(volatile int v)
	{
		if (v != 0) throw static_cast<int>(v);
		return -1;
	}
}

extern "C" int oj32x_test_ctor(void) { return ctor_mark == 0x0C70; }

extern "C" int oj32x_test_new(void)
{
	int *p = new (std::nothrow) int[256];
	if (!p) return 0;
	for (int i = 0; i < 256; ++i) p[i] = i;
	int ok = p[255] == 255;
	delete[] p;
	return ok;
}

/* 0x0123 = thrown and caught; anything else = the catch did not see the value */
extern "C" int oj32x_test_throw(void)
{
	try {
		return thrower(0x0123);
	} catch (int e) {
		return e;
	}
}

/* ---- what stopped the game, instead of a bare "exit code 1" ---------------------------
 * abort() (exit code 1) comes from std::terminate: an exception nobody caught (OpenJazz only
 * catches int), e.g. std::bad_alloc when memory runs out. Now: running out of memory names the
 * size asked for, and any other uncaught exception names its type - both on the red screen. */
#include <exception>
#include <typeinfo>
#include <cxxabi.h>
#include <cstdio>
#include <cstdlib>

extern "C" void plat_fatal(const char *msg) __attribute__((noreturn));
extern "C" size_t oj32x_heap_used(void);
extern "C" size_t oj32x_heap_size(void);

static void oj32x_terminate()
{
	static char msg[96];
	const std::type_info *t = abi::__cxa_current_exception_type();
	snprintf(msg, sizeof msg, "C++ TERMINATE: UNCAUGHT EXCEPTION %s", t ? t->name() : "(NONE)");
	plat_fatal(msg);
}

extern "C" void oj32x_cxx_handlers(void) { std::set_terminate(oj32x_terminate); }

static void *oj32x_new(std::size_t n)
{
	void *p = std::malloc(n ? n : 1);
	if (!p) {
		static char msg[96];
		snprintf(msg, sizeof msg, "OUT OF MEMORY: NEW OF %u BYTES (HEAP TOP %u OF %u)",
		         (unsigned)n, (unsigned)oj32x_heap_used(), (unsigned)oj32x_heap_size());
		plat_fatal(msg);
	}
	return p;
}
void *operator new(std::size_t n) { return oj32x_new(n); }
void *operator new[](std::size_t n) { return oj32x_new(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return std::malloc(n ? n : 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
