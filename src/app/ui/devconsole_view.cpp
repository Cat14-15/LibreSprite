// Aseprite    | Copyright (C) 2001-2016  David Capello
// LibreSprite | Copyright (C) 2021       LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/ui/devconsole_view.h"
#include "app/app_menus.h"
#include "app/file_selector.h"
#include "app/launcher.h"
#include "app/modules/i18n.h"
#include "app/resource_finder.h"
#include "app/script/app_scripting.h"
#include "app/ui/script_editor.h"
#include "app/ui/skin/skin_style_property.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/workspace.h"
#include "script/engine.h"
#include "base/fs.h"
#include "base/path.h"
#include "base/string.h"
#include "ui/button.h"
#include "ui/alert.h"
#include "ui/entry.h"
#include "ui/message.h"
#include "ui/system.h"

#include <fstream>
#include <iterator>

namespace {

bool looksLikePythonScript(const std::string& source)
{
  const auto first = source.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
  return first != std::string::npos &&
    (source.compare(first, 27, "from libresprite import app") == 0 ||
     source.compare(first, 18, "import libresprite") == 0);
}

bool looksLikeJavaScript(const std::string& source)
{
  const auto first = source.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
  if (first == std::string::npos)
    return false;
  return source.compare(first, 2, "//") == 0 ||
         source.compare(first, 4, "var ") == 0 ||
         source.compare(first, 4, "let ") == 0 ||
         source.compare(first, 6, "const ") == 0 ||
         source.find("app.createSprite(", first) != std::string::npos;
}

bool scriptUsesPython(const std::string& source, bool selectedPython)
{
  if (looksLikePythonScript(source))
    return true;
  if (looksLikeJavaScript(source))
    return false;
  return selectedPython;
}

} // anonymous namespace

namespace app {

using namespace ui;
using namespace app::skin;

class DevConsoleView::CommmandEntry : public Entry {
public:
  CommmandEntry() : Entry(256, "") {
    setFocusStop(true);
    setFocusMagnet(true);
  }

  base::Signal1<void, const std::string&> ExecuteCommand;

protected:
  bool onProcessMessage(Message* msg) override {
    switch (msg->type()) {
      case kKeyDownMessage:
        if (hasFocus()) {
          KeyMessage* keymsg = static_cast<KeyMessage*>(msg);
          KeyScancode scancode = keymsg->scancode();

          switch (scancode) {
            case kKeyEnter:
            case kKeyEnterPad: {
              std::string cmd = text();
              ExecuteCommand(cmd);
              setText("");
              return true;
            }
          }
        }
        break;
    }
    return Entry::onProcessMessage(msg);
  }
};

DevConsoleView::DevConsoleView()
  : Box(VERTICAL)
  , m_textBox(app::i18n("Welcome to LibreSprite Scripting Console"), LEFT)
  , m_scriptEditor(new ScriptEditor)
  , m_newButton(app::i18n("New Script"))
  , m_openButton(app::i18n("Open Script"))
  , m_saveButton(app::i18n("Save Script"))
  , m_saveAsButton(app::i18n("Save Script As"))
  , m_openFolderButton(app::i18n("Open Scripts Folder"))
  , m_scriptName(app::i18n("Unsaved Script"))
  , m_runButton(app::i18n("Run Script"))
  , m_clearButton(app::i18n("Clear"))
  , m_stopButton(app::i18n("Stop Script"))
  , m_label(">")
  , m_entry(new CommmandEntry)
{
  SkinTheme* theme = static_cast<SkinTheme*>(this->theme());

  addChild(&m_fileButtons);
  addChild(&m_scriptName);
  addChild(&m_editorView);
  addChild(&m_editorButtons);
  addChild(&m_view);
  addChild(&m_bottomBox);

  m_fileButtons.addChild(&m_newButton);
  m_fileButtons.addChild(&m_openButton);
  m_fileButtons.addChild(&m_saveButton);
  m_fileButtons.addChild(&m_saveAsButton);
  m_fileButtons.addChild(&m_openFolderButton);
  m_fileButtons.addChild(&m_scriptLibrary);
  m_scriptLibrary.setExpansive(true);
  m_scriptLibrary.setClickOpen(true);
  m_newButton.Click.connect([this](Event&) { onNewScript(); });
  m_openButton.Click.connect([this](Event&) { onOpenScript(); });
  m_saveButton.Click.connect([this](Event&) { onSaveScript(); });
  m_saveAsButton.Click.connect([this](Event&) { onSaveScriptAs(); });
  m_openFolderButton.Click.connect(
    [this](Event&) { launcher::open_folder(m_scriptsDirectory); });
  m_scriptLibrary.Change.connect(&DevConsoleView::onLibrarySelection, this);

  m_editorView.attachToView(m_scriptEditor);
  m_editorView.setExpansive(true);
  m_editorButtons.addChild(&m_runButton);
  m_editorButtons.addChild(&m_clearButton);
  m_editorButtons.addChild(&m_stopButton);
  m_runButton.Click.connect([this](Event&) { onRunEditor(); });
  m_clearButton.Click.connect([this](Event&) { onClearEditor(); });
  m_stopButton.Click.connect([this](Event&) { onStopScript(); });
  m_scriptEditor->setSource(
    "// LibreSprite JavaScript\n"
    "var sprite = app.activeSprite;\n"
    "var image = app.activeImage;\n"
    "// image.putPixel(0, 0, app.pixelColor.rgba(255, 0, 0, 255));\n");
  m_scriptEditor->RunRequested.connect(&DevConsoleView::onRunEditor, this);
  m_scriptEditor->SaveRequested.connect(&DevConsoleView::onSaveScript, this);
  m_scriptEditor->OpenRequested.connect(&DevConsoleView::onOpenScript, this);
  m_scriptEditor->Change.connect(&DevConsoleView::updateScriptName, this);

  m_bottomBox.addChild(&m_language);
  m_bottomBox.addChild(&m_label);
  m_bottomBox.addChild(m_entry);

  m_language.addItem("JavaScript");
  m_language.addItem("Python");
  m_language.setSelectedItemIndex(0);

  m_view.setProperty(SkinStylePropertyPtr(
      new SkinStyleProperty(theme->styles.workspaceView())));

  m_view.attachToView(&m_textBox);
  m_view.setExpansive(true);
  m_entry->setExpansive(true);
  m_entry->ExecuteCommand.connect(&DevConsoleView::onExecuteCommand, this);
  m_scriptsDirectory = userScriptDirectory();
  refreshScriptLibrary();
  updateScriptName();
}

DevConsoleView::~DevConsoleView()
{
  // m_document->removeObserver(this);
  // delete m_editor;
}

std::string DevConsoleView::getTabText()
{
  return app::i18n("Scripting Console");
}

TabIcon DevConsoleView::getTabIcon()
{
  return TabIcon::NONE;
}

WorkspaceView* DevConsoleView::cloneWorkspaceView()
{
  return new DevConsoleView();
}

void DevConsoleView::onWorkspaceViewSelected()
{
  m_scriptEditor->requestFocus();
}

bool DevConsoleView::onCloseView(Workspace* workspace, bool quitting)
{
  if (!confirmDiscard())
    return false;
  workspace->removeView(this);
  return true;
}

void DevConsoleView::onTabPopup(Workspace* workspace)
{
  Menu* menu = AppMenus::instance()->getTabPopupMenu();
  if (!menu)
    return;

  menu->showPopup(ui::get_mouse_position());
}

bool DevConsoleView::onProcessMessage(Message* msg)
{
  return Box::onProcessMessage(msg);
}

void DevConsoleView::onExecuteCommand(const std::string& cmd)
{
  if (m_language.getSelectedItemIndex() == 1) {
    m_engine.evalPython(cmd);
    return;
  }
  script::Engine::setDefault("js", {"js"});
  m_engine.printLastResult();
  m_engine.eval(cmd);
}

void DevConsoleView::onRunEditor()
{
  const std::string& source = m_scriptEditor->source();
  const std::string extension = base::string_to_lower(
    base::get_file_extension(m_currentScript));

  // Pasted Python has no filename yet, so recognize the public Python API
  // imports too. This prevents Python source from being sent to JavaScript
  // just because the language combo still has its initial value.
  const bool usePython = scriptUsesPython(
    source, extension == "py" || m_language.getSelectedItemIndex() == 1);
  m_language.setSelectedItemIndex(usePython ? 1 : 0);

  if (usePython)
    m_engine.evalPython(source);
  else
    onExecuteCommand(source);
}

void DevConsoleView::onClearEditor()
{
  m_scriptEditor->setSource("", false, true);
  m_scriptEditor->requestFocus();
}

std::string DevConsoleView::userScriptDirectory()
{
  ResourceFinder finder(false);
  finder.includeUserDir("scripts/.folder");
  const std::string marker = finder.getFirstOrCreateDefault();
  const std::string directory = base::get_file_path(marker);
  base::make_all_directories(directory);
  return directory;
}

void DevConsoleView::refreshScriptLibrary()
{
  m_updatingLibrary = true;
  m_scriptLibrary.removeAllItems();
  m_scriptLibrary.addItem(app::i18n("Saved Scripts"));
  for (const auto& name : base::list_files(m_scriptsDirectory)) {
    const std::string extension = base::string_to_lower(base::get_file_extension(name));
    if (extension == "js" || extension == "py")
      m_scriptLibrary.addItem(name);
  }
  m_scriptLibrary.setSelectedItemIndex(0);
  m_updatingLibrary = false;
}

void DevConsoleView::onLibrarySelection()
{
  if (m_updatingLibrary || m_scriptLibrary.getSelectedItemIndex() <= 0)
    return;
  if (!confirmDiscard()) {
    m_updatingLibrary = true;
    m_scriptLibrary.setSelectedItemIndex(0);
    m_updatingLibrary = false;
    return;
  }
  loadScript(base::join_path(m_scriptsDirectory, m_scriptLibrary.getValue()));
}

void DevConsoleView::onNewScript()
{
  if (!confirmDiscard())
    return;
  m_currentScript.clear();
  m_scriptEditor->setSource(
    m_language.getSelectedItemIndex() == 1 ?
      "from libresprite import app\n\n"
      "sprite = app.create_sprite(32, 32, 0)\n"
      "image = sprite.ensure_image(0, 0)\n":
      "// LibreSprite JavaScript\n\n"
      "var sprite = app.createSprite(32, 32, 0);\n"
      "var image = sprite.ensureImage(0, 0);\n");
  updateScriptName();
  m_scriptEditor->requestFocus();
}

void DevConsoleView::onOpenScript()
{
  if (!confirmDiscard())
    return;
  const std::string filename = app::show_file_selector(
    app::i18n("Open Script"),
    m_currentScript.empty() ? m_scriptsDirectory : m_currentScript,
    "js,py", FileSelectorType::Open);
  if (!filename.empty())
    loadScript(filename);
}

void DevConsoleView::loadScript(const std::string& filename)
{
  std::ifstream input(filename, std::ios::binary);
  if (!input) {
    onConsolePrint(app::i18n("Could not open script file.").c_str());
    return;
  }
  const std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
  m_currentScript = filename;
  m_scriptEditor->setSource(source);
  const std::string extension = base::string_to_lower(base::get_file_extension(filename));
  m_language.setSelectedItemIndex(
    scriptUsesPython(source, extension == "py") ? 1 : 0);
  updateScriptName();
  m_scriptEditor->requestFocus();
}

bool DevConsoleView::saveScript(const std::string& filename)
{
  const bool usePython = scriptUsesPython(
    m_scriptEditor->source(), m_language.getSelectedItemIndex() == 1);
  m_language.setSelectedItemIndex(usePython ? 1 : 0);
  const std::string correctedFilename =
    base::replace_extension(filename, usePython ? "py" : "js");

  std::ofstream output(correctedFilename, std::ios::binary | std::ios::trunc);
  if (!output) {
    onConsolePrint(app::i18n("Could not save script file.").c_str());
    return false;
  }
  output << m_scriptEditor->source();
  if (!output) {
    onConsolePrint(app::i18n("Could not save script file.").c_str());
    return false;
  }
  output.close();

  // A normal Save also fixes a previously wrong extension. Remove the old
  // sibling only after the correctly named file has been written safely.
  if (filename == m_currentScript && filename != correctedFilename &&
      base::is_file(filename))
    base::delete_file(filename);

  m_currentScript = correctedFilename;
  m_scriptEditor->markSaved();
  refreshScriptLibrary();
  updateScriptName();
  return true;
}

void DevConsoleView::onSaveScript()
{
  if (m_currentScript.empty())
    onSaveScriptAs();
  else
    saveScript(m_currentScript);
}

void DevConsoleView::onSaveScriptAs()
{
  const bool usePython = scriptUsesPython(
    m_scriptEditor->source(), m_language.getSelectedItemIndex() == 1);
  m_language.setSelectedItemIndex(usePython ? 1 : 0);
  const std::string extension = usePython ? "py" : "js";
  const std::string initial = m_currentScript.empty() ?
    base::join_path(m_scriptsDirectory, "MyScript."+extension): m_currentScript;
  const std::string filename = app::show_file_selector(
    app::i18n("Save Script As"), initial, extension,
    FileSelectorType::Save);
  if (!filename.empty())
    saveScript(filename);
}

void DevConsoleView::updateScriptName()
{
  const std::string name = m_currentScript.empty() ?
    app::i18n("Unsaved Script") : base::get_file_name(m_currentScript);
  m_scriptName.setText((m_scriptEditor->modified() ? "* " : "")+name);
  layout();
}

bool DevConsoleView::confirmDiscard()
{
  if (!m_scriptEditor->modified())
    return true;
  const std::string message = app::i18n("Warning")+"<<"+
    app::i18n("The script has unsaved changes. Discard them?")+"||"+
    app::i18n("Discard")+"||"+app::i18n("Cancel");
  return ui::Alert::show("%s", message.c_str()) == 1;
}

void DevConsoleView::onStopScript()
{
  if (app::AppScripting::requestStop())
    onConsolePrint(app::i18n("Stopping script; current changes will be rolled back...").c_str());
  else
    onConsolePrint(app::i18n("No script is currently running.").c_str());
}

void DevConsoleView::onConsolePrint(const char* text)
{
  if (text)
    m_textBox.setText(m_textBox.text() + "\n" + text);
}

} // namespace app
