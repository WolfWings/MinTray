#include <stdint.h>
#include <unistd.h>
#include <X11/Xutil.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include "icons.h"

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

static Display *x11_display;
static Window x11_window;

static void send_systray_message( long data1, long data2, long data3 ) {
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
	ev.xclient.data.l[2] = data1;
	ev.xclient.data.l[3] = data2;
	ev.xclient.data.l[4] = data3;

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
	tray_width = ICON_WIDTH;
	tray_height = ICON_HEIGHT;

	// This is also known as a "drawable" in various function calls!
	x11_window = XCreateWindow( x11_display, DefaultRootWindow( x11_display ), 0, 0,
		tray_width, tray_height, 0, 24, InputOutput,
		DefaultVisual( x11_display, 0 ), CWBackPixmap | CWEventMask, &swa );

	send_systray_message( x11_window, 0, 0 );

	XMapWindow( x11_display, x11_window );

	XSync( x11_display, False );
}

static void update_icon( void ) {
	int handle, charge, icon;
	uint64_t v_cur, v_max;

	if ( ( handle = syscall( SYS_open, "/sys/class/power_supply/BAT0/energy_full", O_RDONLY ) ) < 0 ) {
		unrecoverable( "Unable to open battery charge maximum\n" );
	}
	v_max = 0;
	for (;;) {
		unsigned char c;
		uint64_t s = syscall( SYS_read, handle, &c, 1 );
		if ( s < 1 ) {
			break;
		}
		if ( c == 10 ) {
			break;
		}
		if ( ( c >= '0' )
		  && ( c <= '9' ) ) {
			v_max = ( v_max * 10 ) + ( c - '0' );
			continue;
		}
		unrecoverable( "Unable to read battery charge maximum\n" );
	}
	syscall( SYS_close, handle );

	if ( ( handle = syscall( SYS_open, "/sys/class/power_supply/BAT0/energy_now", O_RDONLY ) ) < 0 ) {
		unrecoverable( "Unable to open battery current charge\n" );
	}
	v_cur = 0;
	for (;;) {
		unsigned char c;
		uint64_t s = syscall( SYS_read, handle, &c, 1 );
		if ( s < 1 ) {
			break;
		}
		if ( c == 10 ) {
			break;
		}
		if ( ( c >= '0' )
		  && ( c <= '9' ) ) {
			v_cur = ( v_cur * 10 ) + ( c - '0' );
			continue;
		}
		unrecoverable( "Unable to read battery current charge\n" );
	}
	syscall( SYS_close, handle );

	charge = (int)( ( v_cur * 100 ) / v_max );

	icon = -1;
	for ( int i = 0; i < BREAKPOINTS_TOTAL; i++ ) {
		if ( charge > breakpoints[ i ].threshold ) {
			continue;
		}
		icon = i;
		break;
	}
	if ( icon == -1 ) {
		return;
	}

	XClearWindow( x11_display, x11_window );
	XPutImage( x11_display, x11_window,
		DefaultGC( x11_display, DefaultScreen( x11_display ) ),
		breakpoints[ icon ].image, 0, 0,
		( tray_width - ICON_WIDTH ) / 2,
		( tray_height - ICON_HEIGHT ) / 2,
		ICON_WIDTH, ICON_HEIGHT );
}

int main( void ) {
	XEvent ev;
	uint64_t ret;

	/* init */
	if ( ( x11_display = XOpenDisplay( NULL ) ) == NULL ) {
		unrecoverable( "Unable to open X11 Display\n" );
	}

	create_systray( );

	XVisualInfo visinfo = {0};
	if ( XMatchVisualInfo( x11_display, XDefaultScreen( x11_display ), 24, TrueColor, &visinfo ) == 0 ) {
		unrecoverable( "Unable to find 32-bit visual format compatible with battery icons\n" );
	}

	for ( int i = 0; i < BREAKPOINTS_TOTAL; i++ ) {
		if ( ( breakpoints[i].image = XCreateImage(
			x11_display, visinfo.visual, visinfo.depth,
			ZPixmap, 0, (char *)unpacked_pixels + ( 24 * 19 * 4 * i ),
			ICON_WIDTH, ICON_HEIGHT, 32, 0 ) ) == 0 ) {
			unrecoverable( "Unable to allocate XImage for battery icon\n" );
		}
	}

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
				send_systray_message( x11_window, 0, 0 );
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

		if ( ( ret = syscall( SYS_poll, waiting, 1, 5000 ) ) < 0 ) {
			if ( ret != -EINTR ) {
				unrecoverable( "Error using poll() to avoid 100%% CPU usage\n" );
			}
		}
	}
}
