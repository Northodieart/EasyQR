#pragma once

enum class ScanMode { Fullscreen, Region };

// Captures the desktop, hides the main window, and starts the interactive overlay.
void StartScan(ScanMode mode);

// Used when the process is shutting down while the overlay is open.
void DestroyOverlayForExit();
