#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

// argc is the argument count and argv is the string of arguments
// Usage: ./minitar <operation> -f <archive_name> <file_name_1> <file_name_2> ... <file_name_n>
int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 1;
    }

    // Validate operation flag
    char *operation = argv[1];
    if (strcmp(operation, "-c") != 0 && strcmp(operation, "-a") != 0 &&
        strcmp(operation, "-t") != 0 && strcmp(operation, "-u") != 0 &&
        strcmp(operation, "-x") != 0) {
        fprintf(stderr, "Error: Invalid operation flag '%s'\n", operation);
        return 1;
    }

    // Validate -f flag
    if (strcmp(argv[2], "-f") != 0) {
        fprintf(stderr, "Error: missing -f flag\n");
        return 1;
    }

    // Set archive name and initialize the file list
    char *archive_name = argv[3];
    file_list_t files;
    file_list_init(&files);

    // Add any additional file arguments to the linked list
    for (int i = 4; i < argc; i++) {
        if (file_list_add(&files, argv[i]) != 0) {
            fprintf(stderr, "Error: Could not add file '%s' to linked list\n", argv[i]);
            file_list_clear(&files);
            return 1;
        }
    }

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
        // Populate the file list with archive contents
        result = get_archive_file_list(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to list archive contents.\n");
        } else {
            // Print the list to the terminal
            print_file_list(&files);
        }
    } else if (strcmp(operation, "-u") == 0) {    /// LOOK HERE FOR UPDATE FUNCTION.
        // Check if all files are present in the archive
        const node_t *current = files.head;
        while (current != NULL) {
            int check = is_file_in_archive(archive_name, current->name);
            if (check == -1) { //error checking
                fprintf(stderr, "Error: Failed to check if file '%s' exists in archive.\n", current->name);
                file_list_clear(&files);
                return 1;
            } else if (check == 0) {
                printf("Error: One or more of the specified files is not already present in archive");
                file_list_clear(&files);
                return 1;
            }
            current = current->next;
        }
        // Call update function
        if (update_archive(archive_name, &files) != 0) {
            file_list_clear(&files);
            return 1;
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
