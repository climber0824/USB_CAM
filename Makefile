CC = gcc
CFLAGS = -Wall -O2 -I./include
TARGET = uvc_camera
SRC_DIR = src
EXEC_DIR = execute
INC_DIR = include

SRCS = $(SRC_DIR)/uvc_camera.c $(EXEC_DIR)/main.c
OBJS = $(SRC_DIR)/uvc_camera.o $(EXEC_DIR)/main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/uvc_camera.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXEC_DIR)/%.o: $(EXEC_DIR)/%.c $(INC_DIR)/uvc_camera.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
