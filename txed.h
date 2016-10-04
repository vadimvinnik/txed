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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/operators.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/range/join.hpp>

namespace text_edit {

class text_object;

class iterator_mismatch: public std::domain_error {
  public:
    iterator_mismatch(): std::domain_error("Cannot subtract or compare iterators pointing to different containers") {}
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

class text_iterator:
  public boost::random_access_iterator_helper<text_iterator, const char, int, char const*, char const&>
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

    bool is_at(int k) const { return m_current_index == k; }

    void move(int d) { m_current_index += d; }
    void move_to(int k) { m_current_index = k; }

  public:
    text_iterator(): m_target(nullptr) {}

    int current_index() const { return m_current_index; }

    // extra functionality not required by the standard definition of iterator
    bool is_begin() const;
    bool is_end() const;
    void move_to_begin();
    void move_to_end();

    char const& operator*() const;

    bool operator==(text_iterator const& it) const { return diff(it) == 0; }
    bool operator< (text_iterator const& it) const { return diff(it) <  0; }

    text_iterator& operator++()      { move(+1); return *this; }
    text_iterator& operator--()      { move(-1); return *this; }
    text_iterator& operator+=(int d) { move(+d); return *this; }
    text_iterator& operator-=(int d) { move(-d); return *this; }

    ptrdiff_t operator-(text_iterator const& it) const { return diff(it); }

    static void assert_comparable(text_iterator const& i, text_iterator const& j)
    {
      if (i.m_target != j.m_target)
      {
        throw iterator_mismatch();
      }
    }
};

typedef std::pair<std::string::const_iterator, std::string::const_iterator> string_segment;

typedef std::map<int, string_segment> rope;

typedef rope::value_type rope_node;

class rope_node_trimmer {
  private:
    int m_new_begin_offset;
    int m_new_end_offset;
    int m_shift;

  public:
    rope_node_trimmer(int new_begin_offset, int new_end_offset, int shift):
      m_new_begin_offset(new_begin_offset),
      m_new_end_offset(new_end_offset),
      m_shift(shift)
    {}

    int new_begin_offset() const { return m_new_begin_offset; }
    int new_end_offset() const { return m_new_end_offset; }
    int shift() const { return m_shift; }

    rope_node operator()(rope_node const& x) const {
      auto const& end_offset = x.first;
      auto const& begin = x.second.first;
      auto const& end = x.second.second;
      auto const length = end - begin;
      auto const begin_offset = end_offset - length;

      assert(end_offset >= m_new_begin_offset);
      assert(begin_offset <= m_new_end_offset);

      auto const begin_shift = std::max(0L, m_new_begin_offset - begin_offset);
      auto const end_shift = std::min(0, m_new_end_offset - end_offset);
      auto const new_begin = begin + begin_shift;
      auto const new_end = end + end_shift;
      auto const new_end_offset = end_offset - m_new_begin_offset + end_shift + m_shift;

      return rope_node(new_end_offset, string_segment(new_begin, new_end));
    }
};

class rope_trimmed_range {
  public:
    typedef boost::transform_iterator<rope_node_trimmer, rope::const_iterator> iterator;

  private:
    rope const* const m_base;
    rope_node_trimmer m_trimmer;

    iterator make_iterator(rope::const_iterator const& it) const {
      return boost::make_transform_iterator(it, m_trimmer);
    }

  public:
    rope_trimmed_range(rope const* base, int begin, int end, int shift):
      m_base(base),
      m_trimmer(begin, end, shift)
    {}

    iterator begin() const { return make_iterator(m_base->upper_bound(m_trimmer.new_begin_offset())); }

    iterator end() const {
      auto it = m_base->upper_bound(m_trimmer.new_end_offset());

      if (it != m_base->end()) {
        ++it;
      }

      return make_iterator(it);
    }

    std::pair<iterator, iterator> range() const { return std::make_pair(begin(), end()); }
};

class text_object {
  private:
    text_iterator create_iterator(int i) const { return text_iterator(this, i); }

  protected:
    rope const m_rope;

    text_object(rope const& rope): m_rope(rope) {}

  public:
    typedef text_iterator iterator;

    iterator begin()   const { return create_iterator(       0); }
    iterator end()     const { return create_iterator(length()); }
    iterator cbegin()  const { return begin(); }
    iterator cend()    const { return end();   }

    rope const& get_rope() const { return m_rope; }

    int length() const {
      auto it = m_rope.end();

      if (it == m_rope.begin()) return 0;

      --it;

      return it->first;
    }

    char const& at(int i) const {
      auto segment_it = m_rope.upper_bound(i);

      if (segment_it == m_rope.end())
      {
        throw text_out_of_range(i, length());
      }

      auto const& segment_end_offset = segment_it->first;
      auto const& segment_end = segment_it->second.second;

      auto atom_it = segment_end - (segment_end_offset - i);

      return *atom_it;
    }

    std::string to_string() const { return std::string(cbegin(), cend()); }
};

class text_string : public text_object {
  private:
    std::string const m_value;

    static rope string_to_rope(std::string const& value) {
      return value.cend() != value.cbegin()
        ? rope {
            std::make_pair(
              value.length(),
              string_segment(
                value.cbegin(),
                value.cend()
              )
            )
          }
        : rope();
    }

  public:
    text_string(std::string const& value):
      text_object(string_to_rope(value)),
      m_value(value)
    {}
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

    static rope make_rope(
      text_object const* base,
      int cut_from,
      int cut_to,
      text_object const* patch,
      int patch_from,
      int patch_to
    ) {
      assert(cut_from <= base->length());
      assert(cut_to <= base->length());
      assert(patch_from <= patch->length());
      assert(patch_to <= patch->length());

      auto const& base_map = base->get_rope();
      auto const& patch_map = patch->get_rope();

      auto const prefix_view = rope_trimmed_range(&base_map, 0, cut_from, 0);
      auto const patch_view = rope_trimmed_range(&patch_map, patch_from, patch_to, cut_from);
      auto const postfix_view = rope_trimmed_range(&base_map, cut_to, base->length(), cut_from + patch_to - patch_from);

      auto const joined = boost::join(
        prefix_view.range(),
        boost::join(
          patch_view.range(),
          postfix_view.range()
        )
      );

      return rope(joined.begin(), joined.end());
    }

  public:
    text_replacement(
      text_object const* base,
      text_object::iterator cut_from,
      text_object::iterator cut_to,
      text_object const* patch,
      text_object::iterator patch_from,
      text_object::iterator patch_to
    ):
      text_object(
        make_rope(
          base,
          cut_from - base->begin(),
          cut_to - base->begin(),
          patch,
          patch_from - patch->begin(),
          patch_to - patch->begin()
        )
      ),
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
      text_object(
        make_rope(
          base,
          cut_from,
          cut_to,
          patch,
          patch_from,
          patch_to
        )
      ),
      m_base(base),
      m_patch_begin(patch->begin() + patch_from),
      m_postfix_begin(base->cbegin() + cut_to),
      m_prefix_length(cut_from),
      m_patch_length(patch_to - patch_from),
      m_length(base->length() - (cut_to - cut_from) + (patch_to - patch_from))
    {}
};

bool text_iterator::is_begin() const { return is_at(0); }
bool text_iterator::is_end() const { return is_at(m_target->length()); }
void text_iterator::move_to_begin() { move_to(0); }
void text_iterator::move_to_end() { move_to(m_target->length()); }

char const& text_iterator::operator*() const { return m_target->at(m_current_index); }

};
