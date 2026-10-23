// Aseprite    - Copyright (C) 2016       David Capello
// LibreSprite - Copyright (C) 2021       LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/tools/active_tool.h"

#include "app/pref/preferences.h"
#include "app/tools/active_tool_observer.h"
#include "app/tools/ink.h"
#include "app/tools/pointer.h"
#include "app/tools/tool_box.h"
#include "app/ui/color_bar.h"

namespace app {
namespace tools {

class ActiveToolChangeTrigger {
public:
  ActiveToolChangeTrigger(ActiveToolManager* manager)
    : m_manager(manager)
    , m_oldTool(manager->activeTool()) {
  }

  ~ActiveToolChangeTrigger() {
    Tool* newTool = m_manager->activeTool();
    if (m_oldTool != newTool) {
      m_manager->notifyObservers(
        &ActiveToolObserver::onActiveToolChange, newTool);
    }
  }

private:
  ActiveToolManager* m_manager;
  Tool* m_oldTool;
};

ActiveToolManager::ActiveToolManager(ToolBox* toolbox)
  : m_toolbox(toolbox)
  , m_quickTool(nullptr)
  , m_rightClick(false)
  , m_rightClickTool(nullptr)
  , m_rightClickInk(nullptr)
  , m_rightClickAssignedTool(nullptr)
  , m_proximityTool(nullptr)
  , m_selectedTool(m_toolbox->getToolById(WellKnownTools::Pencil)) // "pencil" is the active tool by default
{
}

Tool* ActiveToolManager::activeTool() const
{
  // A mouse-button assignment must win over keyboard quick tools while
  // that button is held, otherwise the assigned tool can appear selected
  // in the toolbar but the previous tool is still used on the canvas.
  if (m_rightClickTool)
    return m_rightClickTool;

  if (m_quickTool)
    return m_quickTool;

  if (m_proximityTool)
    return m_proximityTool;

  // Active tool should never returns null
  ASSERT(m_selectedTool);
  return m_selectedTool;
}

std::shared_ptr<Ink> ActiveToolManager::activeInk() const
{
  if (m_rightClickInk)
    return m_rightClickInk;

  Tool* tool = activeTool();
  // A tool explicitly assigned to the right button uses that tool's
  // primary action. Without an assignment, keep the original secondary
  // action (e.g. Pencil paints with the background color).
  std::shared_ptr<Ink> ink = tool->getInk(activeToolButton());
  if (ink->isPaint() && !ink->isEffect()) {
    tools::InkType inkType = Preferences::instance().tool(tool).ink();
    const char* id = nullptr;

    switch (inkType) {

      case tools::InkType::SIMPLE: {
        id = tools::WellKnownInks::Paint;

        ColorBar* colorbar = ColorBar::instance();
        app::Color color = (m_rightClick ? colorbar->getBgColor():
                                           colorbar->getFgColor());
        if (color.getAlpha() == 0)
          id = tools::WellKnownInks::PaintCopy;
        break;
      }

      case tools::InkType::ALPHA_COMPOSITING:
        id = tools::WellKnownInks::Paint;
        break;
      case tools::InkType::COPY_COLOR:
        id = tools::WellKnownInks::PaintCopy;
        break;
      case tools::InkType::LOCK_ALPHA:
        id = tools::WellKnownInks::PaintLockAlpha;
        break;
      case tools::InkType::SHADING:
        id = tools::WellKnownInks::Shading;
        break;
    }

    if (id)
      ink = m_toolbox->getInkById(id);
  }

  return ink;
}

int ActiveToolManager::activeToolButton() const
{
  const bool assignedRight = m_rightClick && m_rightClickAssignedTool &&
    m_rightClickTool == m_rightClickAssignedTool;
  return m_rightClick && !assignedRight ? 1 : 0;
}

Tool* ActiveToolManager::quickTool() const
{
  return m_quickTool;
}

Tool* ActiveToolManager::selectedTool() const
{
  return m_selectedTool;
}

Tool* ActiveToolManager::rightClickAssignedTool() const
{
  return m_rightClickAssignedTool;
}

void ActiveToolManager::newToolSelectedInToolBar(Tool* tool)
{
  ActiveToolChangeTrigger trigger(this);
  m_selectedTool = tool;
}

void ActiveToolManager::newQuickToolSelectedFromEditor(Tool* tool)
{
  ActiveToolChangeTrigger trigger(this);
  m_quickTool = tool;
}

void ActiveToolManager::regularTipProximity()
{
  if (m_proximityTool != nullptr) {
    ActiveToolChangeTrigger trigger(this);
    m_proximityTool = nullptr;
  }
}

void ActiveToolManager::eraserTipProximity()
{
  Tool* eraser = m_toolbox->getToolById(WellKnownTools::Eraser);
  if (m_proximityTool != eraser) {
    ActiveToolChangeTrigger trigger(this);
    m_proximityTool = eraser;
  }
}

void ActiveToolManager::pressButton(const Pointer& pointer)
{
  ActiveToolChangeTrigger trigger(this);
  Tool* tool = nullptr;
  std::shared_ptr<Ink> ink = nullptr;

  if (pointer.button() == Pointer::Right) {
    m_rightClick = true;

    if (m_rightClickAssignedTool) {
      tool = m_rightClickAssignedTool;
    }
    else if (isToolAffectedByRightClickMode(activeTool())) {
      switch (Preferences::instance().editor.rightClickMode()) {
        case app::gen::RightClickMode::PAINT_BGCOLOR:
          // Do nothing, use the active tool
          break;
        case app::gen::RightClickMode::PICK_FGCOLOR:
          tool = m_toolbox->getToolById(WellKnownTools::Eyedropper);
          ink = m_toolbox->getInkById(tools::WellKnownInks::PickFg);
          break;
        case app::gen::RightClickMode::ERASE:
          tool = m_toolbox->getToolById(WellKnownTools::Eraser);
          ink = m_toolbox->getInkById(tools::WellKnownInks::Eraser);
          break;
        case app::gen::RightClickMode::SCROLL:
          tool = m_toolbox->getToolById(WellKnownTools::Hand);
          ink = m_toolbox->getInkById(tools::WellKnownInks::Scroll);
          break;
      }
    }
  }
  else {
    m_rightClick = false;
  }

  m_rightClickTool = tool;
  m_rightClickInk = ink;
}

void ActiveToolManager::releaseButtons()
{
  ActiveToolChangeTrigger trigger(this);

  m_rightClick = false;
  m_rightClickTool = nullptr;
  m_rightClickInk = nullptr;
}

void ActiveToolManager::setSelectedTool(Tool* tool)
{
  ActiveToolChangeTrigger trigger(this);

  m_selectedTool = tool;
  notifyObservers(&ActiveToolObserver::onSelectedToolChange, tool);
}

void ActiveToolManager::toggleRightClickAssignedTool(Tool* tool)
{
  m_rightClickAssignedTool =
    (m_rightClickAssignedTool == tool ? nullptr: tool);
  notifyObservers(
    &ActiveToolObserver::onRightClickToolChange,
    m_rightClickAssignedTool);
}

// static
bool ActiveToolManager::isToolAffectedByRightClickMode(Tool* tool)
{
  bool shadingMode = (Preferences::instance().tool(tool).ink() == InkType::SHADING);
  return
    ((tool->getInk(0)->isPaint() && !shadingMode) ||
     (tool->getInk(0)->isEffect())) &&
    (!tool->getInk(0)->isEraser());
}

} // namespace tools
} // namespace app
