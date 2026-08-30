// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#include "app/modules/palettes.h"
#include "app/document.h"
#include "app/document_api.h"
#include "app/script/app_scripting.h"
#include "app/ui_context.h"
#include "doc/object.h"
#include "doc/palette.h"
#include "doc/image.h"
#include "doc/sprite.h"
#include "ui/manager.h"
#include "script/script_object.h"
#include "script/engine.h"

class PaletteScriptObject : public script::ScriptObject {
public:
  PaletteScriptObject() {
    addProperty("length",
                [this]{
                  if (auto pal = palette())
                    return pal->size();
                  return 0;
                },
                [this](int s){
                  if (auto pal = palette()) {
                    prepareMutation();
                    pal->resize(s);
                    modify();
                  }
                  return s;
                });

    addFunction("get", [this](int i){
      if (auto pal = palette())
        return pal->getEntry(i);
      return doc::color_t{};
    });

    addMethod("set", &PaletteScriptObject::set);

    addMethod("setColors", &PaletteScriptObject::setColors)
      .doc("Sets consecutive palette entries from color arguments.")
      .docArg("startIndex", "First palette index.")
      .docArg("colors", "One or more packed colors.");
  }

  doc::Palette* palette() {
    auto pal = handle<doc::Object, doc::Palette>();
    if (!pal)
      throw script::ObjectDestroyedException{};
    return pal;
  }

  void modify() {
    needIncrement = true;
  }

  void set(int i){
        auto pal = palette();
    if (!pal)
      return;
    if (i >= pal->size())
      return;
    auto& args = script::Function::varArgs();
    int c;
    if (args.size() == 2) {
      c = args[1];
    } else if(args.size() == 4) {
      c = doc::rgba(args[1], args[2], args[3], 0xFF);
    } else if(args.size() == 5) {
      c = doc::rgba(args[1], args[2], args[3], args[4]);
    } else
      return;
    prepareMutation();
    pal->setEntry(i, c);
    modify();
  }

  void setColors(int startIndex) {
    auto pal = palette();
    auto& args = script::Function::varArgs();
    if (!pal || startIndex < 0 || args.size() < 2)
      return;
    prepareMutation();
    const int colorCount = int(args.size())-1;
    if (startIndex+colorCount > pal->size())
      pal->resize(startIndex+colorCount);
    for (int i=0; i<colorCount; ++i)
      pal->setEntry(startIndex+i, int(args[i+1]));
    modify();
  }

  void prepareMutation() {
    if (m_before)
      return;
    m_before = palette()->clone();
    m_document = app::UIContext::instance()->activeDocument();
    if (m_document && m_document->sprite()->palette(0) != palette())
      m_document = nullptr;

    getEngine()->afterEval([this](bool success) {
      auto current = palette()->clone();
      m_before->copyColorsTo(*palette());
      if (success) {
        if (m_document) {
          if (m_before->countDiff(*current, nullptr, nullptr) > 0) {
            app::DocumentApi(m_document,
                             app::AppScripting::transaction(m_document))
              .setPalette(m_document->sprite(), 0, current.get());
          }
        }
        else {
          current->copyColorsTo(*palette());
          palette()->incrementVersion();
        }
      }
      palette()->incrementVersion();
      app::set_current_palette(palette(), true);
      if (ui::Manager::getDefault())
        ui::Manager::getDefault()->invalidate();
      m_before.reset();
      m_document = nullptr;
      needIncrement = false;
    });
  }

  bool needIncrement = false;
  std::shared_ptr<doc::Palette> m_before;
  app::Document* m_document = nullptr;
};

static script::ScriptObject::Regular<PaletteScriptObject> palSO(typeid(doc::Palette*).name());
