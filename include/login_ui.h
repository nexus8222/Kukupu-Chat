#ifndef LOGIN_UI_H
#define LOGIN_UI_H

#include "compat.h"
#include <stddef.h>

typedef enum {
    AUTH_RESULT_GUEST = 1,
    AUTH_RESULT_LOGIN,
    AUTH_RESULT_REGISTER,
    AUTH_RESULT_EXIT
} auth_type_t;

// Runs the interactive landing TUI page with actionable dialogs
// Returns 0 on success (with out_username and out_is_registered filled), or -1 on exit/error.
int run_login_tui(socket_t sockfd, char *out_username, size_t ulen, int *out_is_registered);

#endif // LOGIN_UI_H
