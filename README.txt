Alore Readme (development version)
==================================

Copyright (c) 2010-2011 Jukka Lehtosalo


What's Alore?
-------------

Alore is a dynamically-typed, object-oriented general-purpose programming
language.


License
-------

Alore is licensed under the terms of the MIT license.  See the file
LICENSE.txt in the distribution.


Quick start (Linux, OS X, Unix-like)
------------------------------------

To get started under a Unix-like operating system, type

  ./configure
  make

You need a basic C development tool chain for the build.

You can now run the Alore interpreter as ./alore. To also install Alore
under /usr/local:

  su root
  make install

(or "sudo make install", depending on your configuration)


Quick start (Windows)
---------------------

 1. Install and configure MinGW and MSYS (http://www.mingw.org/).
 2. Start MSYS shell.
 3. Change to the source package root directory (that also contains
    README.txt).
 4. Type "./configure" (without the quotes, and followed by Enter). This
    builds the Makefile.
 5. Type "make" to build Alore.

You can now run the Alore interpreter as "alore". To also install Alore
under c:\Alore:

 6. Type "make install".

Finally, you may wish to add c:\Alore to your PATH.


Release notes
-------------

This is a pre-release version of Alore. Things might break or change without
a warning. Documentation may not be up-to-date.

See also "Known bugs and limitations" below.


Running tests
-------------

To run Alore test cases, first build Alore and then type (in shell)

  make test


Alore compiler
--------------

You can package the source files of a program and the Alore interpreter into
a single binary using the alorec tool:

  alorec program.alo

This creates the executable "program" (or "program.exe").

Note that the source code is directly included in the executable file.


Software requirements
---------------------

Alore has been successfully compiled and run on at least the following
operating systems:

 * Windows 7, Windows Vista and Windows XP (32-bit)
 * Linux (several distributions; 32-bit and 64-bit)
 * Mac OS X (64-bit)
 * OpenSolaris (32-bit)
 * FreeBSD (32-bit)

The following tools are recommended for building Alore under Windows:

 * MinGW (Minimalist GNU for Windows)
 * MSYS (shell environment for Windows, part of the MinGW distribution)

On other operating systems, the standard C build environment is supported.


Documentation
-------------

Alore documentation is available online at http://www.alorelang.org/doc/.


How to report bugs
------------------

If you believe that you have found a bug in Alore or a mistake in Alore
documentation, please let the developers know about it so that the defect
can be fixed.

Send bug reports by email to bugs@alorelang.org.


Contributing
------------

Any help in the development, testing and documentation of Alore (and any
other related tasks) is gladly accepted. If you are interested, please
contact the author at jukka@alorelang.org.


Credits
-------

Alore was conceived, designed and implemented by Jukka Lehtosalo
(jukka@alorelang.org), with help and useful comments from many people;
see the file CREDITS.txt for a list of contributors.


Known bugs and limitations
--------------------------

A partial list of major bugs and limitations in the current Alore release:

 * not much testing on some supported operating systems
 * big-endian architectures not supported
 * incomplete support for 64-bit operating systems
   * 64-bit Windows builds not tested
 * limited support for non-ASCII paths and file names
 * file owner information cannot be accessed in Windows
