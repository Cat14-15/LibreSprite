#include "app/animation_workflow.h"
#include "app/document.h"
#include "app/document_api.h"
#include "app/transaction.h"
#include "app/cmd/add_frame_tag.h"
#include "app/cmd/remove_cel.h"
#include "app/cmd/set_frame_tag_range.h"
#include "app/cmd/unlink_cel.h"
#include "doc/cel.h"
#include "doc/frame_tag.h"
#include "doc/frame_tags.h"
#include "doc/image.h"
#include "doc/layer.h"
#include "doc/primitives.h"
#include "doc/sprite.h"
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace app { namespace animation {
using namespace doc;
static void check(Document* document, LayerImage* layer, frame_t from, frame_t to) {
  if (!document || !layer || layer->sprite()!=document->sprite() || !layer->isEditable() || layer->isBackground())
    throw std::invalid_argument("Select an unlocked image layer (not Background).");
  if (from<0 || to<from || to>=document->sprite()->totalFrames())
    throw std::invalid_argument("Select a valid frame range.");
}
FrameTag* appendClip(Document* document, Transaction& tx, const std::string& requested, int count, int ms, frame_t seed, const FrameTag* source) {
  Sprite* sprite=document->sprite();
  if (requested.empty() || count<1 || count>1000 || ms<1 || ms>65535 || seed<0 || seed>=sprite->totalFrames())
    throw std::invalid_argument("Enter a name, 1-1000 frames and a valid duration.");
  std::string name=requested;
  for (int n=1;sprite->frameTags().getByName(name);++n) name=requested+"-"+std::to_string(n);
  const frame_t start=sprite->totalFrames();
  const frame_t sourceFirst=source?source->fromFrame():seed;
  if (source) count=int(source->toFrame()-sourceFirst+1);
  DocumentApi api(document,tx);
  std::vector<std::pair<FrameTag*,std::pair<frame_t,frame_t>>> ranges;
  for (auto tag:sprite->frameTags()) ranges.push_back({tag,{tag->fromFrame(),tag->toFrame()}});
  for (int f=0;f<count;++f) api.addEmptyFrame(sprite,start+f);
  // Normal frame insertion extends the previous tag at its right boundary.
  // A separate animation must keep every existing tag's range unchanged.
  for (const auto& range:ranges)
    if (range.first->fromFrame()!=range.second.first || range.first->toFrame()!=range.second.second)
      tx.execute(new cmd::SetFrameTagRange(range.first,range.second.first,range.second.second));
  std::vector<Layer*> layers; sprite->getLayersList(layers);
  for (Layer* base:layers) {
    if (!base->isImage()) continue;
    auto layer=static_cast<LayerImage*>(base);
    std::unordered_map<ObjectId,frame_t> keys;
    for (int f=0;f<count;++f) {
      const frame_t sf=source?sourceFirst+f:seed;
      auto cel=layer->cel(sf);
      if (!cel) continue;
      auto it=keys.find(cel->data()->id());
      if (it==keys.end()) { api.copyCel(layer,sf,layer,start+f,false);keys[cel->data()->id()]=start+f; }
      else api.copyCel(layer,it->second,layer,start+f,true);
    }
  }
  for (int f=0;f<count;++f) api.setFrameDuration(sprite,start+f,source?sprite->frameDuration(sourceFirst+f):ms);
  auto tag=new FrameTag(start,start+count-1);tag->setName(name);
  if (source) {tag->setAniDir(source->aniDir());tag->setColor(source->color());}
  tx.execute(new cmd::AddFrameTag(sprite,tag));
  return tag;
}
frame_t insertLayerKey(Document* document, Transaction& tx, LayerImage* layer,
                       frame_t frame, frame_t first, frame_t last, bool blank,
                       const FrameTag* activeTag) {
  check(document,layer,first,last);
  if (frame<first || frame>last)
    throw std::invalid_argument("The key must be inside the selected animation.");
  Sprite* sprite=document->sprite();
  const frame_t next=frame+1;
  if (!activeTag) activeTag=sprite->frameTags().innerTag(frame);
  std::vector<std::pair<FrameTag*,std::pair<frame_t,frame_t>>> ranges;
  for (auto tag:sprite->frameTags()) ranges.push_back({tag,{tag->fromFrame(),tag->toFrame()}});
  DocumentApi api(document,tx);
  api.addEmptyFrame(sprite,next);
  // The new time slot holds every other layer's previous drawing. Existing
  // later cels shift right intact; they are never overwritten.
  std::vector<Layer*> layers; sprite->getLayersList(layers);
  for (Layer* base:layers) {
    if (!base->isImage()) continue;
    auto target=static_cast<LayerImage*>(base);
    if (target==layer && blank) continue;
    if (target->cel(frame)) api.copyCel(target,frame,target,next,target!=layer);
  }
  if (!layer->cel(next)) {
    ImageRef image(Image::create(sprite->pixelFormat(),sprite->width(),sprite->height()));
    clear_image(image.get(),sprite->transparentColor());api.addCel(layer,next,image);
  }
  api.setFrameDuration(sprite,next,sprite->frameDuration(frame));
  for (const auto& range:ranges) {
    auto tag=range.first;frame_t from=range.second.first,to=range.second.second;
    if (from>=next) {++from;++to;}
    else if (tag==activeTag || to>=next) ++to;
    // Only the selected animation grows at its right boundary. Unrelated
    // tags ending immediately before the inserted slot keep their range.
    if (tag->fromFrame()!=from || tag->toFrame()!=to)
      tx.execute(new cmd::SetFrameTagRange(tag,from,to));
  }
  return next;
}
void drawingKey(Document* document, Transaction& tx, LayerImage* layer, frame_t frame, frame_t first, frame_t last, bool blank) {
  check(document,layer,first,last);
  if (frame<first || frame>last) throw std::invalid_argument("The key must be inside the selected animation.");
  DocumentApi api(document,tx);
  auto old=layer->cel(frame);
  const auto oldData=old?old->dataRef():CelDataRef();
  frame_t end=frame;
  while (end<last) {
    auto next=layer->cel(end+1);
    if ((oldData && (!next || next->dataRef()!=oldData)) || (!oldData && next)) break;
    ++end;
  }
  if (blank) {
    if (old) tx.execute(new cmd::RemoveCel(old));
    ImageRef image(Image::create(document->sprite()->pixelFormat(),document->sprite()->width(),document->sprite()->height()));
    clear_image(image.get(),document->sprite()->transparentColor());api.addCel(layer,frame,image);
  }
  else if (old) {
    if (old->links()) tx.execute(new cmd::UnlinkCel(old));
  }
  else {
    frame_t previous=frame-1;
    while (previous>=first && !layer->cel(previous)) --previous;
    if (previous>=first) api.copyCel(layer,previous,layer,frame,false);
    else {
      ImageRef image(Image::create(document->sprite()->pixelFormat(),document->sprite()->width(),document->sprite()->height()));
      clear_image(image.get(),document->sprite()->transparentColor());api.addCel(layer,frame,image);
    }
  }
  for (frame_t f=frame+1;f<=end;++f) api.copyCel(layer,frame,layer,f,true);
}
void hold(Document* document, Transaction& tx, LayerImage* layer, frame_t from, frame_t to) {
  check(document,layer,from,to);
  if (!layer->cel(from)) throw std::invalid_argument("The first selected frame needs a drawing.");
  DocumentApi api(document,tx);
  for (frame_t f=from+1;f<=to;++f) api.copyCel(layer,from,layer,f,true);
}
void tweenPosition(Document* document, Transaction& tx, LayerImage* layer, frame_t from, frame_t to) {
  check(document,layer,from,to);
  auto first=layer->cel(from),last=layer->cel(to);
  if (to-from<2 || !first || !last) throw std::invalid_argument("Select two drawing keys with at least one frame between them.");
  const int x1=first->x(),y1=first->y(),x2=last->x(),y2=last->y();
  double total=0,elapsed=0;
  for (frame_t f=from;f<to;++f) total+=document->sprite()->frameDuration(f);
  DocumentApi api(document,tx);
  for (frame_t f=from+1;f<to;++f) {
    elapsed+=document->sprite()->frameDuration(f-1);
    // Cel position belongs to shared CelData: independent copies are essential.
    api.copyCel(layer,from,layer,f,false);
    api.setCelPosition(document->sprite(),layer->cel(f),int(std::lround(x1+(x2-x1)*elapsed/total)),int(std::lround(y1+(y2-y1)*elapsed/total)));
  }
}
}}
