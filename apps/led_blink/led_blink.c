/*
 * led_blink.c
 *
 *  Created on: Oct 13, 2026
 *      Author: shaman
 *
 * led_blink.c - blink an external LED on GPIO17 using the GPIO sysfs interface
 *
 * Wiring: GPIO17 (pin 11) -> 330 ohm -> LED anode, LED cathode -> GND (pin 9)
 * Stop with Ctrl+C: the LED is switched off and the GPIO released.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>

#define BCM_GPIO   17       /* BCM GPIO number (header pin 11) */
#define ON_MS      500      /* LED on time in milliseconds */
#define OFF_MS     500      /* LED off time in milliseconds */

static volatile sig_atomic_t running = 1;

/* Called on Ctrl+C (SIGINT) or kill (SIGTERM) */
static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* Write a string to a sysfs file */
static int write_str(const char *path, const char *value)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return -1;
    }
    int ok = fputs(value, f) >= 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        perror(path);
    return ok ? 0 : -1;
}

/* Read the first line of a sysfs file */
static int read_str(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)len, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

/* Find the sysfs base of the BCM2835 GPIO controller (512 on recent kernels) */
static int find_gpio_base(void)
{
    DIR *dir = opendir("/sys/class/gpio");
    struct dirent *entry;
    char path[256], text[64];
    int base = -1;

    if (!dir) {
        perror("/sys/class/gpio");
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "gpiochip", 8) != 0)
            continue;
        snprintf(path, sizeof(path), "/sys/class/gpio/%s/label", entry->d_name);
        if (read_str(path, text, sizeof(text)) == 0 && strstr(text, "bcm2835")) {
            snprintf(path, sizeof(path), "/sys/class/gpio/%s/base", entry->d_name);
            if (read_str(path, text, sizeof(text)) == 0)
                base = atoi(text);
            break;
        }
    }
    closedir(dir);
    return base;
}

int main(void)
{
    char path[128], num[16];

    int base = find_gpio_base();
    if (base < 0) {
        fprintf(stderr, "BCM2835 GPIO controller not found in /sys/class/gpio\n");
        return 1;
    }
    int gpio = base + BCM_GPIO;
    printf("GPIO%d -> sysfs gpio%d (base %d)\n", BCM_GPIO, gpio, base);

    /* 1. Export the pin, unless it is already exported */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
    if (access(path, F_OK) != 0) {
        snprintf(num, sizeof(num), "%d", gpio);
        if (write_str("/sys/class/gpio/export", num) < 0)
            return 1;
        usleep(100000);     /* let the kernel create the gpioN folder */
    }

    /* 2. Set the pin as output */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    if (write_str(path, "out") < 0)
        return 1;

    /* 3. Stop cleanly on Ctrl+C or kill */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 4. Blink */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    printf("Blinking - press Ctrl+C to stop\n");
    while (running) {
        write_str(path, "1");           /* LED on  */
        usleep(ON_MS * 1000);
        if (!running)
            break;
        write_str(path, "0");           /* LED off */
        usleep(OFF_MS * 1000);
    }

    /* 5. Clean up: LED off, release the pin */
    write_str(path, "0");
    snprintf(num, sizeof(num), "%d", gpio);
    write_str("/sys/class/gpio/unexport", num);
    printf("\nLED off, GPIO released\n");
    return 0;
}
