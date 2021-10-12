#ifndef TVTERM_VTERMADAPT_H
#define TVTERM_VTERMADAPT_H

#include <tvterm/termadapt.h>
#include <utility>
#include <vector>
#include <memory>

#include <vterm.h>

namespace tvterm
{

class VTermAdapter final : public TerminalAdapter
{
    struct LineStack
    {
        enum { maxSize = 10000 };

        std::vector<std::pair<std::unique_ptr<const VTermScreenCell[]>, size_t>> stack;
        void push(size_t cols, const VTermScreenCell *cells);
        bool pop(const VTermAdapter &vterm, size_t cols, VTermScreenCell *cells);
        TSpan<const VTermScreenCell> top() const;
    };

    struct VTerm *vt;
    struct VTermState *state;
    struct VTermScreen *vts;
    ByteArray strFragBuf;
    LineStack linestack;
    bool mouseEnabled {false};
    bool altScreenEnabled {false};

    static const VTermScreenCallbacks callbacks;
    static thread_local TerminalSurface* lockedSurface;

    void writeOutput(const char *data, size_t size);
    int damage(VTermRect rect);
    int moverect(VTermRect dest, VTermRect src);
    int movecursor(VTermPos pos, VTermPos oldpos, int visible);
    int settermprop(VTermProp prop, VTermValue *val);
    int bell();
    int resize(int rows, int cols);
    int sb_pushline(int cols, const VTermScreenCell *cells);
    int sb_popline(int cols, VTermScreenCell *cells);

    VTermScreenCell getDefaultCell() const;
    TPoint getSize() noexcept;

    void (&getChildActions() noexcept)() override;
    void receive(TSpan<const char> buf) noexcept override;
    void flushDamage() noexcept override;
    void setSize(TPoint size) noexcept override;
    void setFocus(bool focus) noexcept override;
    void handleKeyDown(const KeyDownEvent &keyDown) noexcept override;
    void handleMouse(ushort what, const MouseEventType &mouse) noexcept override;

public:

    VTermAdapter(TPoint size) noexcept;
    ~VTermAdapter();

};

inline TSpan<const VTermScreenCell> VTermAdapter::LineStack::top() const
{
    auto &pair = stack.back();
    return {pair.first.get(), pair.second};
}

} // namespace tvterm

#endif // TVTERM_VTERMADAPT_H
