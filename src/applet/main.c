#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/joystick.h>

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>

#include <X11/Xlib.h>

#define SET_BIT(a, n) (a)[(n) / CHAR_BIT] |= \
	(unsigned char)(1U << ((n) % CHAR_BIT))
#define CLEAR_BIT(a, n) (a)[(n) / CHAR_BIT] &= \
	(unsigned char)(~(1U << ((n) % CHAR_BIT)))
#define TEST_BIT(a, n) (((a)[(n) / CHAR_BIT] & \
	(unsigned char)(1U << ((n) % CHAR_BIT))) ? 1 : 0)

struct joystick {
	char *path;
	char *driver_name;
	GIOChannel *io;
	unsigned int source;
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

	js->path = strdup (path);
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

void
joystick_free (struct joystick *js)
{
	int fd = g_io_channel_unix_get_fd (js->io);

	free (js->path);

	g_source_remove (js->source);
	g_io_channel_shutdown (js->io, FALSE, NULL);
	close (fd);

	free (js->driver_name);

	free (js->axes_map);
	free (js->button_map);

	free (js);
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

/* 
 * XXX maybe this should handle when the file goes away, rather than waiting
 * for a gudev event 
 * */
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

static struct joystick *
setup_joystick (const char *path)
{
	struct joystick *js = get_joystick (path);

	js->source = g_io_add_watch (js->io, G_IO_IN,
				     (GIOFunc) js_data_available, js);

	return js;
}

static const char *subsystems[] = {"input", NULL};

static GUdevClient *
make_gudev_client (void)
{
	return g_udev_client_new (subsystems);
}

static struct joystick *
setup_if_joystick (GUdevDevice *dev)
{
	const char *class = g_udev_device_get_property (dev, "ID_CLASS");

	if (class != NULL && !strcmp ("joystick", class)) {
		const char *path = g_udev_device_get_device_file (dev);
		const char *last_slash = rindex (path, '/');

		/* We only care about /dev/input/jsX joysticks */
		if (strstr (last_slash + 1 , "js") != NULL) {
			printf ("joystick found at %s\n", path);
			return setup_joystick (path);
		}
	}

	return NULL;
}

static void
setup_initial_joysticks (GUdevClient *client, GHashTable *joysticks)
{
	GList *devs = g_udev_client_query_by_subsystem (client, subsystems[0]);
	GList *cur = devs;

	while (cur != NULL, cur = cur->next) {
		GUdevDevice *dev = (GUdevDevice *) cur->data;
		struct joystick *js = setup_if_joystick (dev);

		if (js != NULL) {
			g_hash_table_insert (joysticks, js->path, js);
		}
		g_object_unref (dev);
	}

	g_list_free (devs);
}

struct clyde_context {
	GHashTable *joysticks;
	GUdevClient *gudev_client;
	GtkStatusIcon *icon;
};

static void
update_icon_visibility (struct clyde_context *ctx)
{
	gtk_status_icon_set_visible (ctx->icon,
				     g_hash_table_size (ctx->joysticks) > 0);
}

static void
handle_gudev_event (GUdevClient *client, char *action, GUdevDevice *dev,
		     gpointer data)
{
	struct clyde_context *ctx = (struct clyde_context *) data;

	if (!strcmp ("add", action)) {
		printf ("device add detected\n");

		struct joystick *js = setup_if_joystick (dev);

		if (js != NULL) {
			g_hash_table_insert (ctx->joysticks, js->path, js);
		}
	} else if (!strcmp ("remove", action)) {
		const char *path = g_udev_device_get_device_file (dev);

		printf ("device remove detected on %s\n", path);

		struct joystick *js = g_hash_table_lookup (ctx->joysticks,
							   path);

		if (js != NULL) {
			printf ("no longer watching %s on %s\n",
				js->driver_name, js->path);
			g_hash_table_remove (ctx->joysticks, path);
			joystick_free (js);
		}
	}

	update_icon_visibility (ctx);
}

static void
handle_icon_activate (GtkStatusIcon *icon, gpointer data)
{
	printf ("icon activated\n");
}

static void
handle_icon_popup (GtkStatusIcon *icon, guint button, guint activate_time,
		   gpointer data)
{

	printf ("icon popup\n");
}

int
main (int argc, char** argv)
{
	struct clyde_context ctx;
	
	gtk_init (&argc, &argv);

	ctx.joysticks = g_hash_table_new (g_str_hash, g_str_equal);
	ctx.gudev_client = make_gudev_client ();
	setup_initial_joysticks (ctx.gudev_client, ctx.joysticks);

	printf ("%d joystick(s) found on startup\n",
		g_hash_table_size (ctx.joysticks));

	ctx.icon = make_status_icon ();
	update_icon_visibility (&ctx);

	g_signal_connect (ctx.gudev_client, "uevent",
			  G_CALLBACK (handle_gudev_event), &ctx);

	g_signal_connect (ctx.icon, "activate",
			  G_CALLBACK (handle_icon_activate), &ctx);
	g_signal_connect (ctx.icon, "popup-menu",
			  G_CALLBACK (handle_icon_popup), &ctx);

	gtk_main ();

	return 0;
}
