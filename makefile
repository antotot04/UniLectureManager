all: database directory_lookup file_downloader

CC = gcc
# optimizers:
# -O2 level --> 2 optimization
# -Wall -Wextra --> more useful warnings 
# -pedantic --> bad code warned
# -Wshadow --> warns if a variable hides another one in the code
# -Wformat=2 --> strong checks on functions use 
CFLAGS = -g -O2 -Wall -Wextra -pedantic -Wshadow -Wformat=2 -fsanitize=address

database: database.c tdIOlib/tdIOlib.c
	$(CC) $(CFLAGS) database.c tdIOlib/tdIOlib.c -o database

directory_lookup: directory_lookup.c tdIOlib/tdIOlib.c
	$(CC) $(CFLAGS) directory_lookup.c tdIOlib/tdIOlib.c -o directory_lookup

file_downloader: file_downloader.c tdIOlib/tdIOlib.c
	$(CC) $(CFLAGS) file_downloader.c tdIOlib/tdIOlib.c -o file_downloader


.PHONY: clean

clean:
	rm -f database directory_lookup file_downloader
