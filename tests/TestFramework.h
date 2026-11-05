#pragma once
// Mini marco de pruebas sin dependencias (compartido por todos los ficheros de pruebas).
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace solo_test
{
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline std::vector<std::pair<std::string, std::function<void()>>>& registry()
{
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Reg { Reg (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };

// argv[1] opcional: ejecuta solo las pruebas cuyo nombre contenga ese texto.
inline int runAll (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& [name, fn] : registry())
    {
        if (filter && std::strstr (name.c_str(), filter) == nullptr) continue;
        const int before = failures();
        std::printf ("[ RUN  ] %s\n", name.c_str());
        std::fflush (stdout);
        fn();
        std::printf ("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", name.c_str());
        ++ran;
    }
    std::printf ("\n%d pruebas, %d comprobaciones, %d fallos\n", ran, checks(), failures());
    return failures() == 0 ? 0 : 1;
}
} // namespace solo_test

#define TEST(name) static void name(); static solo_test::Reg reg_##name (#name, name); static void name()
#define CHECK(cond) do { ++solo_test::checks(); if (! (cond)) { ++solo_test::failures(); std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, msg) do { ++solo_test::checks(); if (! (cond)) { ++solo_test::failures(); std::printf ("  FALLO %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, std::string (msg).c_str()); } } while (0)
