CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
TARGET = mia_proyecto
SRC = main.cpp \
      Commands/parser.cpp \
      Commands/mkdisk.cpp \
      Commands/rmdisk.cpp \
      Commands/fdisk.cpp \
      Commands/mount.cpp \
      Commands/mkfs.cpp \
      Commands/user_mgmt.cpp \
      Commands/cat_cmd.cpp \
      Commands/file_ops.cpp \
      Commands/rep.cpp

OBJ = $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean
