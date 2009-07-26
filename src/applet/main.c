#include <stdbool.h>
#include <glib.h>
	
int
main (void)
{
	GMainLoop *loop = g_main_new (true);
	g_main_run (loop);

	return 0;
}
