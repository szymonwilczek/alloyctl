// SPDX-License-Identifier: GPL-2.0-only
/*
 * Pointer-transform daemon.
 *
 * Acceleration, deceleration and angle snapping are not firmware features of any
 * supported mouse.
 *
 * This process does it, below the display server so it works identically on X11
 * and every Wayland compositor:
 *
 *   open the mouse's motion evdev node -> EVIOCGRAB (exclusive) ->
 *   transform REL_X/REL_Y (accel_transform.c) -> re-emit through uinput
 *   virtual pointer that mirrors the real device (buttons, wheel, ... untouched).
 *
 * Kernel drops the grab automatically when the fd closes or the process dies,
 * so crash never leaves the physical mouse unusable.
 *
 * One daemon per device, guarded by a pidfile; SIGHUP re-reads the config
 * (live edits from the TUI), SIGTERM/SIGINT tear down cleanly.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include "accel.h"
#include "alloy.h"
#include "driver.h"
#include "state.h"

#define LONG_BITS (8 * (int)sizeof(long))
#define NLONGS(n) (((n) + LONG_BITS - 1) / LONG_BITS)
#define BIT_TEST(arr, bit) \
	((arr)[(bit) / LONG_BITS] & (1UL << ((bit) % LONG_BITS)))

static volatile sig_atomic_t g_reload;
static volatile sig_atomic_t g_quit;

static void on_reload(int sig)
{
	(void)sig;
	g_reload = 1;
}

static void on_quit(int sig)
{
	(void)sig;
	g_quit = 1;
}

static int accel_runtime_dir(char *buf, size_t len)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	int n;

	if (run && *run)
		n = snprintf(buf, len, "%s/alloyctl", run);
	else
		n = snprintf(buf, len, "/tmp/alloyctl-%u", (unsigned)getuid());
	return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

static int accel_pidfile(uint16_t vid, uint16_t pid, char *buf, size_t len)
{
	char dir[256];
	int n;

	if (accel_runtime_dir(dir, sizeof(dir)))
		return -1;
	n = snprintf(buf, len, "%s/accel-%04x-%04x.pid", dir, vid, pid);
	return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

int alloy_accel_pid(uint16_t vid, uint16_t pid)
{
	char path[320];
	FILE *f;
	int running = -1;
	int p;

	if (accel_pidfile(vid, pid, path, sizeof(path)))
		return -1;
	f = fopen(path, "re");
	if (!f)
		return -1;
	if (fscanf(f, "%d", &p) == 1 && p > 0 && kill((pid_t)p, 0) == 0)
		running = p;
	fclose(f);
	return running;
}

int alloy_accel_is_running(uint16_t vid, uint16_t pid)
{
	return alloy_accel_pid(vid, pid) > 0;
}

static int accel_signal(uint16_t vid, uint16_t pid, int sig)
{
	int p = alloy_accel_pid(vid, pid);

	if (p <= 0)
		return -1;
	return kill((pid_t)p, sig) == 0 ? 0 : -1;
}

int alloy_accel_reload(uint16_t vid, uint16_t pid)
{
	return accel_signal(vid, pid, SIGHUP);
}

int alloy_accel_stop(uint16_t vid, uint16_t pid)
{
	return accel_signal(vid, pid, SIGTERM);
}

int alloy_accel_spawn(uint16_t vid, uint16_t pid)
{
	char arg[16];
	pid_t child;
	int i;

	if (alloy_accel_is_running(vid, pid))
		return 0;
	snprintf(arg, sizeof(arg), "%04x:%04x", vid, pid);

	child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		pid_t grandchild;
		int null;

		setsid(); /* detach from the TUI's session/terminal */
		grandchild = fork();
		if (grandchild < 0)
			_exit(127);
		if (grandchild > 0)
			_exit(0);
		null = open("/dev/null", O_RDWR | O_CLOEXEC);
		if (null >= 0) {
			dup2(null, 0);
			dup2(null, 1);
			dup2(null, 2);
		}
		execl("/proc/self/exe", "alloyctl", "--accel-daemon", arg,
		      (char *)NULL);
		_exit(127);
	}
	waitpid(child, NULL, 0); /* reap the intermediate fork */

	/* wait briefly for the daemon to come up and write its pidfile */
	for (i = 0; i < 50 && !alloy_accel_is_running(vid, pid); i++)
		usleep(10000);
	return alloy_accel_is_running(vid, pid) ? 0 : -1;
}

static int autostart_path(uint16_t vid, uint16_t pid, char *dir, size_t dlen,
			  char *file, size_t flen)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	int n;

	if (xdg && *xdg)
		n = snprintf(dir, dlen, "%s/autostart", xdg);
	else if (home && *home)
		n = snprintf(dir, dlen, "%s/.config/autostart", home);
	else
		return -1;
	if (n < 0 || (size_t)n >= dlen)
		return -1;
	n = snprintf(file, flen, "%s/alloyctl-accel-%04x-%04x.desktop", dir,
		     vid, pid);
	return (n < 0 || (size_t)n >= flen) ? -1 : 0;
}

int alloy_accel_autostart_set(uint16_t vid, uint16_t pid, int enable)
{
	char dir[PATH_MAX];
	char file[PATH_MAX];
	char exe[PATH_MAX];
	ssize_t n;
	FILE *f;

	if (autostart_path(vid, pid, dir, sizeof(dir), file, sizeof(file)))
		return -1;
	if (!enable) {
		unlink(file);
		return 0;
	}

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n < 0)
		return -1;
	exe[n] = '\0';

	if (mkdir(dir, 0755) && errno != EEXIST)
		return -1;
	f = fopen(file, "we");
	if (!f)
		return -1;
	fprintf(f,
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=alloyctl pointer transform (%04x:%04x)\n"
		"Comment=Host-side acceleration/deceleration/angle snapping\n"
		"Exec=%s --accel-daemon %04x:%04x\n"
		"X-GNOME-Autostart-enabled=true\n"
		"NoDisplay=true\n",
		vid, pid, exe, vid, pid);
	fclose(f);
	return 0;
}

static int accel_write_pidfile(uint16_t vid, uint16_t pid)
{
	char dir[256];
	char path[320];
	FILE *f;

	if (accel_runtime_dir(dir, sizeof(dir)))
		return -1;
	if (mkdir(dir, 0700) && errno != EEXIST)
		return -1;
	if (accel_pidfile(vid, pid, path, sizeof(path)))
		return -1;
	f = fopen(path, "we");
	if (!f)
		return -1;
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
	return 0;
}

static void accel_remove_pidfile(uint16_t vid, uint16_t pid)
{
	char path[320];

	if (!accel_pidfile(vid, pid, path, sizeof(path)))
		unlink(path);
}

static int read_hex_file(const char *path, unsigned *out)
{
	FILE *f = fopen(path, "re");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%x", out) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

/* node whose capabilities/rel advertises both REL_X and REL_Y is the pointer */
static int node_has_rel_xy(const char *evname)
{
	char path[512];
	unsigned long words[8] = { 0 };
	unsigned long low;
	int n = 0;
	FILE *f;

	snprintf(path, sizeof(path),
		 "/sys/class/input/%s/device/capabilities/rel", evname);
	f = fopen(path, "re");
	if (!f)
		return 0;
	while (n < 8 && fscanf(f, "%lx", &words[n]) == 1)
		n++;
	fclose(f);
	if (n == 0)
		return 0;
	/* sysfs prints most-significant word first
	 * REL_X/REL_Y live in the last */
	low = words[n - 1];
	return (low & (1UL << REL_X)) && (low & (1UL << REL_Y));
}

static int find_event_node(uint16_t vid, uint16_t pid, char *node, size_t len)
{
	struct dirent *ent;
	DIR *dir;
	int found = -1;

	dir = opendir("/sys/class/input");
	if (!dir)
		return -1;

	while ((ent = readdir(dir))) {
		char base[288];
		char vpath[320];
		char ppath[320];
		unsigned v;
		unsigned p;

		if (strncmp(ent->d_name, "event", 5))
			continue;
		snprintf(base, sizeof(base), "/sys/class/input/%s/device/id",
			 ent->d_name);
		snprintf(vpath, sizeof(vpath), "%s/vendor", base);
		snprintf(ppath, sizeof(ppath), "%s/product", base);
		if (read_hex_file(vpath, &v) || read_hex_file(ppath, &p))
			continue;
		if (v != vid || p != pid)
			continue;
		if (!node_has_rel_xy(ent->d_name))
			continue;
		snprintf(node, len, "/dev/input/%s", ent->d_name);
		found = 0;
		break;
	}
	closedir(dir);
	return found;
}

static void mirror_codes(int src, int uinp, int type, int max, int ui_bit)
{
	unsigned long bits[NLONGS(KEY_MAX + 1)];
	int code;

	memset(bits, 0, sizeof(bits));
	if (ioctl(src, EVIOCGBIT(type, sizeof(bits)), bits) < 0)
		return;
	for (code = 0; code <= max; code++) {
		if (!BIT_TEST(bits, code))
			continue;
		ioctl(uinp, UI_SET_EVBIT, type);
		ioctl(uinp, ui_bit, code);
		if (type == EV_ABS) {
			struct uinput_abs_setup abs = { 0 };

			abs.code = (uint16_t)code;
			if (ioctl(src, EVIOCGABS(code), &abs.absinfo) == 0)
				ioctl(uinp, UI_ABS_SETUP, &abs);
		}
	}
}

static int make_uinput(int src, const char *drv_name)
{
	struct uinput_setup setup = { 0 };
	struct input_id id = { 0 };
	int fd;

	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;

	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	mirror_codes(src, fd, EV_KEY, KEY_MAX, UI_SET_KEYBIT);
	mirror_codes(src, fd, EV_REL, REL_MAX, UI_SET_RELBIT);
	mirror_codes(src, fd, EV_ABS, ABS_MAX, UI_SET_ABSBIT);
	mirror_codes(src, fd, EV_MSC, MSC_MAX, UI_SET_MSCBIT);

	if (ioctl(src, EVIOCGID, &id) == 0)
		setup.id = id;
	snprintf(setup.name, sizeof(setup.name),
		 "alloyctl virtual pointer (%.40s)",
		 drv_name ? drv_name : "mouse");

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 ||
	    ioctl(fd, UI_DEV_CREATE) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int accel_setup(uint16_t vid, uint16_t pid, const char *drv_name,
		       int *src_out, int *uinp_out)
{
	char node[288];
	int src;
	int uinp;

	if (find_event_node(vid, pid, node, sizeof(node)))
		return -1;
	src = open(node, O_RDONLY | O_CLOEXEC);
	if (src < 0)
		return -1;
	/* create sink first, then grab to minimise leaked events */
	uinp = make_uinput(src, drv_name);
	if (uinp < 0) {
		close(src);
		return -1;
	}
	if (ioctl(src, EVIOCGRAB, 1) < 0) {
		ioctl(uinp, UI_DEV_DESTROY);
		close(uinp);
		close(src);
		return -1;
	}
	*src_out = src;
	*uinp_out = uinp;
	return 0;
}

static void accel_teardown(int src, int uinp)
{
	if (uinp >= 0) {
		ioctl(uinp, UI_DEV_DESTROY);
		close(uinp);
	}
	if (src >= 0)
		close(src); /* drops EVIOCGRAB */
}

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev = { 0 };

	ev.type = (uint16_t)type;
	ev.code = (uint16_t)code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) {
		/* sink went away
		 * nothing useful to do mid-batch */
	}
}

/* One read/transform/emit pass.
 * Returns 0 normally, -1 if the source vanished */
static int accel_pump(int src, int uinp, const struct alloy_accel_params *p,
		      struct alloy_accel_state *st)
{
	struct input_event evs[64];
	ssize_t n;
	size_t i;
	int dx = 0;
	int dy = 0;

	n = read(src, evs, sizeof(evs));
	if (n < 0)
		return (errno == EINTR || errno == EAGAIN) ? 0 : -1;
	if (n == 0)
		return -1;

	for (i = 0; i < (size_t)n / sizeof(evs[0]); i++) {
		struct input_event *e = &evs[i];

		if (e->type == EV_REL && e->code == REL_X) {
			dx += e->value;
		} else if (e->type == EV_REL && e->code == REL_Y) {
			dy += e->value;
		} else if (e->type == EV_SYN && e->code == SYN_REPORT) {
			int ox;
			int oy;

			alloy_accel_transform(p, st, dx, dy, &ox, &oy);
			if (ox)
				emit(uinp, EV_REL, REL_X, ox);
			if (oy)
				emit(uinp, EV_REL, REL_Y, oy);
			emit(uinp, EV_SYN, SYN_REPORT, 0);
			dx = 0;
			dy = 0;
		} else {
			/* buttons, wheel, MSC_SCAN, ... pass through untouched */
			emit(uinp, e->type, e->code, e->value);
		}
	}
	return 0;
}

static void load_params(const struct alloy_driver *drv,
			struct alloy_accel_params *p)
{
	struct alloy_config cfg;

	alloy_state_load(drv, &cfg);
	alloy_accel_from_config(&cfg, p);
}

int alloy_accel_daemon_run(uint16_t vid, uint16_t pid)
{
	const struct alloy_driver *drv = alloy_driver_find(vid, pid);
	struct alloy_accel_params params;
	struct alloy_accel_state st = { 0 };
	struct sigaction sa = { 0 };
	int retries;
	int src = -1;
	int uinp = -1;

	if (!drv) {
		fprintf(stderr,
			"alloyctl: %04x:%04x is not a supported mouse\n", vid,
			pid);
		return 1;
	}
	if (accel_setup(vid, pid, drv->name, &src, &uinp)) {
		fprintf(stderr,
			"alloyctl: cannot grab %04x:%04x motion device "
			"(is it connected, and do you have access to "
			"/dev/input and /dev/uinput?)\n",
			vid, pid);
		return 1;
	}

	sa.sa_handler = on_quit;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sa.sa_handler = on_reload;
	sigaction(SIGHUP, &sa, NULL);

	load_params(drv, &params);
	accel_write_pidfile(vid, pid);

	while (!g_quit) {
		struct pollfd pfd = { .fd = src, .events = POLLIN };
		int r = poll(&pfd, 1, -1);

		if (g_reload) {
			g_reload = 0;
			load_params(drv, &params);
		}
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (!(pfd.revents & POLLIN))
			continue;

		if (accel_pump(src, uinp, &params, &st) == 0)
			continue;

		/* source vanished (unplug): retry with backoff, then give up */
		accel_teardown(src, uinp);
		src = uinp = -1;
		for (retries = 0; retries < 30 && !g_quit; retries++) {
			sleep(1);
			if (accel_setup(vid, pid, drv->name, &src, &uinp) == 0)
				break;
		}
		if (src < 0)
			break;
		memset(&st, 0, sizeof(st));
	}

	accel_teardown(src, uinp);
	accel_remove_pidfile(vid, pid);
	return 0;
}
