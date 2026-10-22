#pragma once
#include "doc/frame.h"
#include <string>
namespace doc { class FrameTag; class LayerImage; }
namespace app {
class Document; class Transaction;
namespace animation {
doc::FrameTag* appendClip(Document*, Transaction&, const std::string&, int count, int milliseconds, doc::frame_t seed, const doc::FrameTag* source = nullptr);
doc::frame_t insertLayerKey(Document*, Transaction&, doc::LayerImage*, doc::frame_t frame, doc::frame_t first, doc::frame_t last, bool blank, const doc::FrameTag* activeTag);
void drawingKey(Document*, Transaction&, doc::LayerImage*, doc::frame_t frame, doc::frame_t first, doc::frame_t last, bool blank);
void hold(Document*, Transaction&, doc::LayerImage*, doc::frame_t from, doc::frame_t to);
void tweenPosition(Document*, Transaction&, doc::LayerImage*, doc::frame_t from, doc::frame_t to);
}
}
