// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host-side baseline, stored as a flat key=value file under
 * $XDG_CONFIG_HOME/alloyctl/<vid>-<pid>.conf.
 *
 * Core owns the file, not its contents:
 * it writes the header, hands the driver an emit callback, and on the way back
 * offers the driver every line it finds.
 * Keys nobody claims are ignored, so file written by newer build still loads
 * on an older one.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "state.h"

static int state_path(const struct alloy_driver *drv, char *buf, size_t len,
		      int create_dirs)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char dir[PATH_MAX];
	int n;

	if (xdg && *xdg)
		n = snprintf(dir, sizeof(dir), "%s/alloyctl", xdg);
	else if (home && *home)
		n = snprintf(dir, sizeof(dir), "%s/.config/alloyctl", home);
	else
		return -1;
	if (n < 0 || (size_t)n >= sizeof(dir))
		return -1;

	if (create_dirs && mkdir(dir, 0755) && errno != EEXIST)
		return -1;

	n = snprintf(buf, len, "%s/%04x-%04x.conf", dir, drv->vendor_id,
		     drv->product_id);
	if (n < 0 || (size_t)n >= len)
		return -1;
	return 0;
}

/*
 * Load the stored baseline into @cfg.
 * Returns 0 when a file was read,
 * 1 when there is none (factory defaults are left in place)
 * and -1 when the path could not be resolved.
 */
int alloy_state_load(const struct alloy_driver *drv, struct alloy_config *cfg)
{
	char path[PATH_MAX];
	char line[512];
	char *eq;
	FILE *f;

	alloy_config_defaults(drv, cfg);

	if (state_path(drv, path, sizeof(path), 0))
		return -1;

	f = fopen(path, "re");
	if (!f)
		return 1;

	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		if (drv->ops && drv->ops->state_load)
			drv->ops->state_load(drv, cfg, line, eq + 1);
	}
	fclose(f);

	if (drv->ops && drv->ops->state_done)
		drv->ops->state_done(drv, cfg);
	return 0;
}

static void state_emit(void *ctx, const char *key, const char *val)
{
	fprintf((FILE *)ctx, "%s=%s\n", key, val ? val : "");
}

int alloy_state_store(const struct alloy_driver *drv,
		      const struct alloy_config *cfg)
{
	char path[PATH_MAX];
	FILE *f;

	if (!drv->ops || !drv->ops->state_save)
		return 0;
	if (state_path(drv, path, sizeof(path), 1))
		return -1;

	f = fopen(path, "we");
	if (!f)
		return -1;

	fprintf(f, "# alloyctl baseline for %s\n", drv->name);
	drv->ops->state_save(drv, cfg, f, state_emit);
	fclose(f);
	return 0;
}
