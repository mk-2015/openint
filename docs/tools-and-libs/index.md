# Tools and Libraries

OpenINT discovers subprojects below `inner/` using `.project` files.

## Project Types

- `lib`: builds `lib/` and `include/` for copied library outputs.
- `tool`: builds `bin/` and copies executables to `out/bin/`.
- `init`: builds exactly one executable at `bin/init`.

## Current Projects

### Init

- `inner/init/` is the single init project.
- Output: `out/bin/init`
- Headers: `inner/init/include/init/`

The project currently provides init startup, mounting, logging, rescue mode,
Unix-domain IPC, and process environment support.

There are no separate library or tool projects in the repository at present.
