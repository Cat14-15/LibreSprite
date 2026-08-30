// LibreSprite - multiline script editor used by the scripting workspace.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/script_editor.h"

#include "app/ui/skin/skin_theme.h"
#include "base/string.h"
#include "clip/clip.h"
#include "she/font.h"
#include "ui/graphics.h"
#include "ui/message.h"
#include "ui/paint_event.h"
#include "ui/size_hint_event.h"
#include "ui/system.h"
#include "ui/view.h"

#include <algorithm>

namespace app {

using namespace ui;

namespace {
constexpr int kPadding = 4;
constexpr int kGutterGap = 8;

bool continuation(unsigned char value) {
  return (value & 0xc0) == 0x80;
}

}

int ScriptEditor::lineCount() const {
  return 1+int(std::count(m_source.begin(), m_source.end(), '\n'));
}

int ScriptEditor::gutterWidth() const {
  const std::string largest = std::to_string(std::max(1, lineCount()));
  return font()->textLength(largest)+2*kPadding+kGutterGap;
}

ScriptEditor::ScriptEditor()
  : Widget(kGenericWidget) {
  setFocusStop(true);
  setFocusMagnet(true);
  setExpansive(true);
  initTheme();
}

void ScriptEditor::setSource(const std::string& source,
                             bool resetHistory,
                             bool markModified) {
  m_source = source;
  m_caret = m_anchor = std::min(m_caret, m_source.size());
  m_modified = markModified;
  if (resetHistory) {
    m_undo.clear();
    m_redo.clear();
  }
  if (auto* view = View::getView(this))
    view->updateView();
  invalidate();
  if (markModified)
    Change();
}

void ScriptEditor::markSaved() {
  m_modified = false;
  invalidate();
}

std::size_t ScriptEditor::previousCharacter(std::size_t position) const {
  if (!position) return 0;
  --position;
  while (position && continuation(static_cast<unsigned char>(m_source[position])))
    --position;
  return position;
}

std::size_t ScriptEditor::nextCharacter(std::size_t position) const {
  if (position >= m_source.size()) return m_source.size();
  ++position;
  while (position < m_source.size() &&
         continuation(static_cast<unsigned char>(m_source[position])))
    ++position;
  return position;
}

std::size_t ScriptEditor::lineStart(std::size_t position) const {
  if (!position) return 0;
  const auto found = m_source.rfind('\n', position-1);
  return found == std::string::npos ? 0 : found+1;
}

std::size_t ScriptEditor::lineEnd(std::size_t position) const {
  const auto found = m_source.find('\n', position);
  return found == std::string::npos ? m_source.size() : found;
}

std::size_t ScriptEditor::verticalMove(std::size_t position, int direction) const {
  const std::size_t start = lineStart(position);
  const std::size_t column = position-start;
  if (direction < 0) {
    if (!start) return position;
    const std::size_t previousEnd = start-1;
    const std::size_t previousStart = lineStart(previousEnd);
    return std::min(previousStart+column, previousEnd);
  }
  const std::size_t end = lineEnd(position);
  if (end >= m_source.size()) return position;
  const std::size_t nextStart = end+1;
  return std::min(nextStart+column, lineEnd(nextStart));
}

std::pair<std::size_t, std::size_t> ScriptEditor::selection() const {
  return {std::min(m_caret, m_anchor), std::max(m_caret, m_anchor)};
}

bool ScriptEditor::hasSelection() const {
  return m_caret != m_anchor;
}

void ScriptEditor::pushUndo() {
  if (m_undo.empty() || m_undo.back() != m_source)
    m_undo.push_back(m_source);
  if (m_undo.size() > 100)
    m_undo.erase(m_undo.begin());
  m_redo.clear();
}

void ScriptEditor::changed() {
  m_modified = true;
  Change();
  if (auto* view = View::getView(this))
    view->updateView();
  ensureCaretVisible();
  invalidate();
}

void ScriptEditor::replaceSelection(const std::string& text) {
  pushUndo();
  const auto selected = selection();
  m_source.replace(selected.first, selected.second-selected.first, text);
  m_caret = m_anchor = selected.first+text.size();
  changed();
}

void ScriptEditor::eraseSelection() {
  if (hasSelection())
    replaceSelection("");
}

void ScriptEditor::copySelection() {
  if (!hasSelection()) return;
  const auto selected = selection();
  clip::set_text(m_source.substr(selected.first,
                                 selected.second-selected.first));
}

void ScriptEditor::undo() {
  if (m_undo.empty()) return;
  m_redo.push_back(m_source);
  m_source = m_undo.back();
  m_undo.pop_back();
  m_caret = m_anchor = std::min(m_caret, m_source.size());
  changed();
}

void ScriptEditor::redo() {
  if (m_redo.empty()) return;
  m_undo.push_back(m_source);
  m_source = m_redo.back();
  m_redo.pop_back();
  m_caret = m_anchor = std::min(m_caret, m_source.size());
  changed();
}

void ScriptEditor::moveCaret(std::size_t position, bool selecting) {
  m_caret = std::min(position, m_source.size());
  if (!selecting)
    m_anchor = m_caret;
  ensureCaretVisible();
  invalidate();
}

std::size_t ScriptEditor::positionFromPoint(const gfx::Point& point) const {
  const int lineHeight = std::max(1, font()->height());
  int requestedLine = std::max(0, (point.y-clientBounds().y-kPadding)/lineHeight);
  std::size_t start = 0;
  while (requestedLine-- > 0) {
    const auto end = m_source.find('\n', start);
    if (end == std::string::npos) return m_source.size();
    start = end+1;
  }
  const std::size_t end = lineEnd(start);
  const int requestedX = std::max(
    0, point.x-clientBounds().x-gutterWidth()-kPadding);
  std::size_t position = start;
  while (position < end) {
    const std::size_t next = nextCharacter(position);
    const int before = font()->textLength(m_source.substr(start, position-start));
    const int after = font()->textLength(m_source.substr(start, next-start));
    if (requestedX < (before+after)/2)
      break;
    position = next;
  }
  return position;
}

void ScriptEditor::ensureCaretVisible() {
  auto* view = View::getView(this);
  if (!view) return;
  const std::size_t start = lineStart(m_caret);
  int row = int(std::count(m_source.begin(), m_source.begin()+m_caret, '\n'));
  const int x = gutterWidth()+kPadding+
    font()->textLength(m_source.substr(start, m_caret-start));
  const int y = kPadding+row*font()->height();
  gfx::Point scroll = view->viewScroll();
  const gfx::Rect viewport = view->viewportBounds();
  if (x < scroll.x) scroll.x = x;
  else if (x > scroll.x+viewport.w-12) scroll.x = x-viewport.w+12;
  if (y < scroll.y) scroll.y = y;
  else if (y+font()->height() > scroll.y+viewport.h)
    scroll.y = y+font()->height()-viewport.h;
  view->setViewScroll(scroll);
}

bool ScriptEditor::onProcessMessage(Message* msg) {
  // Key-up is broadcast to the whole window by the legacy UI manager. Once
  // this editor owns keyboard focus, consume it too so a toolbar button that
  // was used previously cannot interpret the release as its own click.
  if (msg->type() == kKeyUpMessage && hasFocus())
    return true;

  if (msg->type() == kMouseWheelMessage) {
    if (auto* view = View::getView(this)) {
      gfx::Point scroll = view->viewScroll();
      scroll += static_cast<MouseMessage*>(msg)->wheelDelta()*font()->height()*3;
      view->setViewScroll(scroll);
      return true;
    }
  }

  if (msg->type() == kMouseDownMessage) {
    requestFocus();
    auto* mouse = static_cast<MouseMessage*>(msg);
    moveCaret(positionFromPoint(mouse->position()), msg->shiftPressed());
    captureMouse();
    return true;
  }

  if (msg->type() == kMouseMoveMessage && hasCapture()) {
    moveCaret(positionFromPoint(static_cast<MouseMessage*>(msg)->position()), true);
    return true;
  }

  if (msg->type() == kMouseUpMessage && hasCapture()) {
    releaseMouse();
    return true;
  }

  if (msg->type() == kKeyDownMessage && hasFocus()) {
    auto* key = static_cast<KeyMessage*>(msg);
    const bool selecting = msg->shiftPressed();
    if (msg->ctrlPressed() && !msg->altPressed() && !msg->cmdPressed()) {
      switch (key->scancode()) {
        case kKeyA: m_anchor = 0; m_caret = m_source.size(); invalidate(); return true;
        case kKeyC: copySelection(); return true;
        case kKeyX: copySelection(); eraseSelection(); return true;
        case kKeyV: { std::string text; if (clip::get_text(text)) replaceSelection(text); return true; }
        case kKeyZ: if (msg->shiftPressed()) redo(); else undo(); return true;
        case kKeyY: redo(); return true;
        case kKeyS: SaveRequested(); return true;
        case kKeyO: OpenRequested(); return true;
        case kKeyEnter:
        case kKeyEnterPad: RunRequested(); return true;
        default: break;
      }
    }

    switch (key->scancode()) {
      case kKeyF5: RunRequested(); return true;
      case kKeyLeft:
        moveCaret(hasSelection() && !selecting ? selection().first:
                  previousCharacter(m_caret), selecting); return true;
      case kKeyRight:
        moveCaret(hasSelection() && !selecting ? selection().second:
                  nextCharacter(m_caret), selecting); return true;
      case kKeyUp: moveCaret(verticalMove(m_caret, -1), selecting); return true;
      case kKeyDown: moveCaret(verticalMove(m_caret, 1), selecting); return true;
      case kKeyHome: moveCaret(lineStart(m_caret), selecting); return true;
      case kKeyEnd: moveCaret(lineEnd(m_caret), selecting); return true;
      case kKeyEnter:
      case kKeyEnterPad: replaceSelection("\n"); return true;
      case kKeyTab: replaceSelection("  "); return true;
      case kKeyBackspace:
        if (hasSelection()) eraseSelection();
        else if (m_caret) { m_anchor = previousCharacter(m_caret); eraseSelection(); }
        return true;
      case kKeyDel:
        if (hasSelection()) eraseSelection();
        else if (m_caret < m_source.size()) { m_anchor = nextCharacter(m_caret); eraseSelection(); }
        return true;
      default:
        if (key->unicodeChar() >= 32 &&
            !(msg->modifiers() & (kKeyCtrlModifier | kKeyAltModifier))) {
          std::wstring value(1, static_cast<wchar_t>(key->unicodeChar()));
          replaceSelection(base::to_utf8(value));
          return true;
        }
        // Modifier combinations which are not editor commands must not leak
        // into canvas tools, panel toggles, or button mnemonics.
        return true;
    }
  }
  return Widget::onProcessMessage(msg);
}

void ScriptEditor::onPaint(PaintEvent& ev) {
  auto* skin = static_cast<skin::SkinTheme*>(theme());
  auto* graphics = ev.graphics();
  const gfx::Rect bounds = clientBounds();
  graphics->fillRect(skin->colors.background(), bounds);
  skin->drawRect(graphics, bounds,
                 (hasFocus() ? skin->parts.sunken2Focused().get():
                               skin->parts.sunken2Normal().get()),
                 skin->colors.background());

  const int gutter = gutterWidth();
  graphics->fillRect(skin->colors.face(),
                     gfx::Rect(bounds.x, bounds.y, gutter, bounds.h));
  graphics->drawVLine(skin->colors.disabled(),
                      bounds.x+gutter-1, bounds.y, bounds.h);

  const auto selected = selection();
  std::size_t start = 0;
  int row = 0;
  while (start <= m_source.size()) {
    const std::size_t end = lineEnd(start);
    const int y = bounds.y+kPadding+row*font()->height();
    const std::string number = std::to_string(row+1);
    const int numberX = bounds.x+gutter-kGutterGap-
                        font()->textLength(number);
    graphics->drawString(number, skin->colors.disabled(), gfx::ColorNone,
                         gfx::Point(numberX, y));
    if (selected.first < end && selected.second > start) {
      const std::size_t left = std::max(start, selected.first);
      const std::size_t right = std::min(end, selected.second);
      const int x1 = bounds.x+gutter+kPadding+
        font()->textLength(m_source.substr(start, left-start));
      const int x2 = bounds.x+gutter+kPadding+
        font()->textLength(m_source.substr(start, right-start));
      graphics->fillRect(skin->colors.selected(),
                         gfx::Rect(x1, y, std::max(1, x2-x1), font()->height()));
    }
    graphics->drawString(m_source.substr(start, end-start),
                         skin->colors.text(), gfx::ColorNone,
                         gfx::Point(bounds.x+gutter+kPadding, y));
    if (end == m_source.size()) break;
    start = end+1;
    ++row;
  }

  if (hasFocus()) {
    const std::size_t startOfLine = lineStart(m_caret);
    const int caretRow = int(std::count(m_source.begin(),
                                        m_source.begin()+m_caret, '\n'));
    const int x = bounds.x+gutter+kPadding+
      font()->textLength(m_source.substr(startOfLine, m_caret-startOfLine));
    const int y = bounds.y+kPadding+caretRow*font()->height();
    graphics->drawVLine(skin->colors.text(), x, y, font()->height());
  }
}

void ScriptEditor::onSizeHint(SizeHintEvent& ev) {
  int width = 600*guiscale();
  int lines = 1;
  std::size_t start = 0;
  while (start <= m_source.size()) {
    const std::size_t end = lineEnd(start);
    width = std::max(width, gutterWidth()+
                            font()->textLength(m_source.substr(start, end-start))+
                            2*kPadding);
    ++lines;
    if (end == m_source.size()) break;
    start = end+1;
  }
  ev.setSizeHint(gfx::Size(width, std::max(260*guiscale(),
                                          lines*font()->height()+2*kPadding)));
}

} // namespace app
