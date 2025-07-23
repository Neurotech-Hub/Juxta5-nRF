/*
 * JUXTA File System Test Application
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Test function declarations */
extern int fram_test_main(void);
extern int framfs_test_main(void);

/* Test mode selection */
enum test_mode
{
    TEST_MODE_FRAM_ONLY,
    TEST_MODE_FRAMFS_ONLY,
    TEST_MODE_FULL,
    TEST_MODE_INTERACTIVE
};

#define CURRENT_TEST_MODE TEST_MODE_FULL

/**
 * @brief Display interactive menu and get user selection
 */
static int show_menu(void)
{
    LOG_INF("\n╔══════════════════════════════════════╗");
    LOG_INF("║       JUXTA File System Tests        ║");
    LOG_INF("╠══════════════════════════════════════╣");
    LOG_INF("║ 1. Test FRAM Library                 ║");
    LOG_INF("║ 2. Test File System                  ║");
    LOG_INF("║ 3. Run All Tests                     ║");
    LOG_INF("║ 4. Exit                              ║");
    LOG_INF("╚══════════════════════════════════════╝");
    LOG_INF("Enter selection (1-4):");

    /* In real interactive mode, we'd wait for input here */
    /* For now, default to running all tests */
    return 3;
}

/**
 * @brief Main test suite function
 */
int main(void)
{
    int ret;

    LOG_INF("\n╔══════════════════════════════════════════════════════════════╗");
    LOG_INF("║              JUXTA File System Test Application              ║");
    LOG_INF("║                        Version 1.0.0                         ║");
    LOG_INF("╚══════════════════════════════════════════════════════════════╝\n");

    switch (CURRENT_TEST_MODE)
    {
    case TEST_MODE_FRAM_ONLY:
        LOG_INF("📝 Testing FRAM Library Only...");
        ret = fram_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ FRAM Library test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ FRAM Library test passed");
        break;

    case TEST_MODE_FRAMFS_ONLY:
        LOG_INF("📁 Testing File System Only...");
        ret = framfs_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ File System test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ File System test passed");
        break;

    case TEST_MODE_INTERACTIVE:
        while (1)
        {
            int choice = show_menu();
            if (choice == 1)
            {
                ret = fram_test_main();
                if (ret < 0)
                {
                    LOG_ERR("❌ FRAM Library test failed: %d", ret);
                }
            }
            else if (choice == 2)
            {
                ret = framfs_test_main();
                if (ret < 0)
                {
                    LOG_ERR("❌ File System test failed: %d", ret);
                }
            }
            else if (choice == 3)
            {
                ret = fram_test_main();
                if (ret < 0)
                {
                    LOG_ERR("❌ FRAM Library test failed: %d", ret);
                    break;
                }
                ret = framfs_test_main();
                if (ret < 0)
                {
                    LOG_ERR("❌ File System test failed: %d", ret);
                    break;
                }
                LOG_INF("✅ All tests passed!");
            }
            else if (choice == 4)
            {
                break;
            }
        }
        break;

    case TEST_MODE_FULL:
    default:
        LOG_INF("🚀 Running Full Test Suite");

        LOG_INF("📋 Step 1: FRAM Library Test");
        ret = fram_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ FRAM Library test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ FRAM Library test passed");

        LOG_INF("\n📋 Step 2: File System Test");
        ret = framfs_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ File System test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ File System test passed");

        LOG_INF("\n🎉 All tests completed successfully!");
        break;
    }

    return 0;
}