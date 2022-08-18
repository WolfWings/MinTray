MINimal BATtery SysTray gauge

==What is this?==

An absolutely tiny battery gauge for your system tray on X11/Linux systems.

Also now an equally tiny CPU usage graph! Uses ClearType antialiasing even!

Also a very minimal example of how to create/manage a SysTray icon/state as
there's a lack of modern tutorials for that available online.

==Limitations==

This ONLY supports 32-bit visual depths. So it won't run at all on old GPUs
or ancient hardware. It also won't render properly except on RGBX platforms
but both of these are non-issues on even the cheapest hardware today.

The battery levels are fixed, as are the icons used, and the icons are only
in a header format since that's how I created them. Yes really. Only things
I did was reduce the linewrap down and break things up into per-byte rather
than keeping them as 32-bit values, just in case endianness issues crop up.

This doesn't support anything except /sys/class/power_supply/BAT0, sorry to
any dual-battery laptop owners out there. This is also v0.1, patches are as
they say, always welcome!
