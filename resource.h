/* ======================================================================
 * resource.h
 * Resource ID definitions for MML Transposer Win16.
 * Contains identifiers for dialogs, menus, controls, and UI elements
 * used by the 16-bit Windows edition of the application.
 * ====================================================================== */

#ifndef RESOURCE_H_INCLUDED
#define RESOURCE_H_INCLUDED

/* Main dialog */
#define IDD_MAIN_DIALOG        100

/* Control IDs */
#define IDC_STATIC_FILENAME    1000

/* Shift / Key controls */
#define IDC_SLIDER_SHIFT       1001
#define IDC_EDIT_SHIFT         1002
#define IDC_SPIN_SHIFT         1003

/* Mode checkboxes */
#define IDC_CHECK_FORMAT       1004
#define IDC_CHECK_REL          1005
#define IDC_CHECK_ABS          1006
#define IDC_CHECK_AUTOSAVE     1007

/* Buttons */
#define IDC_BUTTON_SAVE_AUTO   1008
#define IDC_BUTTON_SAVE_AS     1009

/* Labels */
#define IDC_STATIC_KEY         1010
#define IDC_STATIC_OPTION      1011
#define IDC_CHECK_DSHIFT       1012  /* D ch */

/* Menu */
#define IDR_MAINMENU           200
#define IDM_FILE_OPEN          201
#define IDM_FILE_EXIT          202
#define IDM_LANG_EN            210
#define IDM_LANG_JP            211

/* Custom file dialog */
#define IDD_FILE_DIALOG        300
#define IDC_LIST_FILES         2001
#define IDC_LIST_DRIVES        2002
#define IDC_EDIT_PATH          2003
#define IDC_EDIT_TYPE          2004
#define IDC_STATIC_PATH        2005
#define IDC_STATIC_TYPE        2006

#endif /* RESOURCE_H_INCLUDED */

