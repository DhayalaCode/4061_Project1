#include "minitar.h"
#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
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
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
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
    FILE *archive_fpointer = fopen(archive_name, "wb"); //overwrite if exists
    if (!archive_fpointer) {
        perror("Error making archive.");
        return -1;
    }
    // check file_list_t to see why node_t was chosen.
    const node_t *current = files->head; // Iterating through each file in linked list
    while (current != NULL) {
        FILE *current_member_fp = fopen(current->name, "rb");
        if (!current_member_fp) {
            perror("Error opening member file");
            fclose(archive_fpointer);
            return -1;
        }

        tar_header header;
        if (fill_tar_header(&header, current->name) != 0) {
            perror("Error creating header.");
            fclose(current_member_fp);
            fclose(archive_fpointer);
            return -1;
        }

        if (fwrite(&header, 512, 1, archive_fpointer) != 1) {
            perror("Error writing the header");
            fclose(current_member_fp);
            fclose(archive_fpointer);
            return -1;
        }

        char buffer[512] = {0};
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, 512, current_member_fp)) > 0) {
            if (bytes_read < 512) {
                memset(buffer + bytes_read, 0, 512 - bytes_read); 
            }
            if (fwrite(buffer, 512, 1, archive_fpointer) != 1) {
                perror("Error writing file contents");
                fclose(current_member_fp);
                fclose(archive_fpointer);
                return -1;
            }
            memset(buffer, 0, 512); // Clear the buffer
        }
        fclose(current_member_fp);
        current = current->next;
    }

    // Writing the TAR footer (2 blocks of 512 bytes of zeros)
    char zeros[512] = {0};
    for (int i = 0; i < 2; i++) {
        if (fwrite(zeros, 512, 1, archive_fpointer) != 1) {
            perror("Error writing footer");
            fclose(archive_fpointer);
            return -1;
        }
    }

    fclose(archive_fpointer);
    return 0;
}


int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    FILE  *archive_fpointer = fopen(archive_name, "a");
    if(!archive_fpointer) {
        perror("Error with archive.");
        return -1;
    }
    //let Dhayalan continue with this
    return 0;
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    // TODO: Not yet implemented
    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    // TODO: Not yet implemented
    return 0;
}

int is_file_in_archive(const char *archive_name, const char *file_name) {
    //check if the file is in the archive. (HELPER FUNCTIOm)
    return 0;
}

