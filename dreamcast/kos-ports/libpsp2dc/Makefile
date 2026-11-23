# Port Metadata

PORTNAME = libpsp2dc

PORTVERSION = 1.0

MAINTAINER = PSP2DC <info@pspexp.dev>

LICENSE = MIT License - https://opensource.org/licenses/MIT

SHORT_DESC = A PSP as a second screen through the PSP2DC adapter: widgets, drawing, pictures, files, buttons

# No dependencies beyond the base system.
DEPENDENCIES =

# The PSP2DC repository; the library is dreamcast/libpsp2dc in it.
GIT_REPOSITORY = https://github.com/pleft/PSP2DC.git
GIT_BRANCH = main

TARGET = libpsp2dc.a
INSTALLED_HDRS = dreamcast/libpsp2dc/psp2dc.h protocol/psp2dc_protocol.h
HDR_INSTDIR = psp2dc

# KOS Distributed extras (to be copied into the build tree)
KOS_DISTFILES = KOSMakefile.mk

include ${KOS_PORTS}/scripts/kos-ports.mk
