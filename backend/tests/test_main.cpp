#include <cstdio>
#include <exception>

#include "test_harness.hpp"

int main()
{
    int passed = 0;
    int failed = 0;

    for (auto &tc : test_registry())
    {
        try
        {
            tc.fn();
            std::printf("  \033[32m+\033[0m %s\n", tc.name.c_str());
            ++passed;
        }
        catch (const std::exception &e)
        {
            std::printf("  \033[31mx\033[0m %s\n    %s\n", tc.name.c_str(), e.what());
            ++failed;
        }
        catch (...)
        {
            std::printf("  \033[31mx\033[0m %s\n    unknown exception\n", tc.name.c_str());
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed (%d total)\n", passed, failed, passed + failed);
    return failed > 0 ? 1 : 0;
}
