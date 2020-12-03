/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srslte/adt/bounded_vector.h"
#include "srslte/common/test_common.h"

namespace srslte {

struct C {
  static int nof_copy_ctor;
  static int nof_value_ctor;
  static int nof_move_ctor;
  static int nof_dtor;

  C(int val_ = 0) : val(val_) { nof_value_ctor++; }
  C(const C& v) : val(v.val) { nof_copy_ctor++; }
  C(C&& v) : val(v.val)
  {
    v.val = 0;
    nof_move_ctor++;
  }
  ~C() { nof_dtor++; }
  C& operator=(const C&) = default;
  C& operator            =(C&& other)
  {
    val       = other.val;
    other.val = 0;
    return *this;
  }
  bool operator==(const C& other) const { return val == other.val; }
  bool operator!=(const C& other) const { return not(*this == other); }

  int val = 0;
};
int C::nof_copy_ctor  = 0;
int C::nof_value_ctor = 0;
int C::nof_move_ctor  = 0;
int C::nof_dtor       = 0;

int test_ctor()
{
  // TEST: default ctor
  bounded_vector<int, 10> a;
  TESTASSERT(a.size() == 0);
  TESTASSERT(a.capacity() == 10);
  TESTASSERT(a.empty());

  // TEST: copy ctor
  a.push_back(1);
  bounded_vector<int, 10> a2(a);
  TESTASSERT(a2.size() == a.size());
  TESTASSERT(std::equal(a.begin(), a.end(), a2.begin()));

  // TEST: size ctor
  bounded_vector<int, 5> a3(2);
  TESTASSERT(a3.size() == 2);

  // TEST: size+value ctor
  bounded_vector<int, 15> a4(10, 5);
  TESTASSERT(a4.size() == 10);
  for (auto& v : a4) {
    TESTASSERT(v == 5);
  }

  // TEST: initializer_list ctor
  bounded_vector<int, 20> a5({0, 2, 4, 6, 8, 10, 12});
  TESTASSERT(a5.size() == 7);
  for (size_t i = 0; i < a5.size(); ++i) {
    TESTASSERT(a5[i] == (int)i * 2);
  }

  // TEST: move ctor
  bounded_vector<int, 20> a6(std::move(a5));
  TESTASSERT(a6.size() == 7);
  TESTASSERT(a5.size() == 0);

  return SRSLTE_SUCCESS;
}

int test_obj_add_rem()
{
  // TEST: push_back / emplace_back
  bounded_vector<C, 10> a;
  TESTASSERT(a.size() == 0);
  TESTASSERT(a.empty());
  a.push_back(1);
  a.emplace_back(2);
  TESTASSERT(a.size() == 2);
  TESTASSERT(not a.empty());

  // TEST: resize with size growth
  a.resize(10, 3);
  TESTASSERT(a.size() == 10);
  TESTASSERT(a[0] == 1);
  TESTASSERT(a[1] == 2);
  TESTASSERT(a[2] == 3 and a.back() == 3);

  // TEST: copy ctor correct insertion
  bounded_vector<C, 10> a2(a);
  TESTASSERT(a2.size() == a.size());
  TESTASSERT(std::equal(a.begin(), a.end(), a2.begin()));

  // TEST: back() access
  a.back() = 4;
  TESTASSERT(not std::equal(a.begin(), a.end(), a2.begin()));
  a2 = a;
  TESTASSERT(std::equal(a.begin(), a.end(), a2.begin()));

  // TEST: assign
  a.resize(5);
  a2.assign(a.begin(), a.end());
  TESTASSERT(a2.size() == 5);
  TESTASSERT(std::equal(a.begin(), a.end(), a2.begin()));

  // TEST: pop_back
  int last_nof_dtor = C::nof_dtor;
  a.pop_back();
  TESTASSERT(a.size() == 4 and last_nof_dtor == C::nof_dtor - 1);

  // TEST: erase
  a.erase(a.begin() + 1);
  TESTASSERT(std::equal(a.begin(), a.end(), std::initializer_list<C>{1, 3, 3}.begin()));

  // TEST: clear
  last_nof_dtor = C::nof_dtor;
  a.clear();
  TESTASSERT(a.size() == 0 and a.empty() and last_nof_dtor == C::nof_dtor - 3);

  // TEST: move assignment
  TESTASSERT(a2.size() == 5);
  a = std::move(a2);
  TESTASSERT(a.size() == 5 and a2.empty());

  return SRSLTE_SUCCESS;
}

int assert_dtor_consistency()
{
  TESTASSERT(C::nof_dtor == C::nof_copy_ctor + C::nof_value_ctor + C::nof_move_ctor);
  return SRSLTE_SUCCESS;
}

} // namespace srslte

int main()
{
  TESTASSERT(srslte::test_ctor() == SRSLTE_SUCCESS);
  TESTASSERT(srslte::test_obj_add_rem() == SRSLTE_SUCCESS);
  TESTASSERT(srslte::assert_dtor_consistency() == SRSLTE_SUCCESS);
  printf("Success\n");
  return 0;
}