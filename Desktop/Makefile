CC = gcc
CFLAGS = -Wall -O2 -I/usr/include/libdrm -I/usr/include/drm
LDFLAGS = -ldrm

TARGET = drm_fb_test
SRC = drm_fb_test.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all clean install
