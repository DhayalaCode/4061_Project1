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

/*
 * Definitions of constants:
 * NUM_TRAILING_BLOCKS: Number of zero-filled blocks appended at the end of a tar archive.
 * MAX_MSG_LEN: Maximum length for temporary error messages.
 * BLOCK_SIZE: Tar files always use 512-byte blocks.
 */
#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 128
#define BLOCK_SIZE 512

#define MAGIC "ustar"
#define REGTYPE '0'
#define DIRTYPE '5'

/*
 * compute_checksum - Compute and update the checksum for a tar header block.
 * @header: Pointer to a tar_header structure to be updated.
 *
 * This function initializes the header's checksum field with spaces (as required by the POSIX
 * tar standard), computes the sum of all bytes in the header (treated as unsigned values),
 * and then writes the checksum back into the header's checksum field in octal format.
 */
void compute_checksum(tar_header *header) {
    /* Initialize the checksum field with spaces */
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *) header;
    /* Sum all bytes in the header block */
    for (int i = 0; i < BLOCK_SIZE; i++) {
        sum += bytes[i];
    }
    /* Write the checksum (in octal, 7 digits) into the checksum field */
    snprintf(header->chksum, 8, "%07o", sum);
}

/**
 * is_empty_block - Determine if a block of memory (of size BLOCK_SIZE) is entirely empty.
 * @block: Pointer to the memory block to be checked.
 *
 * Returns:
 *   1 if every byte in the block is '\0' (empty),
 *   0 if at least one byte is non-zero.
 *  -1 if the block is NULL (error)
 *
 * This function is used to detect the end-of-archive marker in tar archives, which consists
 * of two consecutive 512-byte blocks of zeros.
 */
int is_empty_block(const char *block) {
    if (block == NULL) {
        perror("NULL pointer exception: the block you are checking is of NULL value");
        return -1;
    }
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (block[i] != '\0') {
            return 0;    // Block is not empty
        }
    }
    return 1;    // Block is empty
}

/*
 * fill_tar_header - Populate a tar header block with file metadata.
 * @header: Pointer to the tar_header structure that will be populated.
 * @file_name: Name of the file whose metadata is used.
 *
 * This function uses stat() to retrieve file metadata (such as size, modification time,
 * permissions, user/group IDs) and fills in the corresponding fields in the tar header.
 * It also performs user and group lookups and computes the checksum.
 *
 * Returns 0 on success or -1 if an error occurs.
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    /* Clear the header block to ensure no leftover data */
    memset(header, 0, BLOCK_SIZE);
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    /* Retrieve file metadata; exit if stat fails */
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    /* Copy file name into header (up to 100 characters) */
    strncpy(header->name, file_name, 100);
    /* Format and store file mode (permissions) in octal format */
    snprintf(header->mode, 8, "%07o", stat_buf.st_mode & 07777);

    /* Store owner UID and lookup owner's name */
    snprintf(header->uid, 8, "%07o", stat_buf.st_uid);
    struct passwd *pwd = getpwuid(stat_buf.st_uid);
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32);

    /* Store group GID and lookup group's name */
    snprintf(header->gid, 8, "%07o", stat_buf.st_gid);
    struct group *grp = getgrgid(stat_buf.st_gid);
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32);

    /* Format and store file size and modification time in octal format */
    snprintf(header->size, 12, "%011o", (unsigned) stat_buf.st_size);
    snprintf(header->mtime, 12, "%011o", (unsigned) stat_buf.st_mtime);
    header->typeflag = REGTYPE;    // Set file type to regular file

    /* Set tar-specific fields */
    strncpy(header->magic, MAGIC, 6);
    memcpy(header->version, "00", 2);
    snprintf(header->devmajor, 8, "%07o", major(stat_buf.st_dev));
    snprintf(header->devminor, 8, "%07o", minor(stat_buf.st_dev));

    /* Compute the checksum and store it in the header */
    compute_checksum(header);
    return 0;
}

/*
 * remove_trailing_bytes - Truncate a file by removing bytes from its end.
 * @file_name: Name of the file to be truncated.
 * @nbytes: Number of bytes to remove from the file's end.
 *
 * This function obtains the current file size using stat(), subtracts nbytes (if possible),
 * and then calls truncate() to resize the file.
 *
 * Returns 0 on success or -1 on failure.
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
    /* Adjust the file size by subtracting nbytes */
    if (nbytes > file_size) {
        file_size = 0;
    } else {
        file_size -= nbytes;
    }

    /* Truncate the file to the new size */
    if (truncate(file_name, file_size) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

/*
 * create_archive - Create a tar archive containing all files in a given list.
 * @archive_name: Name of the archive file to create.
 * @files: Pointer to a file_list_t structure containing the list of file names.
 *
 * This function opens the archive file for writing (overwriting any existing file), iterates
 * over each file in the list, creates a tar header using fill_tar_header(), writes the header and
 * the file's contents (in 512-byte blocks), and finally writes two 512-byte blocks of zeros as
 * footers to signal the end of the archive.
 *
 * Returns 0 on success or -1 if an error occurs.
 */
int create_archive(const char *archive_name, const file_list_t *files) {
    /* Open archive file for writing in binary mode */
    FILE *archive_fp = fopen(archive_name, "wb");
    if (!archive_fp) {
        perror("Error: Failed to open archive file for writing");
        return -1;
    }

    /* Iterate over each file in the list */
    const node_t *current = files->head;
    while (current != NULL) {
        /* Open the current file in binary read mode */
        FILE *file_fp = fopen(current->name, "rb");
        if (!file_fp) {
            perror("Error: Failed to open member file");
            if (fclose(archive_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Create and fill the tar header for this file */
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

        /* Write the tar header to the archive */
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

        /* Read and write file contents in 512-byte blocks */
        char buffer[512] = {0};
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_fp)) > 0) {
            if (ferror(file_fp)) {
                perror("Error reading from file");
            }
            /* If the block is not completely full, pad with zeros */
            if (bytes_read < sizeof(buffer)) {
                memset(buffer + bytes_read, 0, sizeof(buffer) - bytes_read);
            }
            /* Write the block to the archive */
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

        /* Close the current file */
        if (fclose(file_fp) != 0) {
            printf("Error closing file.");
            return -1;
        }
        current = current->next;
    }

    /* Write two trailing 512-byte blocks of zeros as the archive footer */
    char zeros[512] = {0};
    for (int i = 0; i < NUM_TRAILING_BLOCKS; i++) {
        if (fwrite(zeros, sizeof(zeros), 1, archive_fp) != 1) {
            perror("Error: Failed to write footer to archive");
            if (fclose(archive_fp) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }
    }

    /* Close the archive file */
    if (fclose(archive_fp) != 0) {
        printf("Error closing file.");
        return -1;
    }
    return 0;
}

/*
 * append_files_to_archive - Append new files to an existing tar archive.
 * @archive_name: Name of the archive to update.
 * @files: Pointer to a file_list_t containing new file names to append.
 *
 * This function first removes the two trailing footer blocks from the archive using
 * remove_trailing_bytes(). It then opens the archive in append mode, seeks to the end,
 * and appends each new file (writing a tar header and the file contents). Finally, it
 * writes two new footer blocks to the archive.
 *
 * Returns 0 on success or -1 if an error occurs.
 */
int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    /* Remove the existing two footer blocks from the archive */
    if (remove_trailing_bytes(archive_name, 2 * 512) != 0) {
        perror("Could not remove the 2 archive footers.");
        return -1;
    }

    /* Open the archive in append mode */
    FILE *archive_fpointer = fopen(archive_name, "a");
    if (!archive_fpointer) {
        perror("Error with archive file opening.");
        return -1;
    }

    // Iterating through each file in the linked list
    if (fseek(archive_fpointer, 0, SEEK_END) != 0) {
        perror("Error seeking to end of current archive file.");
        if (fclose(archive_fpointer) != 0) {
            printf("Error closing file.");
            return -1;
        }
        return -1;
    }

    /* Iterate over each file to be appended */
    const node_t *current = files->head;
    while (current != NULL) {
        /* Open the file for reading */
        FILE *file_fp = fopen(current->name, "rb");
        if (!file_fp) {
            perror("Error: Failed to open member file");
            if (fclose(archive_fpointer) != 0) {    // checking if file actually closed
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Create and fill the tar header for this file */
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

        /* Write the header block to the archive */
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

        /* Read file content in blocks and write them to the archive */
        char buffer[512] = {0};
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_fp)) > 0) {
            if (ferror(file_fp)) {
                perror("Error reading from file");
            }
            /* Pad the last block with zeros if necessary */
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
        /* Move to the next file and close the current one */
        current = current->next;
        if (fclose(file_fp) != 0) {
            printf("Error closing file.");
            return -1;
        }
    }

    /* Write two new footer blocks to the archive */
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

    /* Close the archive file */
    if (fclose(archive_fpointer) != 0) {
        printf("Error closing file.");
        return -1;
    }
    return 0;
}

/*
 * get_archive_file_list - Populate a file_list_t with names of files from an archive.
 * @archive_name: Name of the tar archive to read.
 * @files: Pointer to a file_list_t structure to be populated.
 *
 * This function reads the archive 512 bytes at a time, extracting the file name from each
 * header block and skipping the file's content blocks (based on the stored file size).
 * It stops processing when it encounters two consecutive empty blocks.
 *
 * Returns 0 on success or -1 if an error occurs.
 */
int get_archive_file_list(const char *archive_name, file_list_t *files) {
    /* Open the archive file for reading in binary mode */
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Unable to open archive file");
        return -1;
    }

    tar_header header;
    size_t num_read;
    int end_of_archive = 0;
    /* Process the archive one 512-byte block at a time */
    while (!end_of_archive && (num_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        /* Check if the block is empty */
        if (is_empty_block((char *) &header)) {
            /* Peek at the next block to check for the archive end marker */
            long current_pos = ftell(archive);
            tar_header next_header;
            if (fread(&next_header, 1, BLOCK_SIZE, archive) != BLOCK_SIZE) {
                break;
            }

            if (is_empty_block((char *) &next_header)) {
                /* Two consecutive empty blocks indicate the end of the archive */
                end_of_archive = 1;
            } else if (is_empty_block((char *) &next_header) == -1) {
                /* An error has occured */
                perror("Error checking if the block is empty");
                if (fclose(archive) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            } else {
                /* Not the end; rewind so the next header is processed */
                if (fseek(archive, current_pos, SEEK_SET) != 0) {
                    perror("Error seeking back in archive");
                    if (fclose(archive) != 0) {
                        printf("Error closing file.");
                        return -1;
                    }
                    return -1;
                }
            }
            continue;
        } else if (is_empty_block((char *) &header) == -1) {
            /* An error has occured */
            perror("Error checking if the block is empty");
            if (fclose(archive) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Truncate file name if necessary and ensure null-termination */
        char truncated_name[MAX_NAME_LEN + 1];
        strncpy(truncated_name, header.name, MAX_NAME_LEN);
        truncated_name[MAX_NAME_LEN] = '\0';
        /* Add the file name to the file list */
        if (file_list_add(files, truncated_name) != 0) {
            perror("Failed to add file to the list");
            if (fclose(archive) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Calculate the number of blocks for the file's contents */
        long file_size = strtol(header.size, NULL, 8);
        long blocks_to_skip = (file_size + 511) / 512;
        /* Skip the content blocks to reach the next header */
        if (fseek(archive, blocks_to_skip * BLOCK_SIZE, SEEK_CUR) != 0) {
            perror("Error seeking in archive");
            if (fclose(archive) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }
    }

    /* Check for reading errors */
    if (ferror(archive) != 0) {
        perror("Error reading archive file");
        if (fclose(archive) != 0) {
            printf("Error closing file.");
            return -1;
        }
        return -1;
    }

    if (fclose(archive) != 0) {
        perror("Error closing file.");
        return -1;
    }
    return 0;
}

/*
 * extract_files_from_archive - Extract all files from a tar archive to the current directory.
 * @archive_name: Name of the tar archive file.
 *
 * This function reads each header block from the archive, constructs the full file name (using
 * the prefix field if present), and then reads the corresponding file content blocks. Each file
 * is written to disk in binary mode. Extraction stops when two consecutive empty blocks are
 * encountered.
 *
 * Returns 0 on success or -1 if an error occurred.
 */
int extract_files_from_archive(const char *archive_name) {
    /* Open the archive file in binary read mode */
    FILE *archive = fopen(archive_name, "rb");
    if (!archive) {
        perror("Error opening archive file");
        return -1;
    }

    tar_header header;
    int end_of_archive = 0;
    size_t num_read;
    /* Buffer to hold the complete file name (prefix + name) */
    char full_file_name[256];

    /* Process each header block until the end-of-archive marker is reached */
    while (!end_of_archive && (num_read = fread(&header, 1, BLOCK_SIZE, archive)) == BLOCK_SIZE) {
        /* Check if the block is empty */
        if (is_empty_block((char *) &header)) {
            /* Peek at the next block to check for the archive end marker */
            long current_pos = ftell(archive);
            if (current_pos == -1L) {
                perror("Error: ftell() failed");
                fclose(archive);
                return -1;
            }

            tar_header next_header;
            if (fread(&next_header, 1, BLOCK_SIZE, archive) != BLOCK_SIZE) {
                break;
            }

            if (is_empty_block((char *) &next_header)) {
                /* Two consecutive empty blocks indicate the end of the archive */
                end_of_archive = 1;
            } else if (is_empty_block((char *) &next_header) == -1) {
                /* An error has occured */
                perror("Error checking if the block is empty");
                if (fclose(archive) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            } else {
                /* Not the end; rewind so the next header is processed */
                if (fseek(archive, current_pos, SEEK_SET) != 0) {
                    perror("Error seeking back in archive");
                    if (fclose(archive) != 0) {
                        printf("Error closing file.");
                        return -1;
                    }
                    return -1;
                }
            }
            continue;
        } else if (is_empty_block((char *) &header) == -1) {
            /* An error has occured */
            perror("Error checking if the block is empty");
            if (fclose(archive) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Construct the full file name. If a prefix is provided, prepend it with a '/'
         * separator.
         */
        if (header.prefix[0] != '\0') {
            snprintf(full_file_name, sizeof(full_file_name), "%s/%s", header.prefix, header.name);
        } else {
            snprintf(full_file_name, sizeof(full_file_name), "%s", header.name);
        }

        /* Convert the file size (stored as an octal string) to a long integer */
        long file_size = strtol(header.size, NULL, 8);

        /* Open the output file for writing. Note: Directories must already exist. */
        FILE *out = fopen(full_file_name, "wb");
        if (!out) {
            perror("Error creating output file");
            if (fclose(archive) != 0) {
                printf("Error closing file.");
                return -1;
            }
            return -1;
        }

        /* Calculate the number of blocks occupied by the file (with padding) */
        int blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        char buffer[BLOCK_SIZE];
        long remaining = file_size;    // Actual number of bytes to write

        /* Read each block from the archive and write to the output file */
        for (int i = 0; i < blocks; i++) {
            size_t bytes_read = fread(buffer, 1, BLOCK_SIZE, archive);
            if (bytes_read != BLOCK_SIZE) {
                perror("Error reading file content from archive");
                if (fclose(out) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                if (fclose(archive) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            }
            /* For the final block, write only the remaining bytes of the file */
            size_t to_write = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
            if (fwrite(buffer, 1, to_write, out) != to_write) {
                perror("Error writing to output file");
                if (fclose(out) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                if (fclose(archive) != 0) {
                    printf("Error closing file.");
                    return -1;
                }
                return -1;
            }
            remaining -= to_write;
        }
        /* Close the output file once writing is complete */
        if (fclose(out) != 0) {
            perror("Error closing file.");
            return -1;
        }
    }

    if (fclose(archive) != 0) {
        perror("Error closing file.");
        return -1;
    }
    return 0;
}
