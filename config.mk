# mailarchiver version
VERSION = 0.1

# Customize below to fit your system

# compiler and linker
CC = gcc
LD = gcc

# installation paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# compiler flags
CPPFLAGS = -DVERSION=\"$(VERSION)\"
CFLAGS   = -g -Wall
LDFLAGS  = -g

