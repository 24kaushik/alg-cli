#define _GNU_SOURCE

// SPDX-License-Identifier: GPL-2.0

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../include/version.h"

#ifndef DEVICE_PATH
#define DEVICE_PATH "/dev/alg_rgb"
#endif

#ifndef ANIMATION_STATE_PATH
#define ANIMATION_STATE_PATH "/run/alg-rgb-animation.pid"
#endif
#define DEFAULT_FRAME_DELAY_MS 120
#define MIN_FRAME_DELAY_MS 40
#define MAX_FRAME_DELAY_MS 2000
#define PI 3.14159265358979323846

struct named_color
{
  const char *name;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static const struct named_color animation_colors[] = {
    {"red", 0xFF, 0x00, 0x00},
    {"orange", 0xFF, 0x7F, 0x00},
    {"yellow", 0xFF, 0xFF, 0x00},
    {"lime", 0x7F, 0xFF, 0x00},
    {"light-green", 0x3F, 0xFF, 0x00},
    {"green", 0x00, 0xFF, 0x00},
    {"green-cyan", 0x00, 0xFF, 0x59},
    {"cyan", 0x00, 0xFF, 0xFF},
    {"light-blue", 0x00, 0xA0, 0xFF},
    {"blue", 0x00, 0x00, 0xFF},
    {"violet", 0xA0, 0x00, 0xFF},
    {"magenta", 0xFF, 0x00, 0xFF},
    {"pink", 0xFF, 0x00, 0xA0},
    {"flesh", 0xFF, 0xA0, 0x50},
    {"bluish-white", 0x80, 0xA0, 0xFF},
    {"white", 0xFF, 0xFF, 0xFF},
};

static volatile sig_atomic_t animation_running = 1;

static void print_version(void)
{
  printf("alg-rgb %s\n", ALG_RGB_VERSION_STRING);
}

static int parse_long_range(
    const char *value,
    long minimum,
    long maximum,
    long *parsed_value)
{
  char *end;
  long parsed;

  errno = 0;
  end = NULL;
  parsed = strtol(value, &end, 10);

  if (errno || !end || *end != '\0' ||
      parsed < minimum || parsed > maximum)
  {
    return -1;
  }

  *parsed_value = parsed;
  return 0;
}

static const struct named_color *find_animation_color(const char *name)
{
  size_t i;

  for (i = 0; i < sizeof(animation_colors) / sizeof(animation_colors[0]); i++)
  {
    if (!strcmp(name, animation_colors[i].name))
      return &animation_colors[i];
  }

  return NULL;
}

static void usage(const char *prog)
{
  printf("ALG RGB Keyboard Controller\n\n");

  printf("Usage:\n");
  printf("  %s <color> [brightness]\n", prog);
  printf("  %s animate wave [frame-delay-ms]\n", prog);
  printf("  %s animate rainbow [frame-delay-ms]\n", prog);
  printf("  %s animate pulse <color> [frame-delay-ms]\n", prog);
  printf("  %s status\n", prog);
  printf("  %s stop\n\n", prog);

  printf("Colors:\n");
  printf("  red, orange, yellow, lime, light-green, green\n");
  printf("  green-cyan, cyan, light-blue, blue, violet\n");
  printf("  magenta, pink, flesh, bluish-white, white, off\n\n");

  printf("Brightness:\n");
  printf("  0 = off, 1 = low, 2 = medium, 3 = high, 4 = maximum\n\n");

  printf("Background animations:\n");
  printf("  wave     color and brightness flow together\n");
  printf("  rainbow  continuous full-brightness color cycle\n");
  printf("  pulse    breathe a selected color\n\n");

  printf("Examples:\n");
  printf("  %s pink 2\n", prog);
  printf("  %s animate wave\n", prog);
  printf("  %s animate rainbow 80\n", prog);
  printf("  %s animate pulse pink 100\n", prog);
  printf("  %s pink 2    (stops animation and sets pink)\n", prog);
  printf("  %s off       (stops animation and turns lights off)\n\n", prog);

  printf("Options:\n");
  printf("  -h, --help       Show this help message\n");
  printf("  --version        Show version information\n");
}

static int open_device(void)
{
  int fd = open(DEVICE_PATH, O_WRONLY);

  if (fd >= 0)
    return fd;

  if (errno == ENOENT)
  {
    fprintf(
        stderr,
        "alg-rgb: %s is missing; try: sudo modprobe alg_rgb\n",
        DEVICE_PATH);
  }
  else if (errno == EACCES)
  {
    fprintf(
        stderr,
        "alg-rgb: permission denied for %s; "
        "reinstall the udev rule or run with sudo\n",
        DEVICE_PATH);
  }
  else
  {
    perror("open(/dev/alg_rgb)");
  }

  return -1;
}

static int write_command(int fd, const char *command)
{
  size_t length = strlen(command);
  ssize_t written;

  do
  {
    written = write(fd, command, length);
  } while (written < 0 && errno == EINTR && animation_running);

  if (written < 0)
  {
    if (errno != EINTR)
      perror("write");
    return -1;
  }

  if ((size_t)written != length)
  {
    fprintf(stderr, "alg-rgb: incomplete write to keyboard driver\n");
    return -1;
  }

  return 0;
}

static void handle_animation_signal(int signal_number)
{
  (void)signal_number;
  animation_running = 0;
}

static int open_animation_state(void)
{
  int fd = open(
      ANIMATION_STATE_PATH,
      O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
      0644);

  if (fd < 0)
    perror("open(/run/alg-rgb-animation.pid)");

  return fd;
}

static int read_animation_state(
    int fd,
    pid_t *pid,
    char *description,
    size_t description_size)
{
  char state[192];
  ssize_t count;
  long parsed_pid;
  char *end;
  char *details;

  count = pread(fd, state, sizeof(state) - 1, 0);
  if (count <= 0)
    return -1;

  state[count] = '\0';
  errno = 0;
  parsed_pid = strtol(state, &end, 10);
  if (errno || end == state || parsed_pid <= 1)
    return -1;

  *pid = (pid_t)parsed_pid;

  while (*end == ' ')
    end++;

  details = end;
  details[strcspn(details, "\r\n")] = '\0';

  if (description && description_size)
  {
    snprintf(description, description_size, "%s", details);
  }

  return 0;
}

static int stop_animation_daemon(int report)
{
  struct timespec wait_time = {
      .tv_sec = 0,
      .tv_nsec = 25000000L};
  char description[128] = "";
  pid_t pid;
  int fd;
  int attempt;

  fd = open_animation_state();
  if (fd < 0)
    return -1;

  if (flock(fd, LOCK_EX | LOCK_NB) == 0)
  {
    ftruncate(fd, 0);
    if (report)
      printf("No background keyboard animation is running.\n");
    close(fd);
    return 0;
  }

  if (errno != EWOULDBLOCK && errno != EAGAIN)
  {
    perror("flock");
    close(fd);
    return -1;
  }

  if (read_animation_state(
          fd,
          &pid,
          description,
          sizeof(description)))
  {
    fprintf(stderr, "alg-rgb: invalid animation state file\n");
    close(fd);
    return -1;
  }

  if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
  {
    perror("kill(animation)");
    close(fd);
    return -1;
  }

  for (attempt = 0; attempt < 80; attempt++)
  {
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
    {
      ftruncate(fd, 0);
      if (report)
      {
        printf(
            "Stopped background animation%s%s.\n",
            description[0] ? ": " : "",
            description);
      }
      close(fd);
      return 0;
    }

    nanosleep(&wait_time, NULL);
  }

  fprintf(stderr, "alg-rgb: animation did not stop within two seconds\n");
  close(fd);
  return -1;
}

static int print_animation_status(void)
{
  char description[128] = "";
  pid_t pid;
  int fd;

  fd = open_animation_state();
  if (fd < 0)
    return 1;

  if (flock(fd, LOCK_EX | LOCK_NB) == 0)
  {
    ftruncate(fd, 0);
    close(fd);
    printf("No background keyboard animation is running.\n");
    return 0;
  }

  if (errno != EWOULDBLOCK && errno != EAGAIN)
  {
    perror("flock");
    close(fd);
    return 1;
  }

  if (read_animation_state(
          fd,
          &pid,
          description,
          sizeof(description)))
  {
    fprintf(stderr, "alg-rgb: invalid animation state file\n");
    close(fd);
    return 1;
  }

  printf(
      "Background keyboard animation is running "
      "(PID %ld%s%s).\n",
      (long)pid,
      description[0] ? ": " : "",
      description);

  close(fd);
  return 0;
}

static void sleep_frame(long delay_ms)
{
  struct timespec remaining = {
      .tv_sec = delay_ms / 1000,
      .tv_nsec = (delay_ms % 1000) * 1000000L};

  while (animation_running &&
         nanosleep(&remaining, &remaining) < 0 &&
         errno == EINTR)
  {
  }
}

static void hsv_to_rgb(
    double hue,
    double value,
    uint8_t *red,
    uint8_t *green,
    uint8_t *blue)
{
  double chroma = value;
  double section = hue / 60.0;
  double x = chroma * (1.0 - fabs(fmod(section, 2.0) - 1.0));
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;

  if (section < 1.0)
  {
    r = chroma;
    g = x;
  }
  else if (section < 2.0)
  {
    r = x;
    g = chroma;
  }
  else if (section < 3.0)
  {
    g = chroma;
    b = x;
  }
  else if (section < 4.0)
  {
    g = x;
    b = chroma;
  }
  else if (section < 5.0)
  {
    r = x;
    b = chroma;
  }
  else
  {
    r = chroma;
    b = x;
  }

  *red = (uint8_t)lround(r * 255.0);
  *green = (uint8_t)lround(g * 255.0);
  *blue = (uint8_t)lround(b * 255.0);
}

static int run_animation(
    int fd,
    const char *mode,
    const struct named_color *pulse_color,
    long frame_delay_ms,
    int ready_fd)
{
  struct sigaction action = {
      .sa_handler = handle_animation_signal};
  unsigned long frame = 0;
  char command[64];

  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);

  /*
   * A normal reliable transaction enables the controller. Animation frames
   * can then use the faster frame-only kernel path.
   */
  if (write_command(fd, "blue 4"))
  {
    if (ready_fd >= 0)
      write(ready_fd, "1", 1);
    return 1;
  }

  if (ready_fd >= 0)
  {
    write(ready_fd, "0", 1);
    close(ready_fd);
  }

  while (animation_running)
  {
    double phase = (double)(frame % 60) * (2.0 * PI / 60.0);
    double value = 1.0;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    int length;

    if (!strcmp(mode, "wave"))
    {
      double hue = fmod((double)frame * 6.0, 360.0);
      value = 0.18 + 0.82 * ((sin(phase) + 1.0) / 2.0);
      hsv_to_rgb(hue, value, &red, &green, &blue);
    }
    else if (!strcmp(mode, "rainbow"))
    {
      double hue = fmod((double)frame * 6.0, 360.0);
      hsv_to_rgb(hue, 1.0, &red, &green, &blue);
    }
    else
    {
      value = 0.12 + 0.88 * ((sin(phase) + 1.0) / 2.0);
      red = (uint8_t)lround((double)pulse_color->r * value);
      green = (uint8_t)lround((double)pulse_color->g * value);
      blue = (uint8_t)lround((double)pulse_color->b * value);
    }

    length = snprintf(
        command,
        sizeof(command),
        "frame %u %u %u 4",
        red,
        green,
        blue);

    if (length < 0 || (size_t)length >= sizeof(command))
    {
      fprintf(stderr, "alg-rgb: failed to encode animation frame\n");
      return 1;
    }

    if (write_command(fd, command))
      return 1;

    frame++;
    sleep_frame(frame_delay_ms);
  }

  return 0;
}

static int start_animation_daemon(
    const char *mode,
    const struct named_color *pulse_color,
    long frame_delay_ms)
{
  char ready_status;
  char description[128];
  int ready_pipe[2];
  pid_t child;
  ssize_t count;

  if (stop_animation_daemon(0))
    return 1;

  if (pipe2(ready_pipe, O_CLOEXEC) < 0)
  {
    perror("pipe2");
    return 1;
  }

  child = fork();
  if (child < 0)
  {
    perror("fork");
    close(ready_pipe[0]);
    close(ready_pipe[1]);
    return 1;
  }

  if (child == 0)
  {
    const char *color_name = pulse_color ? pulse_color->name : "-";
    int state_fd;
    int device_fd;
    int null_fd;
    int result;

    close(ready_pipe[0]);

    if (setsid() < 0)
    {
      write(ready_pipe[1], "1", 1);
      _exit(1);
    }

    signal(SIGHUP, SIG_IGN);

    state_fd = open_animation_state();
    if (state_fd < 0 ||
        flock(state_fd, LOCK_EX | LOCK_NB) < 0 ||
        ftruncate(state_fd, 0) < 0 ||
        dprintf(
            state_fd,
            "%ld %s color=%s delay=%ldms\n",
            (long)getpid(),
            mode,
            color_name,
            frame_delay_ms) < 0)
    {
      write(ready_pipe[1], "1", 1);
      _exit(1);
    }

    device_fd = open_device();
    if (device_fd < 0)
    {
      write(ready_pipe[1], "1", 1);
      _exit(1);
    }

    null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0)
    {
      dup2(null_fd, STDIN_FILENO);
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
      if (null_fd > STDERR_FILENO)
        close(null_fd);
    }

    result = run_animation(
        device_fd,
        mode,
        pulse_color,
        frame_delay_ms,
        ready_pipe[1]);

    close(device_fd);
    close(state_fd);
    _exit(result);
  }

  close(ready_pipe[1]);

  do
  {
    count = read(ready_pipe[0], &ready_status, 1);
  } while (count < 0 && errno == EINTR);

  close(ready_pipe[0]);

  if (count != 1 || ready_status != '0')
  {
    fprintf(stderr, "alg-rgb: failed to start background animation\n");
    return 1;
  }

  snprintf(
      description,
      sizeof(description),
      "%s%s%s, %ld ms",
      mode,
      pulse_color ? ", " : "",
      pulse_color ? pulse_color->name : "",
      frame_delay_ms);

  printf("Started background animation: %s.\n", description);
  return 0;
}

static int handle_animation_command(int argc, char **argv)
{
  const char *mode;
  const struct named_color *pulse_color = NULL;
  long frame_delay_ms = DEFAULT_FRAME_DELAY_MS;

  if (argc < 3 || argc > 5)
  {
    usage(argv[0]);
    return 1;
  }

  mode = argv[2];

  if (!strcmp(mode, "pulse"))
  {
    if (argc < 4)
    {
      fprintf(stderr, "alg-rgb: pulse requires a color\n");
      return 1;
    }

    pulse_color = find_animation_color(argv[3]);
    if (!pulse_color)
    {
      fprintf(stderr, "alg-rgb: unknown animation color '%s'\n", argv[3]);
      return 1;
    }

    if (argc == 5 &&
        parse_long_range(
            argv[4],
            MIN_FRAME_DELAY_MS,
            MAX_FRAME_DELAY_MS,
            &frame_delay_ms))
    {
      fprintf(
          stderr,
          "Frame delay must be between %d and %d milliseconds\n",
          MIN_FRAME_DELAY_MS,
          MAX_FRAME_DELAY_MS);
      return 1;
    }
  }
  else if (!strcmp(mode, "wave") || !strcmp(mode, "rainbow"))
  {
    if (argc == 4 &&
        parse_long_range(
            argv[3],
            MIN_FRAME_DELAY_MS,
            MAX_FRAME_DELAY_MS,
            &frame_delay_ms))
    {
      fprintf(
          stderr,
          "Frame delay must be between %d and %d milliseconds\n",
          MIN_FRAME_DELAY_MS,
          MAX_FRAME_DELAY_MS);
      return 1;
    }

    if (argc == 5)
    {
      usage(argv[0]);
      return 1;
    }
  }
  else
  {
    fprintf(stderr, "alg-rgb: unknown animation '%s'\n", mode);
    return 1;
  }

  return start_animation_daemon(
      mode,
      pulse_color,
      frame_delay_ms);
}

int main(int argc, char **argv)
{
  int fd;
  char command[64];
  int command_length;
  long brightness = 4;
  int result;

  if (argc == 2)
  {
    if (!strcmp(argv[1], "--version"))
    {
      print_version();
      return 0;
    }

    if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))
    {
      usage(argv[0]);
      return 0;
    }

    if (!strcmp(argv[1], "status"))
      return print_animation_status();

    if (!strcmp(argv[1], "stop"))
      return stop_animation_daemon(1) ? 1 : 0;
  }

  if (argc >= 2 &&
      (!strcmp(argv[1], "animate") || !strcmp(argv[1], "effect")))
  {
    return handle_animation_command(argc, argv);
  }

  if (argc != 2 && argc != 3)
  {
    usage(argv[0]);
    return 1;
  }

  if (argc == 3 &&
      parse_long_range(argv[2], 0, 4, &brightness))
  {
    fprintf(stderr, "Brightness must be between 0 and 4\n");
    return 1;
  }

  command_length = snprintf(
      command,
      sizeof(command),
      "%s %ld",
      argv[1],
      brightness);

  if (command_length < 0 ||
      (size_t)command_length >= sizeof(command))
  {
    fprintf(stderr, "alg-rgb: command is too long\n");
    return 1;
  }

  if (stop_animation_daemon(0))
    return 1;

  fd = open_device();
  if (fd < 0)
    return 1;

  result = write_command(fd, command) ? 1 : 0;
  close(fd);

  return result;
}
