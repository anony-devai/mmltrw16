/* ============================================================
 * mmleng16.h  (16bit DOS / C89 準拠・32bit版互換インタフェース)
 * ============================================================ */

#ifndef MMLENG16_H_INCLUDED
#define MMLENG16_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------
 * バッファ制限（16bit向け）
 * ------------------------------------------------------------ */
#define MAX_TEXT     8192
#define MAX_OUT      16384
#define MAX_TOKENS   2048   /* 16bit版では内部では未使用だが互換のため残す */
#define MAX_RAW_LEN  128

/* 最大チャンネル数: 28
 * (大文字 A-Z = 26) + (小文字 a, b = 2)
 */
#define MAX_CHANNELS 28

/* ------------------------------------------------------------
 * トークン種別（32bit版と同じ定義）
 * ------------------------------------------------------------ */
typedef enum {
    TK_NONE = 0,
    TK_NOTE,
    TK_REST,
    TK_RAW
} MMLTokenType;

/* ------------------------------------------------------------
 * エラーコード（32bit版と完全共通）
 * ------------------------------------------------------------ */
#define MML_ERR_NULL_INPUT             -1  /* 入力/出力ポインタがNULL */
#define MML_ERR_EMPTY_INPUT            -2  /* 入力文字列が空 */
#define MML_ERR_OUTBUF                 -3  /* 出力バッファサイズが不正(0以下) */
#define MML_ERR_BAD_SHIFT              -4  /* 移調幅が範囲外 (-12～12 以外) */
#define MML_ERR_BAD_MODE               -5  /* モード指定が範囲外 */
#define MML_ERR_PARSE                  -6  /* 32bit Token版専用（16bitでは未使用） */
#define MML_ERR_OCTAVE_OUT_OF_RANGE   -10 /* 移調によりオクターブ限界を突破 */

/* ------------------------------------------------------------
 * モード体系（3bit + Dch拡張ビット）
 * ------------------------------------------------------------ */
/* 3bit FMT/REL/ABS
   bit2 (4): FMT  … 整形するかどうか
   bit1 (2): REL  … 相対表記（<>）
   bit0 (1): ABS  … 絶対表記（oX） */

#define MODE_PURE        0   /* 0000 : Pure（整形なし・oX/<> 振り直し） */
#define MODE_PURE_ABS    1   /* 0001 : Pure + Abs（整形なし） */
#define MODE_PURE_REL    2   /* 0010 : Pure + Rel（整形なし） */
#define MODE_FMT         4   /* 0100 : FMT（整形あり・oX/<> 振り直し） */
#define MODE_FMT_ABS     5   /* 0101 : FMT + Abs（整形あり） */
#define MODE_FMT_REL     6   /* 0110 : FMT + Rel（整形あり） */

/* Dチャンネル（ノイズ）音符シフト有効化フラグ (-d オプション用) */
#define MODE_NOISE_SHIFT 8   /* 1000 : Dchのみ音符をシフトし、o0固定 */

/* ------------------------------------------------------------
 * エラー詳細情報（32bit版と同じ構造）
 * ------------------------------------------------------------ */
typedef struct {
    int  error_code;       /* エラーコード (MML_ERR_xxx) */
    char channel_char;     /* エラーが発生したチャンネル文字 (A-Z, a, b) */
    int  line_number;      /* エラーが発生したMMLの行番号 (1から開始) */
    int  calculated_value; /* 限界突破した際の、計算上の不正なオクターブ値 */
} MMLErrorInfo;

/* ------------------------------------------------------------
 * Token 構造体（16bit内部では使わないが、32bitとの互換用に定義）
 * ------------------------------------------------------------ */
typedef struct {
    MMLTokenType type;
    int  octave;                 /* NOTE/REST 用の絶対オクターブ */
    int  note;                   /* 0～11, REST のときは -1 */
    char length[16];             /* "4", "8.", "16" など */
    char raw[MAX_RAW_LEN];       /* 生テキスト（内部 RAW） */
    int  is_literal_o0;          /* o0 をそのまま残すかどうか */
    int  is_raw_ox;              /* oX を RAW として扱うかどうか */
    int  is_comment;             /* コメント由来トークンかどうか */
} Token;

/* ------------------------------------------------------------
 * 外部公開 API（32bit版と同じシグネチャ）
 * ------------------------------------------------------------ */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info);

#ifdef __cplusplus
}
#endif

#endif /* MMLENG16_H_INCLUDED */

