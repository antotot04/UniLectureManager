# UniLectureManager
This is a simple university lecture manager programmed in C. It is able to see if a lecture of a given course exists (through a simple directory service server) and download it from the lecture database (if requested). As you can understand, the idea is to store files in the server's file system and the client can search and download files if needed. I've done it mainly to better understand directory services in socket programming but i don't mind sharing it.

- ## Structure 
  In a few words, this lecture manager is structured as a client server application TCP based to handle download and directory service functions    properly. 
  - Servers (database.c & directory_lookup.c) are cuncurrent with direct asymmetric schemes and synchronous mode with buffering both send &           receive sides.
  - The client (file_downloader.c) is single process and contacts both servers through a synchronous comunication (like both servers) send &          receive sides.
