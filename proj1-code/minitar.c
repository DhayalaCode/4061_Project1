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
    FILE *archive_fpointer = fopen(archive_name, "a");
    if (!archive_fpointer) {
        perror("Error with archive.");
        return -1;
    }
    // let Dhayalan continue with this
    return 0;
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Unable to open archive file");
        return -1;
    }

    tar_header header;
    size_t num_read;
    int end_of_archive = 0;

    while (!end_of_archive && (num_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
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

        if (file_list_add(files, truncated_name) != 0) {
            perror("Failed to add file to the list");
            fclose(archive);
            return -1;
        }

        // Calculate the number of blocks to skip for the file content
        long file_size = strtol(header.size, NULL, 8);
        long blocks_to_skip = (file_size + 511) / 512;

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
    // TODO: Not yet implemented
    return 0;
}

int is_file_in_archive(const char *archive_name, const char *file_name) {
    // check if the file is in the archive. (HELPER FUNCTIOm)
    return 0;
}
