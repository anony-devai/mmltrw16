/* ============================================================
 *  mmltrw16.c - International version for Win16 (also runs on Win9x)
 *  Win16 GUI front-end (w20 + w30 unified)
 *  Part of the MML Transposer project (mmltrwin)
 *  Compatible with OpenWatcom C89 toolchain
 *  English-localized comments and UI strings (no logic changes)
 * ============================================================ */

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <io.h>
#include <dos.h>
#include <direct.h>

#include "resource.h"
#include "mmleng16.h"
#include "msgw16.h"

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

#ifndef OPENFILENAME_SIZE_VERSION_300
#define OPENFILENAME_SIZE_VERSION_300 72
#endif

#ifndef CBS_DROPDOWNLIST
#define CBS_DROPDOWNLIST 0x0003
#endif

// Notification for menu deselection
#define WM_MENU_LOST (WM_USER + 100)

// Parameters for custom file dialog
typedef struct {
    int mode;
    const char* defext;
    const char* initdir;
} FILEDLG_PARAM;

#define FILEDLG_OPEN 0
#define FILEDLG_SAVE 1

// Globals
static HINSTANCE g_hInst = NULL;
static char input_path[MAX_PATH] = "";
static int  g_shift    = 0;
static int  g_mode     = MODE_PURE;
static int  g_autosave = 1;

static BOOL g_has_shell   = FALSE;
static BOOL g_has_commdlg = FALSE;

static LONG g_DlgParam = 0;
static char file_path[MAX_PATH];

static HWND s_hComboType  = NULL;
static BOOL s_useFallback = FALSE;

static BOOL g_menu_hidden = FALSE;
static BOOL g_menu_was_selected = FALSE;
static HMENU g_hMenu = NULL;

static GUILang g_lang = LANG_EN;

// Prototype for MMLToGUIError (Win16)
GUIErrorCode MMLToGUIError(const MMLErrorInfo* err);

// Prototypes
BOOL CALLBACK __export DlgProc(HWND,UINT,WPARAM,LPARAM);
void InitShiftControls(HWND);
static void UpdateModeFromUI(HWND);
static void DoConvertAndSave(HWND,const char*,BOOL);

static GUIErrorCode DoConvertAndSave16(HWND hDlg,
                                       const char* inpath,
                                       BOOL use_autosave,
                                       int shift,
                                       int mode);

static const char* ExtractFileName(const char*);

static GUIErrorCode LoadFile16(const char* path,char** outbuf,DWORD* outsize,DWORD* out_filesize);
static GUIErrorCode SaveFile16(const char* path, const char* data, DWORD size);

void MakeAutoSaveName(const char*,int,int,char*,int);
static int TryCommonDialogOpen(HWND,char*);
static int TryCommonDialogSave(HWND,const char*,char*);
BOOL FAR PASCAL FileDialogProc(HWND,WORD,WORD,LONG);
BOOL SelectOpenPath(HWND,char*);
BOOL SelectSavePath(HWND,const char*,char*);
static int  IsDriveValid(int);
static void AddDrives(HWND);
static void RebuildPlaceList(HWND);
static void RebuildFileList(HWND);
static void GoParentDir(void);
static void HandleDropFiles16(HWND hDlg, HDROP hDrop);
static void ApplyLanguage16(HWND hDlg);

// w20-compatible parameter passing
int MyDialogBoxParam(HINSTANCE hInst,LPCSTR tpl,HWND hWnd,FARPROC proc,LONG param)
{
    g_DlgParam = param;
    return DialogBox(hInst,tpl,hWnd,proc);
}

// WinMain
int PASCAL WinMain(HINSTANCE hInst,HINSTANCE hPrev,LPSTR lpCmd,int nShow)
{
    MSG msg;
    HINSTANCE hShell;
    HINSTANCE hCommdlg;
    g_hInst = hInst;

    /* DLL Check (SHELL / COMMDLG) */
    hShell = LoadLibrary("SHELL.DLL");
    if (hShell) {
        g_has_shell = TRUE;
        FreeLibrary(hShell);
    }

    hCommdlg = LoadLibrary("COMMDLG.DLL");
    if (hCommdlg) {
        g_has_commdlg = TRUE;
        FreeLibrary(hCommdlg);
    }

    PeekMessage(&msg,NULL,0,0,PM_NOREMOVE);
    DialogBox(hInst,MAKEINTRESOURCE(IDD_MAIN_DIALOG),NULL,(DLGPROC)DlgProc);
    PostQuitMessage(0);
    while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// Extract filename from path
static const char* ExtractFileName(const char* path)
{
    const char* p=strrchr(path,'\\');
    if(p) return p+1;
    p=strrchr(path,'/');
    if(p) return p+1;
    return path;
}

// Initialize Shift UI controls
void InitShiftControls(HWND hDlg)
{
    SetScrollRange(GetDlgItem(hDlg,IDC_SPIN_SHIFT),SB_CTL,-12,12,FALSE);
    SetScrollPos  (GetDlgItem(hDlg,IDC_SPIN_SHIFT),SB_CTL,g_shift,TRUE);

    SetScrollRange(GetDlgItem(hDlg,IDC_SLIDER_SHIFT),SB_CTL,0,24,FALSE);
    SetScrollPos  (GetDlgItem(hDlg,IDC_SLIDER_SHIFT),SB_CTL,g_shift+12,TRUE);

    SetDlgItemInt(hDlg,IDC_EDIT_SHIFT,g_shift,TRUE);
}

// Update mode flags from UI
static void UpdateModeFromUI(HWND hDlg)
{
    int fmt = IsDlgButtonChecked(hDlg,IDC_CHECK_FORMAT)==1;
    int rel = IsDlgButtonChecked(hDlg,IDC_CHECK_REL)==1;
    int abs = IsDlgButtonChecked(hDlg,IDC_CHECK_ABS)==1;
    int dch = IsDlgButtonChecked(hDlg,IDC_CHECK_DSHIFT)==1;

    if(rel && abs) abs=0;

    g_mode = (fmt?4:0)|(rel?2:0)|(abs?1:0);
    if(g_mode==3||g_mode==7) g_mode=MODE_PURE;
    if(dch) g_mode |= MODE_NOISE_SHIFT;
}

/* ------------------------------------------------------------
 * ShowError16 - GUI error message generator (Win32-compatible)
 * ------------------------------------------------------------ */
static void ShowError16(HWND hDlg,
                        GUIErrorCode code,
                        const MMLErrorInfo* err_info,
                        DWORD aux)
{
    char msg[512];
    const char* tmpl;

    if (code <= GUI_ERR_OK || code >= GUI_ERR_MAX)
        return;

    tmpl = g_lang_table16[g_lang].gui_msg[code];
    if (!tmpl)
        return;

    switch (code)
    {
    case GUI_ERR_LF_TOO_LARGE:
        wsprintf(msg, tmpl, (unsigned long)aux, MAX_TEXT);
        break;

    case GUI_ERR_MML_OCTAVE:
        wsprintf(msg, tmpl,
                 err_info ? err_info->channel_char : '?',
                 err_info ? err_info->line_number  : 0,
                 err_info ? err_info->calculated_value : 0);
        break;

    case GUI_ERR_MML_UNKNOWN:
        wsprintf(msg, tmpl, aux);
        break;

    default:
        lstrcpy(msg, tmpl);
        break;
    }

    MessageBox(hDlg,
               msg,
               "Error",
               MB_OK | MB_ICONSTOP);
}

// Adjust dialog height depending on menu visibility
void AdjustDialogHeight(HWND hDlg,BOOL visible)
{
    RECT rc;
    int mh;
    int h;

    mh=GetSystemMetrics(SM_CYMENU);
    GetWindowRect(hDlg,&rc);
    h=rc.bottom-rc.top;
    if(visible) h+=mh; else h-=mh;
    SetWindowPos(hDlg,NULL,rc.left,rc.top,rc.right-rc.left,h,
                 SWP_NOZORDER|SWP_NOMOVE);
}

// Handle dropped files (*.mml only) - Win16 version
static void HandleDropFiles16(HWND hDlg, HDROP hDrop)
{
    char path[MAX_PATH] = "";
    char* inbuf = NULL;
    DWORD insize = 0;
    DWORD fsize  = 0;
    GUIErrorCode gerr;

    if (!DragQueryFile(hDrop, 0, path, sizeof(path))) {
        DragFinish(hDrop);
        return;
    }

    /* Extension check (*.mml only) */
    {
        char* ext = strrchr(path, '.');
        if (!ext || lstrcmpi(ext, ".mml") != 0) {

            input_path[0] = '\0';
            SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                           (LPSTR)g_lang_table16[g_lang].tip[TIP_INVALID_TYPE]);

            DragFinish(hDrop);
            return;
        }
    }

    /* Try loading file first (Win32-compatible behavior) */
    gerr = LoadFile16(path, &inbuf, &insize, &fsize);
    if (gerr != GUI_ERR_OK) {

        input_path[0] = '\0';
        SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
            g_lang_table16[g_lang].tip[TIP_FILENAME_LABEL]);
        ShowError16(hDlg, gerr, NULL, fsize);
        DragFinish(hDrop);
        return;
    }

    /* Load succeeded: update path and UI */
    lstrcpy(input_path, path);
    SetDlgItemText(hDlg, IDC_STATIC_FILENAME, ExtractFileName(path));

    if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == 1) {
        UpdateModeFromUI(hDlg);
        DoConvertAndSave16(hDlg, input_path, TRUE, g_shift, g_mode);
    }

    free(inbuf);
    DragFinish(hDrop);
}

// Apply language (labels only, Win16)
static void ApplyLanguage16(HWND hDlg)
{
    SetDlgItemText(hDlg, IDC_STATIC_KEY,
                   g_lang_table16[g_lang].ui_label_key);
    SetDlgItemText(hDlg, IDC_STATIC_OPTION,
                   g_lang_table16[g_lang].ui_label_option);
    SetDlgItemText(hDlg, IDC_BUTTON_SAVE_AUTO,
                   g_lang_table16[g_lang].ui_label_quick);
    SetDlgItemText(hDlg, IDC_BUTTON_SAVE_AS,
                   g_lang_table16[g_lang].ui_label_save);
    SetDlgItemText(hDlg, IDC_CHECK_AUTOSAVE,
                   g_lang_table16[g_lang].ui_label_auto);

    if (input_path[0] == '\0') {
        SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                       g_lang_table16[g_lang].tip[TIP_FILENAME_LABEL]);
    } else {
        SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                       ExtractFileName(input_path));
    }
}

// Dialog procedure
BOOL CALLBACK __export DlgProc(HWND hDlg,UINT msg,WPARAM wParam,LPARAM lParam)
{
    char buf[16];

    switch(msg)
    {
    case WM_INITDIALOG:
    {
        RECT r;
        int w;
        int h;
        int x;
        int y;

        InitShiftControls(hDlg);

        CheckDlgButton(hDlg,IDC_CHECK_REL,0);
        CheckDlgButton(hDlg,IDC_CHECK_ABS,0);
        CheckDlgButton(hDlg,IDC_CHECK_FORMAT,0);
        CheckDlgButton(hDlg,IDC_CHECK_DSHIFT,0);
        CheckDlgButton(hDlg,IDC_CHECK_AUTOSAVE,1);

        g_autosave=1;
        UpdateModeFromUI(hDlg);

        EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),FALSE);
        EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),FALSE);

        ApplyLanguage16(hDlg);

        // Hide the menu by default; show it temporarily when Alt is pressed
        g_hMenu = GetMenu(hDlg);
        if (g_hMenu != NULL) {
            SetMenu(hDlg, NULL);
            DrawMenuBar(hDlg);
            g_menu_hidden = TRUE;
            AdjustDialogHeight(hDlg, FALSE);
        } else {
            g_menu_hidden = FALSE;
        }

        // Center dialog
        GetWindowRect(hDlg,&r);
        w=r.right-r.left;
        h=r.bottom-r.top;
        x=(GetSystemMetrics(SM_CXSCREEN)-w)/2;
        y=(GetSystemMetrics(SM_CYSCREEN)-h)/2;
        SetWindowPos(hDlg,NULL,x,y,0,0,SWP_NOZORDER|SWP_NOSIZE);
        return TRUE;
    }

    case WM_SHOWWINDOW:
        if (wParam) DragAcceptFiles(hDlg, g_has_shell);
        break;

    case WM_DROPFILES:
        HandleDropFiles16(hDlg, (HDROP)wParam);
        return TRUE;

    case WM_HSCROLL:
    {
        HWND hs=(HWND)HIWORD(lParam);
        HWND sl=GetDlgItem(hDlg,IDC_SLIDER_SHIFT);
        if(hs==sl){
            int pos=GetScrollPos(sl,SB_CTL);
            switch(wParam){
                case SB_LINELEFT: pos--; break;
                case SB_LINERIGHT: pos++; break;
                case SB_PAGELEFT: pos-=3; break;
                case SB_PAGERIGHT: pos+=3; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    pos=LOWORD(lParam); break;
            }
            if(pos<0) pos=0;
            if(pos>24) pos=24;
            g_shift=pos-12;
            wsprintf(buf,(g_shift>0?"+%d":"%d"),g_shift);
            SetDlgItemText(hDlg,IDC_EDIT_SHIFT,buf);
            SetScrollPos(sl,SB_CTL,pos,TRUE);
            SetScrollPos(GetDlgItem(hDlg,IDC_SPIN_SHIFT),SB_CTL,g_shift,TRUE);
            return TRUE;
        }
        break;
    }

    case WM_VSCROLL:
    {
        HWND hs=(HWND)HIWORD(lParam);
        if(hs==GetDlgItem(hDlg,IDC_SPIN_SHIFT)){
            if(wParam==SB_LINEUP) g_shift++;
            else if(wParam==SB_LINEDOWN) g_shift--;
            if(g_shift<-12) g_shift=-12;
            if(g_shift>12)  g_shift=12;
            wsprintf(buf,(g_shift>0?"+%d":"%d"),g_shift);
            SetDlgItemText(hDlg,IDC_EDIT_SHIFT,buf);
            SetScrollPos(hs,SB_CTL,g_shift,TRUE);
            SetScrollPos(GetDlgItem(hDlg,IDC_SLIDER_SHIFT),SB_CTL,g_shift+12,TRUE);
            return TRUE;
        }
        break;
    }

    // Alt key temporarily shows the menu
    case WM_SYSCOMMAND:
        if((wParam&0xFFF0)==SC_KEYMENU){
            if(g_menu_hidden && GetMenu(hDlg)==NULL){
                SetMenu(hDlg,g_hMenu);
                DrawMenuBar(hDlg);
                AdjustDialogHeight(hDlg,TRUE);
            }
            return FALSE;
        }
        break;

    case WM_SYSKEYUP:
        if(wParam==VK_MENU) return 0;
        break;

    // Track menu selection state
    case WM_MENUSELECT:
    {
        BOOL now=(wParam!=0);
        if(g_menu_was_selected && !now)
            PostMessage(hDlg,WM_MENU_LOST,0,0);
        g_menu_was_selected=now;
    }
    break;

    // When menu is closed, hide it again if needed
    case WM_MENU_LOST:
        if(g_menu_hidden && GetMenu(hDlg)!=NULL){
            SetMenu(hDlg,NULL);
            DrawMenuBar(hDlg);
            AdjustDialogHeight(hDlg,FALSE);
        }
        break;

    case WM_COMMAND:
    {
        WORD id=LOWORD(wParam);
        int cur_check;

        // w20-style checkbox handling
        if(id==IDC_CHECK_AUTOSAVE ||
           id==IDC_CHECK_FORMAT   ||
           id==IDC_CHECK_REL      ||
           id==IDC_CHECK_ABS      ||
           id==IDC_CHECK_DSHIFT)
        {
            cur_check=(int)SendDlgItemMessage(hDlg,id,BM_GETCHECK,0,0L);
            CheckDlgButton(hDlg,id,!cur_check);
            InvalidateRect(GetDlgItem(hDlg,id),NULL,TRUE);
            UpdateWindow(GetDlgItem(hDlg,id));

            if(id==IDC_CHECK_AUTOSAVE){
                g_autosave=!cur_check;
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),!g_autosave);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),!g_autosave);
            }else if(id==IDC_CHECK_REL && cur_check==0){
                CheckDlgButton(hDlg,IDC_CHECK_ABS,0);
            }else if(id==IDC_CHECK_ABS && cur_check==0){
                CheckDlgButton(hDlg,IDC_CHECK_REL,0);
            }

            UpdateModeFromUI(hDlg);
            return TRUE;
        }

        switch(id)
        {
        // Menu: Open
        case IDM_FILE_OPEN:
        {
            char path[MAX_PATH];
            int r;
            path[0] = '\0';

            /* Try common dialog first, then fallback to custom dialog */
            r = TryCommonDialogOpen(hDlg, path);
            if (r == 1) {
                /* OK */
            } else if (r == -1) {
                break;
            } else {
                if (!SelectOpenPath(hDlg, path)) break;
            }

            if (path[0] == '\0') break;

            /* LoadFile check (Win32-compatible behavior) */
            {
                char* tmp = NULL;
                DWORD insize = 0;
                DWORD fsize  = 0;
                GUIErrorCode gerr;

                gerr = LoadFile16(path, &tmp, &insize, &fsize);
                if (gerr != GUI_ERR_OK) {

                    input_path[0] = '\0';
                    SetDlgItemText(hDlg, IDC_STATIC_FILENAME,
                        g_lang_table16[g_lang].tip[TIP_FILENAME_LABEL]);

                    ShowError16(hDlg, gerr, NULL, fsize);

                    if (tmp) free(tmp);
                    break;
                }

                if (tmp) free(tmp);
            }

            lstrcpy(input_path, path);
            SetDlgItemText(hDlg, IDC_STATIC_FILENAME, ExtractFileName(input_path));

            if (IsDlgButtonChecked(hDlg, IDC_CHECK_AUTOSAVE) == 1) {

                UpdateModeFromUI(hDlg);
                DoConvertAndSave16(hDlg, input_path, TRUE, g_shift, g_mode);

                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS), FALSE);

            } else {

                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AUTO), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_SAVE_AS), TRUE);
            }

            break;
        }

        case IDM_FILE_EXIT:
            DragAcceptFiles(hDlg,FALSE);
            EndDialog(hDlg,0);
            return TRUE;

        case IDM_LANG_EN:
            g_lang = LANG_EN;
            ApplyLanguage16(hDlg);
            break;

        case IDM_LANG_JP:
            g_lang = LANG_JP;
            ApplyLanguage16(hDlg);
            break;

        // Buttons
        case IDC_BUTTON_SAVE_AUTO:
            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1) break;
            if(input_path[0]=='\0'){
                ShowError16(hDlg, GUI_ERR_LF_OPEN, NULL, 0);
                break;
            }
            UpdateModeFromUI(hDlg);
            DoConvertAndSave16(hDlg, input_path, TRUE, g_shift, g_mode);
            break;

        case IDC_BUTTON_SAVE_AS:
            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1) break;
            if(input_path[0]=='\0'){
                ShowError16(hDlg, GUI_ERR_LF_OPEN, NULL, 0);
                break;
            }
            UpdateModeFromUI(hDlg);
            DoConvertAndSave16(hDlg, input_path, FALSE, g_shift, g_mode);
            break;

        case IDCANCEL:
            DragAcceptFiles(hDlg,FALSE);
            EndDialog(hDlg,0);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        DragAcceptFiles(hDlg,FALSE);
        EndDialog(hDlg,0);
        return TRUE;
    }

    return FALSE;
}

// ------------------------------------------------------------
// DoConvertAndSave16 - Win16 version using GUIErrorCode
// ------------------------------------------------------------
static GUIErrorCode DoConvertAndSave16(HWND hDlg,
                                       const char* inpath,
                                       BOOL use_autosave,
                                       int shift,
                                       int mode)
{
    char* inbuf = NULL;
    DWORD insize = 0;
    DWORD fsize = 0;
    long outsize;
    char* outbuf = NULL;
    int outlen;
    char outpath[MAX_PATH];
    MMLErrorInfo err;
    GUIErrorCode gerr;

    /* Load input file (MAX_TEXT check is inside LoadFile16) */
    gerr = LoadFile16(inpath, &inbuf, &insize, &fsize);
    if (gerr != GUI_ERR_OK) {
        ShowError16(hDlg, gerr, NULL, fsize);
        return gerr;
    }

    /* Fixed output buffer (MAX_OUT) */
    outsize = MAX_OUT;
    outbuf = (char*)malloc(MAX_OUT);
    if (!outbuf) {
        free(inbuf);
        ShowError16(hDlg, GUI_ERR_MML_OUTBUF, NULL, 0);
        return GUI_ERR_MML_OUTBUF;
    }

    /* Process MML */
    memset(&err, 0, sizeof(err));
    outlen = mml_process(inbuf, shift, mode, outbuf, (int)outsize, &err);
    free(inbuf);

    if (outlen < 0) {
        gerr = MMLToGUIError(&err);
        ShowError16(hDlg, gerr, &err, (DWORD)err.error_code);
        free(outbuf);
        return gerr;
    }

    if (outlen >= outsize) {
        ShowError16(hDlg, GUI_ERR_MML_OUTBUF, NULL, 0);
        free(outbuf);
        return GUI_ERR_MML_OUTBUF;
    }

    outbuf[outlen] = '\0';

    /* Determine output path */
    if (use_autosave) {
        MakeAutoSaveName(inpath, shift, mode, outpath, sizeof(outpath));
    } else {
        int r = TryCommonDialogSave(hDlg, inpath, outpath);
        if (r == 1) {
            /* OK */
        } else if (r == -1) {
            free(outbuf);
            return GUI_ERR_OK; /* Cancel */
        } else {
            if (!SelectSavePath(hDlg, inpath, outpath)) {
                free(outbuf);
                return GUI_ERR_OK; /* Cancel */
            }
        }
    }

    /* Save output file */
    gerr = SaveFile16(outpath, outbuf, (DWORD)outlen);
    if (gerr != GUI_ERR_OK) {
        ShowError16(hDlg, gerr, NULL, 0);
        free(outbuf);
        return gerr;
    }

    free(outbuf);
    return GUI_ERR_OK;
}

// Common dialog Open (1=success,0=fallback,-1=Cancel)
static int TryCommonDialogOpen(HWND hwndOwner, char* outpath)
{
    OPENFILENAME ofn;
    char file_title[MAX_PATH];

    outpath[0] = '\0';

    if (!g_has_commdlg) return 0;

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize     = OPENFILENAME_SIZE_VERSION_300;
    ofn.hwndOwner       = hwndOwner;
    ofn.lpstrFilter     = "MML Files (*.mml)\0*.mml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile       = outpath;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrFileTitle  = file_title;
    ofn.nMaxFileTitle   = sizeof(file_title);
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt     = "mml";

    if (!GetOpenFileName(&ofn)) {
        DWORD err = CommDlgExtendedError();
        if (err == 0) return -1;
        else          return 0;
    }

    return 1;
}

// Common dialog Save (1=success,0=fallback,-1=Cancel)
static int TryCommonDialogSave(HWND hwndOwner,const char* inpath,char* outpath)
{
    OPENFILENAME ofn;
    char file_title[MAX_PATH];

    outpath[0]='\0';

    if (!g_has_commdlg) return 0;

    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize    =OPENFILENAME_SIZE_VERSION_300;
    ofn.hwndOwner      =hwndOwner;
    ofn.lpstrFilter    ="MML Files (*.mml)\0*.mml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile      =outpath;
    ofn.nMaxFile       =MAX_PATH;
    ofn.lpstrFileTitle =file_title;
    ofn.nMaxFileTitle  =sizeof(file_title);
    ofn.Flags          =OFN_OVERWRITEPROMPT|OFN_HIDEREADONLY;
    ofn.lpstrDefExt    ="mml";

    lstrcpy(outpath,ExtractFileName(inpath));

    if(!GetSaveFileName(&ofn)){
        DWORD err = CommDlgExtendedError();
        if (err == 0) return -1;
        else          return 0;
    }

    return 1;
}

// ------------------------------
// Custom File Dialog: Open
// ------------------------------
BOOL SelectOpenPath(HWND hwndOwner,char* outpath)
{
    FARPROC lp;
    int nResult;
    FILEDLG_PARAM param;
    char exePath[MAX_PATH];
    char exeDir[MAX_PATH];
    int i;

    GetModuleFileName(g_hInst,exePath,sizeof(exePath));
    lstrcpy(exeDir,exePath);
    for(i=(int)lstrlen(exeDir)-1;i>=0;i--){
        if(exeDir[i]=='\\'||exeDir[i]=='/'){
            exeDir[i]='\0';
            break;
        }
    }
    chdir(exeDir);

    param.mode   =FILEDLG_OPEN;
    param.defext =".mml";
    param.initdir=NULL;

    lp=MakeProcInstance((FARPROC)FileDialogProc,g_hInst);
    nResult=MyDialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_FILE_DIALOG),
                             hwndOwner,lp,(LONG)&param);
    FreeProcInstance(lp);

    if(nResult==IDOK){
        lstrcpy(outpath,file_path);
        return TRUE;
    }
    return FALSE;
}

// ------------------------------
// Custom File Dialog: Save
// ------------------------------
BOOL SelectSavePath(HWND hwndOwner,const char* inpath,char* outpath)
{
    FARPROC lp;
    int nResult;
    FILEDLG_PARAM param;

    param.mode   =FILEDLG_SAVE;
    param.defext =".mml";
    param.initdir=inpath;

    lp=MakeProcInstance((FARPROC)FileDialogProc,g_hInst);
    nResult=MyDialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_FILE_DIALOG),
                             hwndOwner,lp,(LONG)&param);
    FreeProcInstance(lp);

    if(nResult==IDOK){
        lstrcpy(outpath,file_path);
        return TRUE;
    }
    return FALSE;
}

// ------------------------------
// DOS drive validity check
// ------------------------------
static int IsDriveValid(int drive_num)
{
    union REGS r;
    r.h.ah=0x44; r.h.al=0x08; r.h.bl=(unsigned char)drive_num;
    intdos(&r,&r);
    if(r.x.cflag && r.x.ax==0x0F) return 0;

    r.h.ah=0x44; r.h.al=0x09; r.h.bl=(unsigned char)drive_num;
    r.x.dx=0;
    intdos(&r,&r);
    if(!r.x.cflag && r.x.dx!=0) return 1;
    return 0;
}

// ------------------------------
// Add drive list entries
// ------------------------------
static void AddDrives(HWND hList)
{
    int d;
    char entry[8];
    for(d=1;d<=26;d++){
        if(IsDriveValid(d)){
            entry[0]='['; entry[1]='-';
            entry[2]=(char)('A'+(d-1));
            entry[3]='-'; entry[4]=']';
            entry[5]='\0';
            SendMessage(hList,LB_ADDSTRING,0,(LPARAM)(LPSTR)entry);
        }
    }
}

// ------------------------------
// Rebuild directory and drive list
// ------------------------------
static void RebuildPlaceList(HWND hDlg)
{
    HWND hList=GetDlgItem(hDlg,IDC_LIST_DRIVES);
    char cwd[MAX_PATH];
    struct find_t ff;
    int done;

    SendMessage(hList,LB_RESETCONTENT,0,0);

    if(_getcwd(cwd,sizeof(cwd))==NULL) cwd[0]='\0';

    // Add parent directory entry unless root
    if(cwd[0] && !(cwd[0] && cwd[1]==':' && cwd[2]=='\\' && cwd[3]=='\0')){
        SendMessage(hList,LB_ADDSTRING,0,(LPARAM)"[..]");
    }

    // Add subdirectories
    done=_dos_findfirst("*.*",_A_SUBDIR,&ff);
    while(!done){
        if(ff.attrib&_A_SUBDIR){
            if(lstrcmp(ff.name,".")!=0 && lstrcmp(ff.name,"..")!=0){
                SendMessage(hList,LB_ADDSTRING,0,(LPARAM)ff.name);
            }
        }
        done=_dos_findnext(&ff);
    }

    // Add drives
    AddDrives(hList);
}

// ------------------------------
// Go to parent directory
// ------------------------------
static void GoParentDir(void)
{
    char cwd[MAX_PATH];
    int i;

    if(_getcwd(cwd,sizeof(cwd))==NULL) return;
    if(cwd[0]!='\0' && cwd[1]==':' && cwd[2]=='\\' && cwd[3]=='\0') return;

    for(i=(int)strlen(cwd)-1;i>=0;i--){
        if(cwd[i]=='\\'||cwd[i]=='/'){
            cwd[i]='\0';
            break;
        }
    }

    if(cwd[0]!='\0' && cwd[1]==':' && cwd[2]=='\0'){
        cwd[2]='\\';
        cwd[3]='\0';
    }

    _chdir(cwd);
}

// ------------------------------
// Rebuild file list (filter from combo/list)
// ------------------------------
static void RebuildFileList(HWND hDlg)
{
    char szFilter[64];
    char szSpec[MAX_PATH];

    if(!s_useFallback && s_hComboType && IsWindow(s_hComboType)){
        int sel=(int)SendMessage(s_hComboType,CB_GETCURSEL,0,0L);
        if(sel!=CB_ERR)
            SendMessage(s_hComboType,CB_GETLBTEXT,sel,(LPARAM)szFilter);
        else
            lstrcpy(szFilter,"*.*");
    }else{
        int sel=(int)SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETCURSEL,0,0);
        if(sel!=LB_ERR)
            SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETTEXT,sel,(LPARAM)szFilter);
        else
            lstrcpy(szFilter,"*.*");
    }

    wsprintf(szSpec,"%s",szFilter);
    DlgDirList(hDlg,szSpec,IDC_LIST_FILES,0,0x0000);
}

// ------------------------------
// Custom File Dialog Procedure
// ------------------------------
BOOL FAR PASCAL FileDialogProc(HWND hDlg,WORD msg,WORD wParam,LONG lParam)
{
    static int s_isSaveDlg;
    char szSpec[MAX_PATH];
    char szSelect[MAX_PATH];

    switch(msg)
    {
    case WM_INITDIALOG:
    {
        FILEDLG_PARAM* p;
        static FILEDLG_PARAM defp = { FILEDLG_OPEN,".mml",NULL };

        s_hComboType  = NULL;
        s_useFallback = FALSE;

        p = (g_DlgParam ? (FILEDLG_PARAM*)g_DlgParam : &defp);
        s_isSaveDlg = (p->mode == FILEDLG_SAVE);

        if (p->initdir && p->initdir[0]) _chdir(p->initdir);

        SetWindowText(hDlg,s_isSaveDlg ? "Save As" : "Open File");

        SetDlgItemText(hDlg,IDC_STATIC_TYPE,"*.mml");

        {
            HWND hList;
            HWND hOld = GetDlgItem(hDlg,IDC_EDIT_TYPE);
            RECT rc;
            GetWindowRect(hOld,&rc);
            ScreenToClient(hDlg,(LPPOINT)&rc.left);
            ScreenToClient(hDlg,(LPPOINT)&rc.right);

            DestroyWindow(hOld);

            hList = CreateWindow(
                "LISTBOX",NULL,
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER,
                rc.left,rc.top + 2,
                rc.right - rc.left,20,
                hDlg,(HMENU)IDC_EDIT_TYPE,g_hInst,NULL
            );

            SendMessage(hList,LB_ADDSTRING,0,(LPARAM)"*.mml");
            SendMessage(hList,LB_SETCURSEL,0,0);
        }

        RebuildFileList(hDlg);
        RebuildPlaceList(hDlg);
        SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
        g_DlgParam = 0;
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id=(int)wParam;
        int code=HIWORD(lParam);
        char buf[64]={0};
        char text[64]={0};
        char root[4]={0};
        char drive=0;
        unsigned int drive_count=0;
        int idx=0,cbsel=0,sel=0,nIndex=0;
        HWND hList=NULL;

        // Filter change (combo)
        if(!s_useFallback &&
           s_hComboType && IsWindow(s_hComboType) &&
           id==IDC_EDIT_TYPE && code==CBN_SELCHANGE)
        {
            int selcb=(int)SendMessage(s_hComboType,CB_GETCURSEL,0,0L);
            if(selcb!=CB_ERR){
                SendMessage(s_hComboType,CB_GETLBTEXT,selcb,(LPARAM)buf);
                SetDlgItemText(hDlg,IDC_STATIC_TYPE,buf);
                DlgDirList(hDlg,buf,IDC_LIST_FILES,0,0x0000);
            }
            return TRUE;
        }

        // Filter change (fallback list)
        if(s_useFallback &&
           id==IDC_EDIT_TYPE &&
           code==LBN_SELCHANGE)
        {
            idx=(int)SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETCURSEL,0,0);
            if(idx!=LB_ERR){
                SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETTEXT,idx,(LPARAM)buf);
                DlgDirList(hDlg,buf,IDC_LIST_FILES,0,0x0000);
            }
            return TRUE;
        }

        // STATIC_TYPE click opens the combo box
        if(!s_useFallback && id==IDC_STATIC_TYPE && code==BN_CLICKED){
            if(s_hComboType){
                SetFocus(s_hComboType);
                SendMessage(s_hComboType,WM_LBUTTONDOWN,0,MAKELONG(5,5));
            }
            return TRUE;
        }

        // File selection change
        if(wParam==IDC_LIST_FILES && HIWORD(lParam)==LBN_SELCHANGE){
            memset(szSelect,0,sizeof(szSelect));
            DlgDirSelect(hDlg,szSelect,IDC_LIST_FILES);
            if(szSelect[0]!='\0' &&
               (szSelect[strlen(szSelect)-1]=='\\' ||
                szSelect[strlen(szSelect)-1]=='/'))
            {
                SetDlgItemText(hDlg,IDC_EDIT_PATH,s_isSaveDlg?"":szSelect);
            }else{
                SetDlgItemText(hDlg,IDC_EDIT_PATH,szSelect);
            }
            return TRUE;
        }

        // File double-click
        if(wParam==IDC_LIST_FILES && HIWORD(lParam)==LBN_DBLCLK){
            memset(szSelect,0,sizeof(szSelect));
            if(DlgDirSelect(hDlg,szSelect,IDC_LIST_FILES)){
                if(!s_useFallback){
                    cbsel=(int)SendMessage(s_hComboType,CB_GETCURSEL,0,0L);
                    if(cbsel!=CB_ERR)
                        SendMessage(s_hComboType,CB_GETLBTEXT,cbsel,(LPARAM)buf);
                    else
                        lstrcpy(buf,"*.*");
                }else{
                    idx=(int)SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETCURSEL,0,0);
                    if(idx!=LB_ERR)
                        SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETTEXT,idx,(LPARAM)buf);
                    else
                        lstrcpy(buf,"*.*");
                }
                wsprintf(szSpec,"%s%s",szSelect,buf);
                DlgDirList(hDlg,szSpec,IDC_LIST_FILES,0,0x0000);
                SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
            }else{
                lstrcpy(file_path,szSelect);
                if(s_isSaveDlg && access(file_path,0)==0){
                    if(MessageBox(hDlg,"Overwrite existing file?","Confirm",
                                  MB_YESNO|MB_ICONQUESTION)!=IDYES)
                        return TRUE;
                }
                EndDialog(hDlg,IDOK);
            }
            return TRUE;
        }

        // Drive / [..] double-click
        if(wParam==IDC_LIST_DRIVES && HIWORD(lParam)==LBN_DBLCLK){
            hList=GetDlgItem(hDlg,IDC_LIST_DRIVES);
            idx=(int)SendMessage(hList,LB_GETCURSEL,0,0L);
            if(idx==LB_ERR) return TRUE;
            SendMessage(hList,LB_GETTEXT,idx,(LPARAM)text);

            if(lstrcmp(text,"[..]")==0){
                GoParentDir();
                RebuildFileList(hDlg);
                RebuildPlaceList(hDlg);
                SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
                return TRUE;
            }

            if(text[0]=='[' && text[1]=='-' && text[3]=='-' && text[4]==']'){
                drive=text[2];
                if(drive>='a' && drive<='z')
                    drive=(char)(drive-'a'+'A');
                root[0]=drive; root[1]=':'; root[2]='\\'; root[3]='\0';
                _dos_setdrive((drive-'A')+1,&drive_count);
                if(_chdir(root)==0){
                    RebuildFileList(hDlg);
                    RebuildPlaceList(hDlg);
                    SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
                }else{
                    MessageBeep(0);
                }
                return TRUE;
            }

            if(_chdir(text)==0){
                RebuildFileList(hDlg);
                RebuildPlaceList(hDlg);
                SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
            }
            return TRUE;
        }

        // OK / Cancel
        switch(wParam)
        {
        case IDOK:
            if(s_hComboType){ DestroyWindow(s_hComboType); s_hComboType=NULL; }

            GetDlgItemText(hDlg,IDC_EDIT_PATH,file_path,MAX_PATH);
            if(file_path[0]=='\0'){
                nIndex=(int)SendDlgItemMessage(hDlg,IDC_LIST_FILES,LB_GETCURSEL,0,0L);
                if(nIndex>=0){
                    memset(szSelect,0,sizeof(szSelect));
                    DlgDirSelect(hDlg,szSelect,IDC_LIST_FILES);
                    lstrcpy(file_path,szSelect);
                }
            }
            if(file_path[0]=='\0') return TRUE;

            sel=lstrlen(file_path);
            if(file_path[0]!='\0' &&
               (file_path[sel-1]=='\\' || file_path[sel-1]=='/'))
            {
                if(!s_useFallback){
                    cbsel=(int)SendMessage(s_hComboType,CB_GETCURSEL,0,0L);
                    if(cbsel!=CB_ERR)
                        SendMessage(s_hComboType,CB_GETLBTEXT,cbsel,(LPARAM)buf);
                    else
                        lstrcpy(buf,"*.*");
                }else{
                    idx=(int)SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETCURSEL,0,0);
                    if(idx!=LB_ERR)
                        SendDlgItemMessage(hDlg,IDC_EDIT_TYPE,LB_GETTEXT,idx,(LPARAM)buf);
                    else
                        lstrcpy(buf,"*.*");
                }
                wsprintf(szSpec,"%s%s",file_path,buf);
                DlgDirList(hDlg,szSpec,IDC_LIST_FILES,0,0x0000);
                SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
                return TRUE;
            }

            if(s_isSaveDlg){
                if(sel<4 || lstrcmpi(&file_path[sel-4],".mml")!=0){
                    if(sel+4<MAX_PATH) lstrcat(file_path,".mml");
                }
            }

            if(s_isSaveDlg){
                if(access(file_path,0)==0){
                    if(MessageBox(hDlg,"Overwrite existing file?","Confirm",
                                  MB_YESNO|MB_ICONQUESTION)!=IDYES)
                        return TRUE;
                }
            }else{
                if(access(file_path,0)!=0){
                    MessageBox(hDlg,"File not found.","Error",MB_OK|MB_ICONHAND);
                    return TRUE;
                }
            }

            EndDialog(hDlg,IDOK);
            return TRUE;

        case IDCANCEL:
            if(s_hComboType){ DestroyWindow(s_hComboType); s_hComboType=NULL; }
            EndDialog(hDlg,IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

/* ------------------------------------------------------------
 * LoadFile16 - Win16 file loader with size check
 * ------------------------------------------------------------ */
static GUIErrorCode LoadFile16(const char* path,
                               char** outbuf,
                               DWORD* outsize,
                               DWORD* out_filesize)
{
    FILE* fp;
    long size;
    char* buf;
    size_t read_bytes;
    struct find_t ff;

    if (outbuf)  *outbuf  = NULL;
    if (outsize) *outsize = 0;
    if (out_filesize) *out_filesize = 0;

    fp = fopen(path, "rb");
    if (!fp)
        return GUI_ERR_LF_OPEN;

    /* Get file size via DOS API (FAT32-safe) */
    if (_dos_findfirst(path, _A_NORMAL, &ff) != 0) {
        fclose(fp);
        return GUI_ERR_LF_OPEN;
    }

    size = (long)ff.size;
    if (out_filesize) *out_filesize = (DWORD)size;

    if (size < 0) {
        fclose(fp);
        return GUI_ERR_LF_SIZE;
    }

    if (size == 0) {
        fclose(fp);
        return GUI_ERR_LF_EMPTY;
    }

    if (size > MAX_TEXT) {
        fclose(fp);
        return GUI_ERR_LF_TOO_LARGE;
    }

    buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return GUI_ERR_LF_READ;
    }

    read_bytes = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if (read_bytes != (size_t)size) {
        free(buf);
        return GUI_ERR_LF_READ;
    }

    buf[size] = '\0';

    if (outbuf)  *outbuf  = buf;
    if (outsize) *outsize = (DWORD)size;

    return GUI_ERR_OK;
}

/* ------------------------------------------------------------
 * SaveFile16 - Win16 version returning GUIErrorCode
 * ------------------------------------------------------------ */
static GUIErrorCode SaveFile16(const char* path, const char* data, DWORD size)
{
    FILE* fp;
    size_t written;

    fp = fopen(path, "w");
    if (!fp)
        return GUI_ERR_SF_OPEN;

    written = fwrite(data, 1, (size_t)size, fp);
    fclose(fp);

    if (written != (size_t)size)
        return GUI_ERR_SF_WRITE;

    return GUI_ERR_OK;
}

// ------------------------------
// AutoSave filename generator (DOS 8.3)
// ------------------------------
void MakeAutoSaveName(const char* inpath,int shift,int mode,
                      char* out,int outsize)
{
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];
    char cSign='0';
    int  nAbsShift=0;
    char cModeChar;
    int  dflag=0;

    _splitpath(inpath,drive,dir,fname,ext);

    if(shift!=0){
        cSign=(shift>0)?'p':'m';
        nAbsShift=(shift>0)?shift:-shift;
    }

    cModeChar=(char)('0'+(mode&0x7));
    if(mode&MODE_NOISE_SHIFT) dflag=1;

    if(dflag){
        wsprintf(out,"%s%sof_%c%02d%cd.mml",
                 drive,dir,cSign,nAbsShift,cModeChar);
    }else{
        wsprintf(out,"%s%sof_%c%02d%c.mml",
                 drive,dir,cSign,nAbsShift,cModeChar);
    }

    if(outsize>0) out[outsize-1]='\0';
}

GUIErrorCode MMLToGUIError(const MMLErrorInfo* err)
{
    if (!err)
        return GUI_ERR_MML_UNKNOWN;

    switch (err->error_code)
    {
    case MML_ERR_NULL_INPUT:
        return GUI_ERR_MML_NULL;

    case MML_ERR_EMPTY_INPUT:
        return GUI_ERR_MML_EMPTY;

    case MML_ERR_OUTBUF:
        return GUI_ERR_MML_OUTBUF;

    case MML_ERR_BAD_SHIFT:
        return GUI_ERR_MML_BAD_SHIFT;

    case MML_ERR_BAD_MODE:
        return GUI_ERR_MML_BAD_MODE;

    case MML_ERR_OCTAVE_OUT_OF_RANGE:
        return GUI_ERR_MML_OCTAVE;

    default:
        return GUI_ERR_MML_UNKNOWN;
    }
}
