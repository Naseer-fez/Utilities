# Phase 4 Handoff: Smart Text Editing

Phase 4 is the most ambitious. It moves beyond simple overlays and attempts to manipulate the actual text embedded within the original PDF.

> [!WARNING]
> PDF is fundamentally a presentation format, not a word processor format. Text is often stored as disconnected character glyphs rather than continuous paragraphs.

## Primary Goals
1. **Text Extraction & Selection**: Accurately extract text blocks and allow the user to highlight, copy, and select existing text on the page.
2. **In-Place Text Modification**: (If architecture permits) Allow the user to click on existing PDF text, modify the characters, and have the rendering engine update in real-time.

## Technical Objectives & Challenges
- **PDFium Text APIs**: Utilize `FPDFText_LoadPage`, `FPDFText_GetText`, and `FPDFText_GetRect` to map screen coordinates to underlying text objects.
- **The "Reflow" Problem**: If a user adds a word to an existing PDF sentence, the text will not naturally wrap to the next line. You will need to decide whether to implement a complex reflow algorithm (highly difficult) or restrict smart editing to simple character replacement (e.g., fixing a typo).
- **Performance Hit**: Extracting text data and building a searchable, selectable text layer over the entire document consumes significant CPU and memory. This must be done lazily (only for visible pages) to maintain the 60 FPS target.

---

## 🛑 The Golden Rules (LLM Guidelines)

### Roles & Responsibilities

#### The Text Selection & Editing Engine
**Role**: To parse native PDF text, provide a selection layer for the UI, and handle native text replacement operations safely.

**THE DOs**
- **DO** load text data lazily. Only extract text using `FPDFText_LoadPage` for pages that are currently in view or actively being interacted with.
- **DO** use `FPDFText_GetRect` to map the bounding boxes of characters to the screen so QML can render native-looking text selection highlights.
- **DO** handle coordinate conversions precisely between PDF text bounds and QML screen space when capturing mouse drag events for selection.

**THE DON'Ts**
- **DON'T** attempt to implement a full word processor text reflow system across paragraphs unless strictly requested. Stick to simple inline text replacement to avoid complex line-wrapping math and layout corruption.
- **DON'T** parse text for the entire document upfront. Doing so will freeze the application on large documents (e.g., 1000-page textbooks) and ruin performance.
- **DON'T** conflate native text editing with the Phase 2 Overlay system. Phase 2 floats *new* content above the document; Phase 4 manipulates the *existing* source text.
