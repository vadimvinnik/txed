//  Object model for a text editor with fast insert/delete/replace
//  and easy undo/redo.
//
//  Text objects are immutable. Instead of changing a string, every edit
//  operation applies a decorator. Hence, the current state of the text
//  is a stack of decorators that gives the edit history.
//
//  Text object supports access through STL-style iterators and therefore
//  can be used in any algorithm where a string range is expected.
//
//  Vadim Vinnik
//  vadim.vinnik@gmail.com
//  2016

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

struct iterator_mismatch_exception {};
struct out_of_range_exception {};

// Performs all the hard work implementing the actual text_iterator behaviour.
// Then the text_iterator class itself just wraps this worker object into a rich
// interface required by the Random access text_iterator notion defined in STL.
// Each concrete text object class must define its own implementation of this
// base class.
class text_iterator_helper_base {
  private:
    int m_current_index;

  protected:
    text_iterator_helper_base(int current_index):
      m_current_index(current_index)
    {}

    virtual void move_impl(int d) = 0;

  public:
    int current_index() const { return m_current_index; }
    
    ptrdiff_t diff(text_iterator_helper_base const& it) { return m_current_index - it.current_index(); }

    void move(int d) {
      auto new_index = m_current_index + d;
      move_impl(d);
      m_current_index = new_index;
    }

    virtual text_iterator_helper_base *clone() const = 0;
    virtual char const& value() const = 0;
};

// This is a non-abstract class that does not need to be overrided for
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
  friend class text_object;

  private:
    std::unique_ptr<text_iterator_helper_base> m_helper;

    text_iterator(text_iterator_helper_base *helper): m_helper(helper) {}

    ptrdiff_t diff(text_iterator const& it) const { return m_helper->diff(*it.m_helper); }
    void move(int d) { m_helper->move(d); }

  public:
    text_iterator(): m_helper(nullptr) {}
    text_iterator(text_iterator const& it): m_helper(it.m_helper->clone()) {}

    text_iterator& operator=(text_iterator const& it) { m_helper.reset(it.m_helper->clone()); return *this; }

    char const& operator*() const { return m_helper->value(); }
    char const& operator[](int d) const { return *(*this + d); }

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

class text_object {
  protected:
    typedef text_iterator_helper_base helper;

    // Each concrete text object class must define its own pair of factory methods
    virtual helper *begin_helper() const = 0;
    virtual helper *end_helper() const = 0;

  public:
    typedef text_iterator iterator;

    iterator begin()  const { return iterator(begin_helper()); }
    iterator end()    const { return iterator(end_helper()); }
    iterator cbegin() const { return iterator(begin_helper()); }
    iterator cend()   const { return iterator(end_helper()); }

    virtual int length() const = 0;
    virtual char const& at(int i) const { return *(cbegin() + i); }
    virtual std::string to_string() const { return std::string(cbegin(), cend()); }
};

class string_iterator_helper : public text_iterator_helper_base {
  friend class text_string;

  private:
    std::string::const_iterator m_current;

    string_iterator_helper(int current_index, std::string::const_iterator current):
      text_iterator_helper_base(current_index),
      m_current(current)
    {}

  protected:
    virtual void move_impl(int d) { m_current += d; }

  public:
    virtual text_iterator_helper_base *clone() const { return new string_iterator_helper(*this); }
    virtual char const& value() const { return *m_current; }
};

class text_string : public text_object {
  private:
    std::string const m_value;

  protected:
    virtual helper *begin_helper() const {
      return new string_iterator_helper(0, m_value.cbegin());
    }

    virtual helper *end_helper() const {
      return new string_iterator_helper(length(), m_value.cend());
    }

  public:
    text_string(std::string const& value): m_value(value) {}

    virtual int length() const { return m_value.length(); }
    virtual char const& at(int i) const { return m_value[i]; }
    virtual std::string to_string() const { return m_value; }
};

class replacement_iterator_helper: public text_iterator_helper_base {
  friend class text_replacement;

  private:
    text_object::iterator const m_prefix_begin;
    text_object::iterator const m_patch_begin;
    text_object::iterator const m_postfix_begin;
    int const m_prefix_length;
    int const m_patch_length;

    replacement_iterator_helper(
      int current_index,
      text_object::iterator const prefix_begin,
      text_object::iterator const prefix_end,
      text_object::iterator const patch_begin,
      text_object::iterator const patch_end,
      text_object::iterator const postfix_begin
    ):
      text_iterator_helper_base(current_index),
      m_prefix_begin(prefix_begin),
      m_patch_begin(patch_begin),
      m_postfix_begin(postfix_begin),
      m_prefix_length(prefix_end - prefix_begin),
      m_patch_length(patch_end - patch_begin)
    {}

  protected:
    virtual void move_impl(int d) {} // current index from the base class is enough

  public:
    virtual text_iterator_helper_base *clone() const {
      return new replacement_iterator_helper(*this);
    }

    virtual char const& value() const {
      auto const i_in_prefix = current_index();
      auto const i_in_patch = i_in_prefix - m_prefix_length;
      auto const i_in_postfix = i_in_patch - m_patch_length;

      auto const current = i_in_postfix >= 0
        ? m_postfix_begin + i_in_postfix
        : i_in_patch >= 0
          ? m_patch_begin + i_in_patch
          : m_prefix_begin + i_in_prefix;

      return *current;
    }
};

class text_replacement : public text_object
{
  private:
    text_object const* const m_base;
    text_object::iterator const m_cut_from;
    text_object::iterator const m_cut_to;
    text_object::iterator const m_patch_from;
    text_object::iterator const m_patch_to;
    int m_length;

    helper *create_helper(int index) const {
      return new replacement_iterator_helper(
          index,
          m_base->cbegin(),
          m_cut_from,
          m_patch_from,
          m_patch_to,
          m_cut_to);
    }

  protected:
    virtual helper *begin_helper() const { return create_helper(0); }
    virtual helper *end_helper() const { return create_helper(m_length); }

  public:
    text_replacement(
      text_object const* base,
      text_object::iterator cut_from,
      text_object::iterator cut_to,
      text_object::iterator patch_from,
      text_object::iterator patch_to
    ):
      m_base(base),
      m_cut_from(cut_from),
      m_cut_to(cut_to),
      m_patch_from(patch_from),
      m_patch_to(patch_to),
      m_length(base->length() - (cut_to - cut_from) + (patch_to - patch_from))
    {}

    text_replacement(
      text_object const* base,
      int cut_from,
      int cut_to,
      text_object const* patch,
      int patch_from,
      int patch_to
    ):
      m_base(base),
      m_cut_from(base->cbegin() + cut_from),
      m_cut_to(base->cbegin() + cut_to),
      m_patch_from(patch->cbegin() + patch_from),
      m_patch_to(patch->cbegin() + patch_to),
      m_length(base->length() - (cut_to - cut_from) + (patch_to - patch_from))
    {}

    virtual int length() const { return m_length; }
};
