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
 * Acer's AcpiBridge sends a 256-byte zero-padded buffer.  The command is
 * stored in the first four bytes:
 *
 *     F0 = left/single RGB zone
 *     F1 = middle RGB zone
 *     F2 = right RGB zone
 *     F4 = hardware brightness
 *     E0 = keyboard enable/disable state
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
 *     echo "frame 255 0 127 4" > /dev/alg_rgb
 *
 * Colors and animation frames are validated inside the kernel.
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
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/workqueue.h>
#include "../include/version.h"

#define DEVICE_NAME "alg_rgb"
#define CLASS_NAME "alg"
#define DEFAULT_TRANSMISSIONS 3
#define MAX_TRANSMISSIONS 5
#define TRANSMISSION_DELAY_MS 40
#define RESUME_REAPPLY_DELAY_MS 1000
#define DCHU_BUFFER_SIZE 256
#define RGB_DSM_FUNCTION 0x67

#define RGB_ZONE_LEFT 0xF0
#define RGB_ZONE_MIDDLE 0xF1
#define RGB_ZONE_RIGHT 0xF2
#define RGB_BRIGHTNESS 0xF4

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
 * The embedded controller occasionally accepts the ACPI method call but
 * drops the RGB transaction, especially after a sleep/wake cycle.  A small,
 * bounded burst of the same known-safe transaction is substantially more
 * reliable than a single transmission.
 */
static unsigned int transmissions = DEFAULT_TRANSMISSIONS;
module_param(transmissions, uint, 0644);
MODULE_PARM_DESC(
    transmissions,
    "Number of complete RGB transactions per request (1-5, default 3)");

static DEFINE_MUTEX(alg_lock);
static const struct alg_color *last_color;
static int last_brightness = 4;
static struct delayed_work resume_work;

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
    {"off", 0x00, 0x00, 0x00},

    {"red", 0x00, 0xFF, 0x00},
    {"orange", 0x7F, 0xFF, 0x00},
    {"yellow", 0xFF, 0xFF, 0x00},

    {"lime", 0xFF, 0x7F, 0x00},
    {"light-green", 0xFF, 0x3F, 0x00},
    {"green", 0xFF, 0x00, 0x00},

    {"green-cyan", 0xFF, 0x00, 0x46},
    {"cyan", 0xFF, 0x00, 0xC8},

    {"light-blue", 0x7F, 0x00, 0xC8},
    {"blue", 0x00, 0x00, 0xC8},

    {"violet", 0x00, 0x7F, 0xC8},
    {"magenta", 0x00, 0xFF, 0xC8},
    {"pink", 0x00, 0xFF, 0x7F},

    {"flesh", 0x7F, 0xFF, 0x3F},

    {"bluish-white", 0x7F, 0x3F, 0x7F},

    {"white", 0xFF, 0xFF, 0xFF},
};

/*
 * Acer firmware brightness levels.
 *
 * Reverse engineered from Windows captures.
 */
static const u8 brightness_levels[] = {
    0x00,
    0x2F,
    0x5F,
    0x8F,
    0xBF};

/*
 * Send one packet to firmware.
 *
 * Acer's InsydeDCHU.dll always submits a 256-byte ACPI buffer, even though
 * only the first four bytes contain the keyboard command.  Sending a
 * four-byte buffer is not equivalent on this firmware and is silently
 * ignored in some controller states.
 *
 * The command is wrapped in an ACPI package and passed to DCHU._DSM()
 * function 0x67, matching Acer's AcpiBridge path.
 */
static int send_packet_once(const u8 packet[4])
{
    union acpi_object *out;
    u8 dchu_buf[DCHU_BUFFER_SIZE] = {0};
    union acpi_object buf_obj;
    union acpi_object pkg_obj;
    union acpi_object pkg_elements[1];

    memcpy(dchu_buf, packet, 4);

    buf_obj.type = ACPI_TYPE_BUFFER;
    buf_obj.buffer.length = sizeof(dchu_buf);
    buf_obj.buffer.pointer = dchu_buf;

    pkg_elements[0] = buf_obj;

    pkg_obj.type = ACPI_TYPE_PACKAGE;
    pkg_obj.package.count = 1;
    pkg_obj.package.elements = pkg_elements;

    out = acpi_evaluate_dsm(
        dchu_handle,
        &rgb_guid,
        0,
        RGB_DSM_FUNCTION,
        &pkg_obj);

    if (!out)
        return -EIO;

    ACPI_FREE(out);

    return 0;
}

/*
 * Apply one complete static-color transaction.
 *
 * Brightness is a hardware command of its own; it must not be implemented by
 * scaling the RGB components.  A single-color request must also update all
 * three possible zones and finish with the controller state command.  This is
 * the sequence used by Acer's keyboard application and by the established
 * Clevo Linux driver for the same CLV0001 interface.
 */
static int send_color_sequence(
    const struct alg_color *c,
    int brightness)
{
    const u8 enabled_packet[4] = {0x01, 0xF0, 0x7F, 0xE0};
    const u8 disabled_packet[4] = {0x01, 0x30, 0x00, 0xE0};
    u8 brightness_packet[4] = {
        brightness_levels[brightness],
        0x00,
        0x00,
        RGB_BRIGHTNESS};
    u8 color_packet[4] = {c->g, c->r, c->b, RGB_ZONE_LEFT};
    bool enable = brightness > 0 && (c->g || c->r || c->b);
    int ret;

    ret = send_packet_once(brightness_packet);
    if (ret)
        return ret;

    color_packet[3] = RGB_ZONE_LEFT;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    color_packet[3] = RGB_ZONE_MIDDLE;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    color_packet[3] = RGB_ZONE_RIGHT;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    return send_packet_once(
        enable ? enabled_packet : disabled_packet);
}

/*
 * Send one low-overhead animation frame.
 *
 * A normal static transaction first enables the controller.  Subsequent
 * frames only need to update the three possible color zones.  Skipping the
 * redundant retries, state packet, and informational log on every frame keeps
 * software-rendered animation responsive and avoids flooding the kernel log.
 */
static int send_frame(
    u8 r,
    u8 g,
    u8 b)
{
    u8 color_packet[4] = {
        g,
        r,
        (u8)(((unsigned int)b * 0xC8) / 0xFF),
        RGB_ZONE_LEFT};
    int ret;

    color_packet[3] = RGB_ZONE_LEFT;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    color_packet[3] = RGB_ZONE_MIDDLE;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    color_packet[3] = RGB_ZONE_RIGHT;
    ret = send_packet_once(color_packet);
    if (ret)
        return ret;

    return 0;
}

/*
 * Send a bounded number of complete transactions.  The firmware does not
 * expose a documented acknowledgement for the RGB controller, so a
 * successful AML evaluation alone cannot tell us whether the EC consumed the
 * transaction.
 */
static int send_color(
    const struct alg_color *c,
    int brightness)
{
    unsigned int attempt;
    unsigned int count = clamp_t(
        unsigned int,
        transmissions,
        1,
        MAX_TRANSMISSIONS);
    int ret = -EIO;
    bool evaluated = false;

    for (attempt = 0; attempt < count; attempt++)
    {
        ret = send_color_sequence(c, brightness);
        if (!ret)
            evaluated = true;

        if (attempt + 1 < count)
            msleep(TRANSMISSION_DELAY_MS);
    }

    if (!evaluated)
    {
        pr_err(
            "alg-rgb: ACPI _DSM failed for color '%s' brightness=%d\n",
            c->name,
            brightness);
        return ret;
    }

    pr_info(
        "alg-rgb: applied color '%s' brightness=%d "
        "(%u complete transactions, Acer 256-byte protocol)\n",
        c->name,
        brightness,
        count);

    return 0;
}

static void alg_resume_work(struct work_struct *work)
{
    int ret;

    mutex_lock(&alg_lock);

    if (!last_color)
        goto out;

    ret = send_color(last_color, last_brightness);
    if (ret)
    {
        pr_err(
            "alg-rgb: failed to restore keyboard state after resume: %d\n",
            ret);
    }
    else
    {
        pr_info("alg-rgb: restored keyboard state after resume\n");
    }

out:
    mutex_unlock(&alg_lock);
}

static int alg_pm_notify(
    struct notifier_block *notifier,
    unsigned long action,
    void *data)
{
    switch (action)
    {
    case PM_POST_SUSPEND:
    case PM_POST_HIBERNATION:
    case PM_POST_RESTORE:
        mod_delayed_work(
            system_wq,
            &resume_work,
            msecs_to_jiffies(RESUME_REAPPLY_DELAY_MS));
        break;
    default:
        break;
    }

    return NOTIFY_OK;
}

static struct notifier_block alg_pm_notifier = {
    .notifier_call = alg_pm_notify,
};

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
 *     frame 255 0 127 4
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
    int brightness = 4;
    char color_name[32];
    char extra;
    int fields;
    int ret;
    unsigned int frame_r;
    unsigned int frame_g;
    unsigned int frame_b;

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

    if (!strncmp(kbuf, "frame ", 6))
    {
        fields = sscanf(
            kbuf,
            "frame %u %u %u %d %c",
            &frame_r,
            &frame_g,
            &frame_b,
            &brightness,
            &extra);

        if (fields != 4 ||
            frame_r > 255 ||
            frame_g > 255 ||
            frame_b > 255 ||
            brightness < 0 ||
            brightness > 4)
        {
            pr_err(
                "alg-rgb: expected: frame <red 0-255> "
                "<green 0-255> <blue 0-255> <brightness 0-4>\n");
            return -EINVAL;
        }

        mutex_lock(&alg_lock);

        ret = send_frame(
            (u8)frame_r,
            (u8)frame_g,
            (u8)frame_b);

        mutex_unlock(&alg_lock);

        if (ret)
            return ret;

        return len;
    }

    fields = sscanf(
        kbuf,
        "%31s %d %c",
        color_name,
        &brightness,
        &extra);

    if (fields == 1 && strcmp(kbuf, color_name))
        return -EINVAL;

    if (fields < 1 || fields > 2)
        return -EINVAL;

    if (brightness < 0 || brightness > 4)
    {
        pr_err(
            "alg-rgb: brightness must be between 0 and 4\n");
        return -EINVAL;
    }

    /*
     * Search color table.
     */
    for (i = 0; i < ARRAY_SIZE(colors); i++)
    {

        if (!strcmp(color_name, colors[i].name))
        {
            mutex_lock(&alg_lock);

            ret = send_color(
                &colors[i],
                brightness);

            if (!ret)
            {
                last_color = &colors[i];
                last_brightness = brightness;
            }

            mutex_unlock(&alg_lock);

            if (ret)
                return ret;

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
    int ret;

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

    INIT_DELAYED_WORK(&resume_work, alg_resume_work);

    ret = register_pm_notifier(&alg_pm_notifier);
    if (ret)
    {
        device_destroy(
            alg_class,
            MKDEV(major, 0));

        class_destroy(alg_class);

        unregister_chrdev(
            major,
            DEVICE_NAME);

        pr_err(
            "alg-rgb: failed to register power notifier: %d\n",
            ret);

        return ret;
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
    unregister_pm_notifier(&alg_pm_notifier);
    cancel_delayed_work_sync(&resume_work);

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
