#include "test.h"

namespace gtl {

std::vector<TestCase>& Registry() { static std::vector<TestCase> r; return r; }
int&  Failures() { static int f = 0; return f; }
long& Checks()   { static long c = 0; return c; }

Registrar::Registrar(const char* suite, const char* name, void (*fn)()) {
    Registry().push_back({suite, name, fn});
}

int RunAll() {
    for (const auto& t : Registry()) {
        int before = Failures();
        std::printf("[ RUN  ] %s.%s\n", t.suite, t.name);
        t.fn();
        std::printf("%s %s.%s\n", Failures() == before ? "[  OK  ]" : "[ FAIL ]",
                    t.suite, t.name);
    }
    std::printf("\n%ld checks, %d failures\n", Checks(), Failures());
    return Failures() ? 1 : 0;
}

} // namespace gtl

int main() { return ::gtl::RunAll(); }
