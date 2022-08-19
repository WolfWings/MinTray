#include <stdint.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include "systray.h"
#include "battery_icons.h"

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
	create_systray( );

	for ( int i = 0; i < BREAKPOINTS_TOTAL; i++ ) {
		if ( ( breakpoints[i].image = XCreateImage(
			x11_display, tray_visinfo.visual, tray_visinfo.depth,
			ZPixmap, 0, (char *)unpacked_pixels + ( 24 * 19 * 4 * i ),
			ICON_WIDTH, ICON_HEIGHT, 32, 0 ) ) == 0 ) {
			unrecoverable( "Unable to allocate XImage for battery icon\n" );
		}
	}

	event_loop( 5000, &update_icon );
}
