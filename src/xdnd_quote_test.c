#include "wconfig.h"

#ifdef USE_DOCK_XDND
#include <X11/Xlib.h>
#include <WINGs/WUtil.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>

#include "xdnd.h"

int main(void)
{
	const char *first = "/tmp/a \"quoted\" \\ name";
	const char *second = "/tmp/second file";
	char command[256] = "launcher --";
	char **argv;
	int argc;

	wXDNDAppendQuoted(command + strlen(command), first);
	wXDNDAppendQuoted(command + strlen(command), second);
	wtokensplit(command, &argv, &argc);
	assert(argc == 4);
	assert(strcmp(argv[2], first) == 0);
	assert(strcmp(argv[3], second) == 0);
	wtokenfree(argv, argc);
	return 0;
}
#else
int main(void)
{
	return 0;
}
#endif
