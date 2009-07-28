#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/joystick.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>

#define SET_BIT(a, n) (a)[(n) / CHAR_BIT] |= \
	(unsigned char)(1U << ((n) % CHAR_BIT))
#define CLEAR_BIT(a, n) (a)[(n) / CHAR_BIT] &= \
	(unsigned char)(~(1U << ((n) % CHAR_BIT)))
#define TEST_BIT(a, n) (((a)[(n) / CHAR_BIT] & \
	(unsigned char)(1U << ((n) % CHAR_BIT))) ? 1 : 0)

struct joystick {
	GIOChannel *io;
	int num_axes;
	int num_buttons;
	unsigned char *active_axes;
	int *axes_map;
	int *button_map;
};

void
populate_map (int *map, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		map[i] = 39;
	}
}

struct joystick *
get_joystick (void)
{
	int fd = open ("/dev/input/js0", O_RDONLY);

	if (fd == -1) {
		printf ("Error opening joystick\n");

		return NULL;
	}

	struct joystick *js = malloc (sizeof (struct joystick));

	js->io = g_io_channel_unix_new (fd);

	ioctl (fd, JSIOCGAXES, &(js->num_axes));
	ioctl (fd, JSIOCGBUTTONS, &(js->num_buttons));

	/* XXX do rounding properly */
	js->active_axes = malloc (js->num_axes / 8 + 1);
	memset (js->active_axes, 0, js->num_axes / 8 + 1);

	js->axes_map = malloc (sizeof (int) * js->num_axes);
	js->button_map = malloc (sizeof (int) * js->num_buttons);

	populate_map (js->axes_map, js->num_axes);
	populate_map (js->button_map, js->num_buttons);

	printf ("joystick initialized: %d axes, %d buttons\n", js->num_axes,
		js->num_buttons);

	return js;
}

static int
get_active_window (Display *display, int root_window)
{
	int status;

	Atom request;
	Atom actual_type;
	int actual_format;
	long nitems;
	long bytes;
	long *data;

	request = XInternAtom (display, "_NET_ACTIVE_WINDOW", TRUE);
	status = XGetWindowProperty(display, root_window, request, 0, (~0L),
				    False, AnyPropertyType, &actual_type,
				    &actual_format, &nitems, &bytes,
				    (unsigned char**)&data);

	return data[0];
}

static void
send_key (int key, gboolean press)
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
	event.xkey.keycode = key;

	XSendEvent (display, window, FALSE, event.xkey.state, &event);
	XFlush (display);
}

gboolean
js_data_available (GIOChannel *io, GIOCondition cond, gpointer *data)
{
	struct joystick *js = (struct joystick *) data;
	struct js_event event;
	ssize_t len = read (g_io_channel_unix_get_fd (io), &event,
			    sizeof (event));

	if (len == sizeof (event)) {

		printf ("data :: v - %d t - %d n - %d\n", event.value,
			event.type, event.number);

		if (event.type == JS_EVENT_BUTTON) {
			if (event.value == 1) { // && TEST_BIT (js-> 
				send_key (js->button_map[event.number], TRUE);
			} else if (event.value != 1) {
				send_key (js->button_map[event.number], FALSE);
			}
		}
	}

	return TRUE;
}

int
main (int argc, char** argv)
{
	struct joystick *js = get_joystick ();

	if (js == NULL) {
		printf ("No joystick found. exiting.\n");
		return 0;
	}

	gtk_init (&argc, &argv);

	g_io_add_watch (js->io, G_IO_IN, (GIOFunc) js_data_available, js);

	gtk_main ();

	return 0;
}
