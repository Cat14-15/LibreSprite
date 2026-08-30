// LibreSprite
// Copyright (C) 2021  LibreSprite contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#include "base/base64.h"
#include "app/document.h"
#include "app/cmd/copy_rect.h"
#include "app/script/app_scripting.h"
#include "app/transaction.h"
#include "app/ui_context.h"
#include "script/script_object.h"
#include "script/engine.h"
#include "doc/image.h"
#include "doc/color.h"
#include "doc/image_ref.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "gfx/clip.h"
#include "gfx/point.h"
#include "she/surface.h"
#include "she/system.h"
#include "ui/manager.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <vector>

class ImageScriptObject : public script::ScriptObject {
public:
  ImageScriptObject() {
    addProperty("width", [this]{return img()->width();})
      .doc("read-only. The width of the image.");

    addProperty("height", [this]{return img()->height();})
      .doc("read-only. The height of the image.");

    addProperty("stride", [this]{return img()->getRowStrideSize();})
      .doc("read-only. The number of bytes per image row.");

    addProperty("format", [this]{return (int) img()->pixelFormat();})
      .doc("read-only. The PixelFormat of the image.");

    addFunction("getPixel", [this](int x, int y){return img()->getPixel(x, y);})
      .doc("reads a color from the given coordinate of the image.")
      .docArg("x", "integer")
      .docArg("y", "integer")
      .docReturns("a color value");

    addMethod("putPixel", &ImageScriptObject::putPixel)
      .doc("writes the color onto the image at the the given coordinate.")
      .docArg("x", "integer")
      .docArg("y", "integer")
      .docArg("color", "a 32-bit color in 8888 RGBA format.");

    addMethod("clear", &ImageScriptObject::clear)
      .doc("clears the image with the specified color.")
      .docArg("color", "a 32-bit color in 8888 RGBA format.");

    addMethod("drawLine", &ImageScriptObject::drawLine)
      .doc("Draws a one-pixel Bresenham line clipped to the image.")
      .docArg("x1", "Start X coordinate.")
      .docArg("y1", "Start Y coordinate.")
      .docArg("x2", "End X coordinate.")
      .docArg("y2", "End Y coordinate.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("fillRect", &ImageScriptObject::fillRect)
      .doc("Fills a rectangle clipped to the image.")
      .docArg("x", "Left coordinate.")
      .docArg("y", "Top coordinate.")
      .docArg("width", "Rectangle width.")
      .docArg("height", "Rectangle height.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("drawRect", &ImageScriptObject::drawRect)
      .doc("Draws a one-pixel rectangle outline clipped to the image.")
      .docArg("x", "Left coordinate.")
      .docArg("y", "Top coordinate.")
      .docArg("width", "Rectangle width.")
      .docArg("height", "Rectangle height.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("drawEllipse", &ImageScriptObject::drawEllipse)
      .doc("Draws a one-pixel ellipse outline inside a bounding rectangle.")
      .docArg("x", "Left coordinate.")
      .docArg("y", "Top coordinate.")
      .docArg("width", "Ellipse width.")
      .docArg("height", "Ellipse height.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("fillEllipse", &ImageScriptObject::fillEllipse)
      .doc("Fills an ellipse inside a bounding rectangle.")
      .docArg("x", "Left coordinate.")
      .docArg("y", "Top coordinate.")
      .docArg("width", "Ellipse width.")
      .docArg("height", "Ellipse height.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("drawCircle", &ImageScriptObject::drawCircle)
      .doc("Draws a one-pixel circle outline.")
      .docArg("centerX", "Circle center X.")
      .docArg("centerY", "Circle center Y.")
      .docArg("radius", "Circle radius.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("fillCircle", &ImageScriptObject::fillCircle)
      .doc("Fills a circle.")
      .docArg("centerX", "Circle center X.")
      .docArg("centerY", "Circle center Y.")
      .docArg("radius", "Circle radius.")
      .docArg("color", "Pixel color or palette index.");

    addMethod("floodFill", &ImageScriptObject::floodFill)
      .doc("Fills the connected region containing the start pixel.")
      .docArg("x", "Start X coordinate.")
      .docArg("y", "Start Y coordinate.")
      .docArg("color", "Replacement pixel color or palette index.");

    addMethod("copyRect", &ImageScriptObject::copyRect)
      .doc("Copies a rectangular region within this image; overlapping regions are safe.")
      .docArg("sourceX", "Source left coordinate.")
      .docArg("sourceY", "Source top coordinate.")
      .docArg("width", "Region width.")
      .docArg("height", "Region height.")
      .docArg("destinationX", "Destination left coordinate.")
      .docArg("destinationY", "Destination top coordinate.");

    addMethod("flipHorizontal", &ImageScriptObject::flipHorizontal)
      .doc("Flips all image pixels horizontally.");

    addMethod("flipVertical", &ImageScriptObject::flipVertical)
      .doc("Flips all image pixels vertically.");

    addMethod("fillGradient", &ImageScriptObject::fillGradient)
      .doc("Fills the image with an RGBA gradient. Indexed images use the start color.")
      .docArg("startColor", "RGBA start color.")
      .docArg("endColor", "RGBA end color.")
      .docArg("vertical", "True for top-to-bottom, false for left-to-right.");

    addMethod("putImageData", &ImageScriptObject::putImageData)
      .doc("writes the given pixels onto the image. Must be the same size as the image.")
      .docArg("data", "All of the pixels in the image.");

    addMethod("getImageData", &ImageScriptObject::getImageData)
      .doc("creates an array containing all of the image's pixels.")
      .docReturns("All pixels in a Uint8Array");

    addMethod("getPNGData", &ImageScriptObject::getPNGData)
      .doc("Encodes the image as a PNG.")
      .docReturns("The image as a Base64-encoded PNG string.");
  }

  doc::Image* img() {
    auto img = handle<doc::Object, doc::Image>();
    if (!img)
      throw script::ObjectDestroyedException{};
    return img;
  }

  void putImageData(script::Value::Buffer& data) {
    auto image = img();
    if (data.size() != std::size_t(image->getRowStrideSize()*image->height())) {
      std::cout << "Data size mismatch: " << data.size() << std::endl;
      return;
    }
    prepareMutation();
    std::memcpy(image->getPixelAddress(0, 0), data.data(), data.size());
    modify();
  }

  script::Value getImageData() {
    return {
      img()->getPixelAddress(0, 0),
      std::size_t(img()->getRowStrideSize()*img()->height()),
      false
    };
  }

  std::string getPNGData() {
    auto w = img()->width();
    auto h = img()->height();
    std::shared_ptr<she::Surface> surface{
      she::instance()->createRgbaSurface(w, h),
      [](she::Surface* s) {s->dispose();}
    };
    if (!surface)
      return "";

    for (auto y = 0; y < h; ++y) {
      for (auto x = 0; x < w; ++x) {
        surface->putPixel(img()->getPixel(x, y), x, y);
      }
    }

    std::string encoded;
    base::encode_base64(she::instance()->encodeSurfaceAsPNG(surface.get()), encoded);
    return "data:image/png;base64," + encoded;
  }

  void putPixel(int x, int y, int color) {
    if (unsigned(x) < unsigned(img()->width()) && unsigned(y) < unsigned(img()->height())) {
      prepareMutation();
      img()->putPixel(x, y, color);
      modify();
    }
  }

  void drawLine(int x1, int y1, int x2, int y2, int color) {
    const int dx = std::abs(x2-x1);
    const int sx = (x1 < x2 ? 1: -1);
    const int dy = -std::abs(y2-y1);
    const int sy = (y1 < y2 ? 1: -1);
    int error = dx+dy;
    for (;;) {
      putPixel(x1, y1, color);
      if (x1 == x2 && y1 == y2)
        break;
      const int twiceError = 2*error;
      if (twiceError >= dy) { error += dy; x1 += sx; }
      if (twiceError <= dx) { error += dx; y1 += sy; }
    }
    modify();
  }

  void fillRect(int x, int y, int width, int height, int color) {
    if (width <= 0 || height <= 0)
      return;
    const int x1 = std::max(0, x);
    const int y1 = std::max(0, y);
    const int x2 = std::min(img()->width(), x+width);
    const int y2 = std::min(img()->height(), y+height);
    prepareMutation();
    for (int py=y1; py<y2; ++py)
      for (int px=x1; px<x2; ++px)
        img()->putPixel(px, py, color);
    modify();
  }

  void drawRect(int x, int y, int width, int height, int color) {
    if (width <= 0 || height <= 0)
      return;
    drawLine(x, y, x+width-1, y, color);
    if (height > 1)
      drawLine(x, y+height-1, x+width-1, y+height-1, color);
    if (height > 2) {
      drawLine(x, y+1, x, y+height-2, color);
      if (width > 1)
        drawLine(x+width-1, y+1, x+width-1, y+height-2, color);
    }
    modify();
  }

  void drawEllipse(int x, int y, int width, int height, int color) {
    if (width <= 0 || height <= 0)
      return;
    const double rx = width/2.0;
    const double ry = height/2.0;
    const double cx = x+(width-1)/2.0;
    const double cy = y+(height-1)/2.0;
    const int steps = std::max(12, int(6.283185307179586*std::max(rx, ry)*2));
    int previousX = int(std::lround(cx+rx));
    int previousY = int(std::lround(cy));
    for (int i=1; i<=steps; ++i) {
      const double angle = 6.283185307179586*double(i)/double(steps);
      const int nextX = int(std::lround(cx+rx*std::cos(angle)));
      const int nextY = int(std::lround(cy+ry*std::sin(angle)));
      drawLine(previousX, previousY, nextX, nextY, color);
      previousX = nextX;
      previousY = nextY;
    }
    modify();
  }

  void fillEllipse(int x, int y, int width, int height, int color) {
    if (width <= 0 || height <= 0)
      return;
    const double rx = width/2.0;
    const double ry = height/2.0;
    const double cx = x+(width-1)/2.0;
    const double cy = y+(height-1)/2.0;
    prepareMutation();
    for (int py=y; py<y+height; ++py) {
      const double normalizedY = (double(py)-cy)/ry;
      const double inside = std::max(0.0, 1.0-normalizedY*normalizedY);
      const double halfWidth = rx*std::sqrt(inside);
      const int left = int(std::ceil(cx-halfWidth));
      const int right = int(std::floor(cx+halfWidth));
      for (int px=left; px<=right; ++px)
        if (unsigned(px) < unsigned(img()->width()) &&
            unsigned(py) < unsigned(img()->height()))
          img()->putPixel(px, py, color);
    }
    modify();
  }

  void drawCircle(int centerX, int centerY, int radius, int color) {
    if (radius < 0)
      return;
    drawEllipse(centerX-radius, centerY-radius,
                radius*2+1, radius*2+1, color);
  }

  void fillCircle(int centerX, int centerY, int radius, int color) {
    if (radius < 0)
      return;
    fillEllipse(centerX-radius, centerY-radius,
                radius*2+1, radius*2+1, color);
  }

  void floodFill(int x, int y, int color) {
    if (unsigned(x) >= unsigned(img()->width()) ||
        unsigned(y) >= unsigned(img()->height()))
      return;
    const int source = img()->getPixel(x, y);
    if (source == color)
      return;
    prepareMutation();
    std::vector<gfx::Point> pending(1, gfx::Point(x, y));
    img()->putPixel(x, y, color);
    while (!pending.empty()) {
      const gfx::Point point = pending.back();
      pending.pop_back();
      const gfx::Point neighbors[] = {
        gfx::Point(point.x-1, point.y), gfx::Point(point.x+1, point.y),
        gfx::Point(point.x, point.y-1), gfx::Point(point.x, point.y+1)
      };
      for (const auto& next : neighbors) {
        if (unsigned(next.x) < unsigned(img()->width()) &&
            unsigned(next.y) < unsigned(img()->height()) &&
            img()->getPixel(next.x, next.y) == source) {
          img()->putPixel(next.x, next.y, color);
          pending.push_back(next);
        }
      }
    }
    modify();
  }

  void copyRect(int sourceX, int sourceY, int width, int height,
                int destinationX, int destinationY) {
    if (width <= 0 || height <= 0)
      return;
    std::vector<int> pixels(std::size_t(width)*std::size_t(height), 0);
    std::vector<bool> valid(pixels.size(), false);
    for (int y=0; y<height; ++y)
      for (int x=0; x<width; ++x) {
        const int sx = sourceX+x, sy = sourceY+y;
        const auto index = std::size_t(y)*width+x;
        if (unsigned(sx) < unsigned(img()->width()) &&
            unsigned(sy) < unsigned(img()->height())) {
          pixels[index] = img()->getPixel(sx, sy);
          valid[index] = true;
        }
      }
    prepareMutation();
    for (int y=0; y<height; ++y)
      for (int x=0; x<width; ++x) {
        const int dx = destinationX+x, dy = destinationY+y;
        const auto index = std::size_t(y)*width+x;
        if (valid[index] && unsigned(dx) < unsigned(img()->width()) &&
            unsigned(dy) < unsigned(img()->height()))
          img()->putPixel(dx, dy, pixels[index]);
      }
    modify();
  }

  void flipHorizontal() {
    prepareMutation();
    for (int y=0; y<img()->height(); ++y)
      for (int x=0; x<img()->width()/2; ++x) {
        const int opposite = img()->width()-1-x;
        const int value = img()->getPixel(x, y);
        img()->putPixel(x, y, img()->getPixel(opposite, y));
        img()->putPixel(opposite, y, value);
      }
    modify();
  }

  void flipVertical() {
    prepareMutation();
    for (int y=0; y<img()->height()/2; ++y)
      for (int x=0; x<img()->width(); ++x) {
        const int opposite = img()->height()-1-y;
        const int value = img()->getPixel(x, y);
        img()->putPixel(x, y, img()->getPixel(x, opposite));
        img()->putPixel(x, opposite, value);
      }
    modify();
  }

  void fillGradient(int startColor, int endColor, bool vertical) {
    if (img()->pixelFormat() != doc::IMAGE_RGB) {
      clear(startColor);
      return;
    }
    prepareMutation();
    const int length = std::max(1, (vertical ? img()->height() : img()->width())-1);
    for (int y=0; y<img()->height(); ++y)
      for (int x=0; x<img()->width(); ++x) {
        const int position = vertical ? y : x;
        const auto mix = [=](int a, int b) { return (a*(length-position)+b*position)/length; };
        img()->putPixel(x, y, doc::rgba(
          mix(doc::rgba_getr(startColor), doc::rgba_getr(endColor)),
          mix(doc::rgba_getg(startColor), doc::rgba_getg(endColor)),
          mix(doc::rgba_getb(startColor), doc::rgba_getb(endColor)),
          mix(doc::rgba_geta(startColor), doc::rgba_geta(endColor))));
      }
    modify();
  }

  void modify() {
    if (m_updatePending)
      return;
    m_updatePending = true;
    img()->incrementVersion();
    getEngine()->afterEval([this](bool) {
      if (auto* document = app::UIContext::instance()->activeDocument())
        document->notifyGeneralUpdate();
      if (ui::Manager::getDefault())
        ui::Manager::getDefault()->invalidate();
      m_updatePending = false;
    });
  }

  void prepareMutation() {
    if (m_before)
      return;

    m_before.reset(doc::Image::createCopy(img()));
    m_document = app::UIContext::instance()->activeDocument();
    if (m_document && !m_document->sprite()->getImageRef(img()->id()))
      m_document = nullptr;

    getEngine()->afterEval([this](bool success) {
      if (!m_before)
        return;
      doc::ImageRef after(doc::Image::createCopy(img()));
      doc::copy_image(img(), m_before.get());
      img()->incrementVersion();

      if (success) {
        if (m_document) {
          app::AppScripting::transaction(m_document).execute(
            new app::cmd::CopyRect(
              img(), after.get(),
              gfx::Clip(0, 0, 0, 0, img()->width(), img()->height())));
        }
        else {
          doc::copy_image(img(), after.get());
          img()->incrementVersion();
        }
      }

      if (m_document)
        m_document->notifyGeneralUpdate();
      if (ui::Manager::getDefault())
        ui::Manager::getDefault()->invalidate();
      m_before.reset();
      m_document = nullptr;
      m_updatePending = false;
    });
  }

  void clear(int color) {
    prepareMutation();
    img()->clear(color);
    modify();
  }

  bool m_updatePending = false;
  doc::ImageRef m_before;
  app::Document* m_document = nullptr;
};

static script::ScriptObject::Regular<ImageScriptObject> imageSO(typeid(doc::Image*).name());
