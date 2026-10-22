// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/cmd/set_cel_z_index.h"

#include "app/document.h"
#include "doc/cel.h"
#include "doc/document_event.h"

namespace app {
namespace cmd {

using namespace doc;

SetCelZIndex::SetCelZIndex(std::shared_ptr<Cel> cel, int zIndex)
  : WithCel(cel)
  , m_oldZIndex(cel->zIndex())
  , m_newZIndex(zIndex)
{
}

void SetCelZIndex::onExecute()
{
  cel()->setZIndex(m_newZIndex);
  cel()->incrementVersion();
}

void SetCelZIndex::onUndo()
{
  cel()->setZIndex(m_oldZIndex);
  cel()->incrementVersion();
}

void SetCelZIndex::onFireNotifications()
{
  static_cast<app::Document*>(cel()->document())->notifyGeneralUpdate();
}

} // namespace cmd
} // namespace app
