#include "test_harness.hpp"

std::vector<TestCase> &test_registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

int register_test(const char *name, std::function<void()> fn)
{
    test_registry().push_back({name, std::move(fn)});
    return 0;
}
