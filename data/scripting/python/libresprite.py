"""LibreSprite's bundled Python drawing API.

Python scripts build an operation program which LibreSprite executes on its UI
thread as one undoable transaction. No network service or API key is involved.
"""

import base64
import json

_program = []
_next_id = 0


def _name():
    global _next_id
    _next_id += 1
    return "_py%d" % _next_id


def _js(value):
    if isinstance(value, _Reference):
        return value._expression
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, (int, float)):
        return repr(value)
    raise TypeError("Unsupported LibreSprite value: %r" % (value,))


def _statement(expression):
    _program.append(expression + ";")


def _result(expression, kind):
    variable = _name()
    _statement("var %s=%s" % (variable, expression))
    return kind(variable)


class _Reference:
    def __init__(self, expression):
        self._expression = expression

    def _set(self, name, value):
        _statement("%s.%s=%s" % (self._expression, name, _js(value)))


class PixelColor:
    def rgba(self, red, green, blue, alpha=255):
        return _Reference("app.pixelColor.rgba(%s,%s,%s,%s)" %
                          (_js(red), _js(green), _js(blue), _js(alpha)))


class Image(_Reference):
    def clear(self, color):
        _statement("%s.clear(%s)" % (self._expression, _js(color)))

    def put_pixel(self, x, y, color):
        _statement("%s.putPixel(%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(color)))

    def fill_rect(self, x, y, width, height, color):
        _statement("%s.fillRect(%s,%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(width),
                    _js(height), _js(color)))

    def draw_rect(self, x, y, width, height, color):
        _statement("%s.drawRect(%s,%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(width),
                    _js(height), _js(color)))

    def draw_line(self, x1, y1, x2, y2, color):
        _statement("%s.drawLine(%s,%s,%s,%s,%s)" %
                   (self._expression, _js(x1), _js(y1), _js(x2),
                    _js(y2), _js(color)))

    def draw_circle(self, x, y, radius, color):
        _statement("%s.drawCircle(%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(radius), _js(color)))

    def fill_circle(self, x, y, radius, color):
        _statement("%s.fillCircle(%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(radius), _js(color)))

    def draw_ellipse(self, x, y, width, height, color):
        _statement("%s.drawEllipse(%s,%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(width),
                    _js(height), _js(color)))

    def fill_ellipse(self, x, y, width, height, color):
        _statement("%s.fillEllipse(%s,%s,%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(width),
                    _js(height), _js(color)))

    def flood_fill(self, x, y, color):
        _statement("%s.floodFill(%s,%s,%s)" %
                   (self._expression, _js(x), _js(y), _js(color)))

    def fill_gradient(self, start_color, end_color, vertical=True):
        _statement("%s.fillGradient(%s,%s,%s)" %
                   (self._expression, _js(start_color), _js(end_color),
                    _js(vertical)))

    def copy_rect(self, source_x, source_y, width, height,
                  destination_x, destination_y):
        _statement("%s.copyRect(%s,%s,%s,%s,%s,%s)" %
                   (self._expression, _js(source_x), _js(source_y),
                    _js(width), _js(height), _js(destination_x),
                    _js(destination_y)))

    def flip_horizontal(self):
        _statement("%s.flipHorizontal()" % self._expression)

    def flip_vertical(self):
        _statement("%s.flipVertical()" % self._expression)


class Cel(_Reference):
    @property
    def image(self):
        return _result("%s.image" % self._expression, Image)

    def set_position(self, x, y):
        _statement("%s.setPosition(%s,%s)" %
                   (self._expression, _js(x), _js(y)))

    @property
    def x(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @x.setter
    def x(self, value):
        self._set("x", value)

    @property
    def y(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @y.setter
    def y(self, value):
        self._set("y", value)


class Layer(_Reference):
    @property
    def name(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @name.setter
    def name(self, value):
        self._set("name", value)

    def cel(self, frame):
        return _result("%s.cel(%s)" % (self._expression, _js(frame)), Cel)

    @property
    def is_visible(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @is_visible.setter
    def is_visible(self, value):
        self._set("isVisible", value)

    @property
    def is_editable(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @is_editable.setter
    def is_editable(self, value):
        self._set("isEditable", value)

    @property
    def opacity(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @opacity.setter
    def opacity(self, value):
        self._set("opacity", value)

    @property
    def blend_mode(self):
        raise RuntimeError("Reading values is not available in external Python scripts")

    @blend_mode.setter
    def blend_mode(self, value):
        self._set("blendMode", value)


class Palette(_Reference):
    def set_color(self, index, color):
        _statement("%s.setColor(%s,%s)" %
                   (self._expression, _js(index), _js(color)))

    def set_colors(self, start_index, *colors):
        arguments = ",".join([_js(start_index)] + [_js(color) for color in colors])
        _statement("%s.setColors(%s)" % (self._expression, arguments))


class Sprite(_Reference):
    @property
    def palette(self):
        return _result("%s.palette" % self._expression, Palette)

    def layer(self, index):
        return _result("%s.layer(%s)" % (self._expression, _js(index)), Layer)

    def add_layer(self, name="Layer"):
        return _result("%s.addLayer(%s)" % (self._expression, _js(name)), Layer)

    def ensure_cel(self, layer, frame):
        return _result("%s.ensureCel(%s,%s)" %
                       (self._expression, _js(layer), _js(frame)), Cel)

    def ensure_image(self, layer, frame):
        return _result("%s.ensureImage(%s,%s)" %
                       (self._expression, _js(layer), _js(frame)), Image)

    def add_frame(self, copy_previous=True):
        _statement("%s.addFrame(%s)" % (self._expression, _js(copy_previous)))

    def duplicate_frame(self, source_frame, insert_at):
        _statement("%s.duplicateFrame(%s,%s)" %
                   (self._expression, _js(source_frame), _js(insert_at)))

    def remove_frame(self, frame):
        _statement("%s.removeFrame(%s)" % (self._expression, _js(frame)))

    def move_frame(self, frame, before_frame):
        _statement("%s.moveFrame(%s,%s)" %
                   (self._expression, _js(frame), _js(before_frame)))

    def set_frame_duration(self, frame, milliseconds):
        _statement("%s.setFrameDuration(%s,%s)" %
                   (self._expression, _js(frame), _js(milliseconds)))

    def duplicate_layer(self, layer):
        return _result("%s.duplicateLayer(%s)" %
                       (self._expression, _js(layer)), Layer)

    def remove_layer(self, layer):
        _statement("%s.removeLayer(%s)" % (self._expression, _js(layer)))

    def move_layer(self, layer, new_position):
        _statement("%s.moveLayer(%s,%s)" %
                   (self._expression, _js(layer), _js(new_position)))

    def remove_tag(self, name):
        _statement("%s.removeTag(%s)" % (self._expression, _js(name)))

    def resize(self, width, height):
        _statement("%s.resize(%s,%s)" %
                   (self._expression, _js(width), _js(height)))

    def create_tag(self, name, first_frame, last_frame):
        _statement("%s.createTag(%s,%s,%s)" %
                   (self._expression, _js(name), _js(first_frame),
                    _js(last_frame)))

    def save_to(self, path):
        _statement("%s.saveTo(%s)" % (self._expression, _js(path)))


class App:
    pixel_color = PixelColor()

    @property
    def active_sprite(self):
        return Sprite("app.activeSprite")

    @property
    def active_image(self):
        return Image("app.activeImage")

    def create_sprite(self, width, height, color_mode=0):
        return _result("app.createSprite(%s,%s,%s)" %
                       (_js(width), _js(height), _js(color_mode)), Sprite)


app = App()


def _export_program():
    encoded = base64.b64encode("\n".join(_program).encode("utf-8"))
    return encoded.decode("ascii")
