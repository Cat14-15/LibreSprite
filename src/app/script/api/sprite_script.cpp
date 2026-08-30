// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#include "app/cmd/set_sprite_size.h"
#include "app/cmd/add_frame_tag.h"
#include "app/cmd/remove_frame_tag.h"
#include "app/commands/commands.h"
#include "app/document.h"
#include "app/document_api.h"
#include "app/file/palette_file.h"
#include "app/script/app_scripting.h"
#include "app/transaction.h"
#include "app/ui_context.h"
#include "doc/document_observer.h"
#include "doc/frame_tag.h"
#include "doc/frame_tags.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/layers_range.h"
#include "doc/mask.h"
#include "doc/palette.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "script/engine.h"
#include "script/script_object.h"
#include <memory>

class SpriteScriptObject : public script::ScriptObject {
public:
  SpriteScriptObject() {
    addProperty("layerCount", [this]{return (int) sprite()->countLayers();})
      .doc("read-only. Returns the amount of layers in the sprite.");

    addProperty("frameCount", [this]{return int(sprite()->totalFrames());})
      .doc("read-only. Returns the amount of animation frames.");

    addProperty("tagCount", [this]{return int(sprite()->frameTags().size());})
      .doc("read-only. Returns the amount of animation tags.");

    addProperty("filename", [this]{return doc()->filename();})
      .doc("read-only. Returns the file name of the sprite.");

    addProperty("width",
                [this]{return sprite()->width();},
                [this](int width){
                  transaction().execute(new app::cmd::SetSpriteSize(sprite(), width, sprite()->height()));
                  return 0;
                })
      .doc("read+write. Returns and sets the width of the sprite.");

    addProperty("height",
                [this]{return sprite()->height();},
                [this](int height){
                  transaction().execute(new app::cmd::SetSpriteSize(sprite(), sprite()->width(), height));
                  return 0;
                })
      .doc("read+write. Returns and sets the height of the sprite.");

    addProperty("colorMode", [this]{ return sprite()->pixelFormat();})
      .doc("read-only. Returns the sprite's ColorMode.");

    addProperty("selection", [this]{ return this; })
      .doc("Placeholder. Do not use.");

    addProperty("palette", [this]{
      return getEngine()->getScriptObject(sprite()->palette(0));
    }).doc("read-only. Returns the sprite's palette.");

    addMethod("layer", &SpriteScriptObject::layer)
      .doc("allows you to access a given layer.")
      .docArg("layerNumber", "The number of they layer, starting with zero from the bottom.")
      .docReturns("a Layer object or null if invalid.");

    addMethod("addLayer", &SpriteScriptObject::addLayer)
      .doc("Adds an image layer with a blank cel in frame zero.")
      .docArg("name", "The new layer name.")
      .docReturns("The new Layer object.");

    addMethod("addFrame", &SpriteScriptObject::addFrame)
      .doc("Adds an animation frame.")
      .docArg("copyPrevious", "True copies the previous frame; false creates an empty frame.")
      .docReturns("The zero-based index of the new frame.")
      .setDefault(true);

    addMethod("ensureCel", &SpriteScriptObject::ensureCel)
      .doc("Returns a cel, creating a blank full-canvas cel when missing.")
      .docArg("layerNumber", "Zero-based layer index.")
      .docArg("frameNumber", "Zero-based frame index.")
      .docReturns("A Cel object, or null for an invalid layer/frame.");

    addMethod("ensureImage", &SpriteScriptObject::ensureImage)
      .doc("Returns a cel image, creating a blank cel when missing.")
      .docArg("layerNumber", "Zero-based layer index.")
      .docArg("frameNumber", "Zero-based frame index.")
      .docReturns("An Image object, or null for an invalid layer/frame.");

    addMethod("duplicateLayer", &SpriteScriptObject::duplicateLayer)
      .doc("Duplicates an image layer and places the copy above it.")
      .docArg("layerNumber", "Zero-based layer index.")
      .docReturns("The duplicate Layer object, or null when invalid.");

    addMethod("removeLayer", &SpriteScriptObject::removeLayer)
      .doc("Removes a layer. The final remaining layer cannot be removed.")
      .docArg("layerNumber", "Zero-based layer index.")
      .docReturns("True when the layer was removed.");

    addMethod("frameDuration", &SpriteScriptObject::frameDuration)
      .doc("Returns a frame duration in milliseconds.")
      .docArg("frameNumber", "Zero-based frame index.");

    addMethod("setFrameDuration", &SpriteScriptObject::setFrameDuration)
      .doc("Sets a frame duration in milliseconds.")
      .docArg("frameNumber", "Zero-based frame index.")
      .docArg("milliseconds", "Duration from 1 to 65535 milliseconds.")
      .docReturns("True when the frame was valid.");

    addMethod("duplicateFrame", &SpriteScriptObject::duplicateFrame)
      .doc("Copies a frame and inserts the copy at a requested position.")
      .docArg("sourceFrame", "Zero-based source frame.")
      .docArg("insertAt", "Zero-based insertion position, up to frameCount.");

    addMethod("removeFrame", &SpriteScriptObject::removeFrame)
      .doc("Removes a frame while keeping at least one frame.")
      .docArg("frameNumber", "Zero-based frame index.");

    addMethod("moveFrame", &SpriteScriptObject::moveFrame)
      .doc("Moves a frame before another frame position.")
      .docArg("frameNumber", "Zero-based frame index.")
      .docArg("beforeFrame", "Insertion position from zero through frameCount.");

    addMethod("moveLayer", &SpriteScriptObject::moveLayer)
      .doc("Moves a layer to a zero-based stack position.")
      .docArg("layerNumber", "Current layer index.")
      .docArg("newPosition", "Destination layer index.");

    addMethod("createTag", &SpriteScriptObject::createTag)
      .doc("Creates a named animation tag over an inclusive frame range.")
      .docArg("name", "Tag name.")
      .docArg("fromFrame", "First zero-based frame.")
      .docArg("toFrame", "Last zero-based frame.");

    addMethod("removeTag", &SpriteScriptObject::removeTag)
      .doc("Removes the first animation tag with this name.")
      .docArg("name", "Tag name.");

    addMethod("commit", &SpriteScriptObject::commit)
      .doc("commits the current transaction.");

    addMethod("resize", &SpriteScriptObject::resize)
      .doc("resizes the sprite.")
      .docArg("width", "The new width.")
      .docArg("height", "The new height.");

    addMethod("crop", &SpriteScriptObject::crop)
      .doc("crops the sprite to the specified dimensions.")
      .docArg("x", "The left-most edge of the crop.")
      .docArg("y", "The top-most edge of the crop.")
      .docArg("width", "The width of the cropped area.")
      .docArg("height", "The height of the cropped area.");

    addMethod("save", &SpriteScriptObject::save)
      .doc("saves the sprite.");

    addMethod("saveAs", &SpriteScriptObject::saveAs)
      .doc("saves the sprite.")
      .docArg("fileName", "String. The new name of the file")
      .docArg("asCopy", "If true, the file is saved as a copy. Requires fileName to be specified.");

    addMethod("saveTo", &SpriteScriptObject::saveTo)
      .doc("Saves directly to a path without showing the file selector.")
      .docArg("fileName", "Destination path including extension.");

    addMethod("loadPalette", &SpriteScriptObject::loadPalette)
      .doc("loads a palette file.")
      .docArg("fileName", "The name of the palette file to load");
  }

  doc::Sprite* sprite() {
    auto sprite = handle<doc::Object, doc::Sprite>();
    if (!sprite)
      throw script::ObjectDestroyedException{};
    return sprite;
  }

  app::Document* doc() {
    return static_cast<app::Document*>(sprite()->document());
  }

  app::Transaction& transaction() {
    return app::AppScripting::transaction(doc());
  }

  void commit() {
    // Script transactions are committed automatically only after the whole
    // evaluation succeeds. Kept for compatibility with existing scripts.
  }

  script::ScriptObject* layer(int i) {
    return getEngine()->getScriptObject(sprite()->indexToLayer(doc::LayerIndex(i)));
  }

  script::ScriptObject* addLayer(const std::string& name) {
    app::DocumentApi api(doc(), transaction());
    doc::LayerImage* newLayer = api.newLayer(
      sprite(), name.empty() ? "Layer" : name);
    doc::ImageRef image(doc::Image::create(
      sprite()->pixelFormat(), sprite()->width(), sprite()->height()));
    doc::clear_image(image.get(), sprite()->transparentColor());
    api.addCel(newLayer, doc::frame_t(0), image);
    return getEngine()->getScriptObject(static_cast<doc::Layer*>(newLayer));
  }

  int addFrame(bool copyPrevious) {
    const doc::frame_t frame = sprite()->totalFrames();
    app::DocumentApi api(doc(), transaction());
    if (copyPrevious)
      api.addFrame(sprite(), frame);
    else
      api.addEmptyFrame(sprite(), frame);
    return int(frame);
  }

  script::ScriptObject* ensureCel(int layerNumber, int frameNumber) {
    if (frameNumber < 0 || frameNumber >= sprite()->totalFrames())
      return nullptr;
    doc::Layer* baseLayer = sprite()->indexToLayer(doc::LayerIndex(layerNumber));
    if (!baseLayer || !baseLayer->isImage())
      return nullptr;
    auto* imageLayer = static_cast<doc::LayerImage*>(baseLayer);
    auto cel = imageLayer->cel(doc::frame_t(frameNumber));
    if (!cel) {
      doc::ImageRef image(doc::Image::create(
        sprite()->pixelFormat(), sprite()->width(), sprite()->height()));
      doc::clear_image(image.get(), sprite()->transparentColor());
      cel = app::DocumentApi(doc(), transaction()).addCel(
        imageLayer, doc::frame_t(frameNumber), image);
    }
    return getEngine()->getScriptObject(cel.get());
  }

  script::ScriptObject* ensureImage(int layerNumber, int frameNumber) {
    if (frameNumber < 0 || frameNumber >= sprite()->totalFrames())
      return nullptr;
    doc::Layer* baseLayer = sprite()->indexToLayer(doc::LayerIndex(layerNumber));
    if (!baseLayer || !baseLayer->isImage())
      return nullptr;
    auto* imageLayer = static_cast<doc::LayerImage*>(baseLayer);
    auto cel = imageLayer->cel(doc::frame_t(frameNumber));
    if (!cel) {
      doc::ImageRef image(doc::Image::create(
        sprite()->pixelFormat(), sprite()->width(), sprite()->height()));
      doc::clear_image(image.get(), sprite()->transparentColor());
      cel = app::DocumentApi(doc(), transaction()).addCel(
        imageLayer, doc::frame_t(frameNumber), image);
    }
    return getEngine()->getScriptObject(cel->image());
  }

  script::ScriptObject* duplicateLayer(int layerNumber) {
    doc::Layer* source = sprite()->indexToLayer(doc::LayerIndex(layerNumber));
    if (!source || !source->isImage())
      return nullptr;
    app::DocumentApi(doc(), transaction()).duplicateLayerAfter(source, source);
    return getEngine()->getScriptObject(
      sprite()->indexToLayer(doc::LayerIndex(layerNumber+1)));
  }

  bool removeLayer(int layerNumber) {
    if (int(sprite()->countLayers()) <= 1)
      return false;
    doc::Layer* target = sprite()->indexToLayer(doc::LayerIndex(layerNumber));
    if (!target)
      return false;
    app::DocumentApi(doc(), transaction()).removeLayer(target);
    return true;
  }

  int frameDuration(int frameNumber) {
    return sprite()->frameDuration(doc::frame_t(frameNumber));
  }

  bool setFrameDuration(int frameNumber, int milliseconds) {
    if (frameNumber < 0 || frameNumber >= sprite()->totalFrames())
      return false;
    app::DocumentApi(doc(), transaction()).setFrameDuration(
      sprite(), doc::frame_t(frameNumber), milliseconds);
    return true;
  }

  bool duplicateFrame(int sourceFrame, int insertAt) {
    if (sourceFrame < 0 || sourceFrame >= sprite()->totalFrames() ||
        insertAt < 0 || insertAt > sprite()->totalFrames())
      return false;
    app::DocumentApi(doc(), transaction()).copyFrame(
      sprite(), doc::frame_t(sourceFrame), doc::frame_t(insertAt));
    return true;
  }

  bool removeFrame(int frameNumber) {
    if (sprite()->totalFrames() <= 1 || frameNumber < 0 ||
        frameNumber >= sprite()->totalFrames())
      return false;
    app::DocumentApi(doc(), transaction()).removeFrame(
      sprite(), doc::frame_t(frameNumber));
    return true;
  }

  bool moveFrame(int frameNumber, int beforeFrame) {
    if (frameNumber < 0 || frameNumber >= sprite()->totalFrames() ||
        beforeFrame < 0 || beforeFrame > sprite()->totalFrames())
      return false;
    app::DocumentApi(doc(), transaction()).moveFrame(
      sprite(), doc::frame_t(frameNumber), doc::frame_t(beforeFrame));
    return true;
  }

  bool moveLayer(int layerNumber, int newPosition) {
    const int count = int(sprite()->countLayers());
    if (layerNumber < 0 || layerNumber >= count ||
        newPosition < 0 || newPosition >= count || layerNumber == newPosition)
      return false;
    doc::Layer* moving = sprite()->indexToLayer(doc::LayerIndex(layerNumber));
    doc::Layer* destination = sprite()->indexToLayer(doc::LayerIndex(newPosition));
    app::DocumentApi api(doc(), transaction());
    if (newPosition < layerNumber)
      api.restackLayerBefore(moving, destination);
    else
      api.restackLayerAfter(moving, destination);
    return true;
  }

  bool createTag(const std::string& name, int fromFrame, int toFrame) {
    if (name.empty() || fromFrame < 0 || toFrame < fromFrame ||
        toFrame >= sprite()->totalFrames())
      return false;
    auto* tag = new doc::FrameTag(
      doc::frame_t(fromFrame), doc::frame_t(toFrame));
    tag->setName(name);
    transaction().execute(new app::cmd::AddFrameTag(sprite(), tag));
    return true;
  }

  bool removeTag(const std::string& name) {
    doc::FrameTag* tag = sprite()->frameTags().getByName(name);
    if (!tag)
      return false;
    transaction().execute(new app::cmd::RemoveFrameTag(sprite(), tag));
    return true;
  }

  void resize(int w, int h) {
    app::DocumentApi api(doc(), transaction());
    api.setSpriteSize(sprite(), w, h);
  }

  void crop(script::Value x, script::Value y, script::Value w, script::Value h){
    gfx::Rect bounds;
    commit();

    if (doc()->isMaskVisible())
      bounds = doc()->mask()->bounds();
    else
      bounds = sprite()->bounds();

    // if (x.type != script::Value::Type::UNDEFINED) bounds.x = x;
    // if (y.type != script::Value::Type::UNDEFINED) bounds.y = y;
    // if (w.type != script::Value::Type::UNDEFINED) bounds.w = w;
    // if (h.type != script::Value::Type::UNDEFINED) bounds.h = h;

    // if (!bounds.isEmpty()) {
    //   app::DocumentApi{doc(), transaction()}.cropSprite(sprite(), bounds);
    // }
  }

  void save() {
    app::Document* document = doc();
    app::AppScripting::afterSuccessfulEval([document] {
      auto uiCtx = app::UIContext::instance();
      uiCtx->setActiveDocument(document);
      auto saveCommand = app::CommandsModule::instance()->getCommandByName(app::CommandId::SaveFile);
      uiCtx->executeCommand(saveCommand);
    });
  }

  void saveAs(const std::string& fileName, bool asCopy) {
    if (fileName.empty()) asCopy = false;
    app::Document* document = doc();
    app::AppScripting::afterSuccessfulEval([document, fileName, asCopy] {
      auto uiCtx = app::UIContext::instance();
      uiCtx->setActiveDocument(document);
      auto commandName = asCopy ? app::CommandId::SaveFileCopyAs :
        (!fileName.empty() ? app::CommandId::SaveFileAs : app::CommandId::SaveFile);
      auto saveCommand = app::CommandsModule::instance()->getCommandByName(commandName);
      app::Params params;
      if (!fileName.empty()) params.set("filename", fileName.c_str());
      uiCtx->executeCommand(saveCommand, params);
    });
  }

  void saveTo(const std::string& fileName) {
    if (!fileName.empty())
      saveAs(fileName, false);
  }

  void loadPalette(const std::string& fileName){
    auto palette = app::load_palette(fileName.c_str());
    if (palette) {
      // TODO Merge this with the code in LoadPaletteCommand
      doc()->getApi(transaction()).setPalette(sprite(), 0, palette.get());
    }
  }
};

static script::ScriptObject::Regular<SpriteScriptObject> spriteSO(typeid(doc::Sprite*).name());
