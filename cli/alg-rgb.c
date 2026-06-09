// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/version.h"

static void print_version(void)
{
  printf("alg-rgb %s\n", ALG_RGB_VERSION_STRING);
}

static void usage(const char *prog)
{
  printf("Usage:\n");
  printf("  %s red\n", prog);
  printf("  %s orange\n", prog);
  printf("  %s yellow\n", prog);
  printf("  %s lime\n", prog);
  printf("  %s light-green\n", prog);
  printf("  %s green\n", prog);
  printf("  %s green-cyan\n", prog);
  printf("  %s cyan\n", prog);
  printf("  %s light-blue\n", prog);
  printf("  %s blue\n", prog);
  printf("  %s violet\n", prog);
  printf("  %s magenta\n", prog);
  printf("  %s pink\n", prog);
  printf("  %s flesh\n", prog);
  printf("  %s bluish-white\n", prog);
  printf("  %s white\n", prog);
  printf("  %s off\n", prog);
}

int main(int argc, char **argv)
{
  int fd;

  if (argc != 2)
  {
    usage(argv[0]);
    return 1;
  }

  if (argc == 2 && strcmp(argv[1], "--version") == 0)
  {
    print_version();
    return 0;
  }

  fd = open("/dev/alg_rgb", O_WRONLY);

  if (fd < 0)
  {
    perror("open(/dev/alg_rgb)");
    return 1;
  }

  if (write(fd, argv[1], strlen(argv[1])) < 0)
  {
    perror("write");
    close(fd);
    return 1;
  }

  close(fd);
  return 0;
}