#include "gfx/region.h"
#include "ui/widget.h"
#include "ui/keys.h"
#include "ui/pointer_type.h"
#include <sstream>
#include "base/signal.h"
// Exercise the event routing entry points without opening a native window.
#define private public
#include "ui/manager.h"
#include "ui/window.h"
#undef private

#include "ui/button.h"
#include "ui/message.h"
#include "ui/system.h"
#include "app/document_api.h"
#include "app/context.h"
#include "app/document.h"
#include "app/document_range.h"
#include "app/document_range_ops.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include "doc/test_context.h"
#include "render/render.h"
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int app_main(int, char**) {
  try {
    std::cerr << "CHECK: initialize UI\n";
    ui::UISystem system;
    ui::Manager manager;
    manager.setBoundsQuietly(gfx::Rect(0, 0, 400, 200));
    {
      std::cerr << "CHECK: create desktop\n";
      std::unique_ptr<ui::Window> desktop(new ui::Window(ui::Window::DesktopWindow));
      desktop->setBoundsQuietly(gfx::Rect(0, 0, 400, 200));
      desktop->setVisible(true);
      manager.addChild(desktop.get());
      std::cerr << "CHECK: create buttons\n";
      auto left = new ui::Button("");
      auto right = new ui::Button("");
      desktop->addChild(left);
      desktop->addChild(right);
      left->setBoundsQuietly(gfx::Rect(0, 0, 100, 40));
      right->setBoundsQuietly(gfx::Rect(100, 0, 100, 40));
      int clicks = 0;
      right->Click.connect([&](ui::Event&) { ++clicks; });

      std::cerr << "CHECK: pointer routing\n";
      manager.setMouse(left);
      require(manager.mouseInputTarget(gfx::Point(150, 20)) == right,
              "press target must use current coordinates, not stale hover");
      std::cerr << "CHECK: short clicks\n";
      for (int i=0; i<4; ++i) {
        // Queue a short press and release together, without a preceding move.
        manager.handleMouseDown(gfx::Point(150,20), ui::kButtonLeft,
          ui::kKeyNoneModifier, ui::PointerType::Mouse, 1.0f);
        manager.handleMouseUp(gfx::Point(150,20), ui::kButtonLeft,
          ui::kKeyNoneModifier, ui::PointerType::Mouse);
        manager.pumpQueue();
      }
      require(clicks == 4, "every short button click must fire exactly once");

      manager.handleMouseDown(gfx::Point(150,20), ui::kButtonLeft,
        ui::kKeyNoneModifier, ui::PointerType::Mouse, 1.0f);
      manager.handleMouseUp(gfx::Point(50,20), ui::kButtonLeft,
        ui::kKeyNoneModifier, ui::PointerType::Mouse);
      manager.pumpQueue();
      require(manager.getCapture() == nullptr,
              "a queued short drag must release the original button capture");

      manager.setCapture(left);
      require(manager.mouseInputTarget(gfx::Point(150,20)) == left,
              "drag capture must survive leaving the pressed widget");
      desktop->m_isForeground = true;
      require(manager.mouseInputTarget(gfx::Point(150,20)) == left,
              "foreground window must honor capture owned by its child");
      manager.freeCapture();
      desktop->m_isForeground = false;
      manager.removeChild(desktop.get());
    }
    manager.pumpQueue();
    std::cerr << "CHECK: copy names and render\n";
    std::unique_ptr<doc::Sprite> sprite(doc::Sprite::createBasicSprite(doc::IMAGE_RGB, 2, 2, 2));
    auto layer = sprite->folder()->getFirstLayer();
    layer->setName("Layer 2");
    require(app::DocumentApi::uniqueLayerName(sprite.get(), "Layer 2") == "Layer 2-1",
            "first copy must use -1");
    auto duplicate = new doc::LayerImage(sprite.get());
    duplicate->setName("Layer 2-1");
    sprite->folder()->addLayer(duplicate);
    require(app::DocumentApi::uniqueLayerName(sprite.get(), "Layer 2") == "Layer 2-2",
            "repeated copies must use the next free suffix");
    require(app::DocumentApi::uniqueLayerName(sprite.get(), "Other") == "Other",
            "unoccupied source names must be preserved");
    std::unique_ptr<doc::Image> output(doc::Image::create(doc::IMAGE_RGB, 2, 2));
    doc::clear_image(layer->cel(doc::frame_t(0))->image(), doc::rgba(255,0,0,255));
    render::Render renderer;
    renderer.renderSprite(output.get(), sprite.get(), doc::frame_t(0));
    require(doc::rgba_geta(output->getPixel(0,0)) == 255, "visible layer must render");
    layer->setVisible(false);
    duplicate->setVisible(false);
    renderer.setPreviewImage(layer, doc::frame_t(0),
      layer->cel(doc::frame_t(0))->image(), gfx::Point(0,0), doc::BlendMode::NORMAL);
    renderer.renderSprite(output.get(), sprite.get(), doc::frame_t(0));
    require(doc::rgba_geta(output->getPixel(0,0)) == 0, "all hidden layers must clear the rendered image");
    {
      doc::TestContextT<app::Context> context;
      auto document = static_cast<app::Document*>(context.documents().add(2,2));
      doc::Sprite* ordered = document->sprite();
      doc::Layer* bottom = ordered->folder()->getFirstLayer();
      ordered->folder()->addLayer(new doc::LayerImage(ordered));
      ordered->folder()->addLayer(new doc::LayerImage(ordered));
      app::DocumentRange from, to;
      from.startRange(doc::LayerIndex(0), 0, app::DocumentRange::kLayers);
      to.startRange(doc::LayerIndex(2), 0, app::DocumentRange::kLayers);
      from = app::move_range(document, from, to, app::kDocumentRangeAfter);
      require(ordered->indexToLayer(doc::LayerIndex(2)) == bottom, "layer must move to the top");
      to.startRange(doc::LayerIndex(0), 0, app::DocumentRange::kLayers);
      app::move_range(document, from, to, app::kDocumentRangeBefore);
      require(ordered->indexToLayer(doc::LayerIndex(0)) == bottom, "layer must move back to the bottom");
      context.documents().remove(document);
      delete document;
    }
    std::cout << "PASS: current-position routing, 4 short clicks, queued drag release, drag capture, modal capture, unique copy names, hidden-layer rendering, layer move up/down\n";
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
