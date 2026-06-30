/* ============================================================
 * msgw16.h - Win16 GUI message table (EN/JP, Win32-compatible)
 * ------------------------------------------------------------
 * Structure is identical to Win32 LangTable.
 * Win16 does NOT use tooltip messages, but fields exist
 * for structural compatibility.
 * ============================================================ */

#ifndef MSGW16_H
#define MSGW16_H

#include <windows.h>

/* ------------------------------------------------------------
 * Language IDs
 * ------------------------------------------------------------ */
typedef enum {
    LANG_EN = 0,
    LANG_JP,
    LANG_MAX
} GUILang;

/* ------------------------------------------------------------
 * GUI Error Codes (identical to Win32)
 * ------------------------------------------------------------ */
typedef enum {
    GUI_ERR_OK = 0,

    /* LoadFile errors */
    GUI_ERR_LF_OPEN,
    GUI_ERR_LF_SIZE,
    GUI_ERR_LF_EMPTY,
    GUI_ERR_LF_TOO_LARGE,
    GUI_ERR_LF_READ,

    /* MML processing errors */
    GUI_ERR_MML_OCTAVE,
    GUI_ERR_MML_NULL,
    GUI_ERR_MML_EMPTY,
    GUI_ERR_MML_OUTBUF,
    GUI_ERR_MML_BAD_SHIFT,
    GUI_ERR_MML_BAD_MODE,
    GUI_ERR_MML_UNKNOWN,

    /* SaveFile errors */
    GUI_ERR_SF_OPEN,
    GUI_ERR_SF_WRITE,

    GUI_ERR_MAX
} GUIErrorCode;

/* ------------------------------------------------------------
 * Tooltip IDs (FULL VERSION, Win16 does not use them)
 * ------------------------------------------------------------ */
typedef enum {
    TIP_FILENAME_LABEL = 0,
    TIP_FILENAME_TOOLTIP,
    TIP_SLIDER,
    TIP_KEY_LABEL,
    TIP_KEY_EDIT,
    TIP_KEY_SPIN,
    TIP_SAVE_AUTO,
    TIP_SAVE_AS,
    TIP_OPTION_LABEL,
    TIP_REL,
    TIP_ABS,
    TIP_FORMAT,
    TIP_DSHIFT,
    TIP_AUTOSAVE,
    TIP_INVALID_TYPE,
    TIP_DROP_ERROR,

    TIP_MAX
} GUITooltipID;

/* ------------------------------------------------------------
 * Language Table Structure (identical to Win32)
 * ------------------------------------------------------------ */
typedef struct {
    const char* font_name;
    int         font_size;
    BYTE        charset;

    const char* gui_msg[GUI_ERR_MAX];
    const char* tip[TIP_MAX];   /* Win16 does NOT use tooltips */

    const char* ui_label_key;
    const char* ui_label_option;
    const char* ui_label_quick;
    const char* ui_label_save;
    const char* ui_label_auto;

} LangTable;

/* ------------------------------------------------------------
 * Language Table (Win16 version, identical structure)
 * ------------------------------------------------------------ */
static const LangTable g_lang_table16[LANG_MAX] = {

    /* English */
    {
        "MS Sans Serif",
        8,
        ANSI_CHARSET,

        {
            NULL,
            "Cannot open input file.",
            "Failed to get file size.",
            "Input file is empty.",
            "Input file is too large (%lu bytes).\nMaximum allowed is %d bytes.",
            "Failed to read file.",
            "Octave limit exceeded:\nchannel '%c', line %d (value: o%d).",
            "Input text is NULL.",
            "Input text is empty.",
            "Output buffer is too small.",
            "Shift amount is out of range (-12 to +12).",
            "Invalid mode.",
            "Unknown MML error (code: %d).",
            "Cannot open output file.",
            "Failed to write file."
        },

        {
            "No file selected.",   /* TIP_FILENAME_LABEL    */
            NULL,                  /* TIP_FILENAME_TOOLTIP  */
            NULL,                  /* TIP_SLIDER            */
            NULL,                  /* TIP_KEY_LABEL         */
            NULL,                  /* TIP_KEY_EDIT          */
            NULL,                  /* TIP_KEY_SPIN          */
            NULL,                  /* TIP_SAVE_AUTO         */
            NULL,                  /* TIP_SAVE_AS           */
            NULL,                  /* TIP_OPTION_LABEL      */
            NULL,                  /* TIP_REL               */
            NULL,                  /* TIP_ABS               */
            NULL,                  /* TIP_FORMAT            */
            NULL,                  /* TIP_DSHIFT            */
            NULL,                  /* TIP_AUTOSAVE          */
            "Invalid file type.",  /* TIP_INVALID_TYPE      */
            "Drop error."          /* TIP_DROP_ERROR        */
        },

        "Key:",
        "Opt:",
        "Quick",
        "Save",
        "Auto"
    },

    /* Japanese */
    {
        "ＭＳ Ｐゴシック",
        9,
        SHIFTJIS_CHARSET,

        {
            NULL,
            "入力ファイルを開けません。",
            "ファイルサイズを取得できません。",
            "入力ファイルが空です。",
            "入力ファイルが大きすぎます (%lu バイト)。\n最大 %d バイトです。",
            "ファイル読み込みに失敗しました。",
            "移調によりオクターブ限界を突破しました:\n"
            "チャンネル '%c', %d 行目 (計算値: o%d)。",
            "入力テキストが NULL です。",
            "入力テキストが空です。",
            "出力バッファサイズが不足しています。",
            "移調量が範囲外です（-12～+12）。",
            "モードが無効です。",
            "不明な MML エラー (コード: %d)。",
            "保存ファイルを開けません。",
            "ファイル書き込みに失敗しました。"
        },

        {
            "ファイルが選択されていません。", /* TIP_FILENAME_LABEL    */
            NULL,                               /* TIP_FILENAME_TOOLTIP  */
            NULL,                               /* TIP_SLIDER            */
            NULL,                               /* TIP_KEY_LABEL         */
            NULL,                               /* TIP_KEY_EDIT          */
            NULL,                               /* TIP_KEY_SPIN          */
            NULL,                               /* TIP_SAVE_AUTO         */
            NULL,                               /* TIP_SAVE_AS           */
            NULL,                               /* TIP_OPTION_LABEL      */
            NULL,                               /* TIP_REL               */
            NULL,                               /* TIP_ABS               */
            NULL,                               /* TIP_FORMAT            */
            NULL,                               /* TIP_DSHIFT            */
            NULL,                               /* TIP_AUTOSAVE          */
            "ファイル種別が不正です。",         /* TIP_INVALID_TYPE      */
            "ドロップエラーです。"              /* TIP_DROP_ERROR        */
        },

        "キー",
        "設定",
        "ｸｲｯｸ",
        "保存",
        "自動"
    }
};

#endif /* MSGW16_H */
