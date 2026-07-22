CC = gcc
CFLAGS = -fPIC -shared #-fstack-protector-all -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -s -Wl,--disable-new-dtags,-rpath,./
INCLUDES = -I./include -I../redis-6.0.20/deps/lua/src -I../redis-6.0.20/src
LIBS = -L./lib -luring
TARGET = lib/libkbaio.so
HEADER = include/ae_kbaio.h include/networking_kbaio.h

.PHONY: all install clean

all: $(TARGET)

$(TARGET): src/ae_kbaio.c src/networking_kbaio.c
	@mkdir -p lib
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(INCLUDES)

install: $(TARGET)
	@if [ ! -d /usr/lib64 ]; then \
		echo "Creating /usr/lib64 directory..."; \
		sudo mkdir -p /usr/lib64; \
	fi
	sudo cp $(TARGET) /usr/lib64/
	sudo ldconfig
	@echo "libkbaio.so installed to /usr/lib64"
	@if [ ! -d /usr/local/include ]; then \
		echo "Creating /usr/local/include directory..."; \
		sudo mkdir -p /usr/local/include; \
	fi
	sudo cp $(HEADER ) /usr/local/include
	@echo "ae_kbaio.h networking_kbaio.h installed to /usr/local/include"

clean:
	rm -f $(TARGET)
	@echo "Cleaned up build artifacts"