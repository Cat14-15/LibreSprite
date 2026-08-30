// Aseprite
// Copyright (C) 2001-2016  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/tools/tool_loop_manager.h"

#include "app/context.h"
#include "app/snap_to_grid.h"
#include "app/tools/controller.h"
#include "app/tools/ink.h"
#include "app/tools/intertwine.h"
#include "app/tools/point_shape.h"
#include "app/tools/symmetry.h"
#include "app/tools/tool_loop.h"
#include "doc/image.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include "gfx/region.h"

#include <climits>

namespace app {
namespace tools {

using namespace gfx;
using namespace doc;
using namespace filters;

namespace {

int floorDiv(int value, int divisor)
{
  int quotient = value/divisor;
  if (value%divisor < 0)
    --quotient;
  return quotient;
}

Point pointInsideSnapCell(ToolLoop* loop,
                          const Rect& bounds,
                          int cellX,
                          int cellY)
{
  Point point(bounds.x+cellX*bounds.w+bounds.w/2,
              bounds.y+cellY*bounds.h+bounds.h/2);

  // Center-aligned grids can leave a partial cell at a canvas edge whose
  // mathematical center is outside the sprite. Keep the representative point
  // inside the canvas so the editor never rejects that partial cell.
  if (Sprite* sprite = loop->sprite()) {
    point.x = MID(0, point.x, sprite->width()-1);
    point.y = MID(0, point.y, sprite->height()-1);
  }
  return point;
}

} // anonymous namespace

ToolLoopManager::ToolLoopManager(ToolLoop* toolLoop)
  : m_toolLoop(toolLoop)
  , m_dirtyArea(toolLoop->getDirtyArea())
{
}

ToolLoopManager::~ToolLoopManager()
{
}

bool ToolLoopManager::isCanceled() const
{
 return m_toolLoop->isCanceled();
}

void ToolLoopManager::prepareLoop(const Pointer& pointer)
{
  // Start with no points at all
  m_stroke.reset();

  // Prepare the ink
  m_toolLoop->getInk()->prepareInk(m_toolLoop);
  m_toolLoop->getIntertwine()->prepareIntertwine();
  m_toolLoop->getPointShape()->preparePointShape(m_toolLoop);
}

void ToolLoopManager::notifyToolLoopModifiersChange()
{
  if (isCanceled())
    return;

  if (m_lastPointer.button() != Pointer::None)
    movement(m_lastPointer);
}

void ToolLoopManager::pressButton(const Pointer& pointer)
{
  m_lastPointer = pointer;

  if (isCanceled())
    return;

  // If the user pressed the other mouse button...
  if ((m_toolLoop->getMouseButton() == ToolLoop::Left && pointer.button() == Pointer::Right) ||
      (m_toolLoop->getMouseButton() == ToolLoop::Right && pointer.button() == Pointer::Left)) {
    // Cancel the tool-loop (the destination image should be completelly discarded)
    m_toolLoop->cancel();
    return;
  }

  // Convert the screen point to a sprite point
  Point spritePoint = pointer.point();
  m_toolLoop->setSpeed(Point(0, 0));
  m_oldPoint = spritePoint;
  snapToGrid(spritePoint);

  m_toolLoop->getController()->pressButton(m_stroke, spritePoint, pointer.pressure());

  std::string statusText;
  m_toolLoop->getController()->getStatusBarText(m_stroke, statusText);
  m_toolLoop->updateStatusBar(statusText.c_str());

  doLoopStep(false);
}

bool ToolLoopManager::releaseButton(const Pointer& pointer)
{
  m_lastPointer = pointer;

  if (isCanceled())
    return false;

  Point spritePoint = pointer.point();
  snapToGrid(spritePoint);

  // A release can arrive after the last mouse-move event. Complete every
  // crossed snap cell before ending the stroke so a quick flick has no gap.
  appendSnappedFreehandCells(
    spritePoint,
    (m_stroke.empty() ? pointer.pressure(): m_stroke.lastPoint().pressure));

  bool res = m_toolLoop->getController()->releaseButton(m_stroke, spritePoint);

  if (!res && (m_toolLoop->getInk()->isSelection() || m_toolLoop->getFilled())) {
    m_toolLoop->getInk()->setFinalStep(m_toolLoop, true);
    doLoopStep(true);
    m_toolLoop->getInk()->setFinalStep(m_toolLoop, false);
  }

  return res;
}

void ToolLoopManager::movement(const Pointer& pointer)
{
  m_lastPointer = pointer;

  if (isCanceled())
    return;

  // Convert the screen point to a sprite point
  Point spritePoint = pointer.point();
  // Calculate the speed (new sprite point - old sprite point)
  m_toolLoop->setSpeed(spritePoint - m_oldPoint);
  m_oldPoint = spritePoint;
  snapToGrid(spritePoint);

  if (!appendSnappedFreehandCells(spritePoint, pointer.pressure())) {
    m_toolLoop->getController()->movement(
      m_toolLoop, m_stroke, spritePoint, pointer.pressure());
    doLoopStep(false);
  }

  std::string statusText;
  m_toolLoop->getController()->getStatusBarText(m_stroke, statusText);
  m_toolLoop->updateStatusBar(statusText.c_str());

}

bool ToolLoopManager::appendSnappedFreehandCells(const Point& point,
                                                 float pressure)
{
  if (!m_toolLoop->getSnapToGrid() ||
      !m_toolLoop->getController()->canSnapToGrid() ||
      !m_toolLoop->getController()->isFreehand() ||
      !m_toolLoop->getPointShape()->isGridCell() ||
      m_stroke.empty())
    return false;

  const Rect bounds = m_toolLoop->getSnapBounds();
  if (bounds.w <= 0 || bounds.h <= 0)
    return false;

  const auto& last = m_stroke.lastPoint();
  const Point from(floorDiv(last.x-bounds.x, bounds.w),
                   floorDiv(last.y-bounds.y, bounds.h));
  const Point to(floorDiv(point.x-bounds.x, bounds.w),
                 floorDiv(point.y-bounds.y, bounds.h));

  const auto appendCell = [&](int x, int y) {
    const Point cell = pointInsideSnapCell(m_toolLoop, bounds, x, y);
    m_toolLoop->getController()->movement(
      m_toolLoop, m_stroke, cell, pressure);
    doLoopStep(false);
  };

  // Visit every grid cell touched by the continuous segment. A regular
  // Bresenham line chooses only one side when the pointer crosses a cell
  // corner, which leaves visible holes during quick diagonal strokes.
  int x = from.x;
  int y = from.y;
  const int dx = ABS(to.x-from.x);
  const int dy = ABS(to.y-from.y);
  const int sx = SGN(to.x-from.x);
  const int sy = SGN(to.y-from.y);
  int ix = 0;
  int iy = 0;

  while (ix < dx || iy < dy) {
    const long long decision =
      static_cast<long long>(1+2*ix)*dy-
      static_cast<long long>(1+2*iy)*dx;

    if (decision == 0) {
      const int nextX = x+sx;
      const int nextY = y+sy;
      // The segment touches both side cells at an exact corner. Paint both,
      // then the diagonal destination, so no crossed checker cell is omitted.
      if (ix < dx)
        appendCell(nextX, y);
      if (iy < dy)
        appendCell(x, nextY);
      x = nextX;
      y = nextY;
      ++ix;
      ++iy;
      appendCell(x, y);
    }
    else if (decision < 0) {
      x += sx;
      ++ix;
      appendCell(x, y);
    }
    else {
      y += sy;
      ++iy;
      appendCell(x, y);
    }
  }
  return true;
}

void ToolLoopManager::doLoopStep(bool last_step)
{
  // Original set of points to interwine (original user stroke,
  // relative to sprite origin).
  Stroke main_stroke;
  if (!last_step)
    m_toolLoop->getController()->getStrokeToInterwine(m_stroke, main_stroke);
  else
    main_stroke = m_stroke;

  // Calculate the area to be updated in all document observers.
  Symmetry* symmetry = m_toolLoop->getSymmetry();
  Strokes strokes;
  if (symmetry)
    symmetry->generateStrokes(main_stroke, strokes, m_toolLoop);
  else
    strokes.push_back(main_stroke);

  calculateDirtyArea(strokes);

  // Validate source image area.
  if (m_toolLoop->getInk()->needsSpecialSourceArea()) {
    gfx::Region srcArea;
    m_toolLoop->getInk()->createSpecialSourceArea(m_dirtyArea, srcArea);
    m_toolLoop->validateSrcImage(srcArea);
  }
  else {
    m_toolLoop->validateSrcImage(m_dirtyArea);
  }

  // Invalidate destionation image areas.
  if (m_toolLoop->getTracePolicy() == TracePolicy::Last) {
    // Copy source to destination (reset the previous trace). Useful
    // for tools like Line and Ellipse (we kept the last trace only).
    m_toolLoop->invalidateDstImage();
  }
  else if (m_toolLoop->getTracePolicy() == TracePolicy::AccumulateUpdateLast) {
    // Revalidate only this last dirty area (e.g. pixel-perfect
    // freehand algorithm needs this trace policy to redraw only the
    // last dirty area, which can vary in one pixel from the previous
    // tool loop cycle).
    m_toolLoop->invalidateDstImage(m_dirtyArea);
  }

  m_toolLoop->validateDstImage(m_dirtyArea);

  // Join or fill user points
  if (!m_toolLoop->getFilled() || (!last_step && !m_toolLoop->getPreviewFilled()))
    m_toolLoop->getIntertwine()->joinStroke(m_toolLoop, main_stroke);
  else
    m_toolLoop->getIntertwine()->fillStroke(m_toolLoop, main_stroke);

  if (m_toolLoop->getTracePolicy() == TracePolicy::Overlap) {
    // Copy destination to source (yes, destination to source). In
    // this way each new trace overlaps the previous one.
    m_toolLoop->copyValidDstToSrcImage(m_dirtyArea);
  }

  if (!m_dirtyArea.isEmpty())
    m_toolLoop->updateDirtyArea();
}

// Applies the grid settings to the specified sprite point.
void ToolLoopManager::snapToGrid(Point& point)
{
  if (!m_toolLoop->getController()->canSnapToGrid() ||
      !m_toolLoop->getSnapToGrid())
    return;

  const gfx::Rect bounds = m_toolLoop->getSnapBounds();
  if (bounds.w <= 0 || bounds.h <= 0)
    return;

  if (!m_toolLoop->getPointShape()->isGridCell()) {
    gfx::Rect vertices = bounds;
    vertices.x += vertices.w/2;
    vertices.y += vertices.h/2;
    point = snap_to_grid(vertices, point,
                         PreferSnapTo::ClosestGridVertex);
    return;
  }

  // Select the cell from the actual pointer position. For a partial edge
  // cell, its mathematical center can be outside the canvas, so use an
  // equivalent point clamped inside that same cell.
  const int cellX = floorDiv(point.x-bounds.x, bounds.w);
  const int cellY = floorDiv(point.y-bounds.y, bounds.h);
  point = pointInsideSnapCell(m_toolLoop, bounds, cellX, cellY);
}

// Strokes are relative to sprite origin.
void ToolLoopManager::calculateDirtyArea(const Strokes& strokes)
{
  // Save the current dirty area if it's needed
  Region prevDirtyArea;
  if (m_toolLoop->getTracePolicy() == TracePolicy::Last)
    prevDirtyArea = m_dirtyArea;

  // Start with a fresh dirty area
  m_dirtyArea.clear();

  for (auto& stroke : strokes) {
    gfx::Rect strokeBounds = stroke.bounds();
    if (strokeBounds.isEmpty())
      continue;

    // Expand the dirty-area with the pen width
    Rect r1, r2;

    m_toolLoop->getPointShape()->getModifiedArea(
      m_toolLoop,
      strokeBounds.x,
      strokeBounds.y, r1);

    m_toolLoop->getPointShape()->getModifiedArea(
      m_toolLoop,
      strokeBounds.x+strokeBounds.w-1,
      strokeBounds.y+strokeBounds.h-1, r2);

    m_dirtyArea.createUnion(m_dirtyArea, Region(r1.createUnion(r2)));
  }

  // Merge new dirty area with the previous one (for tools like line
  // or rectangle it's needed to redraw the previous position and
  // the new one)
  if (m_toolLoop->getTracePolicy() == TracePolicy::Last)
    m_dirtyArea.createUnion(m_dirtyArea, prevDirtyArea);

  // Apply tiled mode
  TiledMode tiledMode = m_toolLoop->getTiledMode();
  if (tiledMode != TiledMode::NONE) {
    int w = m_toolLoop->sprite()->width();
    int h = m_toolLoop->sprite()->height();
    Region sprite_area(Rect(0, 0, w, h));
    Region outside;
    outside.createSubtraction(m_dirtyArea, sprite_area);

    switch (tiledMode) {
      case TiledMode::X_AXIS:
        outside.createIntersection(outside, Region(Rect(-w*10000, 0, w*20000, h)));
        break;
      case TiledMode::Y_AXIS:
        outside.createIntersection(outside, Region(Rect(0, -h*10000, w, h*20000)));
        break;
    }

    Rect outsideBounds = outside.bounds();
    if (outsideBounds.x < 0) outside.offset(w * (1+((-outsideBounds.x) / w)), 0);
    if (outsideBounds.y < 0) outside.offset(0, h * (1+((-outsideBounds.y) / h)));
    int x1 = outside.bounds().x;

    while (true) {
      Region in_sprite;
      in_sprite.createIntersection(outside, sprite_area);
      outside.createSubtraction(outside, in_sprite);
      m_dirtyArea.createUnion(m_dirtyArea, in_sprite);

      outsideBounds = outside.bounds();
      if (outsideBounds.isEmpty())
        break;
      else if (outsideBounds.x+outsideBounds.w > w)
        outside.offset(-w, 0);
      else if (outsideBounds.y+outsideBounds.h > h)
        outside.offset(x1-outsideBounds.x, -h);
      else
        break;
    }
  }
}

} // namespace tools
} // namespace app
