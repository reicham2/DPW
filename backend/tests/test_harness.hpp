#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase> &test_registry();
int register_test(const char *name, std::function<void()> fn);

#define TEST(name)                                              \
    static void test_##name();                                  \
    static int _reg_##name = register_test(#name, test_##name); \
    static void test_##name()

#define ASSERT_EQ(a, b)                                                                                              \
    do                                                                                                               \
    {                                                                                                                \
        auto _a = (a);                                                                                               \
        auto _b = (b);                                                                                               \
        if (!(_a == _b))                                                                                             \
        {                                                                                                            \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_EQ failed"); \
        }                                                                                                            \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                                                                                                                       \
    do                                                                                                                                                                            \
    {                                                                                                                                                                             \
        std::string _a = (a);                                                                                                                                                     \
        std::string _b = (b);                                                                                                                                                     \
        if (_a != _b)                                                                                                                                                             \
        {                                                                                                                                                                         \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_STR_EQ failed:\n  got:      \"" + _a + "\"\n  expected: \"" + _b + "\""); \
        }                                                                                                                                                                         \
    } while (0)

#define ASSERT_TRUE(expr)                                                                                                      \
    do                                                                                                                         \
    {                                                                                                                          \
        if (!(expr))                                                                                                           \
        {                                                                                                                      \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_TRUE failed: " #expr); \
        }                                                                                                                      \
    } while (0)

#define ASSERT_FALSE(expr)                                                                                                      \
    do                                                                                                                          \
    {                                                                                                                           \
        if ((expr))                                                                                                             \
        {                                                                                                                       \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": ASSERT_FALSE failed: " #expr); \
        }                                                                                                                       \
    } while (0)
