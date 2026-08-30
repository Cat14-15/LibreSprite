// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#include "app/cmd/set_cel_position.h"
#include "app/document.h"
#include "app/script/app_scripting.h"
#include "app/transaction.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "script/script_object.h"
#include "script/engine.h"

class CelScriptObject : public script::ScriptObject {
public:
  CelScriptObject() {
    addProperty("x",
                [this]{return cel()->x();},
                [this](int x){setPosition(x, cel()->y()); return x;});
    addProperty("y",
                [this]{return cel()->y();},
                [this](int y){setPosition(cel()->x(), y); return y;});
    addProperty("image", [this]{return getEngine()->getScriptObject(cel()->image());});
    addProperty("frame", [this]{return cel()->frame();});

    addFunction("setPosition", [this](int x, int y){
      setPosition(x, y);
      return this;
    });
  }

  doc::Cel* cel() {
    auto cel = handle<doc::Object, doc::Cel>();
    if (!cel)
      throw script::ObjectDestroyedException{};
    return cel;
  }

  void setPosition(int x, int y) {
    auto value = cel()->layer()->cel(cel()->frame());
    auto* document = static_cast<app::Document*>(
      cel()->layer()->sprite()->document());
    app::AppScripting::transaction(document).execute(
      new app::cmd::SetCelPosition(value, x, y));
  }
};

static script::ScriptObject::Regular<CelScriptObject> celSO(typeid(doc::Cel*).name());
