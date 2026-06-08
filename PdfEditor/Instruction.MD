You are a senior systems software architect and rendering-engine engineer.

Your task is to design and implement a high-performance offline PDF workspace application for Windows.

CORE PRODUCT GOALS:

* Extremely fast PDF viewing
* Smooth scrolling and zooming
* Low RAM usage
* GPU accelerated rendering
* Realtime overlay editing
* Smart dark mode rendering
* Offline-first architecture
* Stable long-runtime performance
* Native desktop performance
* 60 FPS interaction target

IMPORTANT:
This is NOT a web application.
This is NOT Electron.
This is NOT a cloud product.
This is NOT an Acrobat clone.

This is a native systems-engineering project focused on performance and responsiveness.

PRIMARY STACK:

* C++
* Qt 6
* PDFium
* DirectX/OpenGL abstraction
* FreeType if needed
* HarfBuzz only if necessary

ARCHITECTURE REQUIREMENTS:

1. PDF Rendering

* Use PDFium as rendering backend
* Implement tiled page rendering
* Render only visible page regions
* Support incremental rerendering
* Use asynchronous rendering workers
* Avoid full-page rerenders

2. Rendering Pipeline
   Implement:
   PDF → Tile Rasterization → GPU Texture Cache → Overlay Composition → Viewport Renderer

3. Threading Model
   Separate:

* UI thread
* render workers
* IO workers
* cache workers
* export workers

Never block UI thread.

4. Editing System
   Do NOT directly mutate PDF contents during editing.

Instead:

* maintain immutable source PDF
* create overlay editing layer
* composite overlays during rendering
* export merged final PDF later

5. Overlay Features
   Implement:

* text overlays
* highlights
* annotation objects
* simple editable text boxes
* draggable objects
* resizable objects

6. Dark Mode
   Implement semantic dark mode:

* darken backgrounds
* lighten text
* preserve images
* adapt vector graphics carefully
* avoid naive color inversion

7. Memory Management
   Implement:

* tile cache
* LRU cache eviction
* lazy page loading
* texture reuse
* zoom-level caching

Target low RAM usage for large PDFs.

8. Performance Constraints
   Target:

* 60 FPS viewport interaction
* instant zoom response
* minimal stutter
* asynchronous rendering
* fast startup
* low idle CPU usage

9. Export Pipeline
   Support:

* export overlays into new PDF
* preserve original PDF integrity
* incremental export architecture

10. Development Phases

PHASE 1:

* PDF viewer
* smooth scrolling
* zoom
* tile renderer
* dark mode
* cache system

PHASE 2:

* overlay editing
* annotations
* text boxes

PHASE 3:

* export engine

PHASE 4:

* smart text editing if architecture permits

STRICT RULES:

* prioritize performance over feature count
* avoid architectural overengineering
* keep modules isolated
* use profiling early
* optimize rendering before adding features
* avoid synchronous disk IO on UI thread
* avoid unnecessary abstractions
* keep renderer modular

DELIVERABLES REQUIRED:

* system architecture
* module breakdown
* threading model
* rendering pipeline
* memory strategy
* cache strategy
* class structure
* development roadmap
* risk analysis
* performance bottlenecks
* recommended libraries
* MVP implementation plan
* export architecture
* dark mode algorithm strategy
* testing strategy
* future scalability analysis
