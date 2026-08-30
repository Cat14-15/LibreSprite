// Aseprite
// Copyright (C) 2001-2016  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#pragma once

#include "app/color.h"
#include "app/tools/active_tool_observer.h"
#include "base/connection.h"
#include "base/time.h"
#include "doc/context_observer.h"
#include "doc/document_observer.h"
#include "doc/documents_observer.h"
#include "doc/layer_index.h"
#include "ui/base.h"
#include "ui/box.h"

#include <string>
#include <vector>

namespace ui {
  class Box;
  class Button;
  class Entry;
  class Label;
  class Window;
}

namespace render {
  class Zoom;
}

namespace app {
  class ButtonSet;
  class CommandExecutionEvent;
  class Editor;
  class ZoomEntry;

  namespace tools {
    class Tool;
  }

  class StatusBar : public ui::HBox
                  , public doc::ContextObserver
                  , public doc::DocumentsObserver
                  , public doc::DocumentObserver
                  , public tools::ActiveToolObserver {
    static StatusBar* m_instance;
  public:
    static StatusBar* instance() { return m_instance; }

    StatusBar();
    ~StatusBar();

    void clearText();

    bool setStatusText(int msecs, const char *format, ...);
    void showTip(int msecs, const char *format, ...);
    void showColor(int msecs, const char* text, const Color& color);
    void showTool(int msecs, tools::Tool* tool);

    // Used by AppEditor to update the zoom level in the status bar.
    void updateFromEditor(Editor* editor);
    void updateWorkspaceButtons();

  protected:
    void onResize(ui::ResizeEvent& ev) override;

    // ContextObserver impl
    void onActiveSiteChange(const doc::Site& site) override;

    // DocumentObservers impl
    void onRemoveDocument(doc::Document* doc) override;

    // DocumentObserver impl
    void onPixelFormatChanged(DocumentEvent& ev) override;

    // ActiveToolObserver impl
    void onSelectedToolChange(tools::Tool* tool) override;

  private:
    void onCelOpacitySliderChange();
    void newFrame();
    void toggleGrid();
    void togglePixelGrid();
    void showCheckedBgMenu();
    void setCheckedBgSize(int type);
    void toggleCheckedBgCentered();
    void togglePalette();
    void toggleTimeline();
    void onChangeZoom(const render::Zoom& zoom);
    void onAfterCommandExecution(CommandExecutionEvent& ev);

    base::tick_t m_timeout;

    // Indicators
    class Indicators;
    class IndicatorsGeneration;
    Indicators* m_indicators;

    // Box of main commands
    ui::Widget* m_docControls;
    ui::Widget* m_workspaceControls;
    ui::Button* m_gridSettings;
    ui::Button* m_pixelGridToggle;
    ui::Button* m_checkedBgButton;
    ui::Button* m_paletteToggle;
    ui::Button* m_timelineToggle;
    ui::Label* m_frameLabel;
    ui::Entry* m_currentFrame;        // Current frame and go to frame entry
    ui::Button* m_newFrame;           // Button to create a new frame
    ZoomEntry* m_zoomEntry;
    doc::Document* m_doc;      // Document used to show the cel slider
    base::ScopedConnection m_afterCmdConn;

    // Tip window
    class CustomizedTipWindow;
    CustomizedTipWindow* m_tipwindow;

  };

} // namespace app
