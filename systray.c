#include <stdint.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <X11/Xutil.h>
#include "systray.h"

int tray_width;
int tray_height;
XVisualInfo tray_visinfo;

Display *x11_display;
Window x11_window;

void connect_to_systray( void ) {
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

void create_systray( void ) {
	XSetWindowAttributes swa;

	if ( ( x11_display = XOpenDisplay( NULL ) ) == NULL ) {
		unrecoverable( "Unable to open X11 Display\n" );
	}

	swa.background_pixmap = ParentRelative;
	swa.event_mask = 0
	               | ExposureMask
	               | StructureNotifyMask
	               | 0;

	XSync( x11_display, True );

	// Default size just to start
	tray_width = 24;
	tray_height = 24;

	// This is also known as a "drawable" in various function calls!
	x11_window = XCreateWindow( x11_display, DefaultRootWindow( x11_display ), 0, 0,
		tray_width, tray_height, 0, 24, InputOutput,
		DefaultVisual( x11_display, 0 ), CWBackPixmap | CWEventMask, &swa );

	connect_to_systray( );

	XMapWindow( x11_display, x11_window );

	XSync( x11_display, False );

	memset( &tray_visinfo, 0, sizeof( tray_visinfo ) );
	if ( XMatchVisualInfo( x11_display, XDefaultScreen( x11_display ), 24, TrueColor, &tray_visinfo ) == 0 ) {
		unrecoverable( "Unable to find 32-bit visual format for internal rendering\n" );
	}
}

void event_loop( int msec_delay, void (*callback)() ) {
	XEvent ev;
	int ret;

	for (;;) {
		struct pollfd waiting[1];

		callback();

		while( XPending( x11_display ) ) {
			XNextEvent( x11_display, &ev );
			if ( ev.type == ConfigureNotify ) {
				if ( ev.xconfigure.window == x11_window ) {
					tray_width = ev.xconfigure.width;
					tray_height = ev.xconfigure.height;
					message( "SysTray resized\n" );
					callback();
					continue;
				}
			}
			if ( ev.type == Expose ) {
				if ( ev.xexpose.window == x11_window ) {
					message( "SysTray exposed\n" );
					callback();
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

		if ( ( ret = syscall( SYS_poll, waiting, 1, msec_delay ) ) < 0 ) {
			if ( ret != -EINTR ) {
				unrecoverable( "Error using poll() to avoid 100%% CPU usage\n" );
			}
		}
	}
}
