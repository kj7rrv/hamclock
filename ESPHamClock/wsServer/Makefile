# Copyright (C) 2016-2022 Davidson Francis <davidsondfgl@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>

CC        = g++
AR        = ar
ARFLAGS   = r
CXXFLAGS  = -Iinclude -Wall -O2
LIB       = libws.a

# Source
SRC =   src/base64.cpp \
	src/handshake.cpp \
	src/sha1.cpp \
	src/utf8.cpp \
	src/ws.cpp

OBJ = $(SRC:.cpp=.o)

# General objects
%.o: %.cpp
	$(CC) $< $(CXXFLAGS) -c -o $@

# Library
libws.a: $(OBJ)
	$(AR) $(ARFLAGS) $(LIB) $^

# Clean
clean:
	@rm -f $(OBJ) $(LIB)
