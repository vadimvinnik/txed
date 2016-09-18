//  Object model for a text editor with easy undo/redo
//
//  Vadim Vinnik
//  vadim.vinnik@gmail.com
//  2016

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

class iterator_const_helper;
class iterator_selection_helper;
class iterator_patch_helper;

struct iterator_mismatch_exception {};
struct out_of_range_exception {};

// Performs all the hard work implementing the actual text_iterator behaviour.
// Then the text_iterator class itself just wraps this worker object into a rich
// interface required by the Random access text_iterator notion defined in STL.
// Each concrete text object class must define its own implementation of this
// base class.
class text_iterator_helper_base {
  protected:
    int const m_end_index;
    int m_current_index;

    text_iterator_helper_base(int end_index, int current_index):
      m_end_index(end_index),
      m_current_index(current_index)
    {}

    virtual void move_impl(int d) = 0;

  public:
    ptrdiff_t diff(text_iterator_helper_base const& it) { return m_current_index - it.m_current_index; }

    virtual void move(int d) {
      move_impl(d);
      m_current_index += d;
    }

    virtual text_iterator_helper_base *clone() const = 0;
    virtual char const& value() const = 0;
};

// This is a non-abstract class that does not need to be ovverrided for
// particular text objects. It just delegates everything to a helper object that
// is aware of the particular text object subclass. This is a normal STL
// iterator -- in particular, it can be passed by value.
class text_iterator: public std::iterator<
    std::random_access_iterator_tag,
    char,
    ptrdiff_t,
    char const*,
    char const&>
{
  private:
    std::unique_ptr<text_iterator_helper_base> m_helper;

    ptrdiff_t diff(text_iterator const& it) const { return m_helper->diff(*it.m_helper); }
    void move(int d) { m_helper->move(d); }

  public:
    text_iterator(): m_helper(nullptr) {}
    text_iterator(text_iterator_helper_base *helper): m_helper(helper) {}
    text_iterator(text_iterator const& it): m_helper(it.m_helper->clone()) {}

    text_iterator& operator=(text_iterator const& it) { m_helper.reset(it.m_helper->clone()); return *this; }

    char operator*() const { return m_helper->value(); }
    char operator[](int d) const { return *(*this + d); }

    bool operator==(text_iterator const& it) const { return diff(it) == 0; }
    bool operator!=(text_iterator const& it) const { return diff(it) != 0; }
    bool operator< (text_iterator const& it) const { return diff(it) <  0; }
    bool operator> (text_iterator const& it) const { return diff(it) >  0; }
    bool operator<=(text_iterator const& it) const { return diff(it) <= 0; }
    bool operator>=(text_iterator const& it) const { return diff(it) >= 0; }

    text_iterator& operator++() { move(+1); return *this; }
    text_iterator& operator--() { move(-1); return *this; }

    text_iterator operator++(int) { text_iterator t(*this); move(+1); return t; }
    text_iterator operator--(int) { text_iterator t(*this); move(-1); return t; }

    text_iterator& operator+=(int d) { move(+d); return *this; }
    text_iterator& operator-=(int d) { move(-d); return *this; }

    text_iterator operator+(int d) const { text_iterator t(*this); return t += d; }
    text_iterator operator-(int d) const { text_iterator t(*this); return t -= d; }

    ptrdiff_t operator-(text_iterator const& it) const { return diff(it); }
};

static text_iterator operator+(int d, text_iterator const& it) { return it + d; }

class TextBase
{
  protected:
    // Each concrete text object class must define its own pair of factory methods
    virtual text_iterator_helper_base *begin_helper() const = 0;
    virtual text_iterator_helper_base *end_helper() const = 0;

  public:
    text_iterator const_begin() const { return text_iterator(begin_helper()); }
    text_iterator const_end() const { return text_iterator(end_helper()); }

    virtual int length() const = 0;
    virtual char index(int i) const = 0;

    virtual std::string toString() const
    {
      return std::string(const_begin(), const_end());
    }

    // todo: convert index() to a non-virtual method with assertion delegating to a protected helper
};

class iterator_const_helper : public text_iterator_helper_base
{
  private:
    std::string::const_iterator m_current;

  public:
    iterator_const_helper(std::string::const_iterator current):
      m_current(current)
    {}

    virtual text_iterator_helper_base *clone() const { return new iterator_const_helper(*this); }
    virtual char const& value() const { return *m_current; }
    virtual void move(int d) { m_current += d; }
    virtual ptrdiff_t diff(text_iterator_helper_base const& it) { return it.diff_const(*this); }

    virtual ptrdiff_t diff_const(iterator_const_helper const& it) const { return it.m_current - m_current; }
    virtual ptrdiff_t diff_selection(iterator_selection_helper const& it) const { throw iterator_mismatch_exception(); }
    virtual ptrdiff_t diff_patch(iterator_patch_helper const& it) const { throw iterator_mismatch_exception(); }
};

class TextConst : public TextBase
{
  private:
    std::string m_value;

  protected:
    virtual text_iterator_helper_base *begin_helper() const { return new iterator_const_helper(m_value.cbegin()); }
    virtual text_iterator_helper_base *end_helper() const { return new iterator_const_helper(m_value.cend()); }

  public:
    TextConst(std::string value): m_value(value) {}

    virtual int length() const { return m_value.length(); }
    virtual char index(int i) const { return m_value[i]; }
    virtual std::string toString() const { return m_value; }
};

class TextSegmentBase : public TextBase
{
  protected:
    TextBase const* const m_base;
    int const m_start;
    int const m_length;

  public:
    TextSegmentBase(TextBase const* base, int start, int length):
      m_base(base),
      m_start(start),
      m_length(length)
    {}
};

class iterator_selection_helper : public text_iterator_helper_base
{
};

class TextSelection : public TextSegmentBase
{
  public:
    TextSelection(TextBase const* base, int start, int length):
      TextSegmentBase(base, start, length)
      {}

    virtual int length() const { return m_length; }
    virtual char index(int i) const { return m_base->index(i + m_start); }
};

class iterator_patch_helper : public text_iterator_helper_base
{
};

class TextPatch : public TextSegmentBase
{
  private:
    TextBase const* const m_patch;

  public:
    TextPatch(TextBase const* base, int start, int length, TextBase const* patch):
      TextSegmentBase(base, start, length),
      m_patch(patch)
    {}

    virtual int length() const { return m_base->length() - m_length + m_patch->length(); }

    virtual char index(int i) const {
      int i_in_patch = i - m_start;

      if (i_in_patch < 0)
      {
        return m_base->index(i);
      }
      else
      {
        int i_in_postfix = i_in_patch - m_patch->length();

        if (i_in_postfix < 0)
        {
          return m_patch->index(i_in_patch);
        }
        else
        {
          return m_base->index(i_in_postfix + m_start + m_length);
        }
      }
    }
};
