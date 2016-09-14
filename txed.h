/*
  Object model for a text editor with easy undo/redo

  Vadim Vinnik, 2016
  e-mail: vadim.vinnik@gmail.com
*/

#include <string>

class TextBase
{
  protected:
    // Performs all the hard work implementing the actual behaviour of the iterator.
    // Then the iterator class itself just wraps this worker object into a rich
    // interface required by the Random access iterator notion defined in STL.
    class iterator_helper_base
    {
      protected:
        // Visitor pattern in diff() to resolve the actual subtype of the 2nd argument
        virtual diff_t diff_const(TextConst::iterator_helper const& it) const = 0;
        virtual diff_t diff_selection(TextSelection::iterator_helper const& it) const = 0;
        virtual diff_t diff_patch(TextPatch::iterator_helper const& it) const = 0;

      public:
        virtual char value() const = 0;
        virtual void move(int d) = 0;
        virtual diff_t diff(iterator_helper_base const& it) = 0;
    };

    virtual iterator_helper_base *begin_helper() const = 0;
    virtual iterator_helper_base *end_helper() const = 0;

  public:
    class iterator
    {
      private:
        iterator_helper_base *m_helper;
        diff_t diff(iterator const& it) const { return m_helper->diff(it.m_helper); }

      public:
        iterator(iterator_helper_base *helper):
          m_helper(helper)
          {}

        // todo: all operations from random access iterator protocol
        iterator& operator++() { m_helper->move(1); return *this; }
        iterator& operator--() { m_helper->move(-1); return *this; }
        char operator*() const { return m_helper->value(); }
        bool operator==(iterator const& it) const { return diff(it) == 0; }
        bool operator!=(iterator const& it) const { return diff(it) != 0; }
        diff_t operator-(iterator const& it) const { return diff(it); }
    };

    iterator const_begin() const { return iterator(begin_helper()); }
    iterator const_end() const { return iterator(end_helper()); }

    virtual int length() const = 0;
    virtual char index(int i) const = 0;

    virtual std::string toString() const
    {
      return std::string(const_begin(), const_end());
    }

    // todo: convert index() to a non-virtual method with assertion delegating to a protected helper
};

class TextConst : public TextBase
{
  private:
    std::string m_value;

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

class TextSelection : public TextSegmentBase
{
  public:
    TextSelection(TextBase const* base, int start, int length):
      TextSegmentBase(base, start, length)
      {}

    virtual int length() const { return m_length; }
    virtual char index(int i) const { return m_base->index(i + m_start); }
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
