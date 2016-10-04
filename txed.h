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

template<class TString>
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

template<class TString>
class text_iterator:
  public boost::random_access_iterator_helper<
    text_iterator<TString>,
    typename TString::value_type,
    typename TString::difference_type,
    typename TString::const_pointer,
    typename TString::const_reference
  >
{
  friend class text_object<TString>;

  private:
    text_object<TString> const* m_target;
    typename TString::size_type m_current_index;

    text_iterator(text_object<TString> const* target, typename TString::size_type current_index):
      m_target(target),
      m_current_index(current_index)
    {}

    typename TString::difference_type diff(text_iterator<TString> const& it) const {
      assert_comparable(*this, it);
      return m_current_index - it.m_current_index;
    }

    bool is_at(typename TString::size_type k) const { return m_current_index == k; }

    void move(typename TString::difference_type d) { m_current_index += d; }
    void move_to(typename TString::size_type k) { m_current_index = k; }

  public:
    text_iterator(): m_target(nullptr) {}

    typename TString::size_type current_index() const { return m_current_index; }

    // extra functionality not required by the standard definition of iterator
    bool is_begin() const;
    bool is_end() const;
    void move_to_begin();
    void move_to_end();

    typename TString::const_reference operator*() const;

    bool operator==(text_iterator<TString> const& it) const { return diff(it) == 0; }
    bool operator< (text_iterator<TString> const& it) const { return diff(it) <  0; }

    text_iterator& operator++() { move(+1); return *this; }
    text_iterator& operator--() { move(-1); return *this; }
    text_iterator& operator+=(typename TString::difference_type d) { move(+d); return *this; }
    text_iterator& operator-=(typename TString::difference_type d) { move(-d); return *this; }

    typename TString::difference_type operator-(text_iterator<TString> const& it) const { return diff(it); }

    static void assert_comparable(text_iterator<TString> const& i, text_iterator<TString> const& j)
    {
      if (i.m_target != j.m_target)
      {
        throw iterator_mismatch();
      }
    }
};

template<class TString>
using string_segment = std::pair<typename TString::const_iterator, typename TString::const_iterator>;

template<class TString>
using rope = std::map<typename TString::size_type, string_segment<TString> >;

template<class TString>
using rope_node = typename rope<TString>::value_type;

template<class TString>
class rope_node_trimmer {
  private:
    typename TString::size_type m_new_begin_offset;
    typename TString::size_type m_new_end_offset;
    typename TString::difference_type m_shift;

  public:
    rope_node_trimmer(
      typename TString::size_type new_begin_offset,
      typename TString::size_type new_end_offset,
      typename TString::difference_type shift
    ):
      m_new_begin_offset(new_begin_offset),
      m_new_end_offset(new_end_offset),
      m_shift(shift)
    {}

    typename TString::size_type new_begin_offset() const { return m_new_begin_offset; }
    typename TString::size_type new_end_offset() const { return m_new_end_offset; }
    typename TString::difference_type shift() const { return m_shift; }

    rope_node<TString> operator()(rope_node<TString> const& x) const {
      auto const& end_offset = x.first;
      auto const& begin = x.second.first;
      auto const& end = x.second.second;
      auto const length = end - begin;
      auto const begin_offset = end_offset - length;

      assert(end_offset >= m_new_begin_offset);
      assert(begin_offset <= m_new_end_offset);

      auto const begin_shift = std::max<typename TString::difference_type>(0, m_new_begin_offset - begin_offset);
      auto const end_shift = std::min<typename TString::difference_type>(0, m_new_end_offset - end_offset);
      auto const new_begin = begin + begin_shift;
      auto const new_end = end + end_shift;
      auto const new_end_offset = end_offset - m_new_begin_offset + end_shift + m_shift;

      return rope_node<TString>(new_end_offset, string_segment<TString>(new_begin, new_end));
    }
};

template<class TString>
class rope_trimmed_range {
  public:
    typedef
      boost::transform_iterator<
        rope_node_trimmer<TString>,
        typename rope<TString>::const_iterator,
        typename rope<TString>::const_reference,
        typename rope<TString>::value_type
      >
      iterator;

  private:
    rope<TString> const* const m_base;
    rope_node_trimmer<TString> m_trimmer;

    iterator make_iterator(typename rope<TString>::const_iterator const& it) const {
      return boost::make_transform_iterator(it, m_trimmer);
    }

  public:
    rope_trimmed_range(
      rope<TString> const* base, 
      typename TString::size_type begin,
      typename TString::size_type end,
      typename TString::difference_type shift
    ):
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

template<class TString>
class text_object {
  private:
    text_iterator<TString> create_iterator(typename TString::size_type i) const {
      return text_iterator<TString>(this, i);
    }

  protected:
    rope<TString> const m_rope;

    text_object(rope<TString> const& rope): m_rope(rope) {}

  public:
    typedef text_iterator<TString> iterator;

    rope<TString> const& get_rope() const { return m_rope; }

    typename TString::size_type length() const {
      auto it = m_rope.end();

      if (it == m_rope.begin()) return 0;

      --it;

      return it->first;
    }

    iterator begin()   const { return create_iterator(       0); }
    iterator end()     const { return create_iterator(length()); }
    iterator cbegin()  const { return begin(); }
    iterator cend()    const { return end();   }

    typename TString::const_reference at(typename TString::size_type i) const {
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

    TString to_string() const { return TString(cbegin(), cend()); }
};

template<class TString>
class text_string : public text_object<TString> {
  private:
    TString const m_value;

    static rope<TString> string_to_rope(TString const& value) {
      return value.cend() != value.cbegin()
        ? rope<TString> {
            std::make_pair(
              value.length(),
              string_segment<TString>(
                value.cbegin(),
                value.cend()
              )
            )
          }
        : rope<TString>();
    }

  public:
    text_string(TString const& value):
      text_object<TString>(string_to_rope(value)),
      m_value(value)
    {}
};

template<class TString>
class text_replacement : public text_object<TString>
{
  private:
    text_object<TString> const* const m_base;
    typename text_object<TString>::iterator const m_patch_begin;
    typename text_object<TString>::iterator const m_postfix_begin;
    typename TString::size_type const m_prefix_length;
    typename TString::size_type const m_patch_length;
    typename TString::size_type const m_length;

    static rope<TString> make_rope(
      text_object<TString> const* base,
      typename TString::size_type cut_from,
      typename TString::size_type cut_to,
      text_object<TString> const* patch,
      typename TString::size_type patch_from,
      typename TString::size_type patch_to
    ) {
      assert(cut_from <= base->length());
      assert(cut_to <= base->length());
      assert(patch_from <= patch->length());
      assert(patch_to <= patch->length());

      auto const& base_map = base->get_rope();
      auto const& patch_map = patch->get_rope();

      auto const prefix_view = rope_trimmed_range<TString>(&base_map, 0, cut_from, 0);
      auto const patch_view = rope_trimmed_range<TString>(&patch_map, patch_from, patch_to, cut_from);
      auto const postfix_view = rope_trimmed_range<TString>(&base_map, cut_to, base->length(), cut_from + patch_to - patch_from);

      auto const joined = boost::join(
        prefix_view.range(),
        boost::join(
          patch_view.range(),
          postfix_view.range()
        )
      );

      return rope<TString>(joined.begin(), joined.end());
    }

  public:
    text_replacement(
      text_object<TString> const* base,
      typename text_object<TString>::iterator cut_from,
      typename text_object<TString>::iterator cut_to,
      text_object<TString> const* patch,
      typename text_object<TString>::iterator patch_from,
      typename text_object<TString>::iterator patch_to
    ):
      text_object<TString>(
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
      text_object<TString> const* base,
      typename TString::size_type cut_from,
      typename TString::size_type cut_to,
      text_object<TString> const* patch,
      typename TString::size_type patch_from,
      typename TString::size_type patch_to
    ):
      text_object<TString>(
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

template<class TString> bool text_iterator<TString>::is_begin() const { return is_at(0); }
template<class TString> bool text_iterator<TString>::is_end() const { return is_at(m_target->length()); }
template<class TString> void text_iterator<TString>::move_to_begin() { move_to(0); }
template<class TString> void text_iterator<TString>::move_to_end() { move_to(m_target->length()); }

template<class TString>
typename TString::const_reference text_iterator<TString>::operator*() const { return m_target->at(m_current_index); }

};
