#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

// argc is the argument count and argv is the string of arguments
// ./minitar <operation> -f <archive_name> <file_name_1> <file_name_2> ... <file_name_n>
int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    // correctly invokes proper archive operations in terms of respective flags
    char *operation = argv[1];
    if (strcmp(operation, "-c") != 0 && strcmp(operation, "-a") != 0 &&
        strcmp(operation, "-t") != 0 && strcmp(operation, "-u") != 0 &&
        strcmp(operation, "-x") != 0) {
        fprintf(stderr, "Error: Invalid operation flags inputted '%s'\n", operation);
        return 1;
    }

    if (strcmp(argv[2], "-f") != 0){
        fprintf(stderr, "Error: missing -f flag");
        return 1;
    }

    //correctly populates linked list
    char *archive_name = argv[3];
    file_list_t files;
    file_list_init(&files);

    for (int i = 4; i <  argc; i++) {
        if (file_list_add(&files, argv[i]) != 0){
            fprintf(stderr, "Error: Could not add file'%s' to linked list", argv[i]);
            file_list_clear(&files);
            return 1;
        }
    }

    //print out file names from main
    const node_t *current = files.head;
    while (current != NULL) {
        printf("%s\n" , current->name);
        current = current -> next;
    }

    //properly checks calls to archive functions
    int result = 0;
    if (strcmp(operation, "-c") == 0) {
        result = create_archive(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to create archive.\n");
        }
    } else if (strcmp(operation, "-a") == 0) {
        result = append_files_to_archive(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to append files to archive.\n");
        }
    } else if (strcmp(operation, "-t") == 0) {
        result = get_archive_file_list(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to list archive contents.\n");
        }
    } else if (strcmp(operation, "-u") == 0) {
        result = append_files_to_archive(archive_name, &files); // Update is similar to append
        if (result != 0) {
            fprintf(stderr, "Error: Failed to update archive.\n");
        }
    } else if (strcmp(operation, "-x") == 0) {
        result = extract_files_from_archive(archive_name);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to extract files from archive.\n");
        }
    }


    file_list_clear(&files);

    if (result != 0) {
        fprintf(stderr, "Error: Archive operation failed.\n");
        return 1;
    }
    return 0;
}
