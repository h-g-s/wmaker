/* Desktop-entry matching for dock launchers. */
#ifndef WM_DESKTOP_ENTRY_H
#define WM_DESKTOP_ENTRY_H

char *wDesktopEntryCommand(const char *application_id, const char *instance, const char *class_name);
char *wDesktopEntryResolve(const char *application_id, const char *instance, const char *class_name);
int wDesktopCommandExecutable(const char *command);

#endif
