//  Object model for a text editor with easy undo/redo
//
//  Vadim Vinnik
//  vadim.vinnik@gmail.com
//  2016

#include <cassert>
#include <cstddef>
#include <list>
#include <memory>
#include <numeric>
#include <string>

class string_iterator_helper;
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
  private:
    int const m_end_index;
    int m_current_index;

  protected:
    text_iterator_helper_base(int end_index, int current_index):
      m_end_index(end_index),
      m_current_index(current_index)
    {}

    virtual void move_impl(int d) = 0;

  public:
    int end_index() const { return m_end_index; }
    int current_index() const { return m_current_index; }
    
    ptrdiff_t diff(text_iterator_helper_base const& it) { return m_current_index - it.current_index(); }

    void move(int d) {
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
  char const&
> {
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

    string_iterator_helper(int end_index, int current_index, std::string::const_iterator current):
      text_iterator_helper_base(end_index, current_index),
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
    std::string m_value;

  protected:
    virtual text_iterator_helper_base *begin_helper() const {
      return new string_iterator_helper(
          length(),
          0,
          m_value.cbegin());
    }

    virtual text_iterator_helper_base *end_helper() const {
      return new string_iterator_helper(
          length(),
          length(),
          m_value.cend());
    }

  public:
    text_string(std::string value): m_value(value) {}

    virtual int length() const { return m_value.length(); }
    virtual char const& at(int i) const { return m_value[i]; }
    virtual std::string to_string() const { return m_value; }
};

class selection_iterator_helper: public text_iterator_helper_base {
  friend class text_selection;

  private:
    text_object::iterator m_current;
    int m_current_index;

    selection_iterator_helper(
      text_object::iterator current,
      int current_index,
      int length
    ):
      text_iterator_helper_base(length, current_index),
      m_current(current),
      m_current_index(current_index)
    {}

  protected:
    virtual void move_impl(int d) { m_current += d; }

  public:
    virtual text_iterator_helper_base *clone() const {
      return new selection_iterator_helper(
          m_current,
          m_current_index,
          end_index());
    }

    virtual char const& value() const { return *m_current; }
};
                                
class text_selection : public text_object
{
  private:
    text_object const* m_base;
    text_object::iterator m_start;
    text_object::iterator m_end;
    int m_length;

  protected:
    virtual helper *begin_helper() const {
      return new selection_iterator_helper(m_start, 0, m_length);
    }

    virtual helper *end_helper() const {
      return new selection_iterator_helper(m_end, m_length, m_length);
    }

  public:
    text_selection(text_object const* base, int start, int end):
      m_base(base),
      m_start(base->cbegin() + start),
      m_end(base->cbegin() + end),
      m_length(end - start)
    {}

    text_selection(text_object const* base, text_object::iterator start, int length):
      m_base(base),
      m_start(start),
      m_end(start + length),
      m_length(length)
    {}

    text_selection(text_object const* base, text_object::iterator start, text_object::iterator end):
      m_base(base),
      m_start(start),
      m_end(end),
      m_length(end - start)
    {}

    virtual int length() const { return m_length; }
    virtual char const& at(int i) const { return *(m_start + i); }
};

class composition_iterator_helper : public text_iterator_helper_base
{
};

class text_composition
{
  private:
    std::list<text_selection const*> m_components;
    int m_length;

  public:
    template<typename It>
    text_composition(It begin, It end):
      m_components(begin, end),
      m_length(std::accumulate(begin, end, 0, [](int s, It i) -> int { return s + i->length(); }))
    {}

    virtual int length() const { return m_length; }
};
