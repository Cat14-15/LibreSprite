// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#include "app/cmd/set_layer_blend_mode.h"
#include "app/cmd/set_layer_flags.h"
#include "app/cmd/set_layer_name.h"
#include "app/cmd/set_layer_opacity.h"
#include "app/document.h"
#include "app/script/app_scripting.h"
#include "app/transaction.h"
#include "script/script_object.h"
#include "doc/layer.h"
#include "doc/cel.h"
#include "script/engine.h"
#include <algorithm>

class LayerScriptObject : public script::ScriptObject {
public:
  LayerScriptObject() {
    addProperty("name",
                [this]{return layer()->name();},
                [this](const std::string& name){
                  transaction().execute(new app::cmd::SetLayerName(layer(), name));
                  return name;
                })
      .doc("read+write. The name of the layer.");

    addProperty("isImage", [this]{return layer()->isImage();})
      .doc("read-only. Returns true if the layer is an image, false if it is a folder.");

    addProperty("isBackground", [this]{return layer()->isBackground();})
      .doc("read-only. Returns true if the layer is a background layer.");

    addProperty("isTransparent", [this]{return layer()->isTransparent();})
      .doc("read-only. Returns true if the layer is a non-background image layer.");

    addProperty("isVisible",
                [this]{return layer()->isVisible();},
                [this](bool i){
                  setFlag(doc::LayerFlags::Visible, i);
                  return i;
                })
      .doc("read+write. Gets/sets whether the layer is visible or not.");

    addProperty("isEditable",
                [this]{return layer()->isEditable();},
                [this](bool i){
                  setFlag(doc::LayerFlags::Editable, i);
                  return i;
                })
      .doc("read+write. Gets/sets whether the layer is editable (unlocked) or not (locked).");

    addProperty("isMovable", [this]{return layer()->isMovable();})
      .doc("read-only. Returns true if the layer is movable.");

    addProperty("isContinuous", [this]{return layer()->isContinuous();})
      .doc("read-only. Prefer to link cels when the user copies them.");

    addProperty("flags", [this]{return (int) layer()->flags();})
      .doc("read-only. Returns all flags OR'd together as an int");

    addProperty("celCount", [this]{
      return layer()->isImage() ? static_cast<doc::LayerImage*>(layer())->getCelsCount() : 0;
    }).doc("read-only. Returns the number of cels.");

    addProperty("opacity",
                [this]{return layer()->isImage() ? static_cast<doc::LayerImage*>(layer())->opacity() : 255;},
                [this](int value){
                  value = std::max(0, std::min(value, 255));
                  if (layer()->isImage())
                    transaction().execute(new app::cmd::SetLayerOpacity(
                      static_cast<doc::LayerImage*>(layer()), value));
                  return value;
                })
      .doc("read+write. Image layer opacity from 0 to 255.");

    addProperty("blendMode",
                [this]{return layer()->isImage() ? int(static_cast<doc::LayerImage*>(layer())->blendMode()) : 0;},
                [this](int value){
                  value = std::max(0, std::min(value, 15));
                  if (layer()->isImage())
                    transaction().execute(new app::cmd::SetLayerBlendMode(
                      static_cast<doc::LayerImage*>(layer()), doc::BlendMode(value)));
                  return value;
                })
      .doc("read+write. Image layer blend mode enum value.");

    addFunction("cel", [this](int i){
      return getEngine()->getScriptObject(layer()->cel(i).get());
    }).doc("retrieves a Cel")
      .docArg("index", "The number of the Cel")
      .docReturns("A Cel object or null if an invalid index is passed");
  }

  doc::Layer* layer() {
    auto layer = handle<doc::Object, doc::Layer>();
    if (!layer)
      throw script::ObjectDestroyedException{};
    return layer;
  }

  app::Transaction& transaction() {
    return app::AppScripting::transaction(
      static_cast<app::Document*>(layer()->sprite()->document()));
  }

  void setFlag(doc::LayerFlags flag, bool enabled) {
    int flags = int(layer()->flags());
    if (enabled) flags |= int(flag);
    else flags &= ~int(flag);
    transaction().execute(new app::cmd::SetLayerFlags(
      layer(), doc::LayerFlags(flags)));
  }
};

static script::ScriptObject::Regular<LayerScriptObject> layerSO(typeid(doc::Layer*).name());
