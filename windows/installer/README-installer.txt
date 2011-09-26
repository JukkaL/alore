Alore Windows Installer Readme
==============================

This directory contains the source files for a Windows installer for Alore.

The installer does the following things:
 * Install:
   * Alore interpreter
   * Alore library modules
   * Alore type checker
   * Alore compiler alorec (which just wraps the source code in an .exe)
   * C headers for writing extensions
   * Alore documentation (optionally)
 * Optionally create Start menu folder with link to documentation and uninstall
   program
 * Associate .alo files to alore interpreter
 * Support uninstalling everything
 * Support Windows 7, Vista and XP

Prerequisities:
 * Working Alore build environment (mingw and msys)
 * NSIS (Nullsoft Scriptable Install System)
   * At least version 2.46 works
 
Building the installer:
 * First build Alore without debug information; use the default C:\Alore 
   prefix:
     $ make clean 
     $ ./configure CFLAGS=-O2 
     $ make && make test
 * Also build HTML documentation:
     $ make doc
 * Install stripped binaries to C:\Alore:
     $ make install-strip
 * Use MakeNSISW to build windows\installer\alore.nsi (in the Alore build
   directory)
   * It uses the files in the build directory (documentation) and the 
     installed files in C:\Alore (the rest; the path is hard coded)
 * If everything works correctly, the resulting installer is 
     windows\installer\Alore-setup.exe

Additional notes:
 * You want to update the version number before building the installer
   (BrandingText in windows\installer\alore.nsi)

About icons:
 * The icon file windows\alore.ico is used for .alo files (note that the icon
   should be in Vista format, with size 256x256 variant using PNG compression)

Limitations/TODO:
 * The Start menu folder is installed only for the current user.
 * If you install Alore as a restricted user, the .alo file association is 
   not set up, but there is no indication in the installer that this has 
   failed.
