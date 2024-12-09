all: httpserver httpserver-arm64 httpserver-amd64 httpserver-universal

httpserver: httpserver.c
	$(CC) -o httpserver httpserver.c

httpserver-arm64: httpserver.c
	arch -arm64 $(CC) -o httpserver-arm64 httpserver.c

httpserver-amd64: httpserver.c
	arch -x86_64 $(CC) -o httpserver-amd64 httpserver.c

httpserver-universal: httpserver-arm64 httpserver-amd64
	lipo -create -output httpserver-universal httpserver-arm64 httpserver-amd64

clean:
	rm -f httpserver httpserver-arm64 httpserver-amd64 httpserver-universal
