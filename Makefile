CC = gcc
CFLAGS = -Wall -O2 -I./include
LDFLAGS = -ljpeg

TARGET = uvc_camera
SRC_DIR = src
EXEC_DIR = execute
INC_DIR = include

# Add ALL source files that need to be compiled
SRCS = $(SRC_DIR)/uvc_camera.c \
       $(SRC_DIR)/mjpeg_parser.c \
       $(SRC_DIR)/image_processing.c \
       $(SRC_DIR)/urb_manager.c \
       $(EXEC_DIR)/main.c

# Generate object file names
OBJS = $(SRC_DIR)/uvc_camera.o \
       $(SRC_DIR)/mjpeg_parser.o \
       $(SRC_DIR)/image_processing.o \
       $(SRC_DIR)/urb_manager.o \
       $(EXEC_DIR)/main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(EXEC_DIR)/%.o: $(EXEC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.rgb *.mp4

.PHONY: all clean
