/* ============================================================
 *  mmleng16.c  (16bit向け・ストリーム版)
 *  32bit mmleng32.c の挙動を可能な限り忠実に再現
 *  ＋ 16bit向け安全化・軽量化
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mmleng16.h"

/* 16bit用固定バッファサイズ（FMT用） */
#define MAX_LINES 4096

/* グローバル FMT 用バッファ */
static char g_fmt_out[MAX_OUT];
static int  g_comment_flags[MAX_LINES];

/* ------------------------------------------------------------
 * 状態
 * ------------------------------------------------------------ */

typedef struct {
    int last_oct_global;
    int last_oct_ch[MAX_CHANNELS];

    int prev_oct_global;
    int prev_oct_ch[MAX_CHANNELS];

    int first_note_global;
    int first_note_ch[MAX_CHANNELS];
} MMLState16;

/* ------------------------------------------------------------
 * NOTE 名テーブル（32bit版と同じ）
 * ------------------------------------------------------------ */

static const char* NOTE_NAMES[12] = {
    "c","c+","d","d+","e","f","f+","g","g+","a","a+","b"
};

/* ------------------------------------------------------------
 * プロトタイプ
 * ------------------------------------------------------------ */

static int  note_to_num(const char* s, int* consumed);
static char get_normalized_char(const char** pp);

static void init_state16(MMLState16* st);
static int  scan_pass1(const char* text, int shift, int mode,
                       MMLState16* st, MMLErrorInfo* err_info);
static int  render_pass2(const char* text, int shift, int mode,
                         MMLState16* st, char* outbuf, int outsize);

static int  detect_channel_at_line_head(const char* line_start,
                                        int* p_ch_index, char* p_ch_char);
static int  find_next_note_oct(const char* text, int start_ip,
                               int shift, int mode,
                               int cur_oct, int current_channel);

/* FMT 関連（32bit版と同じ意味・順番） */
static void mml_remove_blank_lines(char* buf);
static void mml_trim_line_head_spaces(char* buf, int outsize);
static void mml_insert_section_breaks(char* buf, int outsize);
static void mml_compress_spaces(char* buf);

static void mml_mark_comment_lines(const char* buf, int* comment_flags);
static void init_comment_flags(int* flags, int size);

/* 16bit向け 軽量 itoa（sprintf 代替） */
static void mml_itoa_16(int val, char* dst);

/* ------------------------------------------------------------
 * 16bit向け 軽量 itoa
 * ------------------------------------------------------------ */

static void mml_itoa_16(int val, char* dst)
{
    char buf[16];
    int pos = 0;
    int i;

    if (val < 0) {
        *dst++ = '-';
        val = -val;
    }

    if (val == 0) {
        *dst++ = '0';
        *dst = '\0';
        return;
    }

    while (val > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = (char)('0' + (val % 10));
        val /= 10;
    }

    for (i = pos - 1; i >= 0; i--) {
        *dst++ = buf[i];
    }
    *dst = '\0';
}

/* ------------------------------------------------------------
 * 音名 → 半音番号（32bit版と同じ）
 * ------------------------------------------------------------ */

static int note_to_num(const char* s, int* consumed)
{
    char c;
    int base;

    c = s[0];
    base = -1;

    if (c == 'c') base = 0;
    else if (c == 'd') base = 2;
    else if (c == 'e') base = 4;
    else if (c == 'f') base = 5;
    else if (c == 'g') base = 7;
    else if (c == 'a') base = 9;
    else if (c == 'b') base = 11;
    else {
        *consumed = 0;
        return -1;
    }

    if (s[1] == '+' || s[1] == '#') {
        *consumed = 2;
        return base + 1;
    }

    if (s[1] == '-') {
        *consumed = 2;
        return base - 1;
    }

    *consumed = 1;
    return base;
}

/* ------------------------------------------------------------
 * 改行正規化（CR/LF → '\n'）
 * ------------------------------------------------------------ */

static char get_normalized_char(const char** pp)
{
    const char* p;
    char c;

    p = *pp;
    c = *p;

    if (c == '\r') {
        if (p[1] == '\n') {
            *pp += 2;
            return '\n';
        } else {
            *pp += 1;
            return '\n';
        }
    }

    *pp += 1;
    return c;
}

/* ------------------------------------------------------------
 * 状態初期化
 * ------------------------------------------------------------ */

static void init_state16(MMLState16* st)
{
    int i;
    st->last_oct_global   = -999;
    st->prev_oct_global   = -999;
    st->first_note_global = 0;

    for (i = 0; i < MAX_CHANNELS; i++) {
        st->last_oct_ch[i]   = -999;
        st->prev_oct_ch[i]   = -999;
        st->first_note_ch[i] = 0;
    }
}

/* ------------------------------------------------------------
 * チャンネル判定（行頭 "A "～"Z " / "a " / "b "）
 *  index: A-Z -> 0-25, a->26, b->27
 * ------------------------------------------------------------ */

static int detect_channel_at_line_head(const char* line_start,
                                       int* p_ch_index, char* p_ch_char)
{
    const char* p = line_start;
    char c;
    int idx = -1;

    while (*p == ' ' || *p == '\t')
        p++;

    c = *p;

    if (c >= 'A' && c <= 'Z' && p[1] == ' ') {
        idx = c - 'A';
    } else if ((c == 'a' || c == 'b') && p[1] == ' ') {
        idx = 26 + (c - 'a');
    } else {
        return 0;
    }

    if (p_ch_index) *p_ch_index = idx;
    if (p_ch_char)  *p_ch_char  = c;
    return 1;
}

/* ------------------------------------------------------------
 * oX の先の NOTE のオクターブをストリームで探す
 * （チャンネル切り替えで打ち切り：32bit と同じ思想）
 * ------------------------------------------------------------ */

static int find_next_note_oct(const char* text, int start_ip,
                              int shift, int mode,
                              int cur_oct, int current_channel)
{
    const char* p = text + start_ip;
    int base_mode = mode & 7;
    int is_noise_shift = ((mode & MODE_NOISE_SHIFT) != 0);

    (void)base_mode; /* 現状では未使用だが将来拡張用 */

    while (*p) {
        int ch_index;
        char ch_char;

        if (p == text || p[-1] == '\n') {
            if (detect_channel_at_line_head(p, &ch_index, &ch_char)) {
                if (current_channel != -1 && ch_index != current_channel)
                    break;
            }
        }

        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\r' || *p == '\n') {
                    const char* q = p;
                    get_normalized_char(&q);
                    p = q;
                } else {
                    p++;
                }
            }
            if (*p) p += 2;
            continue;
        }

        if (*p == ';') {
            while (*p && *p != '\n') {
                const char* q = p;
                get_normalized_char(&q);
                p = q;
            }
            continue;
        }

        if (*p == '/' && !(p[1] == '*')) {
            while (*p && *p != '\n') {
                const char* q = p;
                get_normalized_char(&q);
                p = q;
            }
            continue;
        }

        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            const char* q = p;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
                get_normalized_char(&q);
            }
            p = q;
            continue;
        }

        if (*p == '<' || *p == '>') {
            const char* q = p;
            while (*q == '<') { cur_oct--; q++; }
            while (*q == '>') { cur_oct++; q++; }
            p = q;
            continue;
        }

        if (*p == 'o') {
            const char* q = p + 1;
            int n = 0;
            int has_digit = 0;
            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (*q - '0');
                q++;
                has_digit = 1;
            }
            if (has_digit) {
                cur_oct = n;
            }
            p = q;
            continue;
        }

        if (*p == '@') {
            const char* q = p + 1;
            if (*q == '@') q++;
            while (((*q >= 'a' && *q <= 'z') ||
                    (*q >= 'A' && *q <= 'Z'))) {
                q++;
            }
            while (*q >= '0' && *q <= '9') {
                q++;
            }
            p = q;
            continue;
        }

        if (*p >= '0' && *p <= '9') {
            const char* q = p;
            while (*q >= '0' && *q <= '9') q++;
            p = q;
            continue;
        }

        if (*p == '&' || *p == '^') {
            p++;
            continue;
        }

        {
            int consumed = 0;
            int num = note_to_num(p, &consumed);

            if (*p == 'r') {
                const char* q = p + 1;
                while (*q >= '0' && *q <= '9') q++;
                p = q;
                continue;
            }

            if (num >= 0) {
                int dst_oct;
                int dst_note;

                /* Dチャンネルの場合は常に octave=0、noise_shift でもオクターブは変わらない */
                if (current_channel == 3) {
                    dst_oct = 0;
                    if (is_noise_shift) {
                        int nn = num + shift;
                        dst_note = nn % 12;
                        if (dst_note < 0) dst_note += 12;
                    } else {
                        dst_note = num;
                    }
                } else {
                    int semis;

                    if (cur_oct < 0) cur_oct = 0;
                    if (cur_oct > 8) cur_oct = 8;

                    semis = cur_oct * 12 + num + shift;
                    dst_oct = semis / 12;
                    dst_note = semis % 12;
                    if (dst_note < 0) {
                        dst_note += 12;
                        dst_oct -= 1;
                    }
                }

                (void)dst_note; /* ここでは octave だけ返せばよい */

                return dst_oct;
            }
        }

        p++;
    }

    return -999;
}

/* ------------------------------------------------------------
 * パス1：絶対オクターブ決定＋オクターブ範囲チェック
 * ------------------------------------------------------------ */

static int scan_pass1(const char* text, int shift, int mode,
                      MMLState16* st, MMLErrorInfo* err_info)
{
    const char* p = text;
    int cur_oct = 4;
    int current_channel = -1;
    char current_ch_char = ' ';
    int current_line = 1;

    int base_mode = mode & 7;
    int is_noise_shift = ((mode & MODE_NOISE_SHIFT) != 0);

    (void)base_mode; /* 現状では未使用だが、将来拡張用に残す */

    while (*p) {

        /* 行頭チャンネル判定 */
        if (p == text || p[-1] == '\n') {
            int ch_index;
            char ch_char;
            if (detect_channel_at_line_head(p, &ch_index, &ch_char)) {
                current_channel = ch_index;
                current_ch_char = ch_char;
            }
        }

        /* ブロックコメント */
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\r' || *p == '\n') {
                    const char* q = p;
                    char c = get_normalized_char(&q);
                    p = q;
                    if (c == '\n') current_line++;
                } else {
                    p++;
                }
            }
            if (*p) p += 2;
            continue;
        }

        /* ';' 行コメント */
        if (*p == ';') {
            while (*p && *p != '\n') {
                const char* q = p;
                char c = get_normalized_char(&q);
                p = q;
                if (c == '\n') current_line++;
            }
            continue;
        }

        /* '/' 行コメント */
        if (*p == '/' && !(p[1] == '*')) {
            while (*p && *p != '\n') {
                const char* q = p;
                char c = get_normalized_char(&q);
                p = q;
                if (c == '\n') current_line++;
            }
            continue;
        }

        /* 空白・改行 */
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            const char* q = p;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
                char c = get_normalized_char(&q);
                if (c == '\n') current_line++;
            }
            p = q;
            continue;
        }

        /* < > */
        if (*p == '<' || *p == '>') {
            const char* q = p;

            if (current_channel == 3) {
                /* Dch: オクターブは常に 0、< > は意味を持たない */
                while (*q == '<') q++;
                while (*q == '>') q++;
                cur_oct = 0;
                p = q;
                continue;
            }

            while (*q == '<') { cur_oct--; q++; }
            while (*q == '>') { cur_oct++; q++; }
            p = q;
            continue;
        }

        /* oX */
        if (*p == 'o') {
            const char* q = p + 1;
            int n = 0, has_digit = 0;
            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (*q - '0');
                q++; has_digit = 1;
            }

            if (current_channel == 3) {
                /* Dch: oX はすべて「o0扱い」、オクターブは常に 0 */
                cur_oct = 0;
                p = q;
                continue;
            }

            if (has_digit) cur_oct = n;
            p = q;
            continue;
        }

        /* @コマンド */
        if (*p == '@') {
            const char* q = p + 1;
            if (*q == '@') q++;
            while ((*q >= 'a' && *q <= 'z') ||
                   (*q >= 'A' && *q <= 'Z')) q++;
            while (*q >= '0' && *q <= '9') q++;
            p = q;
            continue;
        }

        /* 裸の数字 */
        if (*p >= '0' && *p <= '9') {
            const char* q = p;
            while (*q >= '0' && *q <= '9') q++;
            p = q;
            continue;
        }

        /* タイ */
        if (*p == '&' || *p == '^') {
            p++;
            continue;
        }

        /* NOTE / REST */
        {
            int consumed = 0;
            int num = note_to_num(p, &consumed);

            /* REST */
            if (*p == 'r') {
                const char* q = p + 1;
                while (*q >= '0' && *q <= '9') q++;
                p = q;
                continue;
            }

            /* NOTE */
            if (num >= 0) {
                int dst_oct;
                int dst_note;

                if (current_channel == 3) {
                    /* Dch: octave=0固定、noise_shift でもオクターブは変わらない */
                    dst_oct = 0;
                    if (is_noise_shift) {
                        int nn = num + shift;
                        dst_note = nn % 12;
                        if (dst_note < 0) dst_note += 12;
                    } else {
                        dst_note = num;
                    }
                } else {
                    int semis;

                    if (cur_oct < 0) cur_oct = 0;
                    if (cur_oct > 8) cur_oct = 8;

                    semis = cur_oct * 12 + num + shift;
                    dst_oct = semis / 12;
                    dst_note = semis % 12;

                    if (dst_note < 0) {
                        dst_note += 12;
                        dst_oct -= 1;
                    }

                    if (dst_oct < 0 || dst_oct > 8) {
                        if (err_info) {
                            err_info->error_code = MML_ERR_OCTAVE_OUT_OF_RANGE;
                            err_info->channel_char = current_ch_char;
                            err_info->line_number = current_line;
                            err_info->calculated_value = dst_oct;
                        }
                        return MML_ERR_OCTAVE_OUT_OF_RANGE;
                    }
                }

                /* 最初の NOTE のオクターブだけ記録（32bit と同じ思想） */
                if (current_channel >= 0 && current_channel < MAX_CHANNELS) {
                    if (!st->first_note_ch[current_channel]) {
                        st->first_note_ch[current_channel] = 1;
                        st->last_oct_ch[current_channel]   = dst_oct;
                    }
                } else {
                    if (!st->first_note_global) {
                        st->first_note_global = 1;
                        st->last_oct_global   = dst_oct;
                    }
                }

                p += consumed;
                while (*p >= '0' && *p <= '9') p++;
                continue;
            }
        }

        /* その他 */
        p++;
    }

    return 0;
}

/* ------------------------------------------------------------
 * パス2：本番出力（ストリーム）
 * 32bit mml_render_common の完全移植版
 * ------------------------------------------------------------ */
static int render_pass2(const char* text, int shift, int mode,
                        MMLState16* st, char* outbuf, int outsize)
{
    const char* p = text;
    int outpos = 0;

    /* mode: 3bit FMT/REL/ABS + Dch拡張ビット */
    int is_fmt  = (mode & 4) ? 1 : 0;  /* 整形 */
    int is_rel  = (mode & 2) ? 1 : 0;  /* 相対表記 */
    int is_abs  = (mode & 1) ? 1 : 0;  /* 絶対表記 */
    int is_noise_shift = (mode & MODE_NOISE_SHIFT) ? 1 : 0;

    /* Smart Rewrite は下位2bitが 00 のとき */
    int is_smart = (!is_rel && !is_abs);

    /* Pure Layout 判定（32bitと同じ） */
    int is_pure_layout = (!is_fmt && (is_rel || is_abs));

    int cur_oct = 4;
    int current_channel = -1;
    char current_ch_char = ' ';

    /* 32bit版と同じ NOTE 状態管理 */
    int first_note_done[MAX_CHANNELS];
    int last_oct[MAX_CHANNELS];
    int global_first_note_done = 0;
    int global_last_oct = -999;

    int i;

    if (outsize <= 0) return 0;
    memset(outbuf, 0, outsize);

    for (i = 0; i < MAX_CHANNELS; i++) {
        first_note_done[i] = 0;
        last_oct[i] = -999;
    }

    /* Smart Rewrite 用 prev_oct 初期化（32bitと同じ） */
    st->prev_oct_global = st->last_oct_global;
    if (st->prev_oct_global == -999) st->prev_oct_global = 4;

    for (i = 0; i < MAX_CHANNELS; i++) {
        st->prev_oct_ch[i] = st->last_oct_ch[i];
        if (st->prev_oct_ch[i] == -999) st->prev_oct_ch[i] = 4;
    }

    /* --------------------------------------------------------
     * ストリーム走査開始
     * -------------------------------------------------------- */
    while (*p && outpos < outsize - 1) {

        /* 行頭チャンネル判定 */
        if (p == text || p[-1] == '\n') {
            int ch_index;
            char ch_char;
            if (detect_channel_at_line_head(p, &ch_index, &ch_char)) {
                current_channel = ch_index;
                current_ch_char = ch_char;
            }
        }

        /* ブロックコメント：そのままコピー */
        if (p[0] == '/' && p[1] == '*') {
            const char* q = p;
            char ch;
            q += 2;
            if (outpos < outsize - 1) outbuf[outpos++] = '/';
            if (outpos < outsize - 1) outbuf[outpos++] = '*';

            while (*q && !(q[0] == '*' && q[1] == '/')) {
                ch = get_normalized_char(&q);
                if (outpos < outsize - 1) outbuf[outpos++] = ch;
            }
            if (*q) {
                if (outpos < outsize - 1) outbuf[outpos++] = '*';
                if (outpos < outsize - 1) outbuf[outpos++] = '/';
                q += 2;
            }
            p = q;
            continue;
        }

        /* ';' 行コメント */
        if (*p == ';') {
            const char* q = p;
            char ch;
            while (*q) {
                ch = get_normalized_char(&q);
                if (outpos < outsize - 1) outbuf[outpos++] = ch;
                if (ch == '\n') break;
            }
            p = q;
            continue;
        }

        /* '/' 行コメント */
        if (*p == '/' && !(p[1] == '*')) {
            const char* q = p;
            char ch;
            while (*q) {
                ch = get_normalized_char(&q);
                if (outpos < outsize - 1) outbuf[outpos++] = ch;
                if (ch == '\n') break;
            }
            p = q;
            continue;
        }

        /* 空白・改行 */
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            const char* q = p;
            char ch;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
                ch = get_normalized_char(&q);
                if (outpos < outsize - 1) outbuf[outpos++] = ch;
            }
            p = q;
            continue;
        }

        /* < > */
        if (*p == '<' || *p == '>') {
            const char* q = p;

            /* 32bitと同じ：<> 自体は出力しない */
            if (current_channel == 3) {
                /* Dch: octave=0固定、<>は無効 */
                while (*q == '<') q++;
                while (*q == '>') q++;
                cur_oct = 0;
                p = q;
                continue;
            }

            while (*q == '<') { cur_oct--; q++; }
            while (*q == '>') { cur_oct++; q++; }
            p = q;
            continue;
        }

        /* oX */
        if (*p == 'o') {
            const char* q = p + 1;
            int n = 0;
            int has_digit = 0;

            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (*q - '0');
                q++;
                has_digit = 1;
            }

            if (!has_digit) {
                /* 単独 'o' はそのまま出力 */
                if (outpos < outsize - 1) outbuf[outpos++] = 'o';
                p++;
                continue;
            }

            /* Dch の oX は Smart Rewrite の対象外（32bitと同じ） */
            if (current_channel == 3) {
                /* noise_shift OFF のときは元の oX を壊さない */
                if (!is_noise_shift) {
                    if (outpos < outsize - 1) outbuf[outpos++] = 'o';
                    {
                        char tmp[16];
                        int k = 0;
                        mml_itoa_16(n, tmp);
                        while (tmp[k] && outpos < outsize - 1)
                            outbuf[outpos++] = tmp[k++];
                    }
                    p = q;
                    continue;
                }
                /* noise_shift ON のときは Smart Rewrite する */
            }

            if (is_smart) {
                /* Smart Rewrite: 次の NOTE の octave に書き換える */
                int base_oct = find_next_note_oct(text,
                                                  (int)(q - text),
                                                  shift,
                                                  mode,
                                                  n,
                                                  current_channel);
                int use_oct = (base_oct != -999) ? base_oct : n;

                if (outpos < outsize - 1) outbuf[outpos++] = 'o';
                {
                    char tmp[16];
                    int k = 0;
                    mml_itoa_16(use_oct, tmp);
                    while (tmp[k] && outpos < outsize - 1)
                        outbuf[outpos++] = tmp[k++];
                }

                /* prev_oct を同期 */
                if (current_channel >= 0 && current_channel < MAX_CHANNELS)
                    st->prev_oct_ch[current_channel] = use_oct;
                else
                    st->prev_oct_global = use_oct;

                /* cur_oct はソース上の相対オクターブとして n のまま */
                cur_oct = n;

                p = q;
                continue;
            }

            /* 非スマート：oX は出力せず cur_oct だけ更新 */
            cur_oct = n;
            p = q;
            continue;
        }

        /* @コマンド */
        if (*p == '@') {
            const char* q = p + 1;
            if (outpos < outsize - 1) outbuf[outpos++] = '@';
            if (*q == '@') {
                if (outpos < outsize - 1) outbuf[outpos++] = '@';
                q++;
            }
            while (((*q >= 'a' && *q <= 'z') ||
                    (*q >= 'A' && *q <= 'Z'))) {
                if (outpos < outsize - 1) outbuf[outpos++] = *q;
                q++;
            }
            while (*q >= '0' && *q <= '9') {
                if (outpos < outsize - 1) outbuf[outpos++] = *q;
                q++;
            }
            p = q;
            continue;
        }

        /* 裸の数字 */
        if (*p >= '0' && *p <= '9') {
            const char* q = p;
            while (*q >= '0' && *q <= '9' && outpos < outsize - 1) {
                outbuf[outpos++] = *q;
                q++;
            }
            p = q;
            continue;
        }

        /* タイ & ^ */
        if (*p == '&' || *p == '^') {
            if (outpos < outsize - 1) outbuf[outpos++] = *p;
            p++;
            continue;
        }

        /* --------------------------------------------------------
         * NOTE / REST
         * -------------------------------------------------------- */
        {
            int consumed = 0;
            int num = note_to_num(p, &consumed);

            /* REST */
            if (*p == 'r') {
                const char* q = p;
                if (outpos < outsize - 1) outbuf[outpos++] = 'r';
                q++;
                while (*q >= '0' && *q <= '9' && outpos < outsize - 1) {
                    outbuf[outpos++] = *q;
                    q++;
                }
                p = q;
                continue;
            }

            /* NOTE */
            if (num >= 0) {
                int dst_oct;
                int dst_note;
                const char* name;
                char lenbuf[16];
                int lenpos = 0;
                int ch_idx = current_channel;

                /* ----------------------------------------------------
                 * Dチャンネル（ノイズ）専用処理
                 * ---------------------------------------------------- */
                if (ch_idx == 3) {
                    /* octave=0 固定 */
                    dst_oct = 0;

                    /* noise_shift ON のときだけ note を回す */
                    if (is_noise_shift) {
                        int nn = num + shift;
                        dst_note = nn % 12;
                        if (dst_note < 0) dst_note += 12;
                    } else {
                        dst_note = num;
                    }
                }
                else {
                    /* 通常チャンネル */
                    int semis;

                    if (cur_oct < 0) cur_oct = 0;
                    if (cur_oct > 8) cur_oct = 8;

                    semis = cur_oct * 12 + num + shift;
                    dst_oct = semis / 12;
                    dst_note = semis % 12;

                    if (dst_note < 0) {
                        dst_note += 12;
                        dst_oct -= 1;
                    }

                    /* パス1で範囲チェック済みなのでここでは不要 */
                }

                name = NOTE_NAMES[dst_note];

                /* 長さ読み取り */
                p += consumed;
                while (*p >= '0' && *p <= '9' && lenpos < (int)sizeof(lenbuf)-1) {
                    lenbuf[lenpos++] = *p;
                    p++;
                }
                lenbuf[lenpos] = '\0';

                /* ----------------------------------------------------
                 * Dチャンネル（ノイズ）出力（32bit完全互換）
                 * ---------------------------------------------------- */
                if (ch_idx == 3) {
                    int already_has_o0 = 0;
                    int k;

                    /* すでに o0 が出ているか確認 */
                    for (k = 0; k < outpos - 1; k++) {
                        if (outbuf[k] == 'o' && outbuf[k + 1] == '0') {
                            already_has_o0 = 1;
                            break;
                        }
                    }

                    /* 最初の NOTE の前に o0 を補完 */
                    if (!already_has_o0) {
                        if (outpos > 0 &&
                            outbuf[outpos - 1] != ' ' &&
                            outbuf[outpos - 1] != '\n')
                        {
                            if (outpos < outsize - 1)
                                outbuf[outpos++] = ' ';
                        }
                        if (outpos < outsize - 1) outbuf[outpos++] = 'o';
                        if (outpos < outsize - 1) outbuf[outpos++] = '0';
                        if (outpos < outsize - 1) outbuf[outpos++] = ' ';
                    }

                    /* NOTE 出力 */
                    k = 0;
                    while (name[k] && outpos < outsize - 1)
                        outbuf[outpos++] = name[k++];

                    k = 0;
                    while (lenbuf[k] && outpos < outsize - 1)
                        outbuf[outpos++] = lenbuf[k++];

                    continue;
                }

                /* ----------------------------------------------------
                 * Smart Rewrite（通常チャンネル）
                 * ---------------------------------------------------- */
                if (is_smart) {
                    int* p_prev;
                    int prev;
                    int diff;
                    int k;

                    /* prev_oct を取得 */
                    if (ch_idx >= 0 && ch_idx < MAX_CHANNELS)
                        p_prev = &st->prev_oct_ch[ch_idx];
                    else
                        p_prev = &st->prev_oct_global;

                    prev = *p_prev;

                    /* <> の振り直し */
                    diff = dst_oct - prev;
                    if (diff > 0) {
                        for (k = 0; k < diff && outpos < outsize - 1; k++)
                            outbuf[outpos++] = '>';
                    } else if (diff < 0) {
                        diff = -diff;
                        for (k = 0; k < diff && outpos < outsize - 1; k++)
                            outbuf[outpos++] = '<';
                    }

                    /* NOTE 出力 */
                    k = 0;
                    while (name[k] && outpos < outsize - 1)
                        outbuf[outpos++] = name[k++];

                    k = 0;
                    while (lenbuf[k] && outpos < outsize - 1)
                        outbuf[outpos++] = lenbuf[k++];

                    /* prev_oct 更新 */
                    *p_prev = dst_oct;

                    continue;
                }

                /* ----------------------------------------------------
                 * 非スマート（-a / -r）通常チャンネル
                 * 32bit mml_render_common と完全一致
                 * ---------------------------------------------------- */
                {
                    int oct = dst_oct;
                    const char* nname = name;
                    char nlen[16];
                    int nlenpos = 0;
                    int* p_first;
                    int* p_last;
                    int need_prefix = 0;
                    int j;

                    /* lenbuf をコピー */
                    while (lenbuf[nlenpos] && nlenpos < (int)sizeof(nlen)-1) {
                        nlen[nlenpos] = lenbuf[nlenpos];
                        nlenpos++;
                    }
                    nlen[nlenpos] = '\0';

                    if (ch_idx >= 0 && ch_idx < MAX_CHANNELS) {
                        p_first = &first_note_done[ch_idx];
                        p_last  = &last_oct[ch_idx];
                    } else {
                        p_first = &global_first_note_done;
                        p_last  = &global_last_oct;
                    }

                    if (!(*p_first)) {
                        need_prefix = 1;
                        *p_first = 1;
                        *p_last  = oct;
                    } else {
                        if (is_rel) {
                            int last = *p_last;

                            while (last < oct && outpos < outsize - 1) {
                                outbuf[outpos++] = '>';
                                last++;
                            }
                            while (last > oct && outpos < outsize - 1) {
                                outbuf[outpos++] = '<';
                                last--;
                            }

                            *p_last = oct;
                        } else {
                            if (*p_last != oct) {
                                need_prefix = 1;
                                *p_last = oct;
                            }
                        }
                    }

                    /* oX 出力 */
                    if (need_prefix) {
                        char tmp[16];
                        int k2;

                        if (outpos > 0 &&
                            outbuf[outpos - 1] != ' ' &&
                            outbuf[outpos - 1] != '\n')
                        {
                            if (outpos < outsize - 1)
                                outbuf[outpos++] = ' ';
                        }

                        if (outpos < outsize - 1)
                            outbuf[outpos++] = 'o';

                        mml_itoa_16(oct, tmp);
                        k2 = 0;
                        while (tmp[k2] && outpos < outsize - 1)
                            outbuf[outpos++] = tmp[k2++];

                        if (!is_pure_layout && outpos < outsize - 1)
                            outbuf[outpos++] = ' ';
                    }

                    /* NOTE 出力 */
                    j = 0;
                    while (nname[j] && outpos < outsize - 1)
                        outbuf[outpos++] = nname[j++];

                    j = 0;
                    while (nlen[j] && outpos < outsize - 1)
                        outbuf[outpos++] = nlen[j++];

                    continue;
                }
            }
        }

        /* その他1文字 */
        if (outpos < outsize - 1) outbuf[outpos++] = *p;
        p++;
    }

    outbuf[outpos] = '\0';
    return outpos;
}

/* ------------------------------------------------------------
 * コメント行マーキング（32bit版と同じロジック＋MAX_LINESガード）
 * ------------------------------------------------------------ */
static void mml_mark_comment_lines(const char* buf, int* comment_flags)
{
    int in_block = 0;
    int line = 0;
    const char* p = buf;

    while (*p && line < MAX_LINES) {

        if (in_block) {
            comment_flags[line] = 1;
        }

        if (!in_block && p[0] == '/' && p[1] == '*') {
            in_block = 1;
            comment_flags[line] = 1;
        }

        if (in_block && p[0] == '*' && p[1] == '/') {
            in_block = 0;
            comment_flags[line] = 1;
        }

        if (!in_block && (p[0] == ';' || p[0] == '/')) {
            comment_flags[line] = 1;
        }

        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
            line++;
        }
    }
}

static void init_comment_flags(int* flags, int size)
{
    int i;
    for (i = 0; i < size; i++) {
        flags[i] = 0;
    }
}

/* ------------------------------------------------------------
 * FMT: 空行削除（32bit版と同じロジック）
 * ------------------------------------------------------------ */
static void mml_remove_blank_lines(char* buf)
{
    char* src = buf;
    char* dst = buf;
    int at_line_start = 1;
    int line = 0;
    int after_comment = 0;
    int blank_count = 0;
    int current_section = 0;

    int* comment_flags = g_comment_flags;

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (*src) {

        if (line >= MAX_LINES) {
            while (*src) *dst++ = *src++;
            break;
        }

        if (at_line_start && comment_flags[line]) {
            while (*src && *src != '\n') *dst++ = *src++;
            if (*src == '\n') {
                *dst++ = *src++;
                line++;
            }
            after_comment = 1;
            at_line_start = 1;
            blank_count = 0;
            continue;
        }

        if (at_line_start) {

            if (line == 0 && *src == '\n') {
                src++;
                line++;
                continue;
            }

            if (*src == '\n') {

                if (after_comment) {
                    *dst++ = *src++;
                    line++;
                    at_line_start = 1;
                    after_comment = 0;
                    blank_count = 0;
                    continue;
                }

                if (current_section == 1 || current_section == 2) {
                    src++;
                    line++;
                    at_line_start = 1;
                    continue;
                }

                blank_count++;
                if (blank_count > 1) {
                    src++;
                    line++;
                    at_line_start = 1;
                    continue;
                }

                *dst++ = *src++;
                line++;
                at_line_start = 1;
                continue;
            }

            {
                const char* p2 = src;
                while (*p2 == ' ' || *p2 == '\t') p2++;

                if (((*p2 >= 'A' && *p2 <= 'Z') || *p2 == 'a' || *p2 == 'b') &&
                    p2[1] == ' ')
                    current_section = 1;
                else if (*p2 == '@')
                    current_section = 2;
                else
                    current_section = 0;
            }

            blank_count = 0;
            after_comment = 0;
        }

        *dst++ = *src++;

        if (dst[-1] == '\n') {
            at_line_start = 1;
            line++;
        } else {
            at_line_start = 0;
        }
    }

    *dst = '\0';
}

/* ------------------------------------------------------------
 * FMT: 行頭スペース削除
 * ------------------------------------------------------------ */
static void mml_trim_line_head_spaces(char* buf, int outsize)
{
    char* out = g_fmt_out;
    int* comment_flags = g_comment_flags;

    int i = 0, j = 0, line = 0;
    int max_limit = (outsize < MAX_OUT) ? outsize : MAX_OUT;

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[i] && j < max_limit - 1) {

        if (line < MAX_LINES && comment_flags[line]) {
            while (buf[i] && j < max_limit - 1) {
                out[j++] = buf[i++];
                if (out[j-1] == '\n') { line++; break; }
            }
            continue;
        }

        if (i == 0 || buf[i-1] == '\n') {
            while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r')
                i++;
        }

        if (!buf[i]) break;

        out[j++] = buf[i++];
        if (out[j-1] == '\n') line++;
    }

    out[j] = '\0';
    strncpy(buf, out, outsize - 1);
    buf[outsize - 1] = '\0';
}

/* ------------------------------------------------------------
 * FMT: セクション間の空行挿入
 * ------------------------------------------------------------ */
static void mml_insert_section_breaks(char* buf, int outsize)
{
    char* out = g_fmt_out;
    int* comment_flags = g_comment_flags;

    int i = 0, j = 0, line = 0;
    int prev_section = 0, current_section = 0;
    unsigned char prev_channel = 0, current_channel = 0;
    int after_comment = 0;

    int max_limit = (outsize < MAX_OUT) ? outsize : MAX_OUT;

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[i] && j < max_limit - 1) {

        if (line < MAX_LINES && comment_flags[line]) {
            while (buf[i] && buf[i] != '\n' && j < max_limit - 1)
                out[j++] = buf[i++];
            if (buf[i] == '\n' && j < max_limit - 1)
                out[j++] = buf[i++];
            line++;
            after_comment = 1;
            continue;
        }

        current_section = 0;
        current_channel = 0;

        {
            int k2 = i;
            while (buf[k2] == ' ' || buf[k2] == '\t') k2++;

            if (((buf[k2] >= 'A' && buf[k2] <= 'Z') || buf[k2] == 'a' || buf[k2] == 'b') &&
                buf[k2+1] == ' ')
            {
                current_section = 1;
                current_channel = (unsigned char)buf[k2];
            }
            else if (buf[k2] == '@') {
                current_section = 2;
            }
        }

        if (!after_comment &&
            prev_section != 0 &&
            current_section != 0)
        {
            if (prev_section != current_section ||
                (current_section == 1 && prev_channel != current_channel))
            {
                if (j < max_limit - 1)
                    out[j++] = '\n';
            }
        }

        while (buf[i] && buf[i] != '\n' && j < max_limit - 1)
            out[j++] = buf[i++];
        if (buf[i] == '\n' && j < max_limit - 1)
            out[j++] = buf[i++];

        prev_section = current_section;
        prev_channel = current_channel;
        after_comment = 0;
        line++;
    }

    out[j] = '\0';
    strncpy(buf, out, outsize - 1);
    buf[outsize - 1] = '\0';
}

/* ------------------------------------------------------------
 * FMT: スペース圧縮
 * ------------------------------------------------------------ */
static void mml_compress_spaces(char* buf)
{
    int r = 0, w = 0, space = 0, line = 0;
    int* comment_flags = g_comment_flags;

    init_comment_flags(comment_flags, MAX_LINES);
    mml_mark_comment_lines(buf, comment_flags);

    while (buf[r]) {

        if (line < MAX_LINES && comment_flags[line]) {
            while (buf[r]) {
                buf[w++] = buf[r++];
                if (buf[r-1] == '\n') { line++; break; }
            }
            continue;
        }

        if (buf[r] == ' ') {
            if (!space) {
                buf[w++] = ' ';
                space = 1;
            }
        } else {
            buf[w++] = buf[r];
            space = 0;
        }

        if (buf[r] == '\n') line++;
        r++;
    }

    buf[w] = '\0';
}

/* ------------------------------------------------------------
 * 外向き API（32bit版と完全互換）
 * ------------------------------------------------------------ */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info)
{
    MMLState16 st;
    int outlen;
    int base_mode;
    int i;

    if (err_info) memset(err_info, 0, sizeof(MMLErrorInfo));

    if (!in_text || !outbuf) {
        if (err_info) err_info->error_code = MML_ERR_NULL_INPUT;
        return MML_ERR_NULL_INPUT;
    }
    if (in_text[0] == '\0') {
        if (err_info) err_info->error_code = MML_ERR_EMPTY_INPUT;
        return MML_ERR_EMPTY_INPUT;
    }
    if (outsize <= 0) {
        if (err_info) err_info->error_code = MML_ERR_OUTBUF;
        return MML_ERR_OUTBUF;
    }
    if (shift < -12 || shift > 12) {
        if (err_info) err_info->error_code = MML_ERR_BAD_SHIFT;
        return MML_ERR_BAD_SHIFT;
    }

    /* 下位3bitのみでモードチェック（0〜6） */
    base_mode = mode & 7;
    if (base_mode < 0 || base_mode > 6) {
        if (err_info) err_info->error_code = MML_ERR_BAD_MODE;
        return MML_ERR_BAD_MODE;
    }

    init_state16(&st);

    /* パス1：絶対オクターブ決定＋オクターブ範囲チェック */
    {
        int res = scan_pass1(in_text, shift, mode, &st, err_info);
        if (res != 0) return res;
    }

    /* パス2：本番出力 */
    outlen = render_pass2(in_text, shift, mode, &st, outbuf, outsize);

    /* FMT */
    if (mode & 4) {
        mml_remove_blank_lines(outbuf);
        mml_trim_line_head_spaces(outbuf, outsize);
        mml_insert_section_breaks(outbuf, outsize);
        mml_compress_spaces(outbuf);
    }

    /* 改行正規化 */
    for (i = 0; outbuf[i]; i++) {
        if (outbuf[i] == '\r') outbuf[i] = '\n';
    }

    return (int)strlen(outbuf);
}


