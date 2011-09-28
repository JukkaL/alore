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
 * Support Windows 7, Vista and XP (probably also Windows Server 2003/2008 but
   this has not been tested)

Prerequisites:
 * Working Alore build environment (MinGW and MSYS)
 * NSIS (Nullsoft Scriptable Install System)
   * Version 2.46 is known to work; more recent versions are probably fine
 
Building the installer:
 * Clone Alore git repository or extract Alore source zip/tar.gz file to local 
   hard disk.
   * The directory that contains the sources is the build directory.
 * First build Alore without debug information. Do this in the MSYS shell
   (you should not change the default install prefix):
     $ make clean 
     $ ./configure CFLAGS=-O2 
     $ make && make test
 * Also build HTML documentation:
     $ make doc
 * Install stripped binaries to C:\Alore:
     $ make install-strip
 * Use MakeNSISW to build windows\installer\alore.nsi (this file is within the 
   Alore build directory, not in C:\Alore).
   * Set the Alore version number before building an installer for an Alore
     release (ALORE_VERSION in windows\installer\alore.nsi). The correct format
     is "1.2.3" (even if the last number is 0).
   * The installer generation script uses the files in the build directory (for 
     documentation) and the installed files in C:\Alore (for the rest of the 
     files). The latter path is hard coded.
   * If everything works correctly, the resulting installer is 
     windows\installer\alore-setup.exe.
 * Rename alore-setup.exe to reflect the current version number (e.g. 
   alore-setup-1.2.3.exe).

About icons:
 * The icon file windows\alore.ico is used for .alo files (note that the icon
   should be in Vista format; it should also have a size 256x256 variant that 
   uses PNG compression).

Limitations/TODO:
 * The Start menu folder and file associations are always installed for the 
   current user only.
