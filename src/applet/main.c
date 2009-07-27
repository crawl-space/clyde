#include <stdio.h>

#include <linux/joystick.h>

#include <glib.h>

GIOChannel *
get_joystick (void)
{
	int fd = open ("/dev/input/js0");
	GIOChannel *js = g_io_channel_unix_new (fd);

	return js;
}

gboolean
js_data_available (GIOChannel *js, GIOCondition cond, gpointer *data)
{
	struct js_event event;

	ssize_t len = read (g_io_channel_unix_get_fd (js), &event,
			    sizeof (event));

	if (len == sizeof (event)) {
		printf ("data :: v - %d t - %d n - %d\n", event.value,
			event.type, event.number);
	}

	return TRUE;
}

int
main (void)
{
	GMainLoop *loop = g_main_new (TRUE);

	GIOChannel *js = get_joystick ();

	g_io_add_watch (js, G_IO_IN, (GIOFunc) js_data_available, NULL);
	g_main_run (loop);

	return 0;
}
