// LibreSprite Scripting Library
// Copyright (C) 2021-2026  LibreSprite contributors
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if SCRIPT_ENGINE_V8

#include "base/convert_to.h"
#include "base/exception.h"
#include "base/fs.h"
#include "base/memory.h"
#include "script/engine.h"
#include "script/engine_delegate.h"
#include "script/value.h"

#include "app/resource_finder.h"
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <iostream>
#include <string>
#include <unordered_map>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <v8.h>
#include <libplatform/libplatform.h>

using namespace script;

template<typename Inner>
v8::Local<Inner> ToLocal(v8::Local<Inner> thing) {
  return thing;
}

template<typename Inner>
v8::Local<Inner> ToLocal(v8::MaybeLocal<Inner> thing) {
  if (thing.IsEmpty()) return {};
  return thing.ToLocalChecked();
}

template<typename T>
void Check(const T&){}

static v8::Local<v8::Value> returnValue(v8::Isolate* isolate, const Value& value);

class V8Engine : public Engine {
public:
  inject<EngineDelegate> m_delegate;
  v8::Global<v8::Context> m_context;
  v8::Isolate* m_isolate = nullptr;
  std::atomic_bool m_running{false};
  std::atomic_bool m_stopRequested{false};
  std::atomic_bool m_timedOut{false};
  std::atomic_bool m_escapeStopped{false};

  class ExecutionGuard {
  public:
    explicit ExecutionGuard(V8Engine& engine)
      : m_engine(engine) {
      m_engine.m_stopRequested = false;
      m_engine.m_timedOut = false;
      m_engine.m_escapeStopped = false;
      m_engine.m_running = true;
      m_watchdog = std::thread([this] { watch(); });
    }

    ~ExecutionGuard() {
      finish();
    }

    void finish() {
      if (m_finished)
        return;
      m_finished = true;
      m_engine.m_running = false;
      if (m_watchdog.joinable())
        m_watchdog.join();
      if (m_engine.m_isolate->IsExecutionTerminating())
        m_engine.m_isolate->CancelTerminateExecution();
    }

    bool stopped() const {
      return m_engine.m_stopRequested ||
             m_engine.m_timedOut ||
             m_engine.m_escapeStopped;
    }

  private:
    void watch() {
      int timeoutMs = 30000;
      if (const char* configured = std::getenv("LIBRESPRITE_SCRIPT_TIMEOUT_MS"))
        timeoutMs = std::max(1000, std::atoi(configured));
      const auto deadline = std::chrono::steady_clock::now()+
                            std::chrono::milliseconds(timeoutMs);
      while (m_engine.m_running) {
        if (m_engine.m_stopRequested) {
          m_engine.m_isolate->TerminateExecution();
          return;
        }
#ifdef _WIN32
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
          m_engine.m_escapeStopped = true;
          m_engine.m_isolate->TerminateExecution();
          return;
        }
#endif
        if (std::chrono::steady_clock::now() >= deadline) {
          m_engine.m_timedOut = true;
          m_engine.m_isolate->TerminateExecution();
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }

    V8Engine& m_engine;
    std::thread m_watchdog;
    bool m_finished = false;
  };

  V8Engine() {
    InternalScriptObject::setDefault("V8ScriptObject");
    initV8();
  }

  v8::Local<v8::Context> context() { return m_context.Get(m_isolate); }

  bool requestStop() override {
    if (!m_running)
      return false;
    m_stopRequested = true;
    m_isolate->TerminateExecution();
    return true;
  }

  bool isRunning() const override { return m_running; }

  void reportTermination() {
    if (m_timedOut)
      m_delegate->onConsolePrint("Script stopped: execution time limit reached.");
    else if (m_escapeStopped)
      m_delegate->onConsolePrint("Script stopped by Escape key.");
    else
      m_delegate->onConsolePrint("Script stopped by user.");
  }

  void initV8() {
    static std::unique_ptr<v8::Platform> m_platform;
    if (!m_platform) {
      app::ResourceFinder rf;
      rf.includeBinDir("snapshot_blob.bin");
      if (rf.findFirst()) {
	v8::V8::InitializeExternalStartupData(rf.filename().c_str());
      } else {
	// When snapshot_blob.bin is not available, try to initialize from default location
	// If that fails, V8 will use slower initialization without snapshot
	v8::V8::InitializeExternalStartupData(nullptr);
      }
      
      v8::V8::InitializeICU();

      m_platform = v8::platform::NewDefaultPlatform();
      v8::V8::InitializePlatform(m_platform.get());
      v8::V8::Initialize();
    }

    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    m_isolate = v8::Isolate::New(params);

    v8::Isolate::Scope isolatescope(m_isolate);
    v8::HandleScope handle_scope(m_isolate);
    m_context = v8::Global<v8::Context>(m_isolate, v8::Context::New(m_isolate));
  }

    bool raiseEvent(const std::vector<script::Value>& event) override {
    // return eval("if (typeof onEvent === \"function\") onEvent(\"" + event + "\");");
    bool success = true;
    ExecutionGuard execution(*this);
    try {
      v8::Isolate::Scope isolatescope(m_isolate);
      // Create a stack-allocated handle scope.
      v8::HandleScope handle_scope(m_isolate);

      // Enter the context for compiling and running the hello world script.
      v8::Context::Scope context_scope(context());

      v8::TryCatch trycatch(m_isolate);

      auto global = context()->Global();

      auto onEvent = ToLocal(global->Get(context(), ToLocal(v8::String::NewFromUtf8(m_isolate, "onEvent"))));
      if (!onEvent.IsEmpty() && onEvent->IsFunction()) {
        std::vector<v8::Local<v8::Value>> argv;
        argv.reserve(event.size());
        for (auto& arg : event) {
          argv.emplace_back(returnValue(m_isolate, arg));
        }
        Check(onEvent.As<v8::Function>()->Call(context(), global, event.size(), argv.data()));
      }

      if (trycatch.HasTerminated()) {
        reportTermination();
        success = false;
      }
      else if (trycatch.HasCaught()) {
        v8::Local<v8::Value> exception = trycatch.Exception();
        auto trace = trycatch.StackTrace(context());

        v8::String::Utf8Value utf8(m_isolate, exception);

        if (!trace.IsEmpty()){
          v8::String::Utf8Value utf8Trace(m_isolate, ToLocal(trace));
          m_delegate->onConsolePrint(*utf8Trace);
        } else {
          m_delegate->onConsolePrint(*utf8);
        }
        success = false;
      }

    } catch (const std::exception& ex) {
      m_delegate->onConsolePrint(ex.what());
      success = false;
    }
    execution.finish();
    if (success && execution.stopped()) {
      reportTermination();
      success = false;
    }
    execAfterEval(success);
    return success;
  }

  bool eval(const std::string& code) override {
    bool success = true;
    ExecutionGuard execution(*this);
    try {
      v8::Isolate::Scope isolatescope(m_isolate);
      // Create a stack-allocated handle scope.
      v8::HandleScope handle_scope(m_isolate);

      // Enter the context for compiling and running the hello world script.
      v8::Context::Scope context_scope(context());

      v8::TryCatch trycatch(m_isolate);

      initGlobals();

      // Create a string containing the JavaScript source code.
      v8::Local<v8::String> source = ToLocal(v8::String::NewFromUtf8(m_isolate, code.c_str()));

      // Compile the source code.
      v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context(), source);
      // Run the script to get the result.
      v8::MaybeLocal<v8::Value> result;
      if (!script.IsEmpty()) {
          result = ToLocal(script)->Run(context());
      }

      if (result.IsEmpty()) {
        if (trycatch.HasTerminated()) {
          reportTermination();
          success = false;
        }
        else if (trycatch.HasCaught()) {
          v8::Local<v8::Value> exception = trycatch.Exception();
          auto trace = trycatch.StackTrace(context());

          v8::String::Utf8Value utf8(m_isolate, exception);
          m_delegate->onConsolePrint(*utf8);

          if (!trace.IsEmpty()){
            v8::String::Utf8Value utf8Trace(m_isolate, ToLocal(trace));
            m_delegate->onConsolePrint(*utf8Trace);
          }

          std::cout << "Error: [" << *utf8 << "]" << std::endl;
          success = false;
        }
      } else if (m_printLastResult) {
        v8::String::Utf8Value utf8(m_isolate, result.ToLocalChecked());
        m_delegate->onConsolePrint(*utf8);
      }
      if (!m_isolate->IsExecutionTerminating())
        m_isolate->LowMemoryNotification();
    } catch (const std::exception& ex) {
      std::string err = "Error: ";
      err += ex.what();
      m_delegate->onConsolePrint(err.c_str());
      success = false;
      std::cout << "Error: [" << err << "]" << std::endl;
    }
    execution.finish();
    if (success && execution.stopped()) {
      reportTermination();
      success = false;
    }
    execAfterEval(success);
    return success;
  }
};

static Engine::Regular<V8Engine> registration("js", {"js"});

class V8ScriptObject : public InternalScriptObject {
  v8::Persistent<v8::Object> m_local;
public:

  ~V8ScriptObject() {
    if (!m_local.IsEmpty()) {
      m_local.ClearWeak();
      m_local.Reset();
    }
  }

  static Value getValue(v8::Isolate *isolate, v8::Local<v8::Value> local) {
    if (local->IsNullOrUndefined())
      return {};

    if (local->IsString()) {
      v8::String::Utf8Value utf8(isolate, local);
      return {*utf8};
    }

    if (local->IsNumber())
      return local.As<v8::Number>()->Value();

    if (local->IsUint32())
      return local.As<v8::Uint32>()->Value();

    if (local->IsInt32())
      return local.As<v8::Int32>()->Value();

    if (local->IsBoolean())
      return local.As<v8::Boolean>()->Value();

    if (local->IsUint8Array()){
      auto array = local.As<v8::Uint8Array>();
#if V8_MAJOR_VERSION > 7
      auto store = array->Buffer()->GetBackingStore();
#else
      auto storeObj = array->Buffer()->GetContents();
      auto store = &storeObj;
#endif
      return {
        store->Data(),
        store->ByteLength(),
        false
      };
    }

    v8::String::Utf8Value utf8(isolate, local->TypeOf(isolate));
    printf("Unknown type: [%s]\n", *utf8);

    return {};
  }

  static void callFunc(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    auto data = args.Data().As<v8::External>();
    auto& func = *reinterpret_cast<script::Function*>(data->Value());

    for (int i = 0; i < args.Length(); i++) {
      func.arguments.push_back(getValue(isolate, args[i]));
    }

    func.result.makeUndefined();

    try {
      func();
    } catch (const ObjectDestroyedException&) {
      std::cout << "Object Destroyed Exception" << std::endl;
    }
    args.GetReturnValue().Set(returnValue(isolate, func.result));
  }

  void pushFunctions(v8::Local<v8::Object>& object) {
    auto isolate = m_engine.get<V8Engine>()->m_isolate;
    auto context = m_engine.get<V8Engine>()->context();
    for (auto& entry : functions) {
      auto tpl = v8::FunctionTemplate::New(isolate, callFunc, v8::External::New(isolate, &entry.second));
      auto func = tpl->GetFunction(context).ToLocalChecked();
      Check(object->Set(context,
                  ToLocal(v8::String::NewFromUtf8(isolate, entry.first.c_str())),
                  func));
    }
  }

  void pushProperties(v8::Local<v8::Object>& object) {
    auto& isolate = m_engine.get<V8Engine>()->m_isolate;
    auto context = m_engine.get<V8Engine>()->context();

    for (auto& entry : properties) {
      auto getterTpl = v8::FunctionTemplate::New(isolate, callFunc, v8::External::New(isolate, &entry.second.getter));
      auto getter = getterTpl->GetFunction(context).ToLocalChecked();

      auto setterTpl = v8::FunctionTemplate::New(isolate, callFunc, v8::External::New(isolate, &entry.second.setter));
      auto setter = setterTpl->GetFunction(context).ToLocalChecked();

      v8::PropertyDescriptor descriptor(getter, setter);
      Check(object->DefineProperty(context,
                             ToLocal(v8::String::NewFromUtf8(isolate, entry.first.c_str())),
                             descriptor));
    }
  }

  v8::Local<v8::Object> makeLocal() {
    auto isolate = m_engine.get<V8Engine>()->m_isolate;
    if (!m_local.IsEmpty())
      return m_local.Get(isolate);
    auto local = v8::Object::New(isolate);
    pushFunctions(local);
    pushProperties(local);
    m_local.Reset(isolate, local);
    m_local.SetWeak(this, [](const auto& info) {
      reinterpret_cast<V8ScriptObject*>(info.GetParameter())->release();
    }, v8::WeakCallbackType::kParameter);
    return local;
  }

  void release() {
    m_local.ClearWeak();
    m_local.Reset();
    if (onRelease) {
      auto cb = std::move(onRelease);
      onRelease = nullptr;
      cb();
    }
  }

  void makeGlobal(const std::string& name) override {
    auto& isolate = m_engine.get<V8Engine>()->m_isolate;
    auto context = m_engine.get<V8Engine>()->context();
    Check(context->Global()->Set(context,
                                 ToLocal(v8::String::NewFromUtf8(isolate, name.c_str())),
                                 makeLocal()));
  }
};

static v8::Local<v8::Value> returnValue(v8::Isolate* isolate, const Value& value) {
    switch (value.type) {
    case Value::Type::UNDEFINED:
        return v8::Undefined(isolate);

    case Value::Type::INT:
        return v8::Int32::New(isolate, value);

    case Value::Type::DOUBLE:
        return v8::Number::New(isolate, value);

    case Value::Type::STRING:
        return ToLocal(v8::String::NewFromUtf8(isolate, value));

    case Value::Type::OBJECT:
        if (auto object = static_cast<ScriptObject*>(value)) {
            return static_cast<V8ScriptObject*>(object->getInternalScriptObject())->makeLocal();
        }
        return v8::Undefined(isolate);

    case Value::Type::BUFFER: {
        auto& buffer = value.buffer();
        if (buffer.canSteal()) {
#if V8_MAJOR_VERSION > 7
            auto store = v8::ArrayBuffer::NewBackingStore(
                buffer.steal(),
                buffer.size(),
                +[](void* data, size_t length, void* deleter_data){
                    delete[] static_cast<uint8_t*>(data);
                },
                nullptr
                );
            auto arrayBuffer = v8::ArrayBuffer::New(isolate, std::move(store));
            return v8::Uint8Array::New(arrayBuffer, 0, buffer.size());
#else
            auto arrayBuffer = v8::ArrayBuffer::New(isolate, buffer.steal(), buffer.size());
#endif
            return v8::Uint8Array::New(arrayBuffer, 0, buffer.size());

        } else {
            auto arrayBuffer = v8::ArrayBuffer::New(isolate, buffer.size());
#if V8_MAJOR_VERSION > 7
            std::memcpy(arrayBuffer->GetBackingStore()->Data(), buffer.data(), buffer.size());
#else
            std::memcpy(arrayBuffer->GetContents().Data(), buffer.data(), buffer.size());
#endif
            return v8::Uint8Array::New(arrayBuffer, 0, buffer.size());
        }
    }

    default:
        printf("Unknown return type: %d\n", int(value.type));
        break;
    }
    return {};
}

static InternalScriptObject::Regular<V8ScriptObject> v8SO("V8ScriptObject");

#endif
