// SPDX-FileCopyrightText: 2026 Ian Pilcher <arequipeno@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <linux/uinput.h>


static const char DEFAULT_FIFO_PATH[] = "/run/kbdsync/kbdsync.fifo";
static const int DEFAULT_DELAY_MSEC = 3000;

static const char *fifo_path = DEFAULT_FIFO_PATH;
static bool debug = false;
static bool use_syslog = false;
static int delay_msec = DEFAULT_DELAY_MSEC;


[[gnu::format(printf, 2, 3)]]
static void msg(int level, const char *format, ...)
{
	static const char level_names[][sizeof "EMERGENCY"] = {
		[LOG_DEBUG]	= "DEBUG",
		[LOG_INFO]	= "INFO",
		[LOG_NOTICE]	= "NOTICE",
		[LOG_WARNING]	= "WARNING",
		[LOG_ERR]	= "ERROR",
		[LOG_CRIT]	= "CRITICAL",
		[LOG_ALERT]	= "ALERT",
		[LOG_EMERG]	= "EMERGENCY"  // longest name
	};

	va_list ap;
	int save_errno;

	assert(LOG_EMERG <= level && level <= LOG_DEBUG);

	if (!debug && level == LOG_DEBUG)
		return;

	va_start(ap);

	if (use_syslog) {
		vsyslog(level, format, ap);
	}
	else {
		save_errno = errno;
		fprintf(stderr, "%s: ", level_names[level]);
		errno = save_errno;
		vfprintf(stderr, format, ap);
		fputc('\n', stderr);
	}

	va_end(ap);
}

#define DEBUG(format, ...)	msg(LOG_DEBUG, format, ##__VA_ARGS__)
#define INFO(format, ...)	msg(LOG_INFO, format, ##__VA_ARGS__)
#define NOTICE(format, ...)	msg(LOG_NOTICE, format, ##__VA_ARGS__)
#define WARN(format, ...)	msg(LOG_WARNING, format, ##__VA_ARGS__)
#define ERR(format, ...)	msg(LOG_ERR, format, ##__VA_ARGS__)
#define CRIT(format, ...)	msg(LOG_CRIT, format, ##__VA_ARGS__)
#define ALERT(format, ...)	msg(LOG_ALERT, format, ##__VA_ARGS__)
#define EMERG(format, ...)	msg(LOG_EMERG, format, ##__VA_ARGS__)

#define FATAL(format, ...)	\
	do { ERR(format, ##__VA_ARGS__); exit(1); } while (0)

#define ABORT(format, ...)	\
	do { CRIT(format, ##__VA_ARGS__); abort(); } while (0)


static const char *tname(int type)
{
	switch (type) {
		case EV_KEY:	return "EV_KEY";
		case EV_SYN:	return "EV_SYN";
		default:	abort();
	}
}

static const char *cname(int code)
{
	switch (code) {
		case SYN_REPORT:	return "SYN_REPORT";	// 0
		case KEY_NUMLOCK:	return "KEY_NUMLOCK";	// 69
		case KEY_CAPSLOCK:	return "KEY_CAPSLOCK";	// 58
		default:		abort();
	}
}

static void emit(int uinput_fd, int type, int code, int value)
{
	struct input_event ie = {
		.type = type,
		.code = code,
		.value = value
	};

	if (write(uinput_fd, &ie, sizeof ie) < 0) {
		FATAL("write: /dev/uinput: %s: %s: %d: %m",
		      tname(type), cname(code), value);
	}
}

static void keypress(int uinput_fd, int code)
{
	emit(uinput_fd, EV_KEY, code, 1);
	emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
	emit(uinput_fd, EV_KEY, code, 0);
	emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

static void drain(int fd)
{
	char sink[4096];
	int pending;

	while (1) {

		if (ioctl(fd, FIONREAD, &pending) < 0)
			FATAL("ioctl: %s: FIONREAD: %m", fifo_path);
		if (pending == 0)
			return;
		if (read(fd, sink, sizeof sink) < 0)
			FATAL("read: %s: %m", fifo_path);
	}
}

struct option {
	const char *name;
	void (*parsefn)(const char *value, struct option *opt);
	void *context;
	bool has_value;
	bool seen;
};

static const char usage[] =
	"Usage: kbdsync [options]\n"
	"\n"
	"Options:\n"
	"  --debug       enable debug messages\n"
	"  --stderr      log to stderr, even if not a tty\n"
	"  --syslog      log to syslog, even if stderr is a tty\n"
	"  --delay MSEC  delay before syncing LEDs (default: %d milliseconds)\n"
	"  --fifo PATH   FIFO to listen on (default: %s)\n";

[[noreturn]] static void help(const char *, struct option *)
{
	printf(usage, DEFAULT_DELAY_MSEC, DEFAULT_FIFO_PATH);
	exit(0);
}

static void parse_bool(const char *, struct option *opt)
{
	*(bool *)opt->context = true;
}

static void parse_fifo(const char *path, struct option *)
{
	fifo_path = path;
}

static void parse_delay(const char *value, struct option *)
{
	unsigned long milliseconds;
	char *endptr;

	if (strcmp(value, "0") == 0) {
		delay_msec = 0;
		return;
	}

	if (*value < '1' || *value > '9')
		FATAL("Invalid delay: '%s'", value);

	milliseconds = strtoul(value, &endptr, 10);
	if (*endptr != 0)
		FATAL("Invalid delay: '%s'", value);

	if (milliseconds > 60000)
		FATAL("Delay too large (max 60000): %s", value);

	delay_msec = (int)milliseconds;
}

static void parse_args(int, char **argv)
{
	bool force_stderr = false;
	bool force_syslog = false;

	struct option opts[] = {
		{ "--debug", parse_bool, &debug, false, false },
		{ "--stderr", parse_bool, &force_stderr, false, false },
		{ "--syslog", parse_bool, &force_syslog, false, false },
		{ "--fifo", parse_fifo, nullptr, true, false },
		{ "--delay", parse_delay, nullptr, true, false },
		{ "--help", help, nullptr, false, false },
		{}
	};

	char **argp;
	const char *value;
	struct option *opt;

	setlinebuf(stderr);

	for (argp = argv + 1; *argp != nullptr; ++argp) {

		for (opt = opts; opt->name != nullptr; ++opt) {
			if (strcmp(opt->name, *argp) != 0)
				continue;
			if (opt->seen)
				FATAL("Duplicate option: %s", *argp);
			opt->seen = true;
			value = *(argp + 1);
			if (opt->has_value && value == nullptr)
				FATAL("Option value missing: %s", *argp);
			opt->parsefn(*(argp + 1), opt);
			argp += opt->has_value;
			break;
		}

		if (opt->name == nullptr)
			FATAL("Unknown option: '%s'", *argp);
	}

	if (force_stderr && force_syslog)
		FATAL("Mutually exclusive options: '--stderr' and '--syslog'");

	if (force_stderr) {
		use_syslog = false;
		DEBUG("Logging to stderr (explicit)");
	}
	else if (force_syslog) {
		use_syslog = true;
		DEBUG("Logging to syslog (explicit)");
	}
	else {
		use_syslog = !isatty(STDERR_FILENO);
		DEBUG("Logging to %s (automatic)",
		      use_syslog ? "syslog" : "stderr");
	}

	DEBUG("Debug messages enabled");  // Only logged if they actually are
	DEBUG("FIFO path: '%s'", fifo_path);
	DEBUG("Delay: %d milliseconds", delay_msec);
}

int main(int argc, char **argv)
{
	struct uinput_setup usetup = {
		.name		= "kvm-switch keyboard sync",
		.id.bustype	= BUS_VIRTUAL
	};
	int fifo_fd, uinput_fd;
	char c;

	parse_args(argc, argv);

	if ((fifo_fd = open(fifo_path, O_RDWR | O_CLOEXEC)) < 0)
		FATAL("open: %s: %m", fifo_path);

	if ((uinput_fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC)) < 0)
		FATAL("open: /dev/uinput: %m");

	if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0)
		FATAL("ioctl: /dev/uinput: UI_SET_EVBIT: EV_KEY: %m");

	if (ioctl(uinput_fd, UI_SET_KEYBIT, KEY_NUMLOCK) < 0)
		FATAL("ioctl: /dev/uinput: UI_SET_KEYBIT, KEY_NUMLOCK: %m");
	if (ioctl(uinput_fd, UI_SET_KEYBIT, KEY_CAPSLOCK) < 0)
		FATAL("ioctl: /dev/uinput: UI_SET_KEYBIT, KEY_CAPSLOCK: %m");

	if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0)
		FATAL("ioctl: /dev/uinput: UI_DEV_SETUP: %m");
	if (ioctl(uinput_fd, UI_DEV_CREATE) < 0)
		FATAL("ioctl: /dev/uinput: UI_DEV_CREATE: %m");

	INFO("Listening on %s ...", fifo_path);

	while (1) {

		if (read(fifo_fd, &c, 1) < 0)
			FATAL("read: %s: %m", fifo_path);

		INFO("Got trigger");

		DEBUG("Waiting %d milliseconds ...", delay_msec);

		if (poll(nullptr, 0, delay_msec) < 0)
			FATAL("poll: %d milliseconds: %m", delay_msec);

		//if (usleep((useconds_t)(delay_seconds * 1000000)) != 0)
		//	FATAL("usleep: %u seconds: %m", delay_seconds);

		DEBUG("Syncing LEDs");

		keypress(uinput_fd, KEY_NUMLOCK);
		keypress(uinput_fd, KEY_CAPSLOCK);

		if (poll(nullptr, 0, 100) < 0)
			FATAL("poll: %d milliseconds: %m", delay_msec);

		//if (usleep(100000) < 0)
		//	FATAL("usleep: 100000 (1/10 second): %m");

		keypress(uinput_fd, KEY_NUMLOCK);
		keypress(uinput_fd, KEY_CAPSLOCK);

		drain(fifo_fd);
	}

	if (ioctl(uinput_fd, UI_DEV_DESTROY) < 0)
		FATAL("ioctl: /dev/uinput: UI_DEV_DESTROY: %m");

	if (close(uinput_fd) != 0)
		FATAL("close: /dev/uinput: %m");

	if (close(fifo_fd) != 0)
		FATAL("close: %s: %m", fifo_path);

	return 0;
}
