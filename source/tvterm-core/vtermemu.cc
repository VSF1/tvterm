#define Uses_TText
#define Uses_TKeys
#define Uses_TEvent
#include <tvision/tv.h>

#include "util.h"
#include <tvterm/vtermemu.h>
#include <tvterm/debug.h>
#include <unordered_map>

namespace tvterm
{

const VTermScreenCallbacks VTermEmulator::callbacks =
{
    _static_wrap(&VTermEmulator::damage),
    _static_wrap(&VTermEmulator::moverect),
    _static_wrap(&VTermEmulator::movecursor),
    _static_wrap(&VTermEmulator::settermprop),
    _static_wrap(&VTermEmulator::bell),
    _static_wrap(&VTermEmulator::resize),
    _static_wrap(&VTermEmulator::sb_pushline),
    _static_wrap(&VTermEmulator::sb_popline),
};

namespace vtermemu
{

    // Input conversion.

    static const std::unordered_map<ushort, ushort> keys =
    {
        { kbEnter,          VTERM_KEY_ENTER             },
        { kbTab,            VTERM_KEY_TAB               },
        { kbBack,           VTERM_KEY_BACKSPACE         },
        { kbEsc,            VTERM_KEY_ESCAPE            },
        { kbUp,             VTERM_KEY_UP                },
        { kbDown,           VTERM_KEY_DOWN              },
        { kbLeft,           VTERM_KEY_LEFT              },
        { kbRight,          VTERM_KEY_RIGHT             },
        { kbIns,            VTERM_KEY_INS               },
        { kbDel,            VTERM_KEY_DEL               },
        { kbHome,           VTERM_KEY_HOME              },
        { kbEnd,            VTERM_KEY_END               },
        { kbPgUp,           VTERM_KEY_PAGEUP            },
        { kbPgDn,           VTERM_KEY_PAGEDOWN          },
        { kbF1,             VTERM_KEY_FUNCTION(1)       },
        { kbF2,             VTERM_KEY_FUNCTION(2)       },
        { kbF3,             VTERM_KEY_FUNCTION(3)       },
        { kbF4,             VTERM_KEY_FUNCTION(4)       },
        { kbF5,             VTERM_KEY_FUNCTION(5)       },
        { kbF6,             VTERM_KEY_FUNCTION(6)       },
        { kbF7,             VTERM_KEY_FUNCTION(7)       },
        { kbF8,             VTERM_KEY_FUNCTION(8)       },
        { kbF9,             VTERM_KEY_FUNCTION(9)       },
        { kbF10,            VTERM_KEY_FUNCTION(10)      },
        { kbF11,            VTERM_KEY_FUNCTION(11)      },
        { kbF12,            VTERM_KEY_FUNCTION(12)      },
    };

    static constexpr struct { ushort tv; VTermModifier vt; } modifiers[] =
    {
        { kbShift,      VTERM_MOD_SHIFT },
        { kbAltShift,   VTERM_MOD_ALT   },
        { kbCtrlShift,  VTERM_MOD_CTRL  },
    };

    static VTermKey convKey(ushort keyCode)
    {
        auto it = keys.find(keyCode);
        if (it != keys.end())
            return VTermKey(it->second);
        return VTERM_KEY_NONE;
    }

    static VTermModifier convMod(ushort controlKeyState)
    {
        VTermModifier mod = VTERM_MOD_NONE;
        for (const auto &m : modifiers)
            if (controlKeyState & m.tv)
                mod = VTermModifier(mod | m.vt);
        return mod;
    }

    static void convMouse( const MouseEventType &mouse,
                           VTermModifier &mod, int &button )
    {
        mod = convMod(mouse.controlKeyState);
        button =    (mouse.buttons & mbLeftButton)   ? 1 :
                    (mouse.buttons & mbMiddleButton) ? 2 :
                    (mouse.buttons & mbRightButton)  ? 3 :
                    (mouse.wheel & mwUp)             ? 4 :
                    (mouse.wheel & mwDown)           ? 5 :
                                                       0 ;
    }

    static void processKey(VTerm *vt, KeyDownEvent keyDown)
    {
        TKey tvKey(keyDown);
        VTermModifier vtMod = convMod(tvKey.mods);
        // Pass control characters directly, with no modifiers.
        if ( tvKey.mods == kbCtrlShift
             && 'A' <= tvKey.code && tvKey.code <= 'Z' )
        {
            keyDown.text[0] = keyDown.keyCode;
            keyDown.textLength = 1;
            vtMod = VTERM_MOD_NONE;
        }
        // Pass other legacy 'letter+mod' combinations as text.
        else if ( keyDown.textLength == 0
                  && ' ' <= tvKey.code && tvKey.code < '\x7F' )
        {
            keyDown.text[0] = (char) tvKey.code;
            keyDown.textLength = 1;
        }

        if (keyDown.textLength != 0)
            vterm_keyboard_unichar(vt, utf8To32(keyDown.getText()), vtMod);
        else if (VTermKey vtKey = convKey(tvKey.code))
            vterm_keyboard_key(vt, vtKey, vtMod);
    }

    static void processMouse(VTerm *vt, ushort what, const MouseEventType &mouse)
    {
        VTermModifier mod; int button;
        convMouse(mouse, mod, button);
        vterm_mouse_move(vt, mouse.where.y, mouse.where.x, mod);
        if (what & (evMouseDown | evMouseUp | evMouseWheel))
            vterm_mouse_button(vt, button, what != evMouseUp, mod);
    }

    static void wheelToArrow(VTerm *vt, uchar wheel)
    {
        VTermKey key = VTERM_KEY_NONE;
        switch (wheel)
        {
            case mwUp: key = VTERM_KEY_UP; break;
            case mwDown: key = VTERM_KEY_DOWN; break;
            case mwLeft: key = VTERM_KEY_LEFT; break;
            case mwRight: key = VTERM_KEY_RIGHT; break;
        }
        for (int i = 0; i < 3; ++i)
            vterm_keyboard_key(vt, key, VTERM_MOD_NONE);
    }

    // Output conversion.

    static TColorRGB VTermRGBtoRGB(VTermColor c)
    {
        return {c.rgb.red, c.rgb.green, c.rgb.blue};
    }

    static TColorAttr convAttr(const VTermScreenCell &cell)
    {
        auto &vt_fg = cell.fg,
             &vt_bg = cell.bg;
        auto &vt_attr = cell.attrs;
        TColorDesired fg, bg;
        // I prefer '{}', but GCC doesn't optimize it very well.
        memset(&fg, 0, sizeof(fg));
        memset(&bg, 0, sizeof(bg));

        if (!VTERM_COLOR_IS_DEFAULT_FG(&vt_fg))
        {
            if (VTERM_COLOR_IS_INDEXED(&vt_fg))
                fg = TColorXTerm(vt_fg.indexed.idx);
            else if (VTERM_COLOR_IS_RGB(&vt_fg))
                fg = VTermRGBtoRGB(vt_fg);
        }

        if (!VTERM_COLOR_IS_DEFAULT_BG(&vt_bg))
        {
            if (VTERM_COLOR_IS_INDEXED(&vt_bg))
                bg = TColorXTerm(vt_bg.indexed.idx);
            else if (VTERM_COLOR_IS_RGB(&vt_bg))
                bg = VTermRGBtoRGB(vt_bg);
        }

        ushort style =
              (slBold & -!!vt_attr.bold)
            | (slItalic & -!!vt_attr.italic)
            | (slUnderline & -!!vt_attr.underline)
            | (slBlink & -!!vt_attr.blink)
            | (slReverse & -!!vt_attr.reverse)
            | (slStrike & -!!vt_attr.strike)
            ;
        return {fg, bg, style};
    }

    static void convCell( TSpan<TScreenCell> cells, int x,
                          const VTermScreenCell &vtCell )
    {
        if (vtCell.chars[0] == (uint32_t) -1) // Wide char trail.
        {
            // Turbo Vision and libvterm may disagree on what characters
            // are double-width. If libvterm considers a character isn't
            // double-width but Turbo Vision does, it will manage to display it
            // properly anyway. But, in the opposite case, we need to place a
            // space after the double-width character.
            if (x > 0 && !cells[x - 1].isWide())
            {
                ::setChar(cells[x], ' ');
                ::setAttr(cells[x], ::getAttr(cells[x - 1]));
            }
        }
        else
        {
            size_t length = 0;
            while (vtCell.chars[length])
                ++length;
            TSpan<const uint32_t> text {vtCell.chars, max<size_t>(1, length)};
            TText::drawStr(cells, x, text, 0, convAttr(vtCell));
        }
    }

    static void drawArea( VTermScreen *vts, TPoint termSize, TRect area,
                          TerminalSurface &surface )
    {
        TRect r;
        if (surface.size != termSize)
        {
            surface.resize(termSize);
            r = {{0, 0}, termSize};
        }
        else
            r = area.intersect({{0, 0}, termSize});
        if (0 <= r.a.x && r.a.x < r.b.x && 0 <= r.a.y && r.a.y < r.b.y)
            for (int y = r.a.y; y < r.b.y; ++y)
            {
                TSpan<TScreenCell> cells(&surface.at(y, 0), surface.size.x);
                for (int x = r.a.x; x < r.b.x; ++x)
                {
                    VTermScreenCell cell;
                    if (vterm_screen_get_cell(vts, {y, x}, &cell))
                        convCell(cells, x, cell);
                    else
                        cells[x] = {};
                }
                surface.setDamage(y, r.a.x, r.b.x);
            }
    }

} // namespace vtermemu

TerminalEmulator &VTermEmulatorFactory::create(TPoint size, Writer &clientDataWriter, Mutex<TerminalSharedState> &sharedState) noexcept
{
    return *new VTermEmulator(size, clientDataWriter, sharedState);
}

TSpan<const EnvironmentVar> VTermEmulatorFactory::getCustomEnvironment() noexcept
{
    static constexpr EnvironmentVar customEnvironment[] =
    {
        {"TERM", "xterm-256color"},
        {"COLORTERM", "truecolor"},
    };

    return customEnvironment;
}

VTermEmulator::VTermEmulator(TPoint size, Writer &aClientDataWriter, Mutex<TerminalSharedState> &aSharedState) noexcept :
    clientDataWriter(aClientDataWriter),
    sharedState(aSharedState)
{
    vt = vterm_new(max(size.y, 1), max(size.x, 1));
    vterm_set_utf8(vt, 1);

    state = vterm_obtain_state(vt);
    vterm_state_reset(state, true);

    vts = vterm_obtain_screen(vt);
    vterm_screen_enable_altscreen(vts, true);
    vterm_screen_set_callbacks(vts, &callbacks, this);
    vterm_screen_set_damage_merge(vts, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(vts, true);

    vterm_output_set_callback(vt, _static_wrap(&VTermEmulator::writeOutput), this);

    // VTerm's cursor blinks by default, but it shouldn't.
    VTermValue val {0};
    vterm_state_set_termprop(state, VTERM_PROP_CURSORBLINK, &val);

    updateState();
}

VTermEmulator::~VTermEmulator()
{
    vterm_free(vt);
}

void VTermEmulator::updateState() noexcept
{
    sharedState.lock([&] (auto &sharedState) {
        if (localState.cursorChanged)
        {
            sharedState.cursorChanged = true;
            sharedState.cursorPos = localState.cursorPos;
            sharedState.cursorVisible = localState.cursorVisible;
            sharedState.cursorBlink = localState.cursorBlink;
            localState.cursorChanged = false;
        }

        if (localState.titleChanged)
        {
            sharedState.titleChanged = true;
            sharedState.title = std::move(localState.title);
            localState.titleChanged = false;
        }
    });
}

void VTermEmulator::handleEvent(const TerminalEvent &event) noexcept
{
    using namespace vtermemu;
    switch (event.type)
    {
        case TerminalEventType::KeyDown:
            processKey(vt, event.keyDown);
            break;

        case TerminalEventType::Mouse:
            if (mouseEnabled)
                processMouse(vt, event.mouse.what, event.mouse.mouse);
            else if (altScreenEnabled && event.mouse.what == evMouseWheel)
                wheelToArrow(vt, event.mouse.mouse.wheel);
            break;

        case TerminalEventType::ClientDataRead:
        {
            auto &clientData = event.clientDataRead;
            vterm_input_write(vt, clientData.data, clientData.size);
            break;
        }

        case TerminalEventType::ViewportResize:
        {
            TPoint size = {event.viewportResize.x, event.viewportResize.y};
            size.x = max(size.x, 1);
            size.y = max(size.y, 1);
            if (size != getSize())
                vterm_set_size(vt, size.y, size.x);
            break;
        }

        case TerminalEventType::FocusChange:
            if (event.focusChange.focusEnabled)
                vterm_state_focus_in(state);
            else
                vterm_state_focus_out(state);
            break;

        default:
            break;
    }
}

void VTermEmulator::flushState() noexcept
{
    vterm_screen_flush_damage(vts);
    updateState();
}

inline TPoint VTermEmulator::getSize() noexcept
{
    TPoint size;
    vterm_get_size(vt, &size.y, &size.x);
    return size;
}

void VTermEmulator::writeOutput(const char *data, size_t size)
{
    clientDataWriter.write(data, size);
}

int VTermEmulator::damage(VTermRect rect)
{
    using namespace vtermemu;
    sharedState.lock([&] (auto &sharedState) {
        drawArea( vts, getSize(),
                  {rect.start_col, rect.start_row, rect.end_col, rect.end_row},
                  sharedState.surface );
    });
    return true;
}

int VTermEmulator::moverect(VTermRect dest, VTermRect src)
{
    dout << "moverect(" << dest << ", " << src << ")" << std::endl;
    return false;
}

int VTermEmulator::movecursor(VTermPos pos, VTermPos oldpos, int visible)
{
    localState.cursorChanged = true;
    localState.cursorPos = {pos.col, pos.row};
    return true;
}

int VTermEmulator::settermprop(VTermProp prop, VTermValue *val)
{
    dout << "settermprop(" << prop << ", " << val << ")" << std::endl;
    if (vterm_get_prop_type(prop) == VTERM_VALUETYPE_STRING)
    {
#ifdef HAVE_VTERMSTRINGFRAGMENT
        if (val->string.initial)
            strFragBuf.clear();
        strFragBuf.push(val->string.str, val->string.len);
        if (!val->string.final)
            return true;
#else
        strFragBuf.clear();
        strFragBuf.push(val->string, val->string ? strlen(val->string) : 0);
#endif
    }

    switch (prop)
    {
        case VTERM_PROP_TITLE:
            localState.titleChanged = true;
            localState.title = std::move(strFragBuf);
            break;
        case VTERM_PROP_CURSORVISIBLE:
            localState.cursorChanged = true;
            localState.cursorVisible = val->boolean;
            break;
        case VTERM_PROP_CURSORBLINK:
            localState.cursorChanged = true;
            localState.cursorBlink = val->boolean;
            break;
        case VTERM_PROP_MOUSE:
            mouseEnabled = val->boolean;
            break;
        case VTERM_PROP_ALTSCREEN:
            altScreenEnabled = val->boolean;
            break;
        default:
            return false;
    }
    return true;
}

int VTermEmulator::bell()
{
    dout << "bell()" << std::endl;
    return false;
}

int VTermEmulator::resize(int rows, int cols)
{
    return false;
}

int VTermEmulator::sb_pushline(int cols, const VTermScreenCell *cells)
{
    linestack.push(std::max(cols, 0), cells);
    return true;
}

int VTermEmulator::sb_popline(int cols, VTermScreenCell *cells)
{
    return linestack.pop(*this, std::max(cols, 0), cells);
}

inline VTermScreenCell VTermEmulator::getDefaultCell() const
{
    VTermScreenCell cell {};
    cell.width = 1;
    vterm_state_get_default_colors(state, &cell.fg, &cell.bg);
    return cell;
}

void VTermEmulator::LineStack::push(size_t cols, const VTermScreenCell *src)
{
    if (stack.size() < maxSize)
    {
        auto *line = new VTermScreenCell[cols];
        memcpy(line, src, sizeof(VTermScreenCell)*cols);
        stack.emplace_back(line, cols);
    }
}

bool VTermEmulator::LineStack::pop( const VTermEmulator &vterm,
                                    size_t cols, VTermScreenCell *dst )
{
    if (!stack.empty())
    {
        auto line = top();
        size_t dst_size = cols*sizeof(VTermScreenCell);
        size_t copy_bytes = std::min(line.size_bytes(), dst_size);
        memcpy(dst, line.data(), copy_bytes);
        auto cell = vterm.getDefaultCell();
        for (size_t i = line.size(); i < cols; ++i)
            dst[i] = cell;
        stack.pop_back();
        return true;
    }
    return false;
}

} // namespace tvterm
