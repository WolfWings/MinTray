#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <X11/Xutil.h>

static void _message( const char *s, const int l ) {
	syscall( SYS_write, STDOUT_FILENO, s, l );
}
#define message(s) _message( s, sizeof( s ) )

__attribute__((noreturn)) static void _unrecoverable( const char *s, const int l ) {
	syscall( SYS_write, STDERR_FILENO, s, l );
	syscall( SYS_exit, 1 );
	__builtin_unreachable();
}
#define unrecoverable(s) _unrecoverable( s, sizeof( s ) )

static int tray_width;
static int tray_height;
static XImage *tray_image;
static char *tray_pixels;
static int tray_pixels_pages;
XVisualInfo tray_visinfo;

static Display *x11_display;
static Window x11_window;

static void connect_to_systray( void ) {
	Atom selection_atom;
	Window tray;
	uint64_t delay = ( 10L * 1000 * 1000 * 1000 ) / 512;
	struct timespec wait;

	goto test;

retry:
	// Logarithmic backoff up to 10 seconds if no systray
	message( "Retrying...\n" );
	wait.tv_sec = delay / ( 1000L * 1000 * 1000 );
	wait.tv_nsec = delay % ( 1000L * 1000 * 1000 );
	syscall( SYS_nanosleep, &wait, NULL );
	if ( delay < ( 10L * 1000 * 1000 * 1000 ) ) {
		delay *= 2;
	}

test:
	selection_atom = XInternAtom( x11_display, "_NET_SYSTEM_TRAY_S0", True);
	if ( selection_atom == None ) {
		message( "No _NET_SYSTEM_TRAY_S0 Atom found to locate System Tray manager\n" );
		goto retry;
	}

	tray = XGetSelectionOwner( x11_display, selection_atom);
	if ( tray == None ) {
		message( "No active System Tray manager found to communicate with\n" );
		goto retry;
	}

	XSelectInput( x11_display, tray, StructureNotifyMask );

	XEvent ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.xclient.type = ClientMessage;
	ev.xclient.window = tray;
	ev.xclient.message_type = XInternAtom( x11_display, "_NET_SYSTEM_TRAY_OPCODE", False );
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = 0; // SYSTEM_TRAY_REQUEST_DOCK
	ev.xclient.data.l[2] = x11_window;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;

	XSendEvent( x11_display, tray, False, NoEventMask, &ev );
	XSync( x11_display, False );

	wait.tv_sec = 0;
	wait.tv_nsec = 10L * 1000 * 1000;
	syscall( SYS_nanosleep, &wait, NULL );
}

static void create_systray( void ) {
	XSetWindowAttributes swa;

	swa.background_pixmap = ParentRelative;
	swa.event_mask = 0
	               | ExposureMask
	               | StructureNotifyMask
	               | 0;

	XSync( x11_display, True );

	// Default size just to start
	tray_width = 1;
	tray_height = 1;
	tray_image = NULL;
	tray_pixels = NULL;
	tray_pixels_pages = 0;

	// This is also known as a "drawable" in various function calls!
	x11_window = XCreateWindow( x11_display, DefaultRootWindow( x11_display ), 0, 0,
		tray_width, tray_height, 0, 24, InputOutput,
		DefaultVisual( x11_display, 0 ), CWBackPixmap | CWEventMask, &swa );

	connect_to_systray( );

	XMapWindow( x11_display, x11_window );

	XSync( x11_display, False );

	memset( &tray_visinfo, 0, sizeof( tray_visinfo ) );
	if ( XMatchVisualInfo( x11_display, XDefaultScreen( x11_display ), 24, TrueColor, &tray_visinfo ) == 0 ) {
		unrecoverable( "Unable to find 32-bit visual format compatible with battery icons\n" );
	}

	tray_image = NULL;
	tray_pixels = NULL;
}

static double old_total, old_idle;
static double cpu_total, cpu_idle;
static int nproc;

static void update_icon( void ) {
	if ( tray_pixels != NULL ) {
		int needed = ( ( tray_width * tray_height * 4 ) + 4095 ) / 4096;
		if ( needed > tray_pixels_pages ) {
			tray_pixels = realloc( tray_pixels, needed * 4096 );
			memset( tray_pixels + ( tray_pixels_pages * 4096 ), 0, ( needed - tray_pixels_pages ) * 4096 );
			tray_pixels_pages = needed;
		}
	}

	if ( tray_pixels == NULL ) {
		tray_pixels_pages = ( ( tray_width * tray_height * 4 ) + 4095 ) / 4096;
		tray_pixels = calloc( tray_pixels_pages, 4096 );
	}

	if ( tray_image != NULL ) {
		if ( ( tray_width > tray_image->width )
		  || ( tray_height > tray_image->height ) ) {
			if ( tray_width != tray_image->width ) {
				message( "New XImage width detected, implement re-map\n" );
			}
			XFree( tray_image );
			tray_image = NULL;
		}
	}

	if ( tray_image == NULL ) {
		tray_image = XCreateImage(
			x11_display, tray_visinfo.visual, tray_visinfo.depth,
			ZPixmap, 0, tray_pixels, tray_width, tray_height, 32, 0 );
		if ( tray_image == 0 ) {
			unrecoverable( "Unable to allocate XImage for CPU graph\n" );
		}
	}

	FILE *f;

	if ( ( f = fopen( "/proc/uptime", "rb" ) ) == NULL ) {
		unrecoverable( "Unable to open /proc/uptime\n" );
	}

	old_total = cpu_total;
	old_idle = cpu_idle;
	if ( fscanf( f, "%lf %lf\n", &cpu_total, &cpu_idle ) != 2 ) {
		unrecoverable( "Unable to fetch CPU stats\n" );
	}

	cpu_total = cpu_total * nproc * 100.0;
	cpu_idle = cpu_idle * 100.0;

	fclose( f );

	int h = ( ( cpu_idle - old_idle ) / ( cpu_total - old_total ) ) * tray_image->height;
	for ( int y = 0; y < tray_image->height; y++ ) {
		char *p = tray_pixels + ( y * tray_image->bytes_per_line );
		for ( int x = tray_image->width - 1; x > 0; x-- ) {
			p[3] = 0;
			p[2] = p[1];
			p[1] = p[0];
			p[0] = p[4];
			p += 4;
		}
		if ( y < h ) {
			p[0] =
			p[1] =
			p[2] =
			p[3] = 0;
		} else {
			p[-4] += 57;
			p[-3] += 28;
			p[-2] += 0;
			p[-1] += 0;
			p[0] = 28;
			p[1] = 57;
			p[2] = 85;
			p[3] = 0;
		}
	}

	XPutImage( x11_display, x11_window,
		DefaultGC( x11_display, DefaultScreen( x11_display ) ),
		tray_image, 0, 0, 0, 0, tray_width, tray_height );
}

int main( void ) {
	XEvent ev;
	uint64_t ret;

	nproc = get_nprocs();

	/* init */
	if ( ( x11_display = XOpenDisplay( NULL ) ) == NULL ) {
		unrecoverable( "Unable to open X11 Display\n" );
	}

	create_systray( );

	for (;;) {
		struct pollfd waiting[1];

		update_icon();

		while( XPending( x11_display ) ) {
			XNextEvent( x11_display, &ev );
			if ( ev.type == ConfigureNotify ) {
				if ( ev.xconfigure.window == x11_window ) {
					tray_width = ev.xconfigure.width;
					tray_height = ev.xconfigure.height;
					message( "SysTray resized\n" );
					update_icon();
					continue;
				}
			}
			if ( ev.type == Expose ) {
				if ( ev.xexpose.window == x11_window ) {
					message( "SysTray exposed\n" );
					update_icon();
					continue;
				}
			}
			if ( ev.type == ReparentNotify ) {
				if ( ev.xreparent.window == x11_window ) {
					message( "SysTray reparented\n" );
					continue;
				}
			}
			if ( ev.type == MapNotify ) {
				if ( ev.xmap.window == x11_window ) {
					message( "SysTray mapped\n" );
					continue;
				}
			}
			if ( ev.type == UnmapNotify ) {
				if ( ev.xmap.window == x11_window ) {
					message( "SysTray unmapped\n" );
					continue;
				}
			}
			if ( ev.type == DestroyNotify ) {
				message( "Destruction event received, restarting search for systray handler\n" );
				connect_to_systray( );
				continue;
			}
			if ( ev.type == ClientMessage ) {
				if ( ev.xclient.window == x11_window ) {
					if ( ev.xclient.message_type == XInternAtom( x11_display, "_XEMBED", True ) ) {
						if ( ev.xclient.data.l[1] == 0 ) {
							message( "SysTray notified embedding successful via XEMBED\n" );
						}
					}
				}
			}
/*
 			printf( "Unhandled event received! ev_type = %i\n", ev.type );
 			for ( int i = 4; i < sizeof( ev ); i++ ) {
 				printf( "%02X", ((unsigned char *)(void *)&ev)[i] );
 			}
 			printf( "\n" );
 */
		}

		waiting[0].fd = ConnectionNumber( x11_display );
		waiting[0].events = POLLIN;
		waiting[0].revents = 0;

		if ( ( ret = syscall( SYS_poll, waiting, 1, 1000 ) ) < 0 ) {
			if ( ret != -EINTR ) {
				unrecoverable( "Error using poll() to avoid 100%% CPU usage\n" );
			}
		}
	}
}
