// LibreSprite high-level scripting example.
// Creates a 32x32 RGBA sprite without opening the New Sprite dialog.

const sprite = app.createSprite(32, 32, 0);
const image = sprite.layer(0).cel(0).image;
const rgba = app.pixelColor.rgba;

const transparent = rgba(0, 0, 0, 0);
const outline = rgba(34, 32, 52, 255);
const body = rgba(99, 155, 255, 255);
const highlight = rgba(172, 220, 255, 255);

image.clear(transparent);
image.fillRect(10, 10, 12, 12, body);
image.drawRect(9, 9, 14, 14, outline);
image.drawLine(11, 12, 14, 12, highlight);
image.drawLine(17, 12, 20, 12, highlight);

const detailLayer = sprite.addLayer("Details");
const detail = detailLayer.cel(0).image;
detail.fillRect(13, 17, 2, 2, outline);
detail.fillRect(18, 17, 2, 2, outline);
detail.drawCircle(16, 16, 10, highlight);

// Add a copied animation frame and change its duration.
const frame = sprite.addFrame(true);
sprite.setFrameDuration(0, 120);
sprite.setFrameDuration(frame, 120);

// Example of a guaranteed cel for an empty frame/layer:
const frameImage = sprite.ensureCel(1, frame).image;
frameImage.fillCircle(16, 25, 2, outline);

sprite.commit();
app.redraw();

// To save without a file dialog, provide an absolute path:
// sprite.saveTo("C:/pixel-art/character.ase");
