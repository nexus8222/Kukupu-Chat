#include "ui.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void capture_frame(const char *filename, void (*setup_fn)(void))
{
    int saved_stdout = dup(STDOUT_FILENO);
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, STDOUT_FILENO);
    close(fd);

    setup_fn();

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
}

void screen_main(void)
{
    ui_init("alice");
    ui_set_registered(1);
    ui_set_profile_name("Alice");
    ui_set_user_color("\033[1;36m");
    ui_add_user("nexus");
    ui_add_user("bob");
    ui_add_user("matrix_mod");
    ui_add_party("1042");
    ui_add_pm_contact("bob");
    ui_inc_pm_unread("bob");
    ui_add_message(MSG_NORMAL, "bob", "Hey Alice, how is the interactive TUI going?", 1);
    ui_add_message(MSG_SELF, "alice", "Working smoothly! Mouse clicking and modals are fully operational.", 2);
    ui_add_message(MSG_E2EE, "bob", "Verified Signal Double Ratchet with perfect forward secrecy!", 3);
    ui_add_message(MSG_SYSTEM, "Party", "Alice joined Party #1042.", 4);
    ui_trigger_toast("Welcome back Alice! Double Ratchet E2EE is active.", SOUND_SUCCESS);
    ui_render("", 0, "/help - Show command catalog");
    ui_shutdown();
}

void screen_account(void)
{
    ui_init("alice");
    ui_set_registered(1);
    ui_set_profile_name("Alice");
    ui_set_user_color("\033[1;32m");
    ui_add_user("nexus");
    ui_add_user("bob");
    ui_add_party("1042");
    ui_add_pm_contact("bob");
    ui_add_message(MSG_NORMAL, "bob", "Hey Alice, check out the account portal!", 1);
    ui_add_message(MSG_SELF, "alice", "Opening account management settings...", 2);
    ui_open_modal(UI_MODAL_ACCOUNT);
    ui_render("", 0, "");
    ui_shutdown();
}

void screen_party(void)
{
    ui_init("alice");
    ui_set_registered(1);
    ui_set_profile_name("Alice");
    ui_set_user_color("\033[1;35m");
    ui_add_user("nexus");
    ui_add_user("bob");
    ui_add_party("1042");
    ui_add_pm_contact("bob");
    ui_open_modal(UI_MODAL_PARTY_JOIN_CREATE);
    ui_set_party_modal_tab(2); // Create Party Tab
    ui_render("", 0, "");
    ui_shutdown();
}

int main(void)
{
    capture_frame("docs_screen_main.raw", screen_main);
    capture_frame("docs_screen_account.raw", screen_account);
    capture_frame("docs_screen_party.raw", screen_party);
    return 0;
}
