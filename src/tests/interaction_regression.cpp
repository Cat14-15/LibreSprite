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
#include "app/ui/timeline.h"
#undef private

#include "ui/button.h"
#include "ui/message.h"
#include "ui/system.h"
#include "app/document_api.h"
#include "app/context.h"
#include "app/document.h"
#include "app/animation_workflow.h"
#include "app/context_access.h"
#include "app/document_api.h"
#include "app/document_undo.h"
#include "app/transaction.h"
#include "app/cmd/add_frame_tag.h"
#include "doc/frame_tag.h"
#include "doc/frame_tags.h"
#include "app/document_range.h"
#include "app/document_range_ops.h"
#include "doc/layer.h"
#include "doc/sprite.h"
#include "doc/cel.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include "doc/test_context.h"
#include "render/render.h"
#include "app/app.h"
#include "app/app_options.h"
#include "app/ui_context.h"
#include "app/ui/main_window.h"
#include "app/ui/editor/editor.h"
#include "app/commands/commands.h"
#include "app/commands/command.h"
#include "she/system.h"
#include "she/display.h"
#include "she/surface.h"
#include <iostream>
#include <fstream>
#include "ui/listitem.h"
#include "ui/entry.h"
#include <memory>
#include <stdexcept>

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

static int fullUI(const char* executable) {
  auto platform = std::unique_ptr<she::System>(she::create_system());
  return platform->run([&]() -> int {
    app::App application;
    const char* arguments[] = {executable};
    app::AppOptions options(1, arguments);
    application.initialize(options);
    try {
    std::cerr << "CHECK: real app initialized\n";
    auto sprite = doc::Sprite::createBasicSprite(doc::IMAGE_RGB, 8, 8, 2);
    doc::Layer* original = sprite->folder()->getFirstLayer();
    doc::clear_image(original->cel(0)->image(), doc::rgba(255,0,0,255));
    sprite->folder()->addLayer(new doc::LayerImage(sprite));
    sprite->folder()->addLayer(new doc::LayerImage(sprite));
    auto document = new app::Document(sprite);
    app::UIContext::instance()->documents().add(document);
    app::UIContext::instance()->setActiveDocument(document);
    application.mainWindow()->setTimelineVisibility(true);
    auto manager = ui::Manager::getDefault();
    auto timeline = application.timeline();
    auto repaint = [&]() {
      manager->invalidate();
      manager->generateMessages();
      manager->dispatchMessages();
    };
    repaint();
    require(timeline->m_sprite == sprite, "timeline must target the displayed sprite");
    timeline->setViewScroll(gfx::Point(0,0));
    repaint();
    auto point = [&](int x, int y) { return timeline->bounds().origin()+gfx::Point(x,y); };
    auto down = [&](gfx::Point p) {
      manager->handleMouseDown(p,ui::kButtonLeft,ui::kKeyNoneModifier,ui::PointerType::Mouse,1);
      manager->pumpQueue();
    };
    auto up = [&](gfx::Point p) {
      manager->handleMouseUp(p,ui::kButtonLeft,ui::kKeyNoneModifier,ui::PointerType::Mouse);
      manager->pumpQueue();
    };
    auto drag = [&](gfx::Point a, gfx::Point b) {
      down(a);
      manager->handleMouseMove(b,ui::kButtonLeft,ui::kKeyNoneModifier,ui::PointerType::Mouse,1);
      manager->pumpQueue();
      up(b);
    };
    const int headerY = timeline->topHeight()+timeline->headerSize()/2;
    const int rowsY = timeline->topHeight()+timeline->headerSize();
    auto divider = point(timeline->columnX(1),headerY);
    int width = timeline->m_columnWidths[0];
    drag(divider,divider+gfx::Point(10,0));
    require(timeline->m_columnWidths[0] > width, "actual timeline column divider must resize");
    std::cerr << "CHECK: real column drag passed\n";
    auto frameEdge = point(timeline->m_separator_x+timeline->frameSize()-1,headerY);
    width = timeline->frameSize();
    drag(frameEdge,frameEdge+gfx::Point(10,0));
    require(timeline->frameSize() > width, "actual frame divider must resize");
    std::cerr << "CHECK: real frame drag passed\n";
    const int scale = timeline->m_timelineScale;
    manager->handleMouseWheel(point(timeline->m_separator_x+timeline->frameSize()/2,headerY),
      ui::kButtonNone,ui::kKeyCtrlModifier,ui::PointerType::Mouse,gfx::Point(0,-1),false);
    manager->pumpQueue();
    require(timeline->m_timelineScale > scale, "actual Ctrl-wheel must resize the timeline");
    drag(point(timeline->columnX(3)+10,rowsY+2*timeline->layerSize()+timeline->layerSize()/2),
         point(timeline->columnX(3)+10,rowsY+timeline->layerSize()/4));
    require(sprite->indexToLayer(doc::LayerIndex(2)) == original, "dragging a layer name must reorder the real sprite");
    std::cerr << "CHECK: real layer name drag passed\n";
    auto editor = app::UIContext::instance()->activeEditor();
    const gfx::Point sample = editor->editorToScreen(gfx::Point(2,2));
    repaint();
    auto surface = manager->getDisplay()->getSurface();
    const auto before = surface->getPixel(sample.x,sample.y);
    for (int row=0;row<3;++row) {
      const auto eye = point(timeline->m_columnWidths[0]/2,
        rowsY+row*timeline->layerSize()+timeline->layerSize()/2);
      down(eye); up(eye);
    }
    for (int i=0;i<3;++i)
      require(!sprite->indexToLayer(doc::LayerIndex(i))->isVisible(), "each actual row eye must hide its layer on the first click");
    repaint();
    const auto after = surface->getPixel(sample.x,sample.y);
    require(before != after, "actual canvas pixels must change after hiding every layer");
    const auto lock = point(timeline->columnX(1)+timeline->m_columnWidths[1]/2,
      rowsY+timeline->layerSize()/2);
    const bool editable = original->isEditable();
    down(lock); up(lock);
    require(original->isEditable() != editable, "actual row lock must toggle on the first click");
    std::cerr << "CHECK: real visibility and canvas passed\n";

    // Real timeline controls must edit only one layer, preserving held drawings
    // and every other layer's data/position. Use a disposable test document.
    original->setEditable(true);
    auto arm=static_cast<doc::LayerImage*>(original);
    auto body=static_cast<doc::LayerImage*>(sprite->indexToLayer(doc::LayerIndex(0)));
    {
      app::ContextWriter writer(app::UIContext::instance());app::Transaction tx(app::UIContext::instance(),"Key test setup");
      app::DocumentApi api(document,tx);
      for (int f=1;f<5;++f) api.addEmptyFrame(sprite,f);
      doc::ImageRef image(doc::Image::create(doc::IMAGE_RGB,8,8));doc::clear_image(image.get(),doc::rgba(0,0,255,255));api.addCel(body,0,image);
      app::animation::hold(document,tx,body,0,4);
      app::animation::hold(document,tx,arm,0,4);
      tx.commit();
    }
    const auto bodyData=body->cel(0)->dataRef();
    timeline->setLayer(arm);timeline->setFrame(2,true);repaint();
    const auto keyPoint=timeline->m_keyButton.bounds().center();down(keyPoint);up(keyPoint);
    require(arm->cel(2)->dataRef()!=arm->cel(0)->dataRef(),"actual drawing-key button must unlink the selected layer");
    require(arm->cel(3)->dataRef()==arm->cel(2)->dataRef() && arm->cel(4)->dataRef()==arm->cel(2)->dataRef(),"new key must own the following held drawing segment");
    require(arm->cel(1)->dataRef()==arm->cel(0)->dataRef(),"earlier held frames must remain unchanged");
    document->undoHistory()->undo();
    require(arm->cel(4)->dataRef()==arm->cel(0)->dataRef(),"one Undo must restore the entire old hold segment");
    document->undoHistory()->redo();
    require(arm->cel(4)->dataRef()==arm->cel(2)->dataRef() && arm->cel(2)->dataRef()!=arm->cel(0)->dataRef(),"Redo must restore the new key and its holds");
    {
      app::ContextWriter writer(app::UIContext::instance());app::Transaction tx(app::UIContext::instance(),"End key");
      app::animation::drawingKey(document,tx,arm,4,0,4,false);
      app::DocumentApi api(document,tx);api.setCelPosition(sprite,arm->cel(4),8,4);
      api.setFrameDuration(sprite,0,100);api.setFrameDuration(sprite,1,300);api.setFrameDuration(sprite,2,100);api.setFrameDuration(sprite,3,100);tx.commit();
    }
    timeline->m_range.startRange(timeline->getLayerIndex(arm),0,app::DocumentRange::kCels);
    timeline->m_range.endRange(timeline->getLayerIndex(arm),4);
    timeline->applyLayerKey(3);
    require(arm->cel(0)->x()==0 && arm->cel(4)->x()==8,"position tween must preserve both endpoint keys");
    require(arm->cel(1)->x()==1 && arm->cel(2)->x()==5 && arm->cel(3)->x()==7,"position tween must use frame durations and independent cel data");
    for (int f=0;f<5;++f) require(body->cel(f)->dataRef()==bodyData && body->cel(f)->x()==0,"layer-key edits must leave every other layer unchanged");
    timeline->m_range.disableRange();timeline->setFrame(3,true);timeline->applyLayerKey(1);
    require(arm->cel(3)->image()->getPixel(0,0)==0,"blank key must create an explicit transparent drawing");
    document->undoHistory()->undo();
    require(arm->cel(3)->x()==7,"blank-key Undo must restore the previous drawing and position");
    doc::FrameTag *idle,*walk,*copy;
    {
      app::ContextWriter writer(app::UIContext::instance());app::Transaction tx(app::UIContext::instance(),"Animation test");
      idle=new doc::FrameTag(0,4);idle->setName("idle");tx.execute(new app::cmd::AddFrameTag(sprite,idle));
      walk=app::animation::appendClip(document,tx,"walk",4,90,0);
      copy=app::animation::appendClip(document,tx,"walk",4,90,0,walk);tx.commit();
    }
    require(idle->toFrame()==4 && walk->fromFrame()==5 && walk->toFrame()==8 && copy->fromFrame()==9,"new animation must not extend or merge another animation tag");
    require(copy->name()=="walk-1","animation copies must receive a unique name");
    require(arm->cel(5)->dataRef()!=arm->cel(0)->dataRef() && arm->cel(9)->dataRef()!=arm->cel(5)->dataRef(),"animation drawings must be isolated from their source animation");
    require(arm->cel(5)->dataRef()==arm->cel(8)->dataRef() && arm->cel(9)->dataRef()==arm->cel(12)->dataRef(),"held drawing links must survive animation duplication");
    timeline->refreshWorkflow();repaint();
    auto comboArrow=timeline->m_animationList.getButtonWidget()->bounds().center();
    down(comboArrow);up(comboArrow);repaint();
    auto animationChoice=timeline->m_animationList.getItem(2)->bounds().center();
    down(animationChoice);up(animationChoice);repaint();
    require(timeline->firstFrame()==5 && timeline->lastFrame()==8 && timeline->getFrame()==5,"animation selection must isolate and activate the chosen range");
    const auto header=point(timeline->m_separator_x+timeline->frameSize()/2,timeline->topHeight()+timeline->headerSize()/2);
    down(header);up(header);
    require(timeline->getFrame()==5,"first visible cell must target the selected animation's first actual frame");
    timeline->selectAnimation(doc::NullId);repaint();
    require(timeline->firstFrame()==0 && timeline->lastFrame()==12,"All animations must restore the full timeline");
    bool createdFromDialog=false;
    ui::Timer author(10,manager);
    author.Tick.connect([&]{
      auto name=manager->findChildT<ui::Entry>("animationName");
      if (!name) return;
      name->setText("attack");manager->findChildT<ui::Entry>("animationFrames")->setText("3");
      manager->findChildT<ui::Entry>("animationDuration")->setText("120");
      auto button=manager->findChild("animationCreate");author.stop();
      down(button->bounds().center());up(button->bounds().center());createdFromDialog=true;
    });
    author.start();timeline->createAnimation(false);author.stop();repaint();
    require(createdFromDialog && sprite->frameTags().getByName("attack"),"actual creation dialog must create the requested animation");
    require(timeline->firstFrame()==13 && timeline->lastFrame()==15,"creation dialog must activate only the new animation");
    if (const char* path=std::getenv("LIBRESPRITE_WORKFLOW_SCREENSHOT")) {
      const auto png=she::instance()->encodeSurfaceAsPNG(manager->getDisplay()->getSurface());
      std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(png.data()),png.size());
    }
    std::cerr << "CHECK: independent layer keys/holds, duration-aware tween, Undo/Redo, isolated copies, actual animation dropdown and creation dialog passed\n";

    auto palette = app::ColorBar::instance()->getPaletteView();
    auto swatch = palette->getPaletteEntryBounds(1);
    auto swatchPoint = palette->bounds().origin()+gfx::Point(swatch.x+swatch.w/2,swatch.y+swatch.h/2);
    down(swatchPoint); up(swatchPoint);
    require(palette->getSelectedEntry() == 1, "actual palette swatch must select on the first click");
    manager->handleMouseDoubleClick(swatchPoint,ui::kButtonLeft,ui::kKeyNoneModifier,ui::PointerType::Mouse);
    manager->pumpQueue();
    require(app::CommandsModule::instance()->getCommandByName(app::CommandId::PaletteEditor)->isChecked(app::UIContext::instance()),
            "actual palette double-click must open the color editor");
    std::cout << "PASS: real app column drag, frame drag, Ctrl-wheel, layer name drag, row eye/lock clicks, canvas repaint, palette select/edit\n";
    std::cout.flush();
    // App::run normally owns UI shutdown. This isolated test never enters
    // that loop, and exits without saving its temporary document/profile.
    std::_Exit(0);
    }
    catch (const std::exception& error) {
      std::cerr << "FULLUI_FAIL: " << error.what() << '\n';
      std::_Exit(1);
    }
  });
}

int app_main(int argc, char** argv) {
  try {
    if (argc>1 && std::string(argv[1]) == "--full-ui") return fullUI(argv[0]);
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
