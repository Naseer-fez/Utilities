# Phase 3 Handoff: Export Engine

Since Phase 2 relies on an entirely non-destructive architecture (overlays float above the PDF without altering the source file), Phase 3 is responsible for merging these two worlds together to produce a final, portable PDF file.

## Primary Goals
1. **Merge Overlays with Source**: Read the immutable source PDF and programmatically write the data from the OverlayManager (text boxes, highlights) back into the PDF format using PDFium's editing APIs.
2. **Zero Loss of Integrity**: The original PDF structure (forms, metadata, digital signatures, bookmarks) must be preserved as much as possible when saving the new file.
3. **Non-Destructive Saving**: Always export to a new file path or append incrementally. Never overwrite the original file in-place during an active editing session.

## Technical Objectives & Challenges
- **PDFium Write APIs**: You will need to utilize PDFium APIs such as `FPDFPage_InsertObject`, `FPDFPageObj_CreateNewTextObj`, and `FPDFPageObj_CreateNewRect`.
- **Coordinate Translation**: The most significant challenge will be translating the relative overlay coordinates (from Phase 2's QML UI) back into the exact PDF Point coordinate system (which is mathematically bottom-left origin, unlike screen coordinates which are top-left).
- **Font Embedding**: For text overlays, the export engine must correctly embed or subset fonts into the PDF to ensure the exported text looks identical on other machines.

---

## 🛑 The Golden Rules (LLM Guidelines)

### Roles & Responsibilities

#### 1. The Export Engine (C++)
**Role**: To serialize in-memory `OverlayModel` data into raw PDF objects using PDFium and generate a new physical file.

**THE DOs**
- **DO** translate coordinates carefully. Remember that QML/UI uses a Top-Left origin, while the PDF coordinate system traditionally uses a Bottom-Left origin.
- **DO** use a separate PDFium save API (like `FPDF_SaveAsCopy` or `FPDF_SaveWithVersion`) to write out the modified document to a new file stream.
- **DO** properly manage memory for PDFium objects. Ensure that objects like `FPDF_PAGEOBJECT` are properly inserted into the page or destroyed to prevent memory leaks in the C++ layer.
- **DO** load and embed standard fonts (e.g., Arial, Helvetica) when creating new text objects so they render correctly on any machine.

**THE DON'Ts**
- **DON'T** overwrite the active source PDF file while it is currently loaded and being viewed. Always export to a target output path.
- **DON'T** rasterize the overlays into a flat image and paste it over the page. Text must remain selectable vector text (`FPDFPageObj_CreateNewTextObj`), and highlights must remain vector paths with correct blend modes or opacity.
- **DON'T** pollute the `PdfCore` reading logic from Phase 1 and 2. Keep the export logic encapsulated in a separate, dedicated module (e.g., `PdfExporter`).
