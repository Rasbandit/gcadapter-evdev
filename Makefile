all: bin_dir libgcadapter.a gcadapter_evdev

bin_dir:
	-mkdir bin

libgcadapter.a:
	gcc -c -o libgcadapter/libgcadapter.o libgcadapter/src/libgcadapter.c \
		-Ilibgcadapter/include -lusb-1.0
	ar rcs libgcadapter/libgcadapter.a libgcadapter/libgcadapter.o

gcadapter_evdev: libgcadapter.a gcadapter_evdev.o
	gcc -o bin/gcadapter_evdev src/gcadapter_evdev.o -pthread \
		-Llibgcadapter -l:libgcadapter.a -lusb-1.0

gcadapter_evdev.o:
	gcc -c -o src/gcadapter_evdev.o src/gcadapter_evdev.c -pthread \
		-Ilibgcadapter/include -Iinclude -Wall

clean:
	-rm libgcadapter/libgcadapter.o
	-rm libgcadapter/libgcadapter.a
	-rm src/gcadapter_evdev.o
	-rm -rf bin
