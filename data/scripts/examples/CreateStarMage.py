from libresprite import app

# 32x32 original pixel character: "Miro, the Star Mage"
sprite = app.create_sprite(32, 32, 0)
image = sprite.ensure_image(0, 0)

transparent = app.pixel_color.rgba(0, 0, 0, 0)
outline = app.pixel_color.rgba(35, 27, 55, 255)
deep_purple = app.pixel_color.rgba(62, 45, 105, 255)
purple = app.pixel_color.rgba(94, 67, 150, 255)
light_purple = app.pixel_color.rgba(145, 107, 205, 255)
skin_shadow = app.pixel_color.rgba(199, 128, 113, 255)
skin = app.pixel_color.rgba(244, 180, 146, 255)
skin_light = app.pixel_color.rgba(255, 214, 172, 255)
hair = app.pixel_color.rgba(82, 48, 45, 255)
white = app.pixel_color.rgba(245, 239, 231, 255)
gold = app.pixel_color.rgba(246, 190, 60, 255)
gold_light = app.pixel_color.rgba(255, 232, 115, 255)
boot = app.pixel_color.rgba(55, 39, 63, 255)
shadow = app.pixel_color.rgba(45, 35, 70, 110)

image.clear(transparent)

# Ground shadow
image.fill_rect(8, 28, 16, 2, shadow)
image.fill_rect(11, 27, 10, 1, shadow)

# Cape behind the body
image.fill_rect(7, 18, 3, 8, outline)
image.fill_rect(9, 20, 2, 7, outline)
image.fill_rect(8, 19, 2, 6, deep_purple)
image.put_pixel(9, 25, purple)

# Boots
image.fill_rect(11, 26, 4, 3, outline)
image.fill_rect(18, 26, 4, 3, outline)
image.fill_rect(10, 28, 6, 2, boot)
image.fill_rect(17, 28, 6, 2, boot)

# Robe outline and main shape
image.fill_rect(10, 18, 12, 9, outline)
image.fill_rect(9, 22, 14, 4, outline)
image.fill_rect(11, 18, 10, 7, purple)
image.fill_rect(10, 22, 12, 3, purple)
image.fill_rect(12, 19, 3, 6, light_purple)
image.fill_rect(11, 25, 4, 1, deep_purple)
image.fill_rect(18, 25, 4, 1, deep_purple)

# Belt and buckle
image.fill_rect(10, 21, 12, 2, outline)
image.fill_rect(11, 21, 10, 1, gold)
image.fill_rect(15, 21, 3, 2, gold_light)
image.put_pixel(16, 22, gold)

# Neck
image.fill_rect(14, 16, 5, 4, outline)
image.fill_rect(15, 16, 3, 3, skin_shadow)

# Head outline
image.fill_rect(10, 9, 12, 8, outline)
image.fill_rect(11, 7, 10, 11, outline)
image.fill_rect(9, 11, 2, 4, outline)
image.fill_rect(21, 11, 2, 4, outline)

# Face
image.fill_rect(11, 10, 10, 7, skin)
image.fill_rect(12, 9, 8, 2, skin_light)
image.fill_rect(10, 12, 2, 3, skin_shadow)
image.fill_rect(20, 12, 2, 3, skin_shadow)

# Hair fringe
image.fill_rect(11, 9, 10, 2, hair)
image.fill_rect(12, 11, 2, 2, hair)
image.fill_rect(18, 10, 2, 2, hair)

# Eyes
image.fill_rect(13, 13, 2, 2, white)
image.fill_rect(18, 13, 2, 2, white)
image.put_pixel(14, 14, outline)
image.put_pixel(18, 14, outline)

# Small smile and cheek
image.fill_rect(15, 16, 3, 1, skin_shadow)
image.put_pixel(12, 15, skin_light)
image.put_pixel(20, 15, skin_light)

# Wizard hat brim
image.fill_rect(7, 8, 18, 3, outline)
image.fill_rect(9, 8, 14, 2, purple)
image.fill_rect(12, 9, 8, 1, light_purple)

# Pointed hat
image.fill_rect(10, 6, 12, 3, outline)
image.fill_rect(12, 4, 9, 3, outline)
image.fill_rect(14, 2, 7, 3, outline)
image.fill_rect(17, 1, 4, 2, outline)
image.fill_rect(11, 7, 10, 1, deep_purple)
image.fill_rect(13, 5, 7, 2, purple)
image.fill_rect(15, 3, 5, 2, purple)
image.fill_rect(18, 2, 2, 1, light_purple)

# Star emblem on the hat
image.put_pixel(15, 6, gold_light)
image.fill_rect(14, 7, 3, 1, gold)
image.put_pixel(15, 8, gold_light)

# Left hand
image.fill_rect(7, 20, 3, 4, outline)
image.fill_rect(8, 20, 2, 3, skin)
image.put_pixel(8, 20, skin_light)

# Staff outline
image.fill_rect(25, 11, 2, 17, outline)
image.fill_rect(24, 7, 4, 5, outline)
image.fill_rect(23, 8, 6, 3, outline)

# Staff wood and star crystal
image.fill_rect(25, 12, 1, 15, gold)
image.fill_rect(25, 7, 2, 5, gold)
image.fill_rect(24, 9, 4, 1, gold_light)
image.put_pixel(25, 6, gold_light)
image.put_pixel(22, 9, gold_light)
image.put_pixel(29, 9, gold_light)
image.put_pixel(25, 12, gold_light)

# Right hand gripping the staff
image.fill_rect(21, 19, 4, 4, outline)
image.fill_rect(22, 20, 3, 2, skin)
image.put_pixel(22, 20, skin_light)

# A few magical sparkles
for x, y in [(5, 8), (27, 4), (4, 17), (29, 17)]:
    image.put_pixel(x, y, gold_light)

# Keep the generated character open in LibreSprite for editing.
# To save automatically, uncomment and change this path:
# sprite.save_to("C:/StarMage.ase")
