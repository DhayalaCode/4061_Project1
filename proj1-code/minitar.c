#include "minitar.h"

#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 128
#define BLOCK_SIZE 512
// Constants for tar compatibility information
#define MAGIC "ustar"

// Constants to represent different file types
// We'll only use regular files in this project
#define REGTYPE '0'
#define DIRTYPE '5'

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *) header;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/* Helper Function to check if a block is completely empty (all zeroes)
 */
int is_empty_block(const char *block) {
    if (block == NULL) {
        return 0;    // Or handle the error as appropriate
    }
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (block[i] != '\0') {
            return 0;    // Block is not empty
        }
    }
    return 1;    // Block is empty
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, BLOCK_SIZE);
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100);    // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o",
             stat_buf.st_mode & 07777);    // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid);    // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid);       // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32);    // Owner name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid);    // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid);        // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32);    // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o",
             (unsigned) stat_buf.st_size);    // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o",
             (unsigned) stat_buf.st_mtime);    // Modification time, 0-padded octal
    header->typeflag = REGTYPE;                // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6);          // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2);          // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o",
             major(stat_buf.st_dev));    // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o",
             minor(stat_buf.st_dev));    // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];

    struct stat stat_buf;
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    off_t file_size = stat_buf.st_size;
    if (nbytes > file_size) {
        file_size = 0;
    } else {
        file_size -= nbytes;
    }

    if (truncate(file_name, file_size) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

int create_archive(const char *archive_name, const file_list_t *files) {
    // Open the archive file for writing (overwrite if it exists)
    FILE *archive_fp = fopen(archive_name, "wb");
    if (!archive_fp) {
        perror("Error: Failed to open archive file for writing");
        return -1;
    }

    // Iterating through each file in the linked list
    const node_t *current = files->head;
    while (current != NULL) {
        FILE *file_fp = fopen(current->name, "rb");
        if (!file_fp) {
            perror("Error: Failed to open member file");
            if (fclose(archive_fp) != 0) {    // checking if file actually closed
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        // Create and populate the tar header
        tar_header header;
        if (fill_tar_header(&header, current->name) != 0) {
            perror("Error: Failed to create tar header");
            if (fclose(file_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            if (fclose(archive_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        // Write the header to the archive
        if (fwrite(&header, BLOCK_SIZE, 1, archive_fp) != 1) {
            perror("Error: Failed to write header to archive");
            if (fclose(file_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            if (fclose(archive_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        // Write the file contents to the archive in 512-byte blocks
        char buffer[512] = {0};
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_fp)) > 0) {
            if (ferror(file_fp)) {
                perror("Error reading from file");
            }
            // Pad the last block with zeros
            if (bytes_read < sizeof(buffer)) {
                memset(buffer + bytes_read, 0, sizeof(buffer) - bytes_read);
            }

            // Writing the block to the archive
            if (fwrite(buffer, sizeof(buffer), 1, archive_fp) != 1) {
                perror("Error: Failed to write file contents to archive");
                if (fclose(file_fp) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                if (fclose(archive_fp) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            }
        }

        if (fclose(file_fp) != 0) {
            printf("Error closing file.");
            return -1;
        }
        current = current->next;
    }

    // 2 tar footers made of zeroes.
    char zeros[512] = {0};
    for (int i = 0; i < 2; i++) {
        if (fwrite(zeros, sizeof(zeros), 1, archive_fp) != 1) {
            perror("Error: Failed to write footer to archive");
            if (fclose(archive_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }
    }

    if (fclose(archive_fp) != 0) {
        printf("Error closing file.");
        return -1;
    }
    return 0;
}

int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    if (remove_trailing_bytes(archive_name, 2 * 512) != 0) {
        perror("Could not remove the 2 archive footers.");
        return -1;
    }

    FILE *archive_fpointer = fopen(archive_name, "a");
    if (!archive_fpointer) {
        perror("Error with archive file opening.");
        return -1;
    }
    // let Dhayalan continue with this
    // Iterating through each file in the linked list
    if (fseek(archive_fpointer, 0, SEEK_END) != 0) {
        perror("Error seeking to end of current archive file.");
        if (fclose(archive_fpointer) != 0) {
            printf("Error closing file.");
            return -1;
        }
        return -1;
    }

    const node_t *current = files->head;
    while (current != NULL) {
        FILE *file_fp = fopen(current->name, "rb");
        // printf("%s\n", current->name);

        if (!file_fp) {
            perror("Error: Failed to open member file");
            if (fclose(archive_fpointer) != 0) {    // checking if file actually closed
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }
        //  next two lines create and write a tar header.
        tar_header header;
        if (fill_tar_header(&header, current->name) != 0) {
            perror("Error: Failed to create tar header");
            if (fclose(file_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            if (fclose(archive_fpointer) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        if (fwrite(&header, 512, 1, archive_fpointer) != 1) {
            perror("Error: Failed to write file contents to archive");
            if (fclose(file_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            if (fclose(archive_fpointer) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }
        char buffer[512] = {0};
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_fp)) > 0) {
            if (ferror(file_fp)) {
                perror("Error reading from file");
            }
            // Pad the last block with zeros
            if (bytes_read < sizeof(buffer)) {
                memset(buffer + bytes_read, 0, sizeof(buffer) - bytes_read);
            }

            // Writing the block to the archive
            if (fwrite(buffer, sizeof(buffer), 1, archive_fpointer) != 1) {
                perror("Error: Failed to write file contents to archive");
                if (fclose(file_fp) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                if (fclose(archive_fpointer) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            }
    }
    current = current->next;
    if (fclose(file_fp) != 0) {
        printf("Error closing file.");
        return -1;
    }
}

char zeros[512] = {0};
for (int i = 0; i < 2; i++) {
    if (fwrite(zeros, sizeof(zeros), 1, archive_fpointer) != 1) {
        perror("Error: Failed to write footer to archive");
        if (fclose(archive_fpointer) != 0) {
            printf("Error closing file.");
            return -1;
        }
        return -1;
    }
}

if (fclose(archive_fpointer) != 0) {
    printf("Error closing file.");
    return -1;
}
return 0;
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    // Open the archive file for reading purposes
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Unable to open archive file");
        return -1;
    }

    tar_header header;
    size_t num_read;
    int end_of_archive = 0;
    // Continue the loop while we haven't hit the end of the archive and we continue reading in
    // BLOCK_SIZE (512) bits
    while (!end_of_archive && (num_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        // If the block is empty check if the next block is empty
        if (is_empty_block((char *) &header)) {
            // Peek at the next header block
            long current_pos = ftell(archive);    // Save current position
            tar_header next_header;
            if (fread(&next_header, 1, BLOCK_SIZE, archive) != BLOCK_SIZE) {
                // Couldn't read a full header; assume end of archive
                break;
            }

            if (is_empty_block((char *) &next_header)) {
                // Two consecutive empty blocks found: end of archive.
                end_of_archive = 1;
            } else {
                // Not two consecutive empty blocks:
                // Rewind back one header block so that next_header is processed in the next
                // iteration.
                if (fseek(archive, current_pos, SEEK_SET) != 0) {
                    perror("Error seeking back in archive");
                    fclose(archive);
                    return -1;
                }
            }
            continue;
        }

        // Truncate name if necessary to fit into the file list node
        char truncated_name[MAX_NAME_LEN + 1];
        strncpy(truncated_name, header.name, MAX_NAME_LEN);
        truncated_name[MAX_NAME_LEN] = '\0';
        // Add the file name to the file list
        if (file_list_add(files, truncated_name) != 0) {
            perror("Failed to add file to the list");
            fclose(archive);
            return -1;
        }

        // Calculate the number of blocks to skip for the file content
        long file_size = strtol(header.size, NULL, 8);
        long blocks_to_skip = (file_size + 511) / 512;
        // Move the cursor to th enext header block
        if (fseek(archive, blocks_to_skip * 512, SEEK_CUR) != 0) {
            perror("Error seeking in archive");
            fclose(archive);
            return -1;
        }
    }

    if (ferror(archive) != 0) {
        perror("Error reading archive file");
        fclose(archive);
        return -1;
    }

    if (fclose(archive) != 0) {
        perror("Error closing file.");
        return -1;
    }
    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    // Open the archive file in binary read mode.
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Error opening archive file");
        return -1;
    }

    tar_header header;
    int end_of_archive = 0;
    size_t num_read;
    // Buffer for constructing the full file name. Adjust the size if needed.
    char full_file_name[256];

    // Process each header block until we hit an empty block (end-of-archive)
    while (!end_of_archive && (num_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        // If the block is empty, check if the next block is empty
        if (is_empty_block((char *) &header)) {
            // Peek at the next header block
            long current_pos = ftell(archive);    // Save current position
            tar_header next_header;
            if (fread(&next_header, 1, BLOCK_SIZE, archive) != BLOCK_SIZE) {
                // Couldn't read a full header; assume end of archive
                break;
            }

            if (is_empty_block((char *) &next_header)) {
                // Two consecutive empty blocks found: end of archive.
                end_of_archive = 1;
            } else {
                // Not two consecutive empty blocks:
                // Rewind back one header block so that next_header is processed in the next
                // iteration.
                if (fseek(archive, current_pos, SEEK_SET) != 0) {
                    perror("Error seeking back in archive");
                    fclose(archive);
                    return -1;
                }
            }
            continue;
        }

        // Construct the full file name using prefix (if any) and name.
        if (header.prefix[0] != '\0') {
            snprintf(full_file_name, sizeof(full_file_name), "%s/%s", header.prefix, header.name);
        } else {
            snprintf(full_file_name, sizeof(full_file_name), "%s", header.name);
        }

        // Convert file size from an octal string to a long integer.
        long file_size = strtol(header.size, NULL, 8);

        // Open the output file for writing in binary mode.
        FILE *out = fopen(full_file_name, "wb");
        if (!out) {
            perror("Error creating output file");
            fclose(archive);    // Ensure we close the archive as well
            return -1;
        }

        // Determine the number of blocks that the file occupies (including padding)
        int blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        char buffer[BLOCK_SIZE];
        long remaining = file_size;    // The actual number of bytes to write

        for (int i = 0; i < blocks; i++) {
            size_t bytes_read = fread(buffer, 1, BLOCK_SIZE, archive);
            if (bytes_read != BLOCK_SIZE) {
                perror("Error reading file content from archive");
                fclose(out);
                fclose(archive);
                return -1;
            }
            // For the last block, only write the remaining bytes of the file.
            size_t to_write = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
            if (fwrite(buffer, 1, to_write, out) != to_write) {
                perror("Error writing to output file");
                fclose(out);
                fclose(archive);
                return -1;
            }
            remaining -= to_write;
        }
        fclose(out);
    }

    fclose(archive);
    return 0;
}

int is_file_in_archive(const char *archive_name, const char *file_name) {
   FILE *archive_fp = fopen(archive_name, "rb");
   if (!archive_fp) return -1;

   tar_header header;
   while (fread(&header, 512, 1, archive_fp) == 1) {
       if (strcmp(header.name, file_name) == 0) {//do the names match?
           fclose(archive_fp);
           return 1;
       }

       // Skip file contents blocks
       long file_size;
       sscanf(header.size, "%lo", &file_size);
       long blocks = (file_size + 511) / 512; // Round up to nearest block
       fseek(archive_fp, blocks * 512, SEEK_CUR);
   }

   if (fclose(archive_fp) != 0) {
        perror("Error closing file.");
        return -1;
    }
   return 0;
}

int update_archive(const char *archive_name, const file_list_t *files) {
   const node_t *current = files->head;
   while (current != NULL) {
       if (!is_file_in_archive(archive_name, current->name)) {
           printf("Error: One or more of the specified files is not already present in archive");
           return -1;
       }
       current = current->next;
   }
   return append_files_to_archive(archive_name, files);
}

// Helper function to print the contents of the file list
void print_file_list(const file_list_t *list) {
    // Traverse the linked list starting at head and print each file name.
    for (node_t *node = list->head; node != NULL; node = node->next) {
        printf("%s\n", node->name);
    }
}
