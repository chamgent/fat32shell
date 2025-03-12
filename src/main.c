#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
#include "commands.h"
#include "utils.h"

char current_path[512];

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [FAT32 IMAGE]\n", argv[0]);
        return 1;
    }

    if (fs_mount(argv[1]) != 0) {
        fprintf(stderr, "Error: failed to mount image.\n");
        return 1;
    }

    strcpy(current_path, "/"); 

    run_shell();
    fs_unmount();
    return 0;
}

