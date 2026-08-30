// Creates an indexed sprite and replaces the first palette colors at once.

const sprite = app.createSprite(16, 16, 2);
const rgba = app.pixelColor.rgba;

sprite.palette.setColors(
  0,
  rgba(0, 0, 0, 0),
  rgba(34, 32, 52, 255),
  rgba(99, 155, 255, 255),
  rgba(172, 220, 255, 255)
);

const image = sprite.layer(0).cel(0).image;
image.clear(0);
image.fillCircle(8, 8, 6, 2);
image.drawCircle(8, 8, 6, 1);
image.drawLine(5, 6, 7, 6, 3);
image.drawLine(9, 6, 11, 6, 3);

sprite.commit();
app.redraw();
