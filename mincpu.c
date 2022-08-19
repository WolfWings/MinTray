#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <X11/Xutil.h>
#include "systray.h"

static XImage *tray_image;
static char *tray_pixels;
static int tray_pixels_pages;

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
			p[0] = p[6];
			p += 4;
		}
		if ( y < h ) {
			p[3] =
			p[2] =
			p[1] =
			p[0] = 0;
		} else {
			p[ 3] = 0;
			p[ 2] = p[1] + 85;
			p[ 1] = p[0] + 57;
			p[ 0] = 28;
			p[-1] += 0;
			p[-2] += 0;
			p[-3] += 28;
			p[-4] += 57;
		}
	}

	XPutImage( x11_display, x11_window,
		DefaultGC( x11_display, DefaultScreen( x11_display ) ),
		tray_image, 0, 0, 0, 0, tray_width, tray_height );
}

int main( void ) {
	nproc = get_nprocs();

	create_systray( );

	tray_image = NULL;
	tray_pixels = NULL;

	event_loop( 1000, &update_icon );
}
