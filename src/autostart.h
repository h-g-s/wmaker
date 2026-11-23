/* autostart.h - commands launched when Window Maker starts
 *
 *  Window Maker window manager
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef WMAUTOSTART_H_
#define WMAUTOSTART_H_

#include "screen.h"

/* Launch the enabled AutostartCommands items without waiting for them. */
void wAutostartRun(WScreen *scr, WMPropList *dict, Bool restarting);

/* Name used to detect whether a command is already running; caller frees. */
char *wAutostartProcessName(const char *command);

/* Whether a NUL-separated /proc cmdline contains (^|/)name([[:space:]]|$). */
Bool wAutostartCmdlineMatches(const char *cmdline, size_t length, const char *name);

#endif
