/* manager.c
 *
 * Copyright 2025 EricLin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* This program is similar to `clamscan` from ClamAV, but with some performance improvements */
/*
  * This use process pool to implement parallel scanning, which can significantly improve the scanning speed.
  * Use shared memory to share the engine between processes, which can reduce the memory usage.
*/

/* This is the implementation of the `clamscanp` program */

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "manager.h"

/* Get file stat */
static inline bool get_file_stat(const char *path, struct stat *statbuf) {
	if (stat(path, statbuf) != 0) {
		fprintf(stderr, "Failed to stat %s: %s\n", path, strerror(errno));
		return false;
	}
	return true;
}

/* Check if the given path is a directory */
bool is_directory(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

/* Check if the given path is a regular file */
bool is_regular_file(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

/* Set scan options */
static void set_scan_options(struct cl_scan_options *scanoptions) {
	scanoptions->heuristic |= CL_SCAN_GENERAL_HEURISTICS;
    scanoptions->general |= CL_SCAN_GENERAL_ALLMATCHES;
}

/* Check Initalization status */
static inline bool is_initialized(cl_error_t status) {
	return status == CL_SUCCESS;
}

/* Initialize the ClamAV engine */
struct cl_engine *init_engine(struct cl_scan_options *scanoptions) {
	set_scan_options(scanoptions);

    struct cl_engine *engine;
    unsigned int signatures = 0;
    cl_error_t result; // Initialize result

	// Initialize ClamAV engine
	result = cl_init(CL_INIT_DEFAULT);
	if (!is_initialized(result)) {
		fprintf(stderr, "[ERROR] cl_init failed: %s\n", cl_strerror(result));
		return NULL;
	}

    engine = cl_engine_new(); // Create a new ClamAV engine
    if (engine == NULL) {
        fprintf(stderr, "[ERROR] cl_engine_new failed\n");
        return NULL;
    }

    // Load signatures from database directory
	const char *db_dir = cl_retdbdir(); // Get the database directory
    result = cl_load(db_dir, engine, &signatures, CL_DB_STDOPT);
    if (!is_initialized(result)) {
		fprintf(stderr, "[ERROR] cl_load failed: %s\n", cl_strerror(result));
        cl_engine_free(engine);
        return NULL;
	}

    // Compile the signatures
    result = cl_engine_compile(engine);
    if (!is_initialized(result)) {
		fprintf(stderr, "[ERROR] cl_engine_compile failed: %s\n", cl_strerror(result));
        cl_engine_free(engine);
        return NULL;
	}

    printf("[INFO] ClamAV engine initialized with %u signatures\n", signatures);
    return engine;
}

/* Process scan result */
static void process_scan_result(const char *path, cl_error_t result, const char *virname) {
	switch (result) {
		case CL_SUCCESS:
			printf("%s: OK\n", path);
			break;
		case CL_VIRUS:
			printf("%s: %s FOUND\n", path, virname);
			break;
		default:
			printf("%s: SCAN ERROR: %s\n", path, cl_strerror(result));
			break;
	}	
}

/* Scan a file */
void process_file(const char *path, struct cl_engine *engine, struct cl_scan_options *scanoptions) {
	cl_error_t result;
    int fd = open(path, O_RDONLY); // Open the file
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return;
    }
    
    const char *virname = NULL;
    unsigned long scanned = 0;
    result = cl_scandesc(fd, NULL, &virname, &scanned, engine, scanoptions); // Scan the file
    close(fd);

    process_scan_result(path, result, virname);
}
