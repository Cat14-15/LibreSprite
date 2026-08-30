// LibreSprite - multiline script editor used by the scripting workspace.
#pragma once

#include "base/signal.h"
#include "ui/widget.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace app {

class ScriptEditor : public ui::Widget {
public:
  ScriptEditor();

  const std::string& source() const { return m_source; }
  void setSource(const std::string& source,
                 bool resetHistory = true,
                 bool markModified = false);
  bool modified() const { return m_modified; }
  void markSaved();

  base::Signal0<void> Change;
  base::Signal0<void> RunRequested;
  base::Signal0<void> SaveRequested;
  base::Signal0<void> OpenRequested;

protected:
  bool onProcessMessage(ui::Message* msg) override;
  void onPaint(ui::PaintEvent& ev) override;
  void onSizeHint(ui::SizeHintEvent& ev) override;

private:
  std::size_t previousCharacter(std::size_t position) const;
  std::size_t nextCharacter(std::size_t position) const;
  std::size_t lineStart(std::size_t position) const;
  std::size_t lineEnd(std::size_t position) const;
  std::size_t verticalMove(std::size_t position, int direction) const;
  std::size_t positionFromPoint(const gfx::Point& point) const;
  int gutterWidth() const;
  int lineCount() const;
  void moveCaret(std::size_t position, bool selecting);
  void replaceSelection(const std::string& text);
  void eraseSelection();
  bool hasSelection() const;
  std::pair<std::size_t, std::size_t> selection() const;
  void copySelection();
  void pushUndo();
  void undo();
  void redo();
  void changed();
  void ensureCaretVisible();

  std::string m_source;
  std::size_t m_caret = 0;
  std::size_t m_anchor = 0;
  std::vector<std::string> m_undo;
  std::vector<std::string> m_redo;
  bool m_modified = false;
};

} // namespace app
