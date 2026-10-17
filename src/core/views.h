/*
 * SeedSigner's two screen shapes, as stackable views: ButtonListScreen (a
 * title, optional text, a list of buttons) and the LargeIconStatusScreen
 * family (Success, Warning, DireWarning, Error: a headline, a body that pages,
 * and buttons). Each push takes a slot, so a flow can stack them as
 * SeedSigner's back stack does; the strings are copied, so labels can be
 * built on the fly (a fingerprint, a word count).
 *
 * A view carries SeedSigner's View class name ("SeedOptionsView"), which is
 * what the QEMU harness reports and what docs/COVERAGE.md counts.
 *
 * SeedSigner's Destination options map onto the stack: clear_history is
 * view_clear_history() before the push, skip_current_view is a replace.
 */
#pragma once

#include <stdbool.h>

#include "screens-common.h"

#define VIEW_BUTTONS_MAX 12
#define VIEW_LABEL_MAX 32

typedef struct {
    const char *name;           /* SeedSigner's View class */
    const char *title;
    const char *text;           /* list: a line above the buttons; status: the body */
    const char *headline;       /* status only */
    notice_kind_t kind;         /* status only */
    bool status;                /* false: ButtonListScreen */
    const char *buttons[VIEW_BUTTONS_MAX];
    int n_buttons;
    bool destructive_last;      /* SeedSigner's red label on the last button */
    /* What was chosen: the button's index. Called after nothing is popped;
       the handler decides where to go (push, replace, home). */
    void (*select)(int index);
    /* BACK; NULL pops. */
    void (*back)(void);
    /* Free for the handler: which seed, which script type. */
    int arg;
    /* The button selected when it opens (a list returned to keeps its place). */
    int selected;
} view_spec_t;

/* Push a view; replace the top one; or clear to home first and push. */
bool view_push(const view_spec_t *v);
bool view_replace(const view_spec_t *v);
bool view_push_fresh(const view_spec_t *v);

/* The top view's arg (the handlers' context). */
int view_arg(void);

/* The label of the button the top view or menu has selected, or NULL:
   what OK would choose. For tests, and for the harness. */
const char *ui_selected_label(void);

/* SeedSigner's UnhandledExceptionView: what it shows wherever its code
 * raises. `type` is the exception class SeedSigner raises there, `where`
 * and `message` the two lines under it. "Back to Main Menu" goes home. */
void UnhandledExceptionView(const char *type, const char *where, const char *message);

/* Back to the main menu with nothing else on the stack (clear_history). */
void view_clear_history(void);
