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

#include <cstddef>
#include <memory>
#include <string>

namespace text_edit {

class text_object;

class iterator_mismatch: public std::domain_error {
  public:
    iterator_mismatch(): std::domain_error("Cannot compare iterators pointing to different containers") {}
};

class text_out_of_range: public std::out_of_range {
  private:
    int const m_index;
    int const m_length;

  public:
    text_out_of_range(int index, int length):
      std::out_of_range("Cannot dereference an iterator pointing outside the container"),
      m_index(index),
      m_length(length)
    {}

    int index() const { return m_index; }
    int length() const { return m_length; }
};

class text_iterator: public std::iterator<
  std::random_access_iterator_tag,
  char,
  ptrdiff_t,
  char const*,
  char const&>
{
  friend class text_object;

  private:
    text_object const* m_target;
    int m_current_index;

    text_iterator(text_object const* target, int current_index):
      m_target(target),
      m_current_index(current_index)
    {}

    ptrdiff_t diff(text_iterator const& it) const {
      assert_comparable(*this, it);
      return m_current_index - it.m_current_index;
    }

    void move(int d) { m_current_index += d; }

    bool is_at(int k) const { return m_current_index == k; }

  public:
    text_iterator(): m_target(nullptr) {}

    int current_index() const { return m_current_index; }

    bool is_begin() const;
    bool is_end() const;
    bool is_rbegin() const;
    bool is_rend() const;

    text_iterator& operator=(text_iterator const& it) = default;

    char const& operator*() const;
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

    static void assert_comparable(text_iterator const& i, text_iterator const& j)
    {
      if (i.m_target != j.m_target)
      {
        throw iterator_mismatch();
      }
    }
};

static text_iterator operator+(int d, text_iterator const& it) { return it + d; }

class text_object {
  private:
    text_iterator create_iterator(int i) const;

  public:
    typedef text_iterator iterator;

    iterator begin()   const { return create_iterator(           0); }
    iterator cbegin()  const { return create_iterator(           0); }
    iterator end()     const { return create_iterator(    length()); }
    iterator cend()    const { return create_iterator(    length()); }
    iterator rbegin()  const { return create_iterator(length() - 1); }
    iterator crbegin() const { return create_iterator(length() - 1); }
    iterator rend()    const { return create_iterator(          -1); }
    iterator rcend()   const { return create_iterator(          -1); }

    virtual int length() const = 0;
    virtual char const& at(int i) const = 0;
    virtual std::string to_string() const { return std::string(cbegin(), cend()); }
};

text_iterator text_object::create_iterator(int i) const {
  return text_iterator(this, i);
}

class text_string : public text_object {
  private:
    std::string const m_value;

  public:
    text_string(std::string const& value): m_value(value) {}

    virtual int length() const { return m_value.length(); }
    virtual char const& at(int i) const { return m_value.at(i); }
    virtual std::string to_string() const { return m_value; }
};

class text_replacement : public text_object
{
  private:
    text_object const* const m_base;
    text_object::iterator const m_patch_begin;
    text_object::iterator const m_postfix_begin;
    int const m_prefix_length;
    int const m_patch_length;
    int const m_length;

  public:
    text_replacement(
      text_object const* base,
      text_object::iterator cut_from,
      text_object::iterator cut_to,
      text_object::iterator patch_from,
      text_object::iterator patch_to
    ):
      m_base(base),
      m_patch_begin(patch_from),
      m_postfix_begin(cut_to),
      m_prefix_length(cut_from - base->begin()),
      m_patch_length(patch_to - patch_from),
      m_length(base->length() - (cut_to - cut_from) + (patch_to - patch_from))
    {
    }

    text_replacement(
      text_object const* base,
      int cut_from,
      int cut_to,
      text_object const* patch,
      int patch_from,
      int patch_to
    ):
      m_base(base),
      m_patch_begin(patch->begin() + patch_from),
      m_postfix_begin(base->cbegin() + cut_to),
      m_prefix_length(cut_from),
      m_patch_length(patch_to - patch_from),
      m_length(base->length() - (cut_to - cut_from) + (patch_to - patch_from))
    {}

    virtual int length() const { return m_length; }

    virtual char const& at(int i) const {
      auto const i_in_patch = i - m_prefix_length;
      auto const i_in_postfix = i_in_patch - m_patch_length;

      auto const current = i_in_postfix >= 0
        ? m_postfix_begin + i_in_postfix
        : i_in_patch >= 0
          ? m_patch_begin + i_in_patch
          : m_base->cbegin() + i;

      return *current;
    }
};

bool text_iterator::is_begin() const { return is_at(0); }
bool text_iterator::is_end() const { return is_at(m_target->length()); }
bool text_iterator::is_rbegin() const { return is_at(m_target->length() - 1); }
bool text_iterator::is_rend() const { return is_at(-1); }

char const& text_iterator::operator*() const { return m_target->at(m_current_index); }

};
