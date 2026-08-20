# Project Name
TARGET = daisylab

USE_DAISYSP_LGPL = 1

# APP_TYPE = BOOT_SRAM

# Compiler options
OPT = -O3
#OPT=-Os


# Sources
CPP_SOURCES = daisylab.cpp  operator.cpp

# Library Locations
LIBDAISY_DIR = ../deps/libDaisy
DAISYSP_DIR = ../deps/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
