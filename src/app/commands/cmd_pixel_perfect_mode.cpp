// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/params.h"
#include "app/context.h"
#include "app/pref/preferences.h"
#include "app/tools/freehand_algorithm.h"
#include "app/tools/tool.h"
#include "app/ui/context_bar.h"

namespace app {

class PixelPerfectModeCommand : public Command {
public:
  PixelPerfectModeCommand();
  Command* clone() const override { return new PixelPerfectModeCommand(*this); }

protected:
  bool onEnabled(Context* context) override;
  bool onChecked(Context* context) override;
  void onExecute(Context* context) override;
};

PixelPerfectModeCommand::PixelPerfectModeCommand()
  : Command("PixelPerfectMode",
            "Switch Pixel Perfect Mode",
            CmdUIOnlyFlag)
{
}

bool PixelPerfectModeCommand::onEnabled(Context* ctx)
{
  return true;
}

bool PixelPerfectModeCommand::onChecked(Context* ctx)
{
  tools::Tool* tool = App::instance()->activeTool();
  if (!tool)
    return false;

  auto& toolPref = Preferences::instance().tool(tool);
  return (toolPref.freehandAlgorithm() == tools::FreehandAlgorithm::PIXEL_PERFECT);
}

void PixelPerfectModeCommand::onExecute(Context* ctx)
{
  tools::Tool* tool = App::instance()->activeTool();
  if (!tool)
    return;

  auto& toolPref = Preferences::instance().tool(tool);
  const bool enable =
    toolPref.freehandAlgorithm() == tools::FreehandAlgorithm::DEFAULT;
  if (enable)
    toolPref.brush.size(1);
  toolPref.freehandAlgorithm(enable ?
    tools::FreehandAlgorithm::PIXEL_PERFECT:
    tools::FreehandAlgorithm::DEFAULT);
  App::instance()->contextBar()->updateForActiveTool();
}

Command* CommandFactory::createPixelPerfectModeCommand()
{
  return new PixelPerfectModeCommand;
}

} // namespace app
