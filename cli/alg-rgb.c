// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "../include/version.h"

static void print_version(void)
{
  printf("alg-rgb %s\n", ALG_RGB_VERSION_STRING);
}

static void usage(const char *prog)
{
  printf("ALG RGB Keyboard Controller\n\n");

  printf("Usage:\n");
  printf("  %s <color>\n", prog);
  printf("  %s <color> <brightness>\n\n", prog);

  printf("Colors:\n");
  printf("  red\n");
  printf("  orange\n");
  printf("  yellow\n");
  printf("  lime\n");
  printf("  light-green\n");
  printf("  green\n");
  printf("  green-cyan\n");
  printf("  cyan\n");
  printf("  light-blue\n");
  printf("  blue\n");
  printf("  violet\n");
  printf("  magenta\n");
  printf("  pink\n");
  printf("  flesh\n");
  printf("  bluish-white\n");
  printf("  white\n");
  printf("  off\n\n");

  printf("Brightness:\n");
  printf("  0 = off\n");
  printf("  1 = low\n");
  printf("  2 = medium\n");
  printf("  3 = high\n");
  printf("  4 = maximum\n\n");

  printf("Examples:\n");
  printf("  %s red\n", prog);
  printf("  %s red 4\n", prog);
  printf("  %s cyan 2\n", prog);
  printf("  %s white 1\n", prog);

  printf("\n");
  printf("Options:\n");
  printf("  -h, --help       Show this help message\n");
  printf("  --version        Show version information\n");
}

int main(int argc, char **argv)
{
  int fd;
  char buffer[64];

  if (argc == 2)
  {
    if (!strcmp(argv[1], "--version"))
    {
      print_version();
      return 0;
    }

    if (!strcmp(argv[1], "--help") ||
        !strcmp(argv[1], "-h"))
    {
      usage(argv[0]);
      return 0;
    }
  }

  if (argc != 2 && argc != 3)
  {
    usage(argv[0]);
    return 1;
  }

  if (argc == 2)
  {
    /*
     * Backwards compatible:
     *
     *     alg-rgb red
     *
     * Defaults to brightness level 4.
     */
    snprintf(
        buffer,
        sizeof(buffer),
        "%s 4",
        argv[1]);
  }
  else
  {
    int brightness = atoi(argv[2]);

    if (brightness < 0 || brightness > 4)
    {
      fprintf(
          stderr,
          "Brightness must be between 0 and 4\n");
      return 1;
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "%s %d",
        argv[1],
        brightness);
  }

  fd = open("/dev/alg_rgb", O_WRONLY);

  if (fd < 0)
  {
    perror("open(/dev/alg_rgb)");
    return 1;
  }

  if (write(fd, buffer, strlen(buffer)) < 0)
  {
    perror("write");
    close(fd);
    return 1;
  }

  close(fd);

  return 0;
}