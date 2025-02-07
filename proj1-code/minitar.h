// Author: John Kolb <jhkolb@umn.edu>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _MINITAR_H
#define _MINITAR_H
#include "file_list.h"

// Standard tar header layout defined by POSIX
typedef struct {
    // File's name, as a null-terminated string
    char name[100];
    // File's permission bits
    char mode[8];
    // Numerical ID of file's owner, 0-padded octal
    char uid[8];
    // Numerical ID of file's group, 0-padded octal
    char gid[8];
    // Size of file in bytes, 0-padded octal
    char size[12];
    // Modification time of file in Unix epoch time, 0-padded octal
    char mtime[12];
    // Checksum (simple sum) header bytes, 0-padded octal
    char chksum[8];
    // File type (use constants defined below)
    char typeflag;
    // Unused for this project
    char linkname[100];
    // Indicates which tar standard we are using
    char magic[6];
    char version[2];
    // Name of file's user, as a null-terminated string
    char uname[32];
    // Name of file's group, as a null-terminated string
    char gname[32];
    // Major device number, 0-padded octal
    char devmajor[8];
    // Minor device number, 0-padded octal
    char devminor[8];
    // String to prepend to file name above, if name is longer than 100 bytes
    char prefix[155];
    // Padding to bring total struct size up to 512 bytes
    char padding[12];
} tar_header;

/*
 * Create a new archive file with the name 'archive_name'.
 * The archive should contain all files stored in the 'files' list.
 * You can assume in this project that at least one member file is specified.
 * You may also assume that all the elements of 'files' exist.
 * If an archive of the specified name already exists, you should overwrite it
 * with the result of this operation.
 * This function should return 0 upon success or -1 if an error occurred
 */
int create_archive(const char *archive_name, const file_list_t *files);

/*
 * Append each file specified in 'files' to the archive with the name 'archive_name'.
 * You can assume in this project that at least one new file to append is specified.
 * You may also assume that all files to be appended exist.
 * This function should return 0 upon success or -1 if an error occurred.
 */
int append_files_to_archive(const char *archive_name, const file_list_t *files);

/*
 * Add the name of each file contained in the archive identified by 'archive_name'
 * to the 'files' list.
 * NOTE: This function is most obviously relevant to implementing minitar's list
 * operation, but think about how you can reuse it for the update operation.
 * This function should return 0 upon success or -1 if an error occurred.
 */
int get_archive_file_list(const char *archive_name, file_list_t *files);

/*
 * Write each file contained within the archive identified by 'archive_name'
 * as a new file to the current working directory.
 * If there are multiple versions of the same file present in the archive,
 * then only the most recently added version should be present as a new file
 * at the end of the extraction process.
 * This function should return 0 upon success or -1 if an error occurred.
 */
int extract_files_from_archive(const char *archive_name);

/*
 * Determine if a given file is present within an archive.
 *
 * Parameters:
 *   archive_name - Path to the tar archive file.
 *   file_name    - Name of the file to look for within the archive.
 *
 * Returns:
 *   1  if the file is found in the archive.
 *   0  if the file is not present.
 *  -1  if an error occurred while checking the archive.
 */
int is_file_in_archive(const char *archive_name, const char *file_name);

/*
 * Print the list of file names contained in a file_list_t to the standard output.
 *
 * Parameters:
 *   list - Pointer to a file_list_t structure containing the linked list of file nodes.
 *
 * Behavior:
 *   This function iterates through the linked list stored in 'list' and prints each file name,
 *   one per line, to standard output. It is useful for debugging and for implementing the 'list'
 *   operation of the tar utility.
 */
void print_file_list(const file_list_t *list);

/*
 * Update the specified archive with new versions of files.
 *
 * Parameters:
 *   archive_name - Path to the tar archive file to be updated.
 *   files        - Pointer to a file_list_t structure containing the names of files to update.
 *
 * Behavior:
 *   For each file in the provided 'files' list, the function first verifies that the file is
 *   already present in the archive (using is_file_in_archive). If any file is not found, the
 *   update is aborted and the function returns -1. Otherwise, the function appends the new versions
 *   of the files to the archive. When extracting the archive, the most recently added copy of any
 *   file is considered its current version.
 *
 * Returns:
 *   0  on success.
 *  -1  if an error occurred during the update operation.
 */
int update_archive(const char *archive_name, const file_list_t *files);

#endif    // _MINITAR_H
