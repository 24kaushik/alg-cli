// SPDX-License-Identifier: GPL-2.0

/*
 * alg_rgb.c
 *
 * ALG RGB Driver
 * --------------
 *
 * Linux kernel driver for Acer laptop RGB keyboards exposed through
 * the CLV0001 ACPI device.
 *
 * Reverse engineered from Acer's Windows AcpiBridge.sys driver.
 *
 * ---------------------------------------------------------------------------
 * DISCOVERY NOTES
 * ---------------------------------------------------------------------------
 *
 * Windows communicates with the keyboard through:
 *
 *     ACPI Device: CLV0001
 *
 * Linux exposes this device as:
 *
 *     \_SB.DCHU
 *
 * The Windows driver sends a _DSM() call using:
 *
 *     UUID:
 *         93f224e4-fbdc-4bbf-add6-db71bdc0afad
 *
 *     Function:
 *         0x67
 *
 * The payload is a 4-byte buffer:
 *
 *     Byte 0 = Green
 *     Byte 1 = Red
 *     Byte 2 = Blue
 *     Byte 3 = Zone / Command (0xF0)
 *
 * Example:
 *
 *     Red:
 *         00 FF 00 F0
 *
 *     Green:
 *         FF 00 00 F0
 *
 *     Blue:
 *         00 00 FF F0
 *
 *     Yellow:
 *         CC FF 00 F0
 *
 * ---------------------------------------------------------------------------
 * SAFETY
 * ---------------------------------------------------------------------------
 *
 * This driver intentionally uses ONLY the known-safe command:
 *
 *     Function 0x67
 *
 * Other ACPI functions discovered during reverse engineering
 * (0x68, 0x69, 0x6A, etc.) are NOT used.
 *
 * One experimental command was observed to hard-freeze the machine.
 *
 * ---------------------------------------------------------------------------
 * USERSPACE API
 * ---------------------------------------------------------------------------
 *
 * The driver creates:
 *
 *     /dev/alg_rgb
 *
 * Example usage:
 *
 *     echo red    > /dev/alg_rgb
 *     echo green  > /dev/alg_rgb
 *     echo blue   > /dev/alg_rgb
 *     echo yellow > /dev/alg_rgb
 *     echo white  > /dev/alg_rgb
 *
 * Colors are validated inside the kernel.
 *
 * ---------------------------------------------------------------------------
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/uuid.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/string.h>
#include "../include/version.h"

#define DEVICE_NAME "alg_rgb"
#define CLASS_NAME "alg"

/*
 * Character device major number.
 *
 * Assigned dynamically during module load.
 */
static int major;

/*
 * Device class used to create:
 *
 *     /dev/alg_rgb
 */
static struct class *alg_class;

/*
 * Handle to:
 *
 *     \_SB.DCHU
 *
 * The CLV0001 ACPI device.
 */
static acpi_handle dchu_handle;

/*
 * CLV0001 RGB control UUID.
 *
 * Extracted from Acer's Windows AcpiBridge.sys driver.
 */
static guid_t rgb_guid = GUID_INIT(
    0x93f224e4,
    0xfbdc,
    0x4bbf,
    0xad, 0xd6, 0xdb, 0x71, 0xbd, 0xc0, 0xaf, 0xad);

/*
 * Firmware color entry.
 *
 * IMPORTANT:
 *
 * The firmware expects:
 *
 *     G R B
 *
 * NOT:
 *
 *     R G B
 *
 * The values below are stored in firmware order.
 */
struct alg_color
{
    const char *name;

    u8 g;
    u8 r;
    u8 b;
};

/*
 * Known-good colors.
 *
 * Add new colors here as they are discovered.
 *
 * Example:
 *
 *     { "orange", 0x80, 0xFF, 0x00 },
 */
static const struct alg_color colors[] = {
    { "off",            0x00, 0x00, 0x00 },

    { "red",            0x00, 0xFF, 0x00 },
    { "orange",         0x7F, 0xFF, 0x00 },
    { "yellow",         0xFF, 0xFF, 0x00 },

    { "lime",           0xFF, 0x7F, 0x00 },
    { "light-green",    0xFF, 0x3F, 0x00 },
    { "green",          0xFF, 0x00, 0x00 },

    { "green-cyan",     0xFF, 0x00, 0x46 },
    { "cyan",           0xFF, 0x00, 0xC8 },

    { "light-blue",     0x7F, 0x00, 0xC8 },
    { "blue",           0x00, 0x00, 0xC8 },

    { "violet",         0x00, 0x7F, 0xC8 },
    { "magenta",        0x00, 0xFF, 0xC8 },
    { "pink",           0x00, 0xFF, 0x7F },

    { "flesh",          0x7F, 0xFF, 0x3F },

    { "bluish-white",   0x7F, 0x3F, 0x7F },

    { "white",          0xFF, 0xFF, 0xFF },
};

/*
 * Send a color packet to firmware.
 *
 * Packet layout:
 *
 *     [0] Green
 *     [1] Red
 *     [2] Blue
 *     [3] 0xF0
 *
 * The packet is wrapped in an ACPI package and passed to:
 *
 *     DCHU._DSM()
 *
 * using:
 *
 *     Function = 0x67
 */
static void send_color(const struct alg_color *c)
{
    union acpi_object *out;

    u8 rgb_buf[4] = {
        c->g,
        c->r,
        c->b,
        0xF0};

    union acpi_object buf_obj;
    union acpi_object pkg_obj;
    union acpi_object pkg_elements[1];

    buf_obj.type = ACPI_TYPE_BUFFER;
    buf_obj.buffer.length = sizeof(rgb_buf);
    buf_obj.buffer.pointer = rgb_buf;

    pkg_elements[0] = buf_obj;

    pkg_obj.type = ACPI_TYPE_PACKAGE;
    pkg_obj.package.count = 1;
    pkg_obj.package.elements = pkg_elements;

    out = acpi_evaluate_dsm(
        dchu_handle,
        &rgb_guid,
        0,
        0x67,
        &pkg_obj);

    if (out)
        ACPI_FREE(out);

    pr_info("alg-rgb: applied color '%s'\n", c->name);
}

/*
 * Write handler for:
 *
 *     /dev/alg_rgb
 *
 * Expected input:
 *
 *     red
 *     green
 *     blue
 *     yellow
 *     white
 *     off
 *
 * Unknown values are rejected.
 */
static ssize_t alg_write(
    struct file *file,
    const char __user *buf,
    size_t len,
    loff_t *off)
{
    char kbuf[64];
    int i;

    if (len == 0)
        return -EINVAL;

    /*
     * Prevent oversized writes.
     */
    if (len >= sizeof(kbuf))
        len = sizeof(kbuf) - 1;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    /*
     * Remove trailing newline.
     *
     * Example:
     *
     *     echo red > /dev/alg_rgb
     *
     * becomes:
     *
     *     "red"
     */
    strim(kbuf);

    /*
     * Search color table.
     */
    for (i = 0; i < ARRAY_SIZE(colors); i++)
    {

        if (!strcmp(kbuf, colors[i].name))
        {

            send_color(&colors[i]);

            return len;
        }
    }

    pr_err("alg-rgb: unknown color '%s'\n", kbuf);

    return -EINVAL;
}

/*
 * Character device operations.
 */
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = alg_write,
};

/*
 * Module initialization.
 *
 * Steps:
 *
 *   1. Locate \_SB.DCHU
 *   2. Register character device
 *   3. Create /dev/alg_rgb
 */
static int __init alg_init(void)
{
    acpi_status status;

    pr_info("alg-rgb: loading\n");
    pr_info("alg-rgb v%s loaded\n", ALG_RGB_VERSION_STRING);

    /*
     * Locate CLV0001.
     */
    status = acpi_get_handle(
        NULL,
        "\\_SB.DCHU",
        &dchu_handle);

    if (ACPI_FAILURE(status))
    {

        pr_err(
            "alg-rgb: could not locate \\_SB.DCHU\n");

        return -ENODEV;
    }

    major = register_chrdev(
        0,
        DEVICE_NAME,
        &fops);

    if (major < 0)
    {

        pr_err(
            "alg-rgb: failed to register char device\n");

        return major;
    }

    alg_class = class_create(CLASS_NAME);

    if (IS_ERR(alg_class))
    {

        unregister_chrdev(
            major,
            DEVICE_NAME);

        return PTR_ERR(alg_class);
    }

    if (IS_ERR(device_create(
            alg_class,
            NULL,
            MKDEV(major, 0),
            NULL,
            DEVICE_NAME)))
    {

        class_destroy(alg_class);

        unregister_chrdev(
            major,
            DEVICE_NAME);

        return -EINVAL;
    }

    pr_info(
        "alg-rgb: loaded successfully\n");

    pr_info(
        "alg-rgb: device available at /dev/alg_rgb\n");

    return 0;
}

/*
 * Module cleanup.
 */
static void __exit alg_exit(void)
{
    device_destroy(
        alg_class,
        MKDEV(major, 0));

    class_destroy(alg_class);

    unregister_chrdev(
        major,
        DEVICE_NAME);

    pr_info("alg-rgb: unloaded\n");
}

module_init(alg_init);
module_exit(alg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kaushik Sarkar");
MODULE_DESCRIPTION("ALG RGB Driver for Acer CLV0001 keyboards");
MODULE_VERSION(ALG_RGB_VERSION_STRING); 