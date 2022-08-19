#ifndef __UTIL_H__
#define __UTIL_H__

#include <unistd.h>
#include <sys/syscall.h>

#define message(s) syscall( SYS_write, STDOUT_FILENO, s, sizeof( s ) )

__attribute__((noreturn)) static void _unrecoverable( const char *s, const int l ) {
	syscall( SYS_write, STDERR_FILENO, s, l );
	syscall( SYS_exit, 1 );
	__builtin_unreachable();
}
#define unrecoverable(s) _unrecoverable( s, sizeof( s ) )

extern int tray_width;
extern int tray_height;
extern XVisualInfo tray_visinfo;

extern Display *x11_display;
extern Window x11_window;

extern void connect_to_systray( void );
extern void create_systray( void );
extern void event_loop( int msec_delay, void (*callback)() );

#endif // __UTIL_H__
