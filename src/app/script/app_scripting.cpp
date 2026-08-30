// Aseprite
// Copyright (C) 2001-2016  David Capello
// Copyright (C) 2021 LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/document.h"
#include "app/transaction.h"
#include "app/ui_context.h"
#include "app/script/app_scripting.h"
#include "app/task_manager.h"
#include "app/resource_finder.h"
#include "base/file_handle.h"
#include "base/base64.h"
#include "base/fs.h"
#include "base/path.h"
#include "base/range.h"
#include "base/string.h"
#include "base/trim_string.h"
#include "base/time.h"
#include "script/engine.h"
#include "script/engine_delegate.h"
#include "script/value.h"
#include "ui/keys.h"
#include "ui/message.h"
#include "ui/message_type.h"
#include "ui/widget.h"
#include "ui/manager.h"
#include "ui/timer.h"

#include <map>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <string>
#include <string_view>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

inject<script::Engine> engine{nullptr};
std::string previousFileName;
bool wasInit{};

struct ScriptTransactionEntry {
  app::Document* document;
  std::unique_ptr<app::Transaction> transaction;
};

int transactionDepth = 0;
bool transactionFailed = false;
std::vector<ScriptTransactionEntry> scriptTransactions;
std::vector<std::function<void()>> successfulEvalCallbacks;
std::vector<std::function<void(bool)>> completedEvalCallbacks;
std::unique_ptr<ui::Timer> mcpTimer;
std::string mcpDir;
bool mcpRequestPending = false;
std::string mcpSource;
std::string mcpTemporary;
std::string mcpResult;
std::string mcpLanguage;

std::string pythonStringLiteral(const std::string& value)
{
  std::string result = "\"";
  for (const char ch : value) {
    switch (ch) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += ch; break;
    }
  }
  result += '"';
  return result;
}

void executeMcpRequest()
{
  bool success = false;
  std::string detail;
  try {
    script::Engine::setDefault("js", {"js"});
    app::AppScripting scripting;
    success = (mcpLanguage == "py" ? scripting.evalPython(mcpSource):
                                     scripting.eval(mcpSource));
  }
  catch (const std::exception& ex) {
    detail = ex.what();
  }
  catch (...) {
    detail = "unknown native exception";
  }
  {
    std::ofstream output(mcpTemporary, std::ios::trunc);
    output << (success ? "ok" : "error") << '\n' << detail;
  }
  if (base::is_file(mcpResult))
    base::delete_file(mcpResult);
  base::move_file(mcpTemporary, mcpResult);
  mcpSource.clear();
  mcpTemporary.clear();
  mcpResult.clear();
  mcpLanguage.clear();
  mcpRequestPending = false;
}

std::string getMcpDirectory()
{
  if (!mcpDir.empty())
    return mcpDir;
  if (const char* configured = std::getenv("LIBRESPRITE_MCP_DIR")) {
    if (*configured) {
      mcpDir = configured;
      base::make_all_directories(mcpDir);
      return mcpDir;
    }
  }
  app::ResourceFinder finder(false);
  finder.includeUserDir("mcp/.bridge");
  mcpDir = base::get_file_path(finder.getFirstOrCreateDefault());
  return mcpDir;
}

void pollMcpBridge()
{
  const std::string directory = getMcpDirectory();
  {
    std::ofstream heartbeat(base::join_path(directory, "libresprite.ready"),
                            std::ios::trunc);
    heartbeat << "ready\n";
  }
  if (mcpRequestPending)
    return;

  for (const auto& name : base::list_files(directory)) {
    const std::string extension =
      base::string_to_lower(base::get_file_extension(name));
    if (name.rfind("request-", 0) != 0 ||
        (extension != "js" && extension != "py"))
      continue;

    const std::string path = base::join_path(directory, name);
    std::ifstream input(path, std::ios::binary);
    if (!input)
      continue;
    const std::string source{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    base::delete_file(path);

    const std::string stem = name.substr(0, name.size()-extension.size()-1);
    mcpSource = source;
    mcpLanguage = extension;
    mcpTemporary = base::join_path(directory, stem+".result.tmp");
    mcpResult = base::join_path(directory, stem+".result");
    mcpRequestPending = true;
    mcpTimer->stop();
    executeMcpRequest();
    mcpTimer->start();
    break;
  }
}

void beginScriptTransaction()
{
  if (transactionDepth++ == 0) {
    transactionFailed = false;
    scriptTransactions.clear();
    successfulEvalCallbacks.clear();
    completedEvalCallbacks.clear();
  }
}

void finishScriptTransaction(bool success)
{
  if (!success)
    transactionFailed = true;
  if (--transactionDepth > 0)
    return;

  if (!transactionFailed) {
    for (auto& entry : scriptTransactions)
      entry.transaction->commit();
  }
  // Uncommitted transactions rollback from their destructors.
  scriptTransactions.clear();

  if (!transactionFailed) {
    auto callbacks = std::move(successfulEvalCallbacks);
    successfulEvalCallbacks.clear();
    for (auto& callback : callbacks)
      callback();
  }
  else {
    successfulEvalCallbacks.clear();
  }
  auto completed = std::move(completedEvalCallbacks);
  completedEvalCallbacks.clear();
  for (auto& callback : completed)
    callback(!transactionFailed);
}

}

namespace app {
  std::string AppScripting::m_fileName;
  std::multimap<std::string, std::string> eventHooks;

  class EventListener : public ui::Widget {
  public:
    bool onProcessMessage(ui::Message* msg) override {
      auto event = ui::to_string(msg->type());
      auto range = base::range{eventHooks.equal_range(event)};
      if (range.empty())
        return false;

      std::vector<script::Value> args {
        event,
        static_cast<int>(msg->modifiers())
      };

      switch (msg->type()) {
      case ui::kKeyDownMessage:
      case ui::kKeyUpMessage:
        args.push_back(static_cast<int>(static_cast<ui::KeyMessage*>(msg)->unicodeChar()));
        args.push_back(static_cast<int>(static_cast<ui::KeyMessage*>(msg)->scancode()));
        break;

      case ui::kMouseDownMessage:
      case ui::kMouseUpMessage:
      case ui::kDoubleClickMessage:
      case ui::kMouseEnterMessage:
      case ui::kMouseLeaveMessage:
      case ui::kMouseMoveMessage:
      case ui::kSetCursorMessage:
      case ui::kMouseWheelMessage:
        args.push_back(static_cast<int>(static_cast<ui::MouseMessage*>(msg)->position().x));
        args.push_back(static_cast<int>(static_cast<ui::MouseMessage*>(msg)->position().y));
        args.push_back(static_cast<int>(static_cast<ui::MouseMessage*>(msg)->buttons()));
        args.push_back(static_cast<int>(static_cast<ui::MouseMessage*>(msg)->wheelDelta().x));
        args.push_back(static_cast<int>(static_cast<ui::MouseMessage*>(msg)->wheelDelta().y));
        break;

      default:
        break;
      }

      for (auto& entry : range)
        AppScripting::raiseEvent(entry.second, args);

      return false;
    }
  };

  std::unique_ptr<EventListener> listener;

  void AppScripting::clearEventHooks() {
    eventHooks.clear();
    if (listener) {
      ui::Manager::getDefault()->removeMessageListener(listener.get());
      listener.reset();
    }
  }

  bool AppScripting::scanScript(const std::string& fullPath) {
    bool supported = false;
    auto extension = base::string_to_lower(base::get_file_extension(fullPath));
    if (extension == "py")
      return true;
    for (auto& entry : script::Engine::getRegistry()) {
      if (entry.second.hasFlag(extension)) {
        supported = true;
        break;
      }
    }

    if (!supported)
      return false;

    std::ifstream file{fullPath};

    if (!file)
      return false;

    std::string line;
    while (std::getline(file, line)) {
      base::trim_string(line, line);
      auto parts = base::split(line, ' ');

      for (auto& part : parts)
        base::trim_string(part, part);

      if (parts.empty() || parts[0].size() < 3 || parts[0].substr(0, 3) != "///")
        break;

      base::trim_string(parts[0].substr(3), parts[0]);
      if (parts[0].empty()) {
        parts.erase(parts.begin());
        if (parts.empty())
          break;
      }

      auto key = base::string_to_lower(parts[0]);
      parts.erase(parts.begin());

      if (key == "listen") {
        if (!listener) {
          listener.reset(new EventListener());
          ui::Manager::getDefault()->addMessageListener(listener.get());
        }
        for (auto& event : parts) {
          eventHooks.insert({event, fullPath});
        }
      }
    }

    return true;
  }

  void AppScripting::initEngine() {
    if (!wasInit) {
      wasInit = true;
      App::instance()->Exit.connect([]{
        engine = nullptr;
      });
    }

    // if there is no engine OR
    // the engine we have doesn't match the default in the registry,
    // inject a new one
    if (!engine || !script::Engine::getRegistry()[""].match(engine.get())) {
        bool printLast = engine && engine->getPrintLastResult();
        engine = inject<script::Engine>();
        if (engine && printLast)
            engine->getPrintLastResult();
    }
  }

  void AppScripting::raiseEvent(const std::string& fileName, const std::vector<script::Value> &event) {
    TaskManager::instance().delayed([=]{
      if ((engine && fileName == previousFileName) || evalFile(fileName)) {
        beginScriptTransaction();
        const bool result = engine->raiseEvent(event);
        finishScriptTransaction(result);
      }
    });
  }

  bool AppScripting::eval(const std::string& code) {
    initEngine();
    if (engine) {
      beginScriptTransaction();
      const bool result = engine->eval(code);
      finishScriptTransaction(result);
      return result;
    }
    inject<script::EngineDelegate>{}->onConsolePrint("No compatible scripting engine.");
    return false;
  }

  bool AppScripting::evalPython(const std::string& code) {
    ResourceFinder moduleFinder(false);
    moduleFinder.includeDataDir("scripting/python/libresprite.py");
    if (!moduleFinder.findFirst()) {
      inject<script::EngineDelegate>{}->onConsolePrint(
        "Python support file was not found.");
      return false;
    }

    std::string python;
    if (const char* configured = std::getenv("LIBRESPRITE_PYTHON"))
      python = configured;
    if (python.empty()) {
      ResourceFinder bundled(false);
      bundled.includeBinDir("python/python.exe");
      if (bundled.findFirst())
        python = bundled.filename();
    }
#ifdef _WIN32
    if (python.empty() && base::is_file("C:/msys64/mingw64/bin/python.exe"))
      python = "C:/msys64/mingw64/bin/python.exe";
#endif
    if (python.empty())
      python = "python";

    static unsigned int sequence = 0;
    const std::string identifier = std::to_string(base::current_tick())+"-"+
                                   std::to_string(++sequence);
    const std::string sourceFile = base::join_path(
      base::get_temp_path(), "libresprite-script-"+identifier+".py");
    const std::string runnerFile = base::join_path(
      base::get_temp_path(), "libresprite-runner-"+identifier+".py");

    {
      std::ofstream source(sourceFile, std::ios::binary | std::ios::trunc);
      source << code;
    }
    {
      std::ofstream runner(runnerFile, std::ios::binary | std::ios::trunc);
      runner
        << "import os,runpy,sys,threading,traceback\n"
        << "sys.dont_write_bytecode=True\n"
        << "sys.path.insert(0," << pythonStringLiteral(base::get_file_path(moduleFinder.filename())) << ")\n"
        << "timeout=threading.Timer(30.0,lambda:os._exit(124))\n"
        << "timeout.daemon=True\n"
        << "timeout.start()\n"
        << "try:\n"
        << "  runpy.run_path(" << pythonStringLiteral(sourceFile) << ",run_name='__main__')\n"
        << "  import libresprite\n"
        << "  timeout.cancel()\n"
        << "  print('LIBRESPRITE_JS:'+libresprite._export_program())\n"
        << "except BaseException:\n"
        << "  timeout.cancel()\n"
        << "  traceback.print_exc()\n"
        << "  sys.exit(1)\n";
    }

#ifdef _WIN32
    // _popen() delegates to cmd.exe. A command that starts with a quoted
    // executable needs one extra outer pair of quotes on Windows.
    const std::string command = "\"\""+python+"\" \""+runnerFile+"\" 2>&1\"";
    FILE* pipe = _popen(command.c_str(), "r");
#else
    const std::string command = "\""+python+"\" \""+runnerFile+"\" 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
      base::delete_file(sourceFile);
      base::delete_file(runnerFile);
      inject<script::EngineDelegate>{}->onConsolePrint(
        "Could not start Python. Set LIBRESPRITE_PYTHON to python.exe.");
      return false;
    }

    std::string output;
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), pipe))
      output += buffer;
#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif
    base::delete_file(sourceFile);
    base::delete_file(runnerFile);

    const std::string marker = "LIBRESPRITE_JS:";
    const auto markerPosition = output.rfind(marker);
    if (exitCode != 0 || markerPosition == std::string::npos) {
      inject<script::EngineDelegate>{}->onConsolePrint(output.c_str());
      return false;
    }

    std::string consoleOutput = output.substr(0, markerPosition);
    base::trim_string(consoleOutput, consoleOutput);
    if (!consoleOutput.empty())
      inject<script::EngineDelegate>{}->onConsolePrint(consoleOutput.c_str());

    const auto encodedStart = markerPosition+marker.size();
    const auto encodedEnd = output.find_first_of("\r\n", encodedStart);
    const std::string encoded = output.substr(
      encodedStart, encodedEnd == std::string::npos ? std::string::npos:
                                                    encodedEnd-encodedStart);
    base::buffer decoded;
    base::decode_base64(encoded, decoded);
    const std::string javascript(decoded.begin(), decoded.end());
    if (javascript.empty()) {
      inject<script::EngineDelegate>{}->onConsolePrint(
        "Python script completed without LibreSprite operations.");
      return true;
    }
    script::Engine::setDefault("js", {"js"});
    return eval(javascript);
  }

  Transaction& AppScripting::transaction(Document* document) {
    if (!document)
      throw std::runtime_error("Script operation requires a document");
    if (transactionDepth <= 0)
      throw std::runtime_error("Script transaction is not active");
    for (auto& entry : scriptTransactions) {
      if (entry.document == document)
        return *entry.transaction;
    }
    UIContext::instance()->setActiveDocument(document);
    scriptTransactions.push_back({
      document,
      std::unique_ptr<Transaction>(new Transaction(
        UIContext::instance(), "Script Execution", ModifyDocument))
    });
    return *scriptTransactions.back().transaction;
  }

  void AppScripting::afterSuccessfulEval(std::function<void()>&& callback) {
    if (transactionDepth > 0)
      successfulEvalCallbacks.emplace_back(std::move(callback));
    else
      callback();
  }

  void AppScripting::afterEval(std::function<void(bool)>&& callback) {
    if (transactionDepth > 0)
      completedEvalCallbacks.emplace_back(std::move(callback));
    else
      callback(true);
  }

  bool AppScripting::requestStop() {
    return engine && engine->requestStop();
  }

  bool AppScripting::isRunning() {
    return engine && engine->isRunning();
  }

  void AppScripting::initMcpBridge() {
    if (mcpTimer)
      return;
    getMcpDirectory();
    mcpTimer.reset(new ui::Timer(100));
    mcpTimer->Tick.connect(&pollMcpBridge);
    mcpTimer->start();
  }

  void AppScripting::exitMcpBridge() {
    if (mcpTimer)
      mcpTimer->stop();
    mcpTimer.reset();
    if (!mcpDir.empty()) {
      const std::string heartbeat = base::join_path(mcpDir, "libresprite.ready");
      if (base::is_file(heartbeat))
        base::delete_file(heartbeat);
    }
  }

  bool AppScripting::evalFile(const std::string& fileName) {
    m_fileName = fileName;
    std::cout << "Reading file " << fileName << std::endl;
    std::ifstream ifs(fileName);
    if (!ifs) {
      std::cout << "Could not open " << fileName << std::endl;
      return false;
    }

    auto extension = base::string_to_lower(base::get_file_extension(fileName));
    const std::string source{std::istreambuf_iterator<char>(ifs),
                             std::istreambuf_iterator<char>()};
    if (extension == "py") {
      AppScripting instance;
      const bool success = instance.evalPython(source);
      if (success)
        previousFileName = fileName;
      return success;
    }
    script::Engine::setDefault(extension, {extension});

    engine = nullptr;

    AppScripting instance;
    if (!instance.eval(source))
      return false;

    beginScriptTransaction();
    const bool initialized = engine->raiseEvent({"init"});
    finishScriptTransaction(initialized);
    if (!initialized)
      return false;

    previousFileName = fileName;
    return true;
  }

  void AppScripting::printLastResult() {
    if(engine)
      engine->printLastResult();
  }

}
