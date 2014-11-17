// vim:filetype=cpp:textwidth=80:shiftwidth=2:softtabstop=2:expandtab
// Copyright 2014 schwering@kbsg.rwth-aachen.de

#include "./clause.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <tuple>

namespace esbl {

const SimpleClause SimpleClause::EMPTY({});
const Clause Clause::EMPTY(false, Ewff::TRUE, {});
const Clause Clause::MIN_UNIT(false, Ewff::TRUE, {Literal::MIN});
const Clause Clause::MAX_UNIT(false, Ewff::TRUE, {Literal::MAX});

bool SimpleClause::operator==(const SimpleClause& c) const {
  return std::operator==(*this, c);
}

bool SimpleClause::operator<(const SimpleClause& c) const {
  return size() < c.size() || (size() == c.size() && std::operator<(*this, c));
}

SimpleClause SimpleClause::PrependActions(const TermSeq& z) const {
  SimpleClause c;
  for (const Literal& l : *this) {
    c.insert(c.end(), l.PrependActions(z));
  }
  return c;
}

SimpleClause SimpleClause::Substitute(const Unifier& theta) const {
  SimpleClause c;
  for (const Literal& l : *this) {
    c.insert(c.end(), l.Substitute(theta));
  }
  return c;
}

SimpleClause SimpleClause::Ground(const Assignment& theta) const {
  SimpleClause c;
  for (const Literal& l : *this) {
    c.insert(c.end(), l.Ground(theta));
  }
  return c;
}

void SimpleClause::SubsumedBy(const const_iterator first,
                              const const_iterator last,
                              const Unifier& theta,
                              std::list<Unifier>* thetas) const {
  if (first == last) {
    thetas->push_back(theta);
    return;
  }
  const Literal l = first->Substitute(theta);
  const auto first2 = lower_bound(l.LowerBound());
  const auto last2 = lower_bound(l.UpperBound());
  for (auto it = first2; it != last2; ++it) {
    const Literal& ll = *it;
    assert(l.pred() == ll.pred());
    if (l.sign() != ll.sign()) {
      continue;
    }
    Unifier theta2 = theta;
    const bool succ = ll.Matches(l, &theta2);
    if (succ && ll == l.Substitute(theta2)) {
      SubsumedBy(std::next(first), last, theta2, thetas);
    }
  }
}

std::list<Unifier> SimpleClause::Subsumes(const SimpleClause& c) const {
  std::list<Unifier> thetas;
  c.SubsumedBy(begin(), end(), Unifier(), &thetas);
  return thetas;
}

Maybe<Unifier, SimpleClause> SimpleClause::Unify(const Atom& cl_a,
                                                 const Atom& ext_a) const {
  const Maybe<Unifier> theta = Atom::Unify(cl_a, ext_a);
  if (!theta) {
    return Nothing;
  }
  SimpleClause c = Substitute(theta.val);
  return Just(theta.val, c);
}

void SimpleClause::GenerateInstances(const Variable::Set::const_iterator first,
                                     const Variable::Set::const_iterator last,
                                     const StdName::SortedSet& hplus,
                                     Assignment* theta,
                                     std::list<SimpleClause>* instances) const {
  if (first != last) {
    const Variable& x = *first;
    StdName::SortedSet::const_iterator ns_it = hplus.find(x.sort());
    assert(ns_it != hplus.end());
    if (ns_it == hplus.end()) {
      return;
    }
    const StdName::Set& ns = ns_it->second;
    for (const StdName& n : ns) {
      (*theta)[x] = n;
      GenerateInstances(std::next(first), last, hplus, theta, instances);
    }
    theta->erase(x);
  } else {
    const SimpleClause c = Ground(*theta);
    assert(c.is_ground());
    instances->push_back(c);
  }
}

std::list<SimpleClause> SimpleClause::Instances(
    const StdName::SortedSet& hplus) const {
  std::list<SimpleClause> instances;
  Assignment theta;
  const Variable::Set vs = Variables();
  GenerateInstances(vs.begin(), vs.end(), hplus, &theta, &instances);
  return instances;
}

Atom::Set SimpleClause::Sensings() const {
  Atom::Set sfs;
  for (const Literal& l : *this) {
    const TermSeq& z = l.z();
    for (auto it = z.begin(); it != z.end(); ++it) {
      const TermSeq zz(z.begin(), it);
      const Term& t = *it;
      sfs.insert(Atom(zz, Atom::SF, {t}));
    }
  }
  return sfs;
}

Variable::Set SimpleClause::Variables() const {
  Variable::Set vs;
  for (const Literal& l : *this) {
    l.CollectVariables(&vs);
  }
  return vs;
}

void SimpleClause::CollectVariables(Variable::SortedSet* vs) const {
  for (const Literal& l : *this) {
    l.CollectVariables(vs);
  }
}

void SimpleClause::CollectNames(StdName::SortedSet* ns) const {
  for (const Literal& l : *this) {
    l.CollectNames(ns);
  }
}

bool SimpleClause::is_ground() const {
  for (const Literal& l : *this) {
    if (!l.is_ground()) {
      return false;
    }
  }
  return true;
}

bool Clause::operator==(const Clause& c) const {
  return ls_ == c.ls_ && box_ == c.box_ && e_ == c.e_;
}

bool Clause::operator<(const Clause& c) const {
  // shortest clauses first
  return std::tie(ls_, box_, e_) < std::tie(c.ls_, c.box_, c.e_);
}

Clause Clause::InstantiateBox(const TermSeq& z) const {
  assert(box_);
  return Clause(false, e_, ls_.PrependActions(z));
}

Maybe<Clause> Clause::Substitute(const Unifier& theta) const {
  const Maybe<Ewff> e = e_.Substitute(theta);
  if (!e) {
    return Nothing;
  }
  const SimpleClause ls = ls_.Substitute(theta);
  return Just(Clause(box_, e.val, ls));
}

Maybe<Unifier, Clause> Clause::Unify(const Atom& cl_a,
                                     const Atom& ext_a) const {
  if (box_) {
    const Maybe<TermSeq> z = ext_a.z().WithoutLast(cl_a.z().size());
    if (!z) {
      return Nothing;
    }
    const Atom a = cl_a.PrependActions(z.val);
    const Clause c = InstantiateBox(z.val);
    return c.Unify(a, ext_a);
  } else {
    const Maybe<Unifier, SimpleClause> p = ls_.Unify(cl_a, ext_a);
    if (!p) {
      return Nothing;
    }
    const Unifier& theta = p.val1;
    const SimpleClause& ls = p.val2;
    const Maybe<Ewff> e = e_.Substitute(theta);
    if (!e) {
      return Nothing;
    }
    const Clause c = Clause(false, e.val, ls);
    return Just(theta, c);
  }
}

bool Clause::Subsumes(const Clause& c) const {
  if (ls_.empty()) {
    return true;
  }
  if (!box_ && c.box_) {
    return false;
  }
  auto cmp_maybe_subsuming =
      box_
      ? [](const Literal& l1, const Literal& l2) {
          return l1.pred() < l2.pred();
        }
      : [](const Literal& l1, const Literal& l2) {
          return l1.pred() < l2.pred() ||
              (l1.pred() == l2.pred() &&
               l1.z().size() < l2.z().size()) ||
              (l1.pred() == l2.pred() &&
               l1.z() == l2.z() &&
               l1.args().size() < l2.args().size()) ||
              (l1.pred() == l2.pred() &&
               l1.z() == l2.z() &&
               l1.args() == l2.args() &&
               l1.sign() < l2.sign());
        };
  if (!std::includes(c.ls_.begin(), c.ls_.end(), ls_.begin(), ls_.end(),
                     cmp_maybe_subsuming)) {
    return false;
  }
  if (box_) {
    const Literal& l = *ls_.begin();
    const auto first = c.ls_.lower_bound(l.LowerBound());
    const auto last = c.ls_.lower_bound(l.UpperBound());
    for (auto it = first; it != last; ++it) {
      const Literal& ll = *it;
      assert(l.pred() == ll.pred());
      if (l.sign() != ll.sign()) {
        continue;
      }
      const Maybe<TermSeq> z = ll.z().WithoutLast(l.z().size());
      if (z && InstantiateBox(z.val).Subsumes(c)) {
        return true;
      }
    }
    return false;
  } else {
    for (const Unifier& theta : ls_.Subsumes(c.ls_)) {
      const Maybe<Ewff> e = e_.Substitute(theta);
      if (e && c.e_.Subsumes(e.val)) {
        return true;
      }
    }
    return false;
  }
}

void Clause::Rel(const StdName::SortedSet& hplus,
                 const Literal &ext_l,
                 std::deque<Literal>* rel) const {
  const size_t max_z = std::accumulate(ls_.begin(), ls_.end(), 0,
      [](size_t n, const Literal& l) { return std::max(n, l.z().size()); });
  // Given a clause l and a clause e->c such that for some l' in c with
  // l = l'.theta for some theta and |= e.theta.theta' for some theta', for all
  // clauses l'' c.theta.theta', its negation ~l'' is relevant to l.
  // Relevance means that splitting ~l'' may help to prove l.
  // In case c is a box clause, it must be unified as well.
  // Due to the constrained form of BATs, only l' with maximum action length in
  // e->c must be considered. Intuitively, in an SSA Box([a]F <-> ...) this is
  // [a]F, and in Box(SF(a) <-> ...) it is every literal of the fluent.
  for (const Literal& cl_l : ls_) {
    if (ext_l.sign() != cl_l.sign() ||
        ext_l.pred() != cl_l.pred() ||
        cl_l.z().size() < max_z) {
      continue;
    }
    Maybe<Unifier, Clause> p = Unify(cl_l, ext_l);
    if (!p) {
      continue;
    }
    const Unifier& theta = p.val1;
    Clause& c = p.val2;
    size_t n = c.ls_.erase(ext_l.Substitute(theta));
    assert(n >= 1);
    for (const Assignment& theta : c.e_.Models(hplus)) {
      for (const Literal& ll : c.ls_) {
        rel->insert(rel->end(), ll.Ground(theta).Flip());
      }
    }
  }
}

bool Clause::SplitRelevant(const Atom& a, const Clause& c, int k) const {
  assert(k >= 0);
  const SimpleClause::const_iterator it1 = ls_.find(Literal(true, a));
  const SimpleClause::const_iterator it2 = ls_.find(Literal(false, a));
  // ls_.size() - c.size() is a (bad) approximation of (d \ c).size() where d is
  // a unification of ls_ and c
  const int ls_size = static_cast<int>(ls_.size());
  const int c_size = static_cast<int>(c.ls_.size());
  return (it1 != ls_.end() || it2 != ls_.end()) &&
         (ls_size <= k + 1 || ls_size - c_size <= k);
}

size_t Clause::ResolveWithUnit(const Clause& unit, Clause::Set* rs) const {
  assert(unit.is_unit());
  const Literal& unit_l = *unit.literals().begin();
  const auto first = ls_.lower_bound(unit_l.LowerBound());
  const auto last = ls_.lower_bound(unit_l.UpperBound());
  size_t n_new_clauses = 0;
  for (auto it = first; it != last; ++it) {
    const Literal& l = *it;
    assert(unit_l.pred() == l.pred());
    if (unit_l.sign() == l.sign()) {
      continue;
    }
    Maybe<Unifier, Clause> p = Unify(l, unit_l);
    if (!p) {
      continue;
    }
    const Unifier& theta = p.val1;
    Clause& d = p.val2;
    const Maybe<Ewff> unit_e = unit.e_.Substitute(theta);
    if (!unit_e) {
      continue;
    }
    const size_t n = d.ls_.erase(unit_l.Substitute(theta).Flip());
    assert(n > 0);
    d.e_ = Ewff::And(d.e_, unit_e.val);
    d.e_.RestrictVariable(d.ls_.Variables());
    const auto pp = rs->insert(d);
    if (pp.second) {
      ++n_new_clauses;
    }
  }
  return n_new_clauses;
}

void Clause::CollectVariables(Variable::SortedSet* vs) const {
  e_.CollectVariables(vs);
  ls_.CollectVariables(vs);
}

void Clause::CollectNames(StdName::SortedSet* ns) const {
  e_.CollectNames(ns);
  ls_.CollectNames(ns);
}

std::ostream& operator<<(std::ostream& os, const SimpleClause& c) {
  os << '(';
  for (auto it = c.begin(); it != c.end(); ++it) {
    if (it != c.begin()) {
      os << " v ";
    }
    os << *it;
  }
  os << ')';
  return os;
}

std::ostream& operator<<(std::ostream& os, const Clause& c) {
  if (c.box()) {
    os << "box(";
  }
  os << c.ewff() << " -> " << c.literals();
  if (c.box()) {
    os << ")";
  }
  return os;
}

}  // namespace esbl

