# MML Transposer Win16 (mmltrw16)

This is the Win16 Unified Edition of the NSF-style MML transposition tool “MML Transposer,”  
designed to run on all versions of Windows 1.x, 2.x, and 3.x.

The application automatically adapts to the capabilities of each Win16 environment,  
providing a single executable that works across all Win16-based systems.

---

## Supported Environments

- Windows 3.x (drag & drop available)
- Windows 1.x / 2.x / 3.x (standard or custom file dialog depending on COMMDLG.DLL)
- Windows 95 / 98 Win16 subsystem

Actual hardware for Windows 1.x / 2.x / 3.x is not available, so real-machine behavior is unverified.  
Operation has been confirmed on Windows 95 (Virtual PC 2007).

---

## Drag & Drop (SHELL.DLL)

Drag & drop is available only when **SHELL.DLL** is present.  
This applies to all Windows 3.x systems.

If **Auto** is enabled, the file is saved immediately after drag & drop.

---

## File Dialog Behavior (COMMDLG.DLL)

File dialog selection depends solely on the presence of **COMMDLG.DLL**,  
not on the Windows version.

- If **COMMDLG.DLL is present**  
  → The standard Windows common file dialog is used.  
  (Windows 3.1 always includes COMMDLG.DLL.)

- If **COMMDLG.DLL is not present**  
  → A custom file dialog is used.  
  (Possible on Windows 1.x, 2.x, and some Windows 3.0 systems.)

If **Auto** is enabled, the file is saved immediately after opening.

---

## Menu Visibility (Unified Behavior)

Menu visibility is controlled by the application itself and does **not** follow  
the native UI behavior of Windows 1.x / 2.x / 3.x.

- The menu is **always hidden** when the dialog is created.
- Pressing **Alt** temporarily displays the menu.
- When the menu is closed, it is hidden again automatically.

This behavior is applied uniformly across all Win16 environments,  
including Windows 1.x and 2.x.

---

## Language Menu (English / Japanese)

When the menu is shown (via Alt), the following structure is available:

File  
- Open  
- Exit  

Language  
- English  
- Japanese  

Selecting **Japanese** enables partial Japanese localization:  
- Some UI labels become Japanese  
- Error messages are displayed in Japanese  

Selecting **English** restores the default English interface.

---

## Win1.0 / Win2.0 Fallback UI (Not Implemented)

The source code contains a commented-out mechanism intended to switch between  
Windows 1.x–style and Windows 2.x–style file dialogs depending on whether  
a COMBOBOX control can be created.

This mechanism is currently **not implemented**, because the application  
only handles `*.mml` files and does not require a file list UI.

As a result, the custom file dialog always uses a simple single-file input  
without attempting UI fallback.

---

## Main Dialog Layout

- File name display  
- Transpose amount (horizontal scrollbar / spin / edit)  
- Quick / Save / Auto  
- FMT / Rel / Abs / D-ch  

---

## Auto Save (Automatic Saving)

Win16 saves files using **8.3-style filenames**.

### Automatically generated filename

```
of_<sign><shift><mode>[d].mml
```

- `<sign>` … p (+) / m (-) / 0 (0)  
- `<shift>` … 00–12  
- `<mode>` … 0–7 (FMT / Rel / Abs combinations)  
- `[d]` … appended only when D-ch is enabled

---

## Quick / Save

- **Quick**  
  → Immediately saves using the automatic naming rule.

- **Save**  
  → Opens the “Save As” dialog.

Both buttons are available only when Auto is OFF.

---

## Notes

- The transposition logic is identical to the CUI version (mmltrd16).  
- UI structure is defined in `mmltrw16.rc`.  
- This application was developed with assistance from Copilot.  
  Unexpected issues may occur.

