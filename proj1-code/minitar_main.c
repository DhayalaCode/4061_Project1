#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

/*
 * main - Entry point for the minitar program.
 *
 * Usage:
 *   ./minitar <operation> -f <archive_name> <file_name_1> <file_name_2> ... <file_name_n>
 *
 * Supported operations:
 *   -c : Create a new archive.
 *   -a : Append files to an existing archive.
 *   -t : List the contents of an archive.
 *   -u : Update files in the archive (only if they already exist in the archive).
 *   -x : Extract all files from the archive.
 *
 * The "-f" flag specifies the archive file name. Any additional file names (after "-f" and the
 * archive name) are used as input for the create, append, or update operations.
 */
int main(int argc, char **argv) {
    // Check that at least the minimum arguments are provided.
    if (argc < 4) {
        fprintf(stderr, "Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 1;
    }

    // Retrieve and validate the operation flag.
    char *operation = argv[1];
    if (strcmp(operation, "-c") != 0 && strcmp(operation, "-a") != 0 &&
        strcmp(operation, "-t") != 0 && strcmp(operation, "-u") != 0 &&
        strcmp(operation, "-x") != 0) {
        fprintf(stderr, "Error: Invalid operation flag '%s'\n", operation);
        return 1;
    }

    // Verify that the second argument is the "-f" flag (denoting the archive file).
    if (strcmp(argv[2], "-f") != 0) {
        fprintf(stderr, "Error: missing -f flag\n");
        return 1;
    }

    // Set the archive name (provided after the "-f" flag).
    char *archive_name = argv[3];

    // Initialize a file_list_t structure to hold file names from the command line.
    file_list_t files;
    file_list_init(&files);

    // For operations that require file arguments (create, append, update), add each provided file
    // name.
    for (int i = 4; i < argc; i++) {
        if (file_list_add(&files, argv[i]) != 0) {
            fprintf(stderr, "Error: Could not add file '%s' to linked list\n", argv[i]);
            file_list_clear(&files);
            return 1;
        }
    }

    int result = 0;    // This variable will store the return code from the chosen operation.

    if (strcmp(operation, "-c") == 0) {
        // Create a new archive with the specified files.
        result = create_archive(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to create archive.\n");
        }
    } else if (strcmp(operation, "-a") == 0) {
        // Append new files to an existing archive.
        result = append_files_to_archive(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to append files to archive.\n");
        }
    } else if (strcmp(operation, "-t") == 0) {
        // List the contents of the archive:
        // Populate the file list with file names extracted from the archive.
        result = get_archive_file_list(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to list archive contents.\n");
        } else {
            // If successful, print the file names to the terminal.
            for (node_t *node = files.head; node != NULL; node = node->next) {
                printf("%s\n", node->name);
            }
        }
    } else if (strcmp(operation, "-u") == 0) {
        /*
         * Update branch:
         *
         * We must ensure that every file specified on the command line is already present in the
         * archive. Since the function is_file_in_archive has been removed, we retrieve the list of
         * file names from the archive using get_archive_file_list() into a temporary list and then
         * use file_list_contains() to check membership.
         */
        file_list_t archive_list;
        file_list_init(&archive_list);

        // Retrieve the list of files already in the archive.
        if (get_archive_file_list(archive_name, &archive_list) != 0) {
            fprintf(stderr, "Error: Failed to retrieve archive file list.\n");
            file_list_clear(&files);
            file_list_clear(&archive_list);
            return 1;
        }

        // For each file specified on the command line, check if it exists in the archive.
        const node_t *current = files.head;
        while (current != NULL) {
            if (!file_list_contains(&archive_list, current->name)) {
                fprintf(
                    stderr,
                    "Error: One or more of the specified files is not already present in archive");
                file_list_clear(&files);
                file_list_clear(&archive_list);
                return 1;
            }
            current = current->next;
        }
        file_list_clear(&archive_list);    // Clean up the temporary archive list.

        // If all files are present, update the archive.
        result = append_files_to_archive(archive_name, &files);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to update archive.\n");
        }
    } else if (strcmp(operation, "-x") == 0) {
        // Extract all files from the archive into the current directory.
        result = extract_files_from_archive(archive_name);
        if (result != 0) {
            fprintf(stderr, "Error: Failed to extract files from archive.\n");
        }
    }

    // Clean up the file list (free any allocated memory).
    file_list_clear(&files);

    // If any operation failed, print a final error message.
    if (result != 0) {
        fprintf(stderr, "Error: Archive operation failed.\n");
        return 1;
    }

    // Exit successfully.
    return 0;
}
