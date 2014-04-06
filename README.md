SimpleChat
===
Very simple chat program by George Luna

Usage: server [servername] [port number]

To build, simply run the makefile. To clean
up, just run make clean. This program should
be able to run on any POSIX-compliant system
without issues, though some effort will need
to be made to port this program to Windows.

By default when no arguments are specified,
the server will run on port 8000 and a
default server name, both which can be
changed in the server.c file through the
defines at the top of the code.

This program has been tested on Fedora 18,
OS X Mavericks 10.9.2, and Debian 6.0,
compiled with GCC 4.4.5 and 4.7.2, and
Apple LLVM version 5.1.

KNOWN BUGS:
-----------
The server will run oddly when a user is
connected to it through PuTTY and Microsoft
Telnet client in a default configuration,
possibly due to issues with character
encodings, as well as these terminal
emulators not transmitting line-at-a-time.
Connecting to the server via Unix-based
systems, such as GNU/Linux systems and OS X
should be sufficient to connect and use the
server correctly.

In addition to this, clients will have
their input text onscreen overwritten when
they receive messages from the server and
other users when connecting through telnet.
This problem could be dealt with by creating
a dedicated client program for this protocol
complete with a graphical gui, or at least a
bit more sophisticated text gui made in
ncurses.
