# OpenINT Information

OpenINT is a minimal Linux init system written primarily in C.

## Components

- `inner/init/` contains the init executable.
- `inner/init/src/init/` contains init-system startup and mount handling.
- `inner/init/src/server/` contains the Unix-domain IPC server and command
  action handlers.
- `inner/init/src/rescue/` contains rescue-mode UI and commands.
- `out/` contains staged build outputs.
- `qemu/` contains staged filesystem and VM image outputs.

## IPC

The init server listens on `/run/oinit.sock` using a Unix-domain stream socket.
Requests are received in 32-byte chunks until the client closes the write side.
The server passes the accumulated request and byte length to `action()`.

The current action parser tokenizes a space-separated command line, including
single quotes, double quotes, and backslash escaping.

## Logging

The init logger is defined in `init/log.h`. Available levels are debug, info,
warning, and error.
