PROJECT = psx_soundfont_generator

CC = gcc
CFLAGS = -O2

SRCS = 	source/main.c \
		source/adpcm.c \
		source/cdrom.c \

HEADERS = 	source/libpsxav.h \
			source/wav.h \

TARGET_DIR = bin

all: $(TARGET_DIR)/$(PROJECT)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(TARGET_DIR)/$(PROJECT): $(SRCS) $(HEADERS) | $(TARGET_DIR)
	$(CC) $(CFLAGS) -o $(TARGET_DIR)/$(PROJECT) $(SRCS)

clean:
	rm -rf $(TARGET_DIR)/$(PROJECT)

.PHONY: all clean