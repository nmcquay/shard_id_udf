SOURCES=shard_id.c
TARGET=shard_id.so
DESTDIR=/usr/lib/mysql/plugin

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) `mysql_config --cflags` \
		-Wall -W -pthread -shared -fPIC -D_GNU_SOURCE=1 \
		$(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

install: all
	install -b -o 0 -g 0 $(TARGET) $(DESTDIR)/$(TARGET)

uninstall:
	rm $(DESTDIR)/$(TARGET)

