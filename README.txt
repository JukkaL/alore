Alore ReadMe
============


What is Alore?
--------------

Alore is an object-oriented general-purpose programming language with an
optional static type system. Its syntax and libraries resemble Python.


License
-------

Alore is licensed under the terms of the MIT license.  See the file
LICENSE.txt in the distribution.


Quick start (Linux, OS X, Unix-like)
------------------------------------

To get started in a Unix-like operating system, clone the Alore git repository
or download a source tarball and type

  ./configure
  make

You need a basic C development tool chain for the build.

You can now run the Alore interpreter as ./alore. To also install Alore
under /usr/local:

  su root
  make install

(or "sudo make install", depending on your configuration)

Now you can run an Alore program using the Alore interpreter:

  alore program.alo


Quick start (Windows)
---------------------

The easiest way to use Alore in Windows is to download the Windows installer
from http://www.alorelang.org/ and use it to install Alore. [It should be
available any day now... Until it is, build from source (see below).]

You may also wish to add c:\Alore to your PATH (assuming you used the default
target folder when installing). Do it like this in Command prompt:

  C:\>set path=%path%;c:\alore

Now you can run an Alore program using the Alore interpreter:

  C:\Work>alore program.alo


Building from source code (Windows)
-----------------------------------

Alternatively, you can build Alore from source code:

 1. Install MinGW and MSYS (http://www.mingw.org/). These are free software
    products that provide the GCC C compiler and a lightweight Unix-like build
    environment for Windows.
 2. Start MSYS shell.
 3. Change to the source package root directory (that also contains
    README.txt).
 4. Type "./configure" (without the quotes, and followed by Enter). This
    builds the Makefile.
 5. Type "make" to build Alore.

You can now run the Alore interpreter as "alore". To also install Alore
under c:\Alore:

 6. Type "make install".


Documentation
-------------

The documentation for the latest Alore release is available online at
http://www.alorelang.org/doc/.

You can build the documentation from sources using make:

  make doc

The documentation is generated in directory doc/html.


Release notes
-------------

Alore is still pre-beta software. Although Alore developers try to avoid
unnecessary changes that break compatibility, this may happen occasionally.
Documentation may not always be up-to-date.

See also "Known bugs and limitations" below and the issue tracker.


Software requirements
---------------------

Alore has been successfully compiled and run on at least the following
operating systems:

 * Linux (several distributions; 32-bit and 64-bit)
 * Windows 7, Windows Vista and Windows XP (32-bit)
 * Mac OS X (64-bit)
 * OpenSolaris (32-bit)
 * FreeBSD (32-bit)

The following tools are recommended for building Alore under Windows:

 * MinGW (Minimalist GNU for Windows)
 * MSYS (shell environment for Windows, part of the MinGW distribution)

On other operating systems, the standard C build environment is supported.


Discussion and feedback
-----------------------

Send any questions or comments about Alore and suggestions for future
improvements to the Alore mailing list:

  https://groups.google.com/group/alorelang


Reporting bugs
--------------

Submit bug reports using the Alore issue tracker:

  https://github.com/JukkaL/alore/issues


Credits
-------

Alore was conceived, designed and implemented by Jukka Lehtosalo, with help
and useful comments from many people; see the file CREDITS.txt for a list of
contributors.


Running tests
-------------

To run Alore test cases, first build Alore and then type (in the shell)

  make test


Alore compiler
--------------

You can package the source files of a program and the Alore interpreter into
a single binary using the alorec tool:

  alorec program.alo

This creates the executable "program" (or "program.exe"). You need to have
a supported C compiler (gcc) installed and in your PATH.

In Windows you can use MinGW (see above); you may have to add C:\MinGW\bin to
your PATH (the exact location of MinGW files may differ in your configuration):

  C:\>set path=%path%;c:\mingw\bin

NOTE: The source code of your program is directly included in the generated
      executable file.


Known bugs and limitations
--------------------------

A partial list of major bugs and limitations in the current Alore release:

 * not much testing on some supported operating systems
 * big-endian architectures not tested
 * incomplete support for 64-bit operating systems
   * 64-bit Windows builds not tested
 * limited support for non-ASCII paths and file names
 * file owner information cannot be accessed in Windows
