from libresprite import app

sprite = app.create_sprite(32, 32, 0)
image = sprite.ensure_image(0, 0)

transparent = app.pixel_color.rgba(0, 0, 0, 0)
outline = app.pixel_color.rgba(35, 30, 55, 255)
blue = app.pixel_color.rgba(55, 125, 235, 255)
shine = app.pixel_color.rgba(170, 220, 255, 255)

image.clear(transparent)
image.fill_rect(8, 8, 16, 16, outline)
image.fill_rect(10, 10, 12, 12, blue)
image.fill_rect(12, 11, 5, 3, shine)

# Saving is optional. Without this line the new sprite stays open for editing.
# sprite.save_to("C:/pixel-art.ase")
