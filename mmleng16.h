/* ============================================================
 * mmleng16.h  (16-bit DOS / C89 compliant, 32-bit compatible interface)
 *
 * Common MML Transposer Engine interface used by:
 *      - mmltrdos   (MS-DOS / CUI)
 *      - mmltrw16   (Windows 16-bit / GUI)
 * and callable from 32-bit applications as well.
 *
 * The API is fully shared with the 32-bit engine, allowing
 * cross-version compatibility. However, the 32-bit engine
 * cannot be used in 16-bit environments (except via DOS extenders).
 * ============================================================ */

#ifndef MMLENG16_H_INCLUDED
#define MMLENG16_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------
 * Buffer limits for the 16-bit environment
 * ------------------------------------------------------------ */
#define MAX_TEXT     8192
#define MAX_OUT      16384
#define MAX_RAW_LEN  128

/* Maximum number of channels: 28
 * (Uppercase A–Z = 26) + (Lowercase a, b = 2)
 */
#define MAX_CHANNELS 28

/* ------------------------------------------------------------
 * Error codes (fully shared with the 32-bit version)
 * ------------------------------------------------------------ */
#define MML_ERR_NULL_INPUT             -1  /* Input/output pointer is NULL */
#define MML_ERR_EMPTY_INPUT            -2  /* Input string is empty */
#define MML_ERR_OUTBUF                 -3  /* Output buffer size is invalid (<= 0) */
#define MML_ERR_BAD_SHIFT              -4  /* Transpose value out of range (-12 to +12) */
#define MML_ERR_BAD_MODE               -5  /* Mode value out of range */
/* Note:
 * MML_ERR_PARSE is reserved for the 32-bit token-based parser.
 * The 16-bit engine does not use tokenization and never returns this code.
 */
#define MML_ERR_PARSE                  -6  /* Reserved for 32-bit token parser (unused in 16-bit) */
#define MML_ERR_OCTAVE_OUT_OF_RANGE   -10 /* Octave exceeded limit due to transposition */

/* ------------------------------------------------------------
 * Mode flags (3-bit base + D-channel extension bit)
 *
 * 3-bit FMT / REL / ABS
 *   bit2 (4): FMT  – Enable formatting
 *   bit1 (2): REL  – Relative notation (<>)
 *   bit0 (1): ABS  – Absolute notation (oX)
 * ------------------------------------------------------------ */
#define MODE_PURE        0   /* 0000 : Pure (no formatting, reassign oX/<> automatically) */
#define MODE_PURE_ABS    1   /* 0001 : Pure + Abs (no formatting) */
#define MODE_PURE_REL    2   /* 0010 : Pure + Rel (no formatting) */
#define MODE_FMT         4   /* 0100 : Format enabled (reassign oX/<> automatically) */
#define MODE_FMT_ABS     5   /* 0101 : Format + Abs */
#define MODE_FMT_REL     6   /* 0110 : Format + Rel */

/* D-channel (noise) note-shift enable flag (-d option)
 * When enabled, only D-channel notes are shifted and octave is fixed at o0.
 */
#define MODE_NOISE_SHIFT 8   /* 1000 : Shift notes only on D-channel, keep o0 fixed */

/* ------------------------------------------------------------
 * Detailed error information (same structure as 32-bit version)
 * ------------------------------------------------------------ */
typedef struct {
    int  error_code;       /* Error code (MML_ERR_xxx) */
    char channel_char;     /* Channel character where the error occurred (A–Z, a, b) */
    int  line_number;      /* Line number in the MML text (starting from 1) */
    int  calculated_value; /* Invalid octave value calculated when exceeding limits */
} MMLErrorInfo;

/* ------------------------------------------------------------
 * Public API (same signature as the 32-bit version)
 * ------------------------------------------------------------ */
int mml_process(const char* in_text, int shift, int mode,
                char* outbuf, int outsize, MMLErrorInfo* err_info);

#ifdef __cplusplus
}
#endif

#endif /* MMLENG16_H_INCLUDED */
