#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/joystick.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <devkit-gobject/devkit-gobject.h>

#include <X11/Xlib.h>

#define SET_BIT(a, n) (a)[(n) / CHAR_BIT] |= \
	(unsigned char)(1U << ((n) % CHAR_BIT))
#define CLEAR_BIT(a, n) (a)[(n) / CHAR_BIT] &= \
	(unsigned char)(~(1U << ((n) % CHAR_BIT)))
#define TEST_BIT(a, n) (((a)[(n) / CHAR_BIT] & \
	(unsigned char)(1U << ((n) % CHAR_BIT))) ? 1 : 0)

struct joystick {
	char *driver_name;
	GIOChannel *io;
	u_int8_t num_axes;
	u_int8_t num_buttons;
	unsigned char *active_axes;
	int *axes_map;
	int *button_map;
};

void
populate_map (int *map, int size)
{
	/* XXX static for mednafen */
	static int mednafen[18] = {23, -1, -1, 36, 25, 39, 52, 38, -1, -1, 84,
		85, -1, -1, 89, 88, -1, -1};
	int i;

	for (i = 0; i < size; i++) {
		map[i] = mednafen[i];
	}
}

#define BUF_SIZE 256

struct joystick *
get_joystick (const char *path)
{
	int fd = open (path, O_RDONLY);

	if (fd == -1) {
		printf ("Error opening joystick\n");

		return NULL;
	}

	struct joystick *js = malloc (sizeof (struct joystick));

	js->io = g_io_channel_unix_new (fd);

	js->driver_name = malloc (sizeof (char) * BUF_SIZE);
	if (ioctl (fd, JSIOCGNAME (BUF_SIZE), js->driver_name) < 0) {
		strcpy (js->driver_name, "Generic Joystick");
	}
	
	ioctl (fd, JSIOCGAXES, &(js->num_axes));
	ioctl (fd, JSIOCGBUTTONS, &(js->num_buttons));

	/* XXX do rounding properly */
	js->active_axes = malloc (js->num_axes / 8 + 1);
	memset (js->active_axes, 0, js->num_axes / 8 + 1);

	js->axes_map = malloc (sizeof (int) * js->num_axes);
	js->button_map = malloc (sizeof (int) * js->num_buttons);

	populate_map (js->axes_map, js->num_axes);
	populate_map (js->button_map, js->num_buttons);

	printf ("%s initialized: %d axes, %d buttons\n",
		js->driver_name, js->num_axes, js->num_buttons);

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

	XCloseDisplay (display);
}

gboolean
js_data_available (GIOChannel *io, GIOCondition cond, gpointer *data)
{
	struct joystick *js = (struct joystick *) data;
	struct js_event event;
	ssize_t len = read (g_io_channel_unix_get_fd (io), &event,
			    sizeof (event));

	if (len == sizeof (event)) {
		if (event.type == JS_EVENT_BUTTON &&
		    js->button_map[event.number] != -1) {
			printf ("button event :: v - %d n - %d\n", event.value,
				event.number);

			if (event.value == 1) { // && TEST_BIT (js-> 
				send_key (js->button_map[event.number], TRUE);
			} else if (event.value != 1) {
				send_key (js->button_map[event.number], FALSE);
			}
		}
	}

	return TRUE;
}

static GtkStatusIcon *
make_status_icon (void)
{
	return gtk_status_icon_new_from_icon_name ("input-gaming");
}

static void
setup_joystick (const char *path)
{
	struct joystick *js = get_joystick (path);

	g_io_add_watch (js->io, G_IO_IN, (GIOFunc) js_data_available, js);
}

static const char *subsystems[] = {"input", NULL};

static DevkitClient *
make_devkit_client (void)
{
	DevkitClient *client = devkit_client_new (subsystems);

	if (!devkit_client_connect (client, NULL)) {
		return NULL;
	}

	return client;
}

static void
setup_initial_joysticks (DevkitClient *client)
{
	GList *devs = devkit_client_enumerate_by_subsystem (client, subsystems,
							    NULL);
	GList *cur = devs;

	while (cur != NULL, cur = cur->next) {
		DevkitDevice *dev = (DevkitDevice *) cur->data;
		const char *class = devkit_device_get_property (dev,
								"ID_CLASS");

		if (class != NULL && !strcmp ("joystick", class)) {
			const char *path = devkit_device_get_device_file (dev);
			const char *last_slash = rindex (path, '/');

			/* We only care about /dev/input/jsX joysticks */
			if (strstr (last_slash + 1 , "js") != NULL) {
				printf ("joystick found at %s\n", path);
				setup_joystick (path);
			}
		}

		g_object_unref (dev);
	}

	g_list_free (devs);
}

int
main (int argc, char** argv)
{
	gtk_init (&argc, &argv);

	DevkitClient *client = make_devkit_client ();
	setup_initial_joysticks (client);

	GtkStatusIcon *icon = make_status_icon ();

	gtk_main ();

	return 0;
}
