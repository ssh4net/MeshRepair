#ifndef MESHREPAIR_C_API_H
#define MESHREPAIR_C_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Status codes for C-style entry points
typedef enum MRStatusCode {
    MR_STATUS_OK               = 0,
    MR_STATUS_ERROR            = 1,
    MR_STATUS_EXCEPTION        = 2,
    MR_STATUS_INVALID_ARGUMENT = 3
} MRStatusCode;

// Standard status payload (fixed-size message buffer to avoid allocations)
typedef struct MRStatus {
    MRStatusCode code;
    int exit_code;  // Exit code returned by the underlying function
    char message[256];
} MRStatus;

// Library/app version string (null-terminated, owned by library)
const char*
mr_version(void);

// Run the CLI mode using existing argument parsing/behavior.
// Returns the same exit code as the CLI; fills out_status when provided.
int
mr_run_cli(int argc, char** argv, MRStatus* out_status);

// Run the engine (IPC) mode using existing argument parsing/behavior.
// Returns the same exit code as the engine; fills out_status when provided.
int
mr_run_engine(int argc, char** argv, MRStatus* out_status);

#ifdef __cplusplus
}
#endif

#endif  // MESHREPAIR_C_API_H
