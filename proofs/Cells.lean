/-
  PrepLang core: what the analysis needs to know about the data.

  The analysis compares the bounds of a column only with the numbers of the
  program (FILTER c >= k, FILTER c <= k, EXPECT c BETWEEN a AND b), and its
  transfer functions only raise, lower or join bounds with numbers. So two
  abstract states that agree on every such comparison, and on whether a column
  may have missing values, lead to the same decisions at every later statement.

  For a table, the comparisons are about cells: does the column have a missing
  value, does it have any value, and for each number k of the program, is there
  a value below k, is there one above k. These are the occupied cells of the
  partition the program's numbers cut each column into.

  Proved here, for programs without DERIVE (whose interval addition compares sums
  of bounds, not bounds, with the numbers):
    provesAt_stateEq  two states that agree on every comparison with the
                      program's numbers prove the same statements;
    cells_decide      two tables with the same occupied cells get the same
                      decisions from the analysis: what the compiler removes
                      or proves depends on the data only through which cells
                      are occupied.
  This is the property a private compilation relies on: a release of which cells
  are occupied is all the analysis can use, so a differentially private release of
  them loses nothing the analysis would otherwise have used, other than its noise.

  Lean 4 core only (no Mathlib). Build with `lake build` in proofs/.
-/
import Core

namespace PrepLang.Core

/-! ## States the program's comparisons cannot tell apart -/

/-- The numbers a statement compares a bound with. -/
def consts : Stmt → List Int
  | .filterGe _ k => [k]
  | .filterLe _ k => [k]
  | .expectBetween _ a b => [a, b]
  | _ => []

def noDerive : Stmt → Bool
  | .derive _ _ _ => false
  | _ => true

def ColEq (K : List Int) (a b : Col) : Prop :=
  a.nullable = b.nullable ∧ (∀ k, k ∈ K → loAtLeast a.lo k = loAtLeast b.lo k) ∧
    (∀ k, k ∈ K → hiAtMost a.hi k = hiAtMost b.hi k)

def StateEq (K : List Int) (σ τ : AState) : Prop := ∀ c, ColEq K (σ c) (τ c)

/-! ### How the transfer functions act on comparisons -/

theorem loAtLeast_raiseLo (a : Option Int) (k0 k : Int) :
    loAtLeast (raiseLo a k0) k = (loAtLeast a k || decide (k ≤ k0)) := by
  cases a with
  | none => simp [raiseLo, loAtLeast]
  | some x =>
    simp only [raiseLo, loAtLeast]
    by_cases h : x ≤ k0 <;> by_cases h1 : k ≤ x <;> by_cases h2 : k ≤ k0 <;> simp [h, h1, h2] <;> omega

theorem hiAtMost_lowerHi (a : Option Int) (k0 k : Int) :
    hiAtMost (lowerHi a k0) k = (hiAtMost a k || decide (k0 ≤ k)) := by
  cases a with
  | none => simp [lowerHi, hiAtMost]
  | some x =>
    simp only [lowerHi, hiAtMost]
    by_cases h : k0 ≤ x <;> by_cases h1 : x ≤ k <;> by_cases h2 : k0 ≤ k <;> simp [h, h1, h2] <;> omega

theorem loAtLeast_joinLo (a : Option Int) (v k : Int) :
    loAtLeast (joinLo a v) k = (loAtLeast a k && decide (k ≤ v)) := by
  cases a with
  | none => simp [joinLo, loAtLeast]
  | some x =>
    simp only [joinLo, loAtLeast]
    by_cases h : x ≤ v <;> by_cases h1 : k ≤ x <;> by_cases h2 : k ≤ v <;> simp [h, h1, h2] <;> omega

theorem hiAtMost_joinHi (a : Option Int) (v k : Int) :
    hiAtMost (joinHi a v) k = (hiAtMost a k && decide (v ≤ k)) := by
  cases a with
  | none => simp [joinHi, hiAtMost]
  | some x =>
    simp only [joinHi, hiAtMost]
    by_cases h : v ≤ x <;> by_cases h1 : x ≤ k <;> by_cases h2 : v ≤ k <;> simp [h, h1, h2] <;> omega

theorem stateEq_setCol (K : List Int) (σ τ : AState) (c : Nat) (a b : Col)
    (h : StateEq K σ τ) (hab : ColEq K a b) : StateEq K (setCol σ c a) (setCol τ c b) := by
  intro c'
  by_cases hc : c' = c
  · simp only [setCol, hc, ite_true]; exact hab
  · simp only [setCol, hc, ite_false]; exact h c'

/-! ### One statement -/

theorem proves_stateEq (K : List Int) (s : Stmt) (σ τ : AState)
    (hK : ∀ x, x ∈ consts s → x ∈ K) (h : StateEq K σ τ) : proves σ s = proves τ s := by
  cases s with
  | fill c v => simp only [proves, (h c).1]
  | filterGe c k => simp only [proves, (h c).1, (h c).2.1 k (hK k (by simp [consts]))]
  | filterLe c k => simp only [proves, (h c).1, (h c).2.2 k (hK k (by simp [consts]))]
  | dropNull c => simp only [proves, (h c).1]
  | expectBetween c a b =>
    simp only [proves, (h c).2.1 a (hK a (by simp [consts])), (h c).2.2 b (hK b (by simp [consts]))]
  | expectNotNull c => simp only [proves, (h c).1]
  | derive c a b => rfl

theorem absStep_stateEq (K : List Int) (s : Stmt) (σ τ : AState)
    (hd : noDerive s = true) (h : StateEq K σ τ) : StateEq K (absStep s σ) (absStep s τ) := by
  cases s with
  | fill c v =>
    simp only [absStep, (h c).1]
    split
    · apply stateEq_setCol K σ τ c _ _ h
      refine ⟨rfl, ?_, ?_⟩
      · intro k hk; simp only [loAtLeast_joinLo, (h c).2.1 k hk]
      · intro k hk; simp only [hiAtMost_joinHi, (h c).2.2 k hk]
    · exact h
  | filterGe c k0 =>
    apply stateEq_setCol K σ τ c _ _ h
    refine ⟨rfl, ?_, (h c).2.2⟩
    intro k hk; simp only [loAtLeast_raiseLo, (h c).2.1 k hk]
  | filterLe c k0 =>
    apply stateEq_setCol K σ τ c _ _ h
    refine ⟨rfl, (h c).2.1, ?_⟩
    intro k hk; simp only [hiAtMost_lowerHi, (h c).2.2 k hk]
  | dropNull c => exact stateEq_setCol K σ τ c _ _ h ⟨rfl, (h c).2.1, (h c).2.2⟩
  | expectBetween c a b =>
    apply stateEq_setCol K σ τ c _ _ h
    refine ⟨(h c).1, ?_, ?_⟩
    · intro k hk; simp only [loAtLeast_raiseLo, (h c).2.1 k hk]
    · intro k hk; simp only [hiAtMost_lowerHi, (h c).2.2 k hk]
  | expectNotNull c => exact stateEq_setCol K σ τ c _ _ h ⟨rfl, (h c).2.1, (h c).2.2⟩
  | derive c a b => simp [noDerive] at hd

/-! ### Whole programs -/

/-- Two states that agree on every comparison with the program's numbers prove the
    same statements, everywhere in the program. -/
theorem provesAt_stateEq (K : List Int) : ∀ (ss : List Stmt) (σ τ : AState) (k j : Nat),
    (∀ s, s ∈ ss → noDerive s = true) → (∀ s, s ∈ ss → ∀ x, x ∈ consts s → x ∈ K) →
    StateEq K σ τ → provesAt σ ss k j = provesAt τ ss k j := by
  intro ss
  induction ss with
  | nil => intro _ _ _ _ _ _ _; rfl
  | cons s ss ih =>
    intro σ τ k j hd hK h
    by_cases hj : j = k
    · simp only [provesAt, hj, ite_true]
      exact proves_stateEq K s σ τ (hK s List.mem_cons_self) h
    · simp only [provesAt, hj, ite_false]
      exact ih (absStep s σ) (absStep s τ) (k + 1) j
        (fun s' hs' => hd s' (List.mem_cons_of_mem s hs'))
        (fun s' hs' => hK s' (List.mem_cons_of_mem s hs'))
        (absStep_stateEq K s σ τ (hd s List.mem_cons_self) h)

/-! ## Cells of a table -/

/-- The column has a value in some row. -/
def present (c : Nat) : Table → Bool
  | [] => false
  | r :: rs => (r c).isSome || present c rs

/-- No value of the column is below k (the cells below k are empty). -/
def allGe (c : Nat) (k : Int) : Table → Bool
  | [] => true
  | r :: rs => (match r c with | some x => decide (k ≤ x) | none => true) && allGe c k rs

/-- No value of the column is above k (the cells above k are empty). -/
def allLe (c : Nat) (k : Int) : Table → Bool
  | [] => true
  | r :: rs => (match r c with | some x => decide (x ≤ k) | none => true) && allLe c k rs

theorem lowest_none (c : Nat) : ∀ t : Table, lowest c t = none → present c t = false := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [lowest] at h
    cases hr : r c with
    | none =>
      rw [hr] at h
      simp only [present, hr, Option.isSome_none, Bool.false_or]
      exact ih h
    | some x =>
      rw [hr] at h
      cases hl : lowest c rs <;> rw [hl] at h <;> simp at h

theorem lowest_some_present (c : Nat) : ∀ (t : Table) (y : Int), lowest c t = some y → present c t = true := by
  intro t
  induction t with
  | nil => intro y h; simp [lowest] at h
  | cons r rs ih =>
    intro y h
    simp only [lowest] at h
    cases hr : r c with
    | none =>
      rw [hr] at h
      simp only [present, hr, Option.isSome_none, Bool.false_or]
      exact ih y h
    | some x => simp [present, hr]

theorem allGe_of_not_present (c : Nat) (k : Int) : ∀ t : Table, present c t = false → allGe c k t = true := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [present, Bool.or_eq_false_iff] at h
    cases hr : r c with
    | none => simp only [allGe, hr, Bool.true_and]; exact ih h.2
    | some x => rw [hr] at h; simp at h

theorem allLe_of_not_present (c : Nat) (k : Int) : ∀ t : Table, present c t = false → allLe c k t = true := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [present, Bool.or_eq_false_iff] at h
    cases hr : r c with
    | none => simp only [allLe, hr, Bool.true_and]; exact ih h.2
    | some x => rw [hr] at h; simp at h

theorem highest_none (c : Nat) : ∀ t : Table, highest c t = none → present c t = false := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [highest] at h
    cases hr : r c with
    | none =>
      rw [hr] at h
      simp only [present, hr, Option.isSome_none, Bool.false_or]
      exact ih h
    | some x =>
      rw [hr] at h
      cases hl : highest c rs <;> rw [hl] at h <;> simp at h

theorem highest_some_present (c : Nat) : ∀ (t : Table) (y : Int), highest c t = some y → present c t = true := by
  intro t
  induction t with
  | nil => intro y h; simp [highest] at h
  | cons r rs ih =>
    intro y h
    simp only [highest] at h
    cases hr : r c with
    | none =>
      rw [hr] at h
      simp only [present, hr, Option.isSome_none, Bool.false_or]
      exact ih y h
    | some x => simp [present, hr]

/-- The analysis's lower-bound comparison with k, for a table's own facts, is a
    question about cells: some value exists and none is below k. -/
theorem loAtLeast_lowest (c : Nat) (k : Int) :
    ∀ t : Table, loAtLeast (lowest c t) k = (present c t && allGe c k t) := by
  intro t
  induction t with
  | nil => rfl
  | cons r rs ih =>
    cases hr : r c with
    | none =>
      simp only [lowest, hr, present, allGe, Option.isSome_none, Bool.false_or, Bool.true_and]
      exact ih
    | some x =>
      cases hl : lowest c rs with
      | none =>
        have hp := lowest_none c rs hl
        simp only [lowest, hr, hl, present, allGe, Option.isSome_some, Bool.true_or, Bool.true_and,
          allGe_of_not_present c k rs hp, loAtLeast, Bool.and_true]
      | some y =>
        have hp := lowest_some_present c rs y hl
        rw [hl, hp] at ih
        simp only [loAtLeast, Bool.true_and] at ih
        simp only [lowest, hr, hl, present, allGe, Option.isSome_some, Bool.true_or, Bool.true_and,
          loAtLeast, ← ih]
        by_cases h : x ≤ y <;> by_cases h1 : k ≤ x <;> by_cases h2 : k ≤ y <;> simp [h, h1, h2] <;> omega

theorem hiAtMost_highest (c : Nat) (k : Int) :
    ∀ t : Table, hiAtMost (highest c t) k = (present c t && allLe c k t) := by
  intro t
  induction t with
  | nil => rfl
  | cons r rs ih =>
    cases hr : r c with
    | none =>
      simp only [highest, hr, present, allLe, Option.isSome_none, Bool.false_or, Bool.true_and]
      exact ih
    | some x =>
      cases hl : highest c rs with
      | none =>
        have hp := highest_none c rs hl
        simp only [highest, hr, hl, present, allLe, Option.isSome_some, Bool.true_or, Bool.true_and,
          allLe_of_not_present c k rs hp, hiAtMost, Bool.and_true]
      | some y =>
        have hp := highest_some_present c rs y hl
        rw [hl, hp] at ih
        simp only [hiAtMost, Bool.true_and] at ih
        simp only [highest, hr, hl, present, allLe, Option.isSome_some, Bool.true_or, Bool.true_and,
          hiAtMost, ← ih]
        by_cases h : y ≤ x <;> by_cases h1 : x ≤ k <;> by_cases h2 : y ≤ k <;> simp [h, h1, h2] <;> omega

/-- Two tables have the same occupied cells at the numbers K: for every column, a
    missing value in both or neither, a value in both or neither, and for every k in
    K, values below k in both or neither and values above k in both or neither. -/
def SameCells (K : List Int) (t1 t2 : Table) : Prop :=
  ∀ c, anyMissing c t1 = anyMissing c t2 ∧ present c t1 = present c t2 ∧
    (∀ k, k ∈ K → allGe c k t1 = allGe c k t2) ∧ (∀ k, k ∈ K → allLe c k t1 = allLe c k t2)

theorem stateEq_alpha (K : List Int) (t1 t2 : Table) (h : SameCells K t1 t2) :
    StateEq K (alpha t1) (alpha t2) := by
  intro c
  obtain ⟨hm, hp, hge, hle⟩ := h c
  refine ⟨hm, ?_, ?_⟩
  · intro k hk
    show loAtLeast (lowest c t1) k = loAtLeast (lowest c t2) k
    rw [loAtLeast_lowest, loAtLeast_lowest, hp, hge k hk]
  · intro k hk
    show hiAtMost (highest c t1) k = hiAtMost (highest c t2) k
    rw [hiAtMost_highest, hiAtMost_highest, hp, hle k hk]

/-- What the analysis proves of a program without DERIVE depends on the data only
    through which cells, at the program's numbers, are occupied. -/
theorem cells_decide (ss : List Stmt) (K : List Int)
    (hd : ∀ s, s ∈ ss → noDerive s = true) (hK : ∀ s, s ∈ ss → ∀ x, x ∈ consts s → x ∈ K)
    (t1 t2 : Table) (h : SameCells K t1 t2) (j : Nat) :
    provesAt (alpha t1) ss 0 j = provesAt (alpha t2) ss 0 j :=
  provesAt_stateEq K ss (alpha t1) (alpha t2) 0 j hd hK (stateEq_alpha K t1 t2 h)

end PrepLang.Core

#print axioms PrepLang.Core.cells_decide
#print axioms PrepLang.Core.provesAt_stateEq
