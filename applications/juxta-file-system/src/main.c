/*
 * JUXTA File System Test Application
 *
 * Copyright (c) 2024 NeurotechHub
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Forward declarations */
extern int fram_test_main(void);
extern int framfs_test_main(void);

/* Test mode selection */
enum test_mode
{
    TEST_MODE_FRAM_ONLY,   /* Test FRAM library only */
    TEST_MODE_FRAMFS_ONLY, /* Test file system only */
    TEST_MODE_FULL,        /* Test both in sequence */
    TEST_MODE_INTERACTIVE  /* Interactive menu */
};

/* Configure which test to run */
#define CURRENT_TEST_MODE TEST_MODE_FULL

static void print_banner(void)
{
    printk("\n");
    printk("╔══════════════════════════════════════════════════════════════╗\n");
    printk("║              JUXTA File System Test Application              ║\n");
    printk("║                        Version %s                         ║\n", APP_VERSION_STRING);
    printk("╠══════════════════════════════════════════════════════════════╣\n");
    printk("║  Tests:                                                      ║\n");
    printk("║  • FRAM Library (juxta_fram)                                ║\n");
    printk("║  • File System (juxta_framfs)                               ║\n");
    printk("║                                                              ║\n");
    printk("║  Board: Juxta5-1_ADC                                        ║\n");
    printk("║  FRAM:  MB85RS1MTPW-G-APEWE1 (1Mbit)                        ║\n");
    printk("╚══════════════════════════════════════════════════════════════╝\n");
    printk("\n");
}

static void run_interactive_menu(void)
{
    printk("🎯 Interactive Test Menu:\n");
    printk("  1. FRAM Library Test Only\n");
    printk("  2. File System Test Only  \n");
    printk("  3. Full Test Suite\n");
    printk("  4. Continuous Testing\n");
    printk("\n");
    printk("💡 To change test mode, modify CURRENT_TEST_MODE in main.c\n");
    printk("🔄 Running full test suite by default...\n\n");
}

int main(void)
{
    int ret;

    print_banner();

    switch (CURRENT_TEST_MODE)
    {
    case TEST_MODE_FRAM_ONLY:
        LOG_INF("🧪 Running FRAM Library Test Only");
        ret = fram_test_main();
        break;

    case TEST_MODE_FRAMFS_ONLY:
        LOG_INF("🗂️  Running File System Test Only");
        ret = framfs_test_main();
        break;

    case TEST_MODE_FULL:
        LOG_INF("🚀 Running Full Test Suite");

        LOG_INF("📋 Step 1: FRAM Library Test");
        ret = fram_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ FRAM Library test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ FRAM Library test completed successfully\n");

        k_sleep(K_SECONDS(2));

        LOG_INF("📋 Step 2: File System Test");
        ret = framfs_test_main();
        if (ret < 0)
        {
            LOG_ERR("❌ File System test failed: %d", ret);
            return ret;
        }
        LOG_INF("✅ File System test completed successfully");
        break;

    case TEST_MODE_INTERACTIVE:
        run_interactive_menu();
        ret = fram_test_main();
        if (ret == 0)
        {
            k_sleep(K_SECONDS(1));
            ret = framfs_test_main();
        }
        break;

    default:
        LOG_ERR("❌ Invalid test mode: %d", CURRENT_TEST_MODE);
        return -1;
    }

    if (ret == 0)
    {
        LOG_INF("🎉 All tests completed successfully!");
        printk("\n");
        printk("╔══════════════════════════════════════════════════════════════╗\n");
        printk("║                        TEST RESULTS                         ║\n");
        printk("║                                                              ║\n");
        printk("║  ✅ FRAM Library:    PASSED                                 ║\n");
        printk("║  ✅ File System:     PASSED                                 ║\n");
        printk("║                                                              ║\n");
        printk("║  🎯 Ready for application development!                      ║\n");
        printk("╚══════════════════════════════════════════════════════════════╝\n");
    }
    else
    {
        LOG_ERR("❌ Test suite failed with error: %d", ret);
    }

    return ret;
}