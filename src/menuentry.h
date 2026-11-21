#ifndef WM_MENUENTRY_H
#define WM_MENUENTRY_H

#include <string.h>
#include <WINGs/WUtil.h>

typedef struct {
	const char *title;
	const char *icon;
	const char *shortcut;
	const char *command;
	const char *parameter;
} WMenuEntrySpec;

static inline Bool wMenuParseEntry(WMPropList *item, WMenuEntrySpec *spec)
{
	int i, count;
	WMPropList *value;

	memset(spec, 0, sizeof(*spec));
	if (!item || !WMIsPLArray(item))
		return False;
	count = WMGetPropListItemCount(item);
	if (count < 2)
		return False;
	value = WMGetFromPLArray(item, 0);
	if (!WMIsPLString(value))
		return False;
	spec->title = WMGetFromPLString(value);

	for (i = 1; i < count; i++) {
		const char *token;
		const char **option;

		value = WMGetFromPLArray(item, i);
		if (!WMIsPLString(value))
			return False;
		token = WMGetFromPLString(value);
		if (strcmp(token, "ICON") == 0)
			option = &spec->icon;
		else if (strcmp(token, "SHORTCUT") == 0)
			option = &spec->shortcut;
		else {
			spec->command = token;
			break;
		}
		if (*option || ++i >= count)
			return False;
		value = WMGetFromPLArray(item, i);
		if (!WMIsPLString(value))
			return False;
		*option = WMGetFromPLString(value);
	}
	if (!spec->command)
		return False;
	if (++i < count) {
		value = WMGetFromPLArray(item, i);
		if (!WMIsPLString(value))
			return False;
		spec->parameter = WMGetFromPLString(value);
	}
	return i + 1 >= count;
}

#endif
