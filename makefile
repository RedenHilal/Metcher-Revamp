CC = gcc
CFLAGS = -lcurl -lncursesw

TARGET = metcher
SRC = metcher.c
all: $(TARGET)

$(TARGET): $(SRC)
		$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET)
