## Overview

This project provides a user-space shell-like utility capable of interpreting a FAT32 file system image. It allows users to navigate directories, list entries, create files and directories, read and write files, rename entries, and remove files and directories, all within the image's file system space.


## File Listing

```
.
├── Makefile
├── README.md
├── include
│   ├── commands.h
│   ├── fat32.h
│   ├── fs.h
│   ├── utils.h
└── src
    ├── main.c
    ├── fs.c
    ├── commands.c
    ├── utils.c
└── bin
    └── filesys (produced after running `make`)
```

Description of Key Files:
- `main.c`: Entry point of the program. Handles command-line arguments, mounts the image, launches the shell, and unmounts on exit.
- `fs.c`: Core FAT32 file system operations (reading/writing sectors, manipulating FAT, directory entries, cluster chains, file and directory operations).
- `commands.c`: Implements the shell command parsing and executes the corresponding fs_* functions.
- `utils.c`: Utility functions for parsing flags, trimming whitespace, formatting names, and printing errors.
- `fat32.h`, `fs.h`, `utils.h`, `commands.h`: Header files providing function prototypes and structures shared across the codebase.

## How to Compile and Run

Compilation:
```
make
```
This produces the filesys executable in the bin directory.

Running:
```
./bin/filesys [FAT32_IMAGE]
```

Example:
```
./bin/filesys fat32.img
```
This will mount the given FAT32 image and present a shell prompt. Type info, ls, cd, mkdir, creat, open, close, lsof, size, lseek, read, write, rename, rm, rmdir, or exit to manipulate and inspect the file system image.

