#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <linux/joystick.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>

GIOChannel *
get_joystick (void)
{
	int fd = open ("/dev/input/js0", O_RDONLY);
	GIOChannel *js = g_io_channel_unix_new (fd);

	return js;
}

static int
get_active_window (Display *display, int root_window)
{
	int status;

	Atom actual_type;
	int actual_format;
	long nitems;
	long bytes;
	long *data;

        status = XGetWindowProperty(display, root_window,
				    XInternAtom(display, "_NET_ACTIVE_WINDOW",
						TRUE),
				    0, (~0L), False, AnyPropertyType,
				    &actual_type, &actual_format, &nitems,
				    &bytes, (unsigned char**)&data);

	return data[0];
}

static void
send_key (char key, gboolean press)
{
	Display *display = XOpenDisplay (NULL);
	int screen = DefaultScreen (display);
	int root_window = RootWindow (display, screen);
	int window = get_active_window (display, root_window);
	XEvent event;

	memset (&event, 0, sizeof (event));
	event.xkey.root = root_window;
	event.xkey.window = window;
	event.xkey.subwindow = 0x0;
	event.xkey.same_screen = TRUE;
	event.xkey.time = CurrentTime;
	event.xkey.display = display;

	event.xkey.type = press ? KeyPress : KeyRelease;
	event.xkey.state = 0x0;
	event.xkey.keycode = 38;

	XSendEvent (display, window, FALSE, event.xkey.state, &event);
	XFlush (display);
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

		send_key ('a', event.value == 1 ? TRUE : FALSE);

	}

	return TRUE;
}

int
main (int argc, char** argv)
{
	GIOChannel *js = get_joystick ();

	gtk_init (&argc, &argv);

	g_io_add_watch (js, G_IO_IN, (GIOFunc) js_data_available, NULL);

	gtk_main ();

	return 0;
}
