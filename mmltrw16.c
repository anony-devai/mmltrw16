// mmltrw16.c  Win16/C89/OpenWatcom v2  mmleng16対応 w20+w30統合版

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

#ifndef MAX_PATH
#define MAX_PATH 256
#endif

#ifndef OPENFILENAME_SIZE_VERSION_300
#define OPENFILENAME_SIZE_VERSION_300 72
#endif

#ifndef CBS_DROPDOWNLIST
#define CBS_DROPDOWNLIST 0x0003
#endif

// メニュー選択解除通知
#define WM_MENU_LOST (WM_USER + 100)

// 自作ファイルダイアログ用
typedef struct {
    int mode;
    const char* defext;
    const char* initdir;
} FILEDLG_PARAM;

#define FILEDLG_OPEN 0
#define FILEDLG_SAVE 1

// グローバル
static HINSTANCE g_hInst = NULL;
static char input_path[MAX_PATH] = "";
static int  g_shift    = 0;
static int  g_mode     = MODE_PURE;
static int  g_autosave = 1;

static LONG g_DlgParam = 0;
static char file_path[MAX_PATH];

static HWND s_hComboType  = NULL;
static BOOL s_useFallback = FALSE;

static BOOL g_menu_hidden = FALSE;
static BOOL g_menu_was_selected = FALSE;
HMENU g_hMenu = NULL;

// プロトタイプ
BOOL CALLBACK __export DlgProc(HWND,UINT,WPARAM,LPARAM);
void InitShiftControls(HWND);
static void UpdateModeFromUI(HWND);
static void DoConvertAndSave(HWND,const char*,BOOL);
static const char* ExtractFileName(const char*);
BOOL LoadFile(const char*,char**,DWORD*);
BOOL SaveFile(const char*,const char*,DWORD);
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

// w20互換パラメータ渡し
int MyDialogBoxParam(HINSTANCE hInst,LPCSTR tpl,HWND hWnd,FARPROC proc,LONG param)
{
    g_DlgParam = param;
    return DialogBox(hInst,tpl,hWnd,proc);
}

// WinMain
int PASCAL WinMain(HINSTANCE hInst,HINSTANCE hPrev,LPSTR lpCmd,int nShow)
{
    MSG msg;
    g_hInst = hInst;
    PeekMessage(&msg,NULL,0,0,PM_NOREMOVE);
    DialogBox(hInst,MAKEINTRESOURCE(IDD_MAIN_DIALOG),NULL,(DLGPROC)DlgProc);
    PostQuitMessage(0);
    while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// パス末尾名抽出
static const char* ExtractFileName(const char* path)
{
    const char* p=strrchr(path,'\\');
    if(p) return p+1;
    p=strrchr(path,'/');
    if(p) return p+1;
    return path;
}

// Shift UI 初期化
void InitShiftControls(HWND hDlg)
{
    SetScrollRange(GetDlgItem(hDlg,IDC_SPIN_SHIFT),SB_CTL,-12,12,FALSE);
    SetScrollPos  (GetDlgItem(hDlg,IDC_SPIN_SHIFT),SB_CTL,g_shift,TRUE);

    SetScrollRange(GetDlgItem(hDlg,IDC_SLIDER_SHIFT),SB_CTL,0,24,FALSE);
    SetScrollPos  (GetDlgItem(hDlg,IDC_SLIDER_SHIFT),SB_CTL,g_shift+12,TRUE);

    SetDlgItemInt(hDlg,IDC_EDIT_SHIFT,g_shift,TRUE);
}

// モード更新
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

// メニュー有無で高さ調整
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

// DlgProc
BOOL CALLBACK __export DlgProc(HWND hDlg,UINT msg,WPARAM wParam,LPARAM lParam)
{
    char buf[16];

    switch(msg)
    {
    case WM_INITDIALOG:
    {
        RECT r;
        HINSTANCE hShell;
        HMENU m;
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

        SetDlgItemText(hDlg,IDC_STATIC_FILENAME,"No file selected");

        // SHELL.DLL があれば D&D → メニュー隠す
        hShell=LoadLibrary("SHELL.DLL");
        if(hShell){
            FARPROC p=GetProcAddress(hShell,"DragQueryFile");
            if(p){
                g_menu_hidden=TRUE;
                m=GetMenu(hDlg);
                if(m){
                    g_hMenu=m;
                    SetMenu(hDlg,NULL);
                    DrawMenuBar(hDlg);
                    AdjustDialogHeight(hDlg,FALSE);
                }
            }else g_menu_hidden=FALSE;
            FreeLibrary(hShell);
        }

        // 中央配置
        GetWindowRect(hDlg,&r);
        w=r.right-r.left;
        h=r.bottom-r.top;
        x=(GetSystemMetrics(SM_CXSCREEN)-w)/2;
        y=(GetSystemMetrics(SM_CYSCREEN)-h)/2;
        SetWindowPos(hDlg,NULL,x,y,0,0,SWP_NOZORDER|SWP_NOSIZE);
        return TRUE;
    }

    case WM_SHOWWINDOW:
        if(wParam) DragAcceptFiles(hDlg,TRUE);
        break;

    case WM_DROPFILES:
    {
        HDROP h=(HDROP)wParam;
        char path[MAX_PATH]; path[0]='\0';
        if(DragQueryFile(h,0,path,sizeof(path))){
            char* ext=strrchr(path,'.');
            if(!ext||lstrcmpi(ext,".mml")!=0){
                input_path[0]='\0';
                SetDlgItemText(hDlg,IDC_STATIC_FILENAME,"ERROR: Cannot OPEN.");
                DragFinish(h);
                return TRUE;
            }
            lstrcpy(input_path,path);
            SetDlgItemText(hDlg,IDC_STATIC_FILENAME,ExtractFileName(input_path));

            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1){
                UpdateModeFromUI(hDlg);
                DoConvertAndSave(hDlg,input_path,TRUE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),FALSE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),FALSE);
            }else{
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),TRUE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),TRUE);
            }
        }
        DragFinish(h);
        return TRUE;
    }

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

    // Alt → メニュー一時表示
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

    // メニュー選択状態監視
    case WM_MENUSELECT:
    {
        BOOL now=(wParam!=0);
        if(g_menu_was_selected && !now)
            PostMessage(hDlg,WM_MENU_LOST,0,0);
        g_menu_was_selected=now;
    }
    break;

    // メニュー閉じ処理
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

        // w20方式チェックボックス処理
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
        // メニュー: Open
        case IDM_FILE_OPEN:
        {
            char path[MAX_PATH]; int r;
            path[0]='\0';

            // まず共通DLGを試し、ダメなら自作DLGへフォールバック
            r=TryCommonDialogOpen(hDlg,path);
            if(r==1){
                // OK
            }else if(r==-1){
                break;
            }else{
                if(!SelectOpenPath(hDlg,path)) break;
            }

            if(path[0]=='\0') break;

            lstrcpy(input_path,path);
            SetDlgItemText(hDlg,IDC_STATIC_FILENAME,ExtractFileName(input_path));

            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1){
                UpdateModeFromUI(hDlg);
                DoConvertAndSave(hDlg,input_path,TRUE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),FALSE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),FALSE);
            }else{
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AUTO),TRUE);
                EnableWindow(GetDlgItem(hDlg,IDC_BUTTON_SAVE_AS),TRUE);
            }
            break;
        }

        case IDM_FILE_EXIT:
            DragAcceptFiles(hDlg,FALSE);
            EndDialog(hDlg,0);
            return TRUE;

        // ボタン
        case IDC_BUTTON_SAVE_AUTO:
            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1) break;
            if(input_path[0]=='\0'){
                MessageBox(hDlg,"No input file.","Error",MB_OK|MB_ICONSTOP);
                break;
            }
            UpdateModeFromUI(hDlg);
            DoConvertAndSave(hDlg,input_path,TRUE);
            break;

        case IDC_BUTTON_SAVE_AS:
            if(IsDlgButtonChecked(hDlg,IDC_CHECK_AUTOSAVE)==1) break;
            if(input_path[0]=='\0'){
                MessageBox(hDlg,"No input file.","Error",MB_OK|MB_ICONSTOP);
                break;
            }
            UpdateModeFromUI(hDlg);
            DoConvertAndSave(hDlg,input_path,FALSE);
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

// 変換＋保存
static void DoConvertAndSave(HWND hDlg,const char* inpath,BOOL use_autosave)
{
    char* inbuf=NULL;
    DWORD insize=0;
    int outsize;
    char* outbuf;
    int outlen;
    char outpath[MAX_PATH];
    MMLErrorInfo err;

    if(!LoadFile(inpath,&inbuf,&insize)){
        MessageBox(hDlg,"入力ファイルを読み込めません。","エラー",MB_OK|MB_ICONSTOP);
        return;
    }

    outsize=(int)insize*2+1024;
    outbuf=(char*)malloc(outsize);
    if(!outbuf){
        free(inbuf);
        MessageBox(hDlg,"メモリ確保に失敗しました。","エラー",MB_OK|MB_ICONSTOP);
        return;
    }

    memset(&err,0,sizeof(err));
    outlen=mml_process(inbuf,g_shift,g_mode,outbuf,outsize,&err);
    free(inbuf);

    if(outlen<0){
        char msg[256];
        switch(err.error_code){
        case MML_ERR_NULL_INPUT:
            lstrcpy(msg,"内部エラー: NULL 入力です。"); break;
        case MML_ERR_EMPTY_INPUT:
            lstrcpy(msg,"入力が空です。"); break;
        case MML_ERR_OUTBUF:
            lstrcpy(msg,"出力バッファサイズが不正です。"); break;
        case MML_ERR_BAD_SHIFT:
            lstrcpy(msg,"移調幅が範囲外です (-12～12)。"); break;
        case MML_ERR_BAD_MODE:
            lstrcpy(msg,"モード指定が不正です。"); break;
        case MML_ERR_OCTAVE_OUT_OF_RANGE:
            wsprintf(msg,
                     "オクターブ範囲外エラーが発生しました。\n"
                     "チャンネル: %c\n行: %d\n計算値: o%d",
                     err.channel_char?err.channel_char:'?',
                     err.line_number,
                     err.calculated_value);
            break;
        default:
            lstrcpy(msg,"変換に失敗しました。"); break;
        }
        MessageBox(hDlg,msg,"エラー",MB_OK|MB_ICONSTOP);
        free(outbuf);
        return;
    }

    if(use_autosave){
        MakeAutoSaveName(inpath,g_shift,g_mode,outpath,sizeof(outpath));
    }else{
        int r;
        // Save As も共通DLG優先、不可なら自作DLGへ
        r=TryCommonDialogSave(hDlg,inpath,outpath);
        if(r==1){
        }else if(r==-1){
            free(outbuf);
            return;
        }else{
            if(!SelectSavePath(hDlg,inpath,outpath)){
                free(outbuf);
                return;
            }
        }
    }

    if(!SaveFile(outpath,outbuf,(DWORD)outlen)){
        MessageBox(hDlg,"保存に失敗しました。","エラー",MB_OK|MB_ICONSTOP);
    }
    free(outbuf);
}

// 共通DLG Open (1=成功,0=フォールバック,-1=Cancel)
static int TryCommonDialogOpen(HWND hwndOwner,char* outpath)
{
    HINSTANCE hComdlg;
    BOOL (WINAPI *pGetOpenFileName)(LPOPENFILENAME);
    OPENFILENAME ofn;
    char file_title[MAX_PATH];

    outpath[0]='\0';
    hComdlg=LoadLibrary("COMMDLG.DLL");
    if(!hComdlg) return 0;

    pGetOpenFileName=(BOOL (WINAPI *)(LPOPENFILENAME))
        GetProcAddress(hComdlg,"GetOpenFileName");
    if(!pGetOpenFileName){
        FreeLibrary(hComdlg);
        return 0;
    }

    memset(&ofn,0,sizeof(ofn));
    ofn.lStructSize    =OPENFILENAME_SIZE_VERSION_300;
    ofn.hwndOwner      =hwndOwner;
    ofn.lpstrFilter    ="MML Files (*.mml)\0*.mml\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile      =outpath;
    ofn.nMaxFile       =MAX_PATH;
    ofn.lpstrFileTitle =file_title;
    ofn.nMaxFileTitle  =sizeof(file_title);
    ofn.Flags          =OFN_FILEMUSTEXIST|OFN_HIDEREADONLY;
    ofn.lpstrDefExt    ="mml";

    outpath[0]='\0';
    if(!pGetOpenFileName(&ofn)){
        DWORD err=CommDlgExtendedError();
        FreeLibrary(hComdlg);
        if(err==0) return -1;
        else       return 0;
    }

    FreeLibrary(hComdlg);
    return 1;
}

// 共通DLG Save (1=成功,0=不可,-1=Cancel)
static int TryCommonDialogSave(HWND hwndOwner,const char* inpath,char* outpath)
{
    HINSTANCE hComdlg;
    BOOL (WINAPI *pGetSaveFileName)(LPOPENFILENAME);
    OPENFILENAME ofn;
    char file_title[MAX_PATH];

    outpath[0]='\0';
    hComdlg=LoadLibrary("COMMDLG.DLL");
    if(!hComdlg) return 0;

    pGetSaveFileName=(BOOL (WINAPI *)(LPOPENFILENAME))
        GetProcAddress(hComdlg,"GetSaveFileName");
    if(!pGetSaveFileName){
        FreeLibrary(hComdlg);
        return 0;
    }

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
    if(!pGetSaveFileName(&ofn)){
        FreeLibrary(hComdlg);
        return -1;
    }

    FreeLibrary(hComdlg);
    return 1;
}

// 自作DLG Open
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

// 自作DLG Save
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

// DOSドライブ有効判定
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

// ドライブ一覧追加
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

// カレントディレクトリとサブディレクトリ＋ドライブ一覧
static void RebuildPlaceList(HWND hDlg)
{
    HWND hList=GetDlgItem(hDlg,IDC_LIST_DRIVES);
    char cwd[MAX_PATH];
    struct find_t ff;
    int done;

    SendMessage(hList,LB_RESETCONTENT,0,0);

    if(_getcwd(cwd,sizeof(cwd))==NULL) cwd[0]='\0';

    if(cwd[0] && !(cwd[0] && cwd[1]==':' && cwd[2]=='\\' && cwd[3]=='\0')){
        SendMessage(hList,LB_ADDSTRING,0,(LPARAM)"[..]");
    }

    done=_dos_findfirst("*.*",_A_SUBDIR,&ff);
    while(!done){
        if(ff.attrib&_A_SUBDIR){
            if(lstrcmp(ff.name,".")!=0 && lstrcmp(ff.name,"..")!=0){
                SendMessage(hList,LB_ADDSTRING,0,(LPARAM)ff.name);
            }
        }
        done=_dos_findnext(&ff);
    }

    AddDrives(hList);
}

// 親ディレクトリへ
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

// ファイル一覧再構築（フィルタはコンボ or リストから取得）
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

// 自作ファイルダイアログ
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
        static FILEDLG_PARAM defp={FILEDLG_OPEN,".mml",NULL};
        HWND hEdit,hStatic,hList=NULL;
        RECT rcEdit;
        POINT p1,p2;

        s_hComboType=NULL;
        s_useFallback=FALSE;

        p=(g_DlgParam?(FILEDLG_PARAM*)g_DlgParam:&defp);
        s_isSaveDlg=(p->mode==FILEDLG_SAVE);

        if(p->initdir && p->initdir[0]) _chdir(p->initdir);

        SetWindowText(hDlg,s_isSaveDlg?"Save As":"Open File");
        SetDlgItemText(hDlg,IDC_STATIC_TYPE,"*.mml");

        hEdit  =GetDlgItem(hDlg,IDC_EDIT_TYPE);
        hStatic=GetDlgItem(hDlg,IDC_STATIC_TYPE);

        if(hEdit){
            GetWindowRect(hEdit,&rcEdit);
            p1.x=rcEdit.left;  p1.y=rcEdit.top;
            p2.x=rcEdit.right; p2.y=rcEdit.bottom;
            ScreenToClient(hDlg,&p1);
            ScreenToClient(hDlg,&p2);
            rcEdit.left  =p1.x;
            rcEdit.top   =p1.y;
            rcEdit.right =p2.x;
            rcEdit.bottom=p2.y;

            // COMBOBOX が作れない環境ではリストボックスにフォールバック
            {
                HWND hTest=CreateWindow("COMBOBOX",NULL,WS_CHILD,
                                        0,0,0,0,hDlg,NULL,g_hInst,NULL);
                if(hTest){
                    DestroyWindow(hTest);
                    s_useFallback=FALSE;
                }else{
                    s_useFallback=TRUE;
                }
            }

            if(!s_useFallback){
                int x=rcEdit.left;
                int y=rcEdit.top;
                int cx=100;
                int cy=62;

                s_hComboType=CreateWindow(
                    "COMBOBOX",NULL,
                    WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,
                    x,y,cx,cy,
                    hDlg,(HMENU)IDC_EDIT_TYPE,g_hInst,NULL
                );
                if(s_hComboType){
                    SendMessage(s_hComboType,CB_ADDSTRING,0,(LPARAM)"*.mml");
                    SendMessage(s_hComboType,CB_ADDSTRING,0,(LPARAM)"*.*");
                    SendMessage(s_hComboType,CB_SETCURSEL,0,0L);
                }
                ShowWindow(hEdit,SW_HIDE);
            }else{
                ShowWindow(hStatic,SW_HIDE);
                EnableWindow(hStatic,FALSE);
                DestroyWindow(hEdit);

                hList=CreateWindow(
                    "LISTBOX",NULL,
                    WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_BORDER,
                    rcEdit.left,rcEdit.top,
                    rcEdit.right-rcEdit.left,
                    40,
                    hDlg,(HMENU)IDC_EDIT_TYPE,g_hInst,NULL
                );
                SendMessage(hList,LB_ADDSTRING,0,(LPARAM)"*.mml");
                SendMessage(hList,LB_ADDSTRING,0,(LPARAM)"*.*");
                SendMessage(hList,LB_SETCURSEL,0,0);
            }
        }else{
            s_useFallback=TRUE;
        }

        RebuildFileList(hDlg);
        RebuildPlaceList(hDlg);
        SetDlgItemText(hDlg,IDC_EDIT_PATH,"");
        g_DlgParam=0;
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis=(LPDRAWITEMSTRUCT)lParam;
        if(s_useFallback) return FALSE;
        if(dis->CtlID==IDC_STATIC_TYPE){
            HBRUSH hbr;
            char text[64];
            hbr=(HBRUSH)GetClassWord(hDlg,GCW_HBRBACKGROUND);
            FillRect(dis->hDC,&dis->rcItem,hbr);
            SetBkMode(dis->hDC,TRANSPARENT);
            GetWindowText(dis->hwndItem,text,sizeof(text));
            TextOut(dis->hDC,dis->rcItem.left,dis->rcItem.top,
                    text,lstrlen(text));
            return TRUE;
        }
        break;
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

        // フィルタ変更（コンボ）
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

        // フィルタ変更（フォールバックリスト）
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

        // STATIC_TYPE クリックでコンボを開く
        if(!s_useFallback && id==IDC_STATIC_TYPE && code==BN_CLICKED){
            if(s_hComboType){
                SetFocus(s_hComboType);
                SendMessage(s_hComboType,WM_LBUTTONDOWN,0,MAKELONG(5,5));
            }
            return TRUE;
        }

        // ファイル選択変更
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

        // ファイルダブルクリック
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

        // ドライブ／[..] ダブルクリック
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

// ファイル読み込み
BOOL LoadFile(const char* path,char** outbuf,DWORD* outsize)
{
    FILE* fp;
    long size;
    char* buf;
    size_t read_bytes;

    fp=fopen(path,"rb");
    if(!fp) return FALSE;

    fseek(fp,0,SEEK_END);
    size=ftell(fp);
    fseek(fp,0,SEEK_SET);
    if(size<=0){
        fclose(fp);
        return FALSE;
    }

    buf=(char*)malloc((size_t)size+1);
    if(!buf){
        fclose(fp);
        return FALSE;
    }

    read_bytes=fread(buf,1,(size_t)size,fp);
    fclose(fp);
    if(read_bytes!=(size_t)size){
        free(buf);
        return FALSE;
    }

    buf[size]='\0';
    *outbuf =buf;
    *outsize=(DWORD)size;
    return TRUE;
}

// ファイル保存
BOOL SaveFile(const char* path,const char* data,DWORD size)
{
    FILE* fp;
    DWORD i;
    fp=fopen(path,"w");
    if(!fp) return FALSE;
    for(i=0;i<size;i++){
        if(fputc(data[i],fp)==EOF){
            fclose(fp);
            return FALSE;
        }
    }
    fclose(fp);
    return TRUE;
}

// AutoSave ファイル名生成
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

