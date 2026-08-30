// Aseprite
// Copyright (C) 2001-2016  David Capello
// Copyright (C) 2021 LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#pragma once

#include "base/injection.h"

#include <functional>
#include <vector>

namespace script {
    class Engine;
    class Value;
};

namespace app {

  class Document;
  class Transaction;

  class AppScripting {
    void initEngine();
    static std::string m_fileName;

  public:
    static const std::string& getFileName() {return m_fileName;}
    static bool evalFile(const std::string& fileName);
    static void raiseEvent(const std::string& fileName, const std::vector<script::Value>& event);
    static bool scanScript(const std::string& fullPath);
    static void clearEventHooks();

    // All script mutations share one transaction per document. Successful
    // evaluation commits them as one undo step; errors roll everything back.
    static Transaction& transaction(Document* document);
    static void afterSuccessfulEval(std::function<void()>&& callback);
    static void afterEval(std::function<void(bool)>&& callback);
    static bool requestStop();
    static bool isRunning();
    static void initMcpBridge();
    static void exitMcpBridge();

    bool eval(const std::string& code);
    bool evalPython(const std::string& code);
    void printLastResult();
  };

} // namespace app
