/-
  PrepLang core: the analysis of numeric table columns, mechanized.

  Deopt.lean proves selective deoptimization exact for any analysis that is
  monotone and sound. This file gives such an analysis for a core of PrepLang
  and proves both properties, so that for the core the chain from the data to
  the exactness and composition theorems is mechanized end to end.

  The core: a table is a list of rows; a row maps a column to a value or to
  "missing". A column is abstracted, as in PrepLang, by whether it may have
  missing values and an interval (a missing bound is infinite). Statements:
    FILL_NULL c WITH v, FILTER c >= k, FILTER c <= k, DROP_NULL c,
    EXPECT c BETWEEN a AND b, EXPECT c NOT_NULL.
  A failing EXPECT stops the run. Values are integers (PrepLang's columns are
  integers, floats or text; the interval reasoning is the same).

  Proved here:
    step_sound      the transfer functions are sound;
    proves_noop     a no-effect proof (or a proven contract) holds for every
                    table in the concretization;
    absStep_mono,
    proves_mono     monotonicity (Lemma 4.1 of the paper) for this analysis;
    gamma_alpha     a table's own facts describe it;
    core_deopt_exact
                    Theorem 5.1 for the core, with the run-time check of the
                    generated code (null counts and bounds of the columns an
                    item certificate mentions);
    core_compose    Theorem 6.1 for the core: a removal of stage 2 whose item
                    certificate contains stage 1's guarantee never runs again
                    on stage 1's output.

  Lean 4 core only (no Mathlib). Build with `lake build` in proofs/.
-/
import Deopt

namespace PrepLang.Core

/-! ## Concrete tables and statements -/

abbrev Row := Nat → Option Int
abbrev Table := List Row

inductive Stmt where
  | fill (c : Nat) (v : Int)
  | filterGe (c : Nat) (k : Int)
  | filterLe (c : Nat) (k : Int)
  | dropNull (c : Nat)
  | expectBetween (c : Nat) (a b : Int)
  | expectNotNull (c : Nat)

def fillRow (c : Nat) (v : Int) (r : Row) : Row :=
  fun c' => if c' = c then (match r c with | none => some v | some x => some x) else r c'

def geRow (c : Nat) (k : Int) (r : Row) : Bool :=
  match r c with | some x => decide (k ≤ x) | none => false

def leRow (c : Nat) (k : Int) (r : Row) : Bool :=
  match r c with | some x => decide (x ≤ k) | none => false

def betweenRow (c : Nat) (a b : Int) (r : Row) : Bool :=
  match r c with | some x => decide (a ≤ x) && decide (x ≤ b) | none => true

/-- What a statement does to a table; `none` is a run stopped by a failing check. -/
def step : Stmt → Table → Option Table
  | .fill c v, t => some (t.map (fillRow c v))
  | .filterGe c k, t => some (t.filter (geRow c k))
  | .filterLe c k, t => some (t.filter (leRow c k))
  | .dropNull c, t => some (t.filter (fun r => (r c).isSome))
  | .expectBetween c a b, t => if t.all (betweenRow c a b) then some t else none
  | .expectNotNull c, t => if t.all (fun r => (r c).isSome) then some t else none

/-! ## The abstract domain -/

/-- A column: may it have missing values, and an interval of its present values
    (`none` is an infinite bound). -/
structure Col where
  nullable : Bool
  lo : Option Int
  hi : Option Int

abbrev AState := Nat → Col

def loOk : Option Int → Int → Prop
  | none, _ => True
  | some l, x => l ≤ x

def hiOk : Option Int → Int → Prop
  | none, _ => True
  | some h, x => x ≤ h

def inCol (a : Col) : Option Int → Prop
  | none => a.nullable = true
  | some x => loOk a.lo x ∧ hiOk a.hi x

/-- The tables an abstract state describes. -/
def gamma (σ : AState) (t : Table) : Prop := ∀ r, r ∈ t → ∀ c, inCol (σ c) (r c)

/-- `loLe a b`: lower bound a is at least as tight as b. -/
def loLe : Option Int → Option Int → Prop
  | _, none => True
  | none, some _ => False
  | some x, some y => y ≤ x

def hiLe : Option Int → Option Int → Prop
  | _, none => True
  | none, some _ => False
  | some x, some y => x ≤ y

def Col.le (a b : Col) : Prop := (a.nullable = true → b.nullable = true) ∧ loLe a.lo b.lo ∧ hiLe a.hi b.hi

/-- σ ⊑ τ: σ is at least as precise as τ, column by column. -/
def le (σ τ : AState) : Prop := ∀ c, (σ c).le (τ c)

theorem loLe_trans : ∀ a b c : Option Int, loLe a b → loLe b c → loLe a c := by
  intro a b c h1 h2
  cases a <;> cases b <;> cases c <;> simp_all [loLe] <;> omega

theorem hiLe_trans : ∀ a b c : Option Int, hiLe a b → hiLe b c → hiLe a c := by
  intro a b c h1 h2
  cases a <;> cases b <;> cases c <;> simp_all [hiLe] <;> omega

theorem le_trans (σ τ υ : AState) (h1 : le σ τ) (h2 : le τ υ) : le σ υ := fun c =>
  ⟨fun h => (h2 c).1 ((h1 c).1 h), loLe_trans _ _ _ (h1 c).2.1 (h2 c).2.1,
   hiLe_trans _ _ _ (h1 c).2.2 (h2 c).2.2⟩

theorem le_refl (σ : AState) : le σ σ := fun c => by
  refine ⟨id, ?_, ?_⟩
  · cases (σ c).lo <;> simp [loLe]
  · cases (σ c).hi <;> simp [hiLe]

theorem loOk_mono : ∀ (a b : Option Int) (x : Int), loLe a b → loOk a x → loOk b x := by
  intro a b x h1 h2
  cases a <;> cases b <;> simp_all [loLe, loOk] <;> omega

theorem hiOk_mono : ∀ (a b : Option Int) (x : Int), hiLe a b → hiOk a x → hiOk b x := by
  intro a b x h1 h2
  cases a <;> cases b <;> simp_all [hiLe, hiOk] <;> omega

/-- Concretization is monotone: a weaker state describes more tables. -/
theorem gamma_mono (σ τ : AState) (t : Table) (h : le σ τ) (ht : gamma σ t) : gamma τ t := by
  intro r hr c
  have hc := ht r hr c
  cases hrc : r c with
  | none => simp only [hrc, inCol] at hc ⊢; exact (h c).1 hc
  | some x =>
    simp only [hrc, inCol] at hc ⊢
    exact ⟨loOk_mono _ _ x (h c).2.1 hc.1, hiOk_mono _ _ x (h c).2.2 hc.2⟩

/-! ## Transfer functions and proofs -/

def setCol (σ : AState) (c : Nat) (a : Col) : AState := fun c' => if c' = c then a else σ c'

def raiseLo : Option Int → Int → Option Int
  | none, k => some k
  | some x, k => some (if x ≤ k then k else x)

def lowerHi : Option Int → Int → Option Int
  | none, k => some k
  | some x, k => some (if k ≤ x then k else x)

def joinLo : Option Int → Int → Option Int
  | none, _ => none
  | some x, v => some (if x ≤ v then x else v)

def joinHi : Option Int → Int → Option Int
  | none, _ => none
  | some x, v => some (if v ≤ x then x else v)

def absStep : Stmt → AState → AState
  | .fill c v, σ =>
    if (σ c).nullable then setCol σ c ⟨false, joinLo (σ c).lo v, joinHi (σ c).hi v⟩ else σ
  | .filterGe c k, σ => setCol σ c ⟨false, raiseLo (σ c).lo k, (σ c).hi⟩
  | .filterLe c k, σ => setCol σ c ⟨false, (σ c).lo, lowerHi (σ c).hi k⟩
  | .dropNull c, σ => setCol σ c ⟨false, (σ c).lo, (σ c).hi⟩
  | .expectBetween c a b, σ => setCol σ c ⟨(σ c).nullable, raiseLo (σ c).lo a, lowerHi (σ c).hi b⟩
  | .expectNotNull c, σ => setCol σ c ⟨false, (σ c).lo, (σ c).hi⟩

def loAtLeast : Option Int → Int → Bool
  | none, _ => false
  | some x, k => decide (k ≤ x)

def hiAtMost : Option Int → Int → Bool
  | none, _ => false
  | some x, k => decide (x ≤ k)

/-- The analysis proves the statement does nothing (for a contract: that it holds). -/
def proves (σ : AState) : Stmt → Bool
  | .fill c _ => !(σ c).nullable
  | .filterGe c k => !(σ c).nullable && loAtLeast (σ c).lo k
  | .filterLe c k => !(σ c).nullable && hiAtMost (σ c).hi k
  | .dropNull c => !(σ c).nullable
  | .expectBetween c a b => loAtLeast (σ c).lo a && hiAtMost (σ c).hi b
  | .expectNotNull c => !(σ c).nullable

/-! ### Lemmas about lists -/

theorem map_id_of (f : Row → Row) : ∀ t : Table, (∀ r, r ∈ t → f r = r) → t.map f = t := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [List.map_cons]
    rw [h r (List.mem_cons_self), ih (fun r' hr' => h r' (List.mem_cons_of_mem r hr'))]

theorem filter_id_of (p : Row → Bool) : ∀ t : Table, (∀ r, r ∈ t → p r = true) → t.filter p = t := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [List.filter_cons, h r (List.mem_cons_self), ite_true]
    rw [ih (fun r' hr' => h r' (List.mem_cons_of_mem r hr'))]

theorem all_of (p : Row → Bool) : ∀ t : Table, (∀ r, r ∈ t → p r = true) → t.all p = true := by
  intro t
  induction t with
  | nil => intro _; rfl
  | cons r rs ih =>
    intro h
    simp only [List.all_cons, h r (List.mem_cons_self), Bool.true_and]
    exact ih (fun r' hr' => h r' (List.mem_cons_of_mem r hr'))

theorem of_all (p : Row → Bool) : ∀ t : Table, t.all p = true → ∀ r, r ∈ t → p r = true := by
  intro t
  induction t with
  | nil => intro _ r hr; cases hr
  | cons r rs ih =>
    intro h r' hr'
    simp only [List.all_cons, Bool.and_eq_true] at h
    cases hr' with
    | head => exact h.1
    | tail _ hm => exact ih h.2 r' hm

theorem mem_of_mem_filter (p : Row → Bool) : ∀ (t : Table) (r : Row), r ∈ t.filter p → r ∈ t ∧ p r = true := by
  intro t
  induction t with
  | nil => intro r hr; cases hr
  | cons a as ih =>
    intro r hr
    cases hp : p a with
    | true =>
      simp only [List.filter_cons, hp, ite_true] at hr
      cases hr with
      | head => exact ⟨List.mem_cons_self, hp⟩
      | tail _ hm => exact ⟨List.mem_cons_of_mem a (ih r hm).1, (ih r hm).2⟩
    | false =>
      simp only [List.filter_cons, hp] at hr
      exact ⟨List.mem_cons_of_mem a (ih r hr).1, (ih r hr).2⟩

theorem mem_of_mem_map (f : Row → Row) : ∀ (t : Table) (r : Row), r ∈ t.map f → ∃ r', r' ∈ t ∧ r = f r' := by
  intro t
  induction t with
  | nil => intro r hr; cases hr
  | cons a as ih =>
    intro r hr
    simp only [List.map_cons] at hr
    cases hr with
    | head => exact ⟨a, List.mem_cons_self, rfl⟩
    | tail _ hm =>
      obtain ⟨r', h1, h2⟩ := ih r hm
      exact ⟨r', List.mem_cons_of_mem a h1, h2⟩

/-! ### Soundness -/

theorem inCol_setCol (σ : AState) (c c' : Nat) (a : Col) (v : Option Int)
    (hsame : c' = c → inCol a v) (hother : c' ≠ c → inCol (σ c') v) : inCol (setCol σ c a c') v := by
  unfold setCol
  by_cases h : c' = c
  · simp only [h, ite_true]; exact hsame h
  · simp only [h, ite_false]; exact hother h

theorem raiseLo_ok (l : Option Int) (k x : Int) (h1 : loOk l x) (h2 : k ≤ x) : loOk (raiseLo l k) x := by
  cases l with
  | none => simp [raiseLo, loOk]; exact h2
  | some y => simp only [raiseLo, loOk] at h1 ⊢; split <;> omega

theorem lowerHi_ok (h : Option Int) (k x : Int) (h1 : hiOk h x) (h2 : x ≤ k) : hiOk (lowerHi h k) x := by
  cases h with
  | none => simp [lowerHi, hiOk]; exact h2
  | some y => simp only [lowerHi, hiOk] at h1 ⊢; split <;> omega

theorem joinLo_ok (l : Option Int) (v x : Int) (h1 : loOk l x) : loOk (joinLo l v) x := by
  cases l with
  | none => simp [joinLo, loOk]
  | some y => simp only [joinLo, loOk] at h1 ⊢; split <;> omega

theorem joinHi_ok (h : Option Int) (v x : Int) (h1 : hiOk h x) : hiOk (joinHi h v) x := by
  cases h with
  | none => simp [joinHi, hiOk]
  | some y => simp only [joinHi, hiOk] at h1 ⊢; split <;> omega

theorem joinLo_self (l : Option Int) (v : Int) : loOk (joinLo l v) v := by
  cases l with
  | none => simp [joinLo, loOk]
  | some y => simp only [joinLo, loOk]; split <;> omega

theorem joinHi_self (h : Option Int) (v : Int) : hiOk (joinHi h v) v := by
  cases h with
  | none => simp [joinHi, hiOk]
  | some y => simp only [joinHi, hiOk]; split <;> omega

/-- The transfer functions are sound (Lemma 3.1, for the core). -/
theorem step_sound (s : Stmt) (σ : AState) (t t' : Table) (ht : gamma σ t) (hs : step s t = some t') :
    gamma (absStep s σ) t' := by
  cases s with
  | fill c v =>
    simp only [step, Option.some.injEq] at hs
    subst hs
    intro r hr c'
    obtain ⟨r0, hr0, rfl⟩ := mem_of_mem_map _ t r hr
    have h0 := ht r0 hr0
    simp only [absStep]
    cases hn : (σ c).nullable with
    | true =>
      simp only [ite_true]
      apply inCol_setCol
      · intro hc; subst hc
        simp only [fillRow, ite_true]
        cases hv : r0 c' with
        | none => simp only [inCol]; exact ⟨joinLo_self _ _, joinHi_self _ _⟩
        | some x =>
          have hx := h0 c'
          simp only [hv, inCol] at hx ⊢
          exact ⟨joinLo_ok _ _ _ hx.1, joinHi_ok _ _ _ hx.2⟩
      · intro hc
        simp only [fillRow, hc, ite_false]; exact h0 c'
    | false =>
      simp only [Bool.false_eq_true, ite_false]
      by_cases hc : c' = c
      · subst hc
        simp only [fillRow, ite_true]
        have hx := h0 c'
        cases hv : r0 c' with
        | none => simp only [hv, inCol] at hx; rw [hn] at hx; cases hx
        | some x => simp only [hv] at hx; exact hx
      · simp only [fillRow, hc, ite_false]; exact h0 c'
  | filterGe c k =>
    simp only [step, Option.some.injEq] at hs
    subst hs
    intro r hr c'
    obtain ⟨hmem, hp⟩ := mem_of_mem_filter _ t r hr
    have h0 := ht r hmem
    simp only [absStep]
    apply inCol_setCol
    · intro hc; subst hc
      simp only [geRow] at hp
      cases hv : r c' with
      | none => simp [hv] at hp
      | some x =>
        simp only [hv, decide_eq_true_eq] at hp
        have hx := h0 c'
        simp only [hv, inCol] at hx ⊢
        exact ⟨raiseLo_ok _ _ _ hx.1 hp, hx.2⟩
    · intro _; exact h0 c'
  | filterLe c k =>
    simp only [step, Option.some.injEq] at hs
    subst hs
    intro r hr c'
    obtain ⟨hmem, hp⟩ := mem_of_mem_filter _ t r hr
    have h0 := ht r hmem
    simp only [absStep]
    apply inCol_setCol
    · intro hc; subst hc
      simp only [leRow] at hp
      cases hv : r c' with
      | none => simp [hv] at hp
      | some x =>
        simp only [hv, decide_eq_true_eq] at hp
        have hx := h0 c'
        simp only [hv, inCol] at hx ⊢
        exact ⟨hx.1, lowerHi_ok _ _ _ hx.2 hp⟩
    · intro _; exact h0 c'
  | dropNull c =>
    simp only [step, Option.some.injEq] at hs
    subst hs
    intro r hr c'
    obtain ⟨hmem, hp⟩ := mem_of_mem_filter _ t r hr
    have h0 := ht r hmem
    simp only [absStep]
    apply inCol_setCol
    · intro hc; subst hc
      cases hv : r c' with
      | none => simp [hv] at hp
      | some x =>
        have hx := h0 c'
        simp only [hv, inCol] at hx ⊢
        exact hx
    · intro _; exact h0 c'
  | expectBetween c a b =>
    simp only [step] at hs
    split at hs
    · rename_i hall
      simp only [Option.some.injEq] at hs
      subst hs
      intro r hr c'
      have h0 := ht r hr
      have hp := of_all _ t hall r hr
      simp only [absStep]
      apply inCol_setCol
      · intro hc; subst hc
        simp only [betweenRow] at hp
        cases hv : r c' with
        | none => have hx := h0 c'; simp only [hv, inCol] at hx ⊢; exact hx
        | some x =>
          simp only [hv, Bool.and_eq_true, decide_eq_true_eq] at hp
          have hx := h0 c'
          simp only [hv, inCol] at hx ⊢
          exact ⟨raiseLo_ok _ _ _ hx.1 hp.1, lowerHi_ok _ _ _ hx.2 hp.2⟩
      · intro _; exact h0 c'
    · cases hs
  | expectNotNull c =>
    simp only [step] at hs
    split at hs
    · rename_i hall
      simp only [Option.some.injEq] at hs
      subst hs
      intro r hr c'
      have h0 := ht r hr
      have hp := of_all _ t hall r hr
      simp only [absStep]
      apply inCol_setCol
      · intro hc; subst hc
        cases hv : r c' with
        | none => simp [hv] at hp
        | some x =>
          have hx := h0 c'
          simp only [hv, inCol] at hx ⊢
          exact hx
      · intro _; exact h0 c'
    · cases hs

theorem present_of_not_nullable (σ : AState) (t : Table) (c : Nat) (ht : gamma σ t)
    (hn : (σ c).nullable = false) (r : Row) (hr : r ∈ t) : ∃ x, r c = some x ∧ loOk (σ c).lo x ∧ hiOk (σ c).hi x := by
  have h0 := ht r hr c
  cases hv : r c with
  | none => simp only [hv, inCol] at h0; rw [hn] at h0; cases h0
  | some x => simp only [hv, inCol] at h0; exact ⟨x, rfl, h0⟩

/-- A proof of the analysis holds for every table the state describes: the
    statement leaves such a table unchanged (Lemma 3.1, for the core). -/
theorem proves_noop (s : Stmt) (σ : AState) (t : Table) (ht : gamma σ t) (hp : proves σ s = true) :
    step s t = some t := by
  cases s with
  | fill c v =>
    simp only [proves, Bool.not_eq_true'] at hp
    simp only [step, Option.some.injEq]
    apply map_id_of
    intro r hr
    obtain ⟨x, hx, _⟩ := present_of_not_nullable σ t c ht hp r hr
    funext c'
    unfold fillRow
    by_cases hc : c' = c
    · subst hc; simp only [ite_true, hx]
    · simp only [hc, ite_false]
  | filterGe c k =>
    simp only [proves, Bool.and_eq_true, Bool.not_eq_true'] at hp
    simp only [step, Option.some.injEq]
    apply filter_id_of
    intro r hr
    obtain ⟨x, hx, hlo, _⟩ := present_of_not_nullable σ t c ht hp.1 r hr
    have hk := hp.2
    simp only [geRow, hx, decide_eq_true_eq]
    cases hl : (σ c).lo with
    | none => rw [hl] at hk; simp [loAtLeast] at hk
    | some l => rw [hl] at hk hlo; simp only [loAtLeast, decide_eq_true_eq] at hk; simp only [loOk] at hlo; omega
  | filterLe c k =>
    simp only [proves, Bool.and_eq_true, Bool.not_eq_true'] at hp
    simp only [step, Option.some.injEq]
    apply filter_id_of
    intro r hr
    obtain ⟨x, hx, _, hhi⟩ := present_of_not_nullable σ t c ht hp.1 r hr
    have hk := hp.2
    simp only [leRow, hx, decide_eq_true_eq]
    cases hh : (σ c).hi with
    | none => rw [hh] at hk; simp [hiAtMost] at hk
    | some h => rw [hh] at hk hhi; simp only [hiAtMost, decide_eq_true_eq] at hk; simp only [hiOk] at hhi; omega
  | dropNull c =>
    simp only [proves, Bool.not_eq_true'] at hp
    simp only [step, Option.some.injEq]
    apply filter_id_of
    intro r hr
    obtain ⟨x, hx, _⟩ := present_of_not_nullable σ t c ht hp r hr
    rw [hx]; rfl
  | expectBetween c a b =>
    simp only [proves, Bool.and_eq_true] at hp
    simp only [step]
    have hall : t.all (betweenRow c a b) = true := by
      apply all_of
      intro r hr
      have h0 := ht r hr c
      simp only [betweenRow]
      cases hv : r c with
      | none => rfl
      | some x =>
        simp only [hv, inCol] at h0
        simp only [Bool.and_eq_true, decide_eq_true_eq]
        have hlo := h0.1
        have hhi := h0.2
        have ha := hp.1
        have hb := hp.2
        cases hl : (σ c).lo with
        | none => rw [hl] at ha; simp [loAtLeast] at ha
        | some l =>
          cases hh : (σ c).hi with
          | none => rw [hh] at hb; simp [hiAtMost] at hb
          | some h =>
            rw [hl] at ha hlo; rw [hh] at hb hhi
            simp only [loAtLeast, hiAtMost, decide_eq_true_eq] at ha hb
            simp only [loOk, hiOk] at hlo hhi
            exact ⟨by omega, by omega⟩
    simp only [hall, ite_true]
  | expectNotNull c =>
    simp only [proves, Bool.not_eq_true'] at hp
    simp only [step]
    have hall : t.all (fun r => (r c).isSome) = true := by
      apply all_of
      intro r hr
      obtain ⟨x, hx, _⟩ := present_of_not_nullable σ t c ht hp r hr
      rw [hx]; rfl
    simp only [hall, ite_true]

/-! ### Monotonicity (Lemma 4.1, for the core) -/

theorem loAtLeast_mono (a b : Option Int) (k : Int) (h : loLe a b) (hb : loAtLeast b k = true) :
    loAtLeast a k = true := by
  cases a <;> cases b <;> simp_all [loLe, loAtLeast] <;> omega

theorem hiAtMost_mono (a b : Option Int) (k : Int) (h : hiLe a b) (hb : hiAtMost b k = true) :
    hiAtMost a k = true := by
  cases a <;> cases b <;> simp_all [hiLe, hiAtMost] <;> omega

theorem not_nullable_mono (a b : Col) (h : a.le b) (hb : b.nullable = false) : a.nullable = false := by
  cases ha : a.nullable with
  | false => rfl
  | true => have := h.1 ha; rw [hb] at this; cases this

/-- A more precise state proves at least as much. -/
theorem proves_mono (s : Stmt) (σ τ : AState) (h : le σ τ) (hp : proves τ s = true) : proves σ s = true := by
  cases s with
  | fill c v =>
    simp only [proves, Bool.not_eq_true'] at hp ⊢; exact not_nullable_mono _ _ (h c) hp
  | filterGe c k =>
    simp only [proves, Bool.and_eq_true, Bool.not_eq_true'] at hp ⊢
    exact ⟨not_nullable_mono _ _ (h c) hp.1, loAtLeast_mono _ _ _ (h c).2.1 hp.2⟩
  | filterLe c k =>
    simp only [proves, Bool.and_eq_true, Bool.not_eq_true'] at hp ⊢
    exact ⟨not_nullable_mono _ _ (h c) hp.1, hiAtMost_mono _ _ _ (h c).2.2 hp.2⟩
  | dropNull c =>
    simp only [proves, Bool.not_eq_true'] at hp ⊢; exact not_nullable_mono _ _ (h c) hp
  | expectBetween c a b =>
    simp only [proves, Bool.and_eq_true] at hp ⊢
    exact ⟨loAtLeast_mono _ _ _ (h c).2.1 hp.1, hiAtMost_mono _ _ _ (h c).2.2 hp.2⟩
  | expectNotNull c =>
    simp only [proves, Bool.not_eq_true'] at hp ⊢; exact not_nullable_mono _ _ (h c) hp

theorem raiseLo_mono (a b : Option Int) (k : Int) (h : loLe a b) : loLe (raiseLo a k) (raiseLo b k) := by
  cases a <;> cases b <;> simp only [loLe, raiseLo] at h ⊢ <;> (try split) <;> (try split) <;> omega

theorem lowerHi_mono (a b : Option Int) (k : Int) (h : hiLe a b) : hiLe (lowerHi a k) (lowerHi b k) := by
  cases a <;> cases b <;> simp only [hiLe, lowerHi] at h ⊢ <;> (try split) <;> (try split) <;> omega

theorem joinLo_mono (a b : Option Int) (v : Int) (h : loLe a b) : loLe (joinLo a v) (joinLo b v) := by
  cases a <;> cases b <;> simp only [loLe, joinLo] at h ⊢ <;> (try split) <;> (try split) <;> omega

theorem joinHi_mono (a b : Option Int) (v : Int) (h : hiLe a b) : hiLe (joinHi a v) (joinHi b v) := by
  cases a <;> cases b <;> simp only [hiLe, joinHi] at h ⊢ <;> (try split) <;> (try split) <;> omega

theorem joinLo_weaker (a : Option Int) (v : Int) (b : Option Int) (h : loLe b a) : loLe b (joinLo a v) := by
  cases a <;> cases b <;> simp only [loLe, joinLo] at h ⊢ <;> (try split) <;> omega

theorem joinHi_weaker (a : Option Int) (v : Int) (b : Option Int) (h : hiLe b a) : hiLe b (joinHi a v) := by
  cases a <;> cases b <;> simp only [hiLe, joinHi] at h ⊢ <;> (try split) <;> omega

theorem setCol_le (σ τ : AState) (c : Nat) (a b : Col) (h : le σ τ) (hab : a.le b) :
    le (setCol σ c a) (setCol τ c b) := by
  intro c'
  unfold setCol
  by_cases hc : c' = c
  · simp only [hc, ite_true]; exact hab
  · simp only [hc, ite_false]; exact h c'

/-- The transfer functions are monotone. -/
theorem absStep_mono (s : Stmt) (σ τ : AState) (h : le σ τ) : le (absStep s σ) (absStep s τ) := by
  cases s with
  | fill c v =>
    simp only [absStep]
    cases hs : (σ c).nullable with
    | true =>
      have ht : (τ c).nullable = true := (h c).1 hs
      simp only [ht, ite_true]
      exact setCol_le σ τ c _ _ h
        ⟨fun h => h, joinLo_mono _ _ _ (h c).2.1, joinHi_mono _ _ _ (h c).2.2⟩
    | false =>
      simp only [Bool.false_eq_true, ite_false]
      cases ht : (τ c).nullable with
      | false => simp only [Bool.false_eq_true, ite_false]; exact h
      | true =>
        -- the fill runs for τ but is a no-op for σ: σ is already below the filled state
        simp only [ite_true]
        intro c'
        unfold setCol
        by_cases hc : c' = c
        · subst hc
          simp only [ite_true]
          exact ⟨(fun hn => by rw [hs] at hn; cases hn), joinLo_weaker _ _ _ (h c').2.1,
                 joinHi_weaker _ _ _ (h c').2.2⟩
        · simp only [hc, ite_false]; exact h c'
  | filterGe c k =>
    exact setCol_le σ τ c _ _ h ⟨fun h => h, raiseLo_mono _ _ _ (h c).2.1, (h c).2.2⟩
  | filterLe c k =>
    exact setCol_le σ τ c _ _ h ⟨fun h => h, (h c).2.1, lowerHi_mono _ _ _ (h c).2.2⟩
  | dropNull c =>
    exact setCol_le σ τ c _ _ h ⟨fun h => h, (h c).2.1, (h c).2.2⟩
  | expectBetween c a b =>
    exact setCol_le σ τ c _ _ h ⟨(h c).1, raiseLo_mono _ _ _ (h c).2.1, lowerHi_mono _ _ _ (h c).2.2⟩
  | expectNotNull c =>
    exact setCol_le σ τ c _ _ h ⟨fun h => h, (h c).2.1, (h c).2.2⟩

/-! ## Programs -/

/-- The source program as instructions on tables. -/
def prog (ss : List Stmt) : List (Table → Option Table) := ss.map step

/-- The analysis of a program from state σ proves the statement at absolute index j
    (the first statement of `ss` has index k). -/
def provesAt : AState → List Stmt → Nat → Nat → Bool
  | _, [], _, _ => false
  | σ, s :: ss, k, j => if j = k then proves σ s else provesAt (absStep s σ) ss (k + 1) j

/-- The state the analysis computes at the end of a program (stage 1's guarantee
    is this state, computed from its admission certificate). -/
def absRun : AState → List Stmt → AState
  | σ, [] => σ
  | σ, s :: ss => absRun (absStep s σ) ss

theorem noopAt_before : ∀ (fs : List (Table → Option Table)) (k : Nat) (t : Table) (j : Nat),
    j < k → NoopAt fs k t j := by
  intro fs
  induction fs with
  | nil => intro _ _ _ _; trivial
  | cons f fs ih =>
    intro k t j hj
    refine ⟨fun h => absurd h (by omega), ?_⟩
    cases f t with
    | none => trivial
    | some t' => exact ih (k + 1) t' j (by omega)

/-- Proofs of the program's analysis are no-ops along the source run. -/
theorem provesAt_sound : ∀ (ss : List Stmt) (σ : AState) (k : Nat) (t : Table) (j : Nat),
    gamma σ t → provesAt σ ss k j = true → NoopAt (prog ss) k t j := by
  intro ss
  induction ss with
  | nil => intro σ k t j _ hp; simp [provesAt] at hp
  | cons s ss ih =>
    intro σ k t j ht hp
    simp only [prog, List.map_cons, NoopAt]
    by_cases hj : j = k
    · subst hj
      simp only [provesAt, ite_true] at hp
      have hn := proves_noop s σ t ht hp
      refine ⟨fun _ => hn, ?_⟩
      rw [hn]
      exact noopAt_before _ (j + 1) t j (by omega)
    · simp only [provesAt, hj, ite_false] at hp
      refine ⟨fun h => absurd h hj, ?_⟩
      cases hs : step s t with
      | none => trivial
      | some t' => exact ih (absStep s σ) (k + 1) t' j (step_sound s σ t t' ht hs) hp

/-- Monotonicity for whole programs. -/
theorem provesAt_mono : ∀ (ss : List Stmt) (σ τ : AState) (k j : Nat),
    le σ τ → provesAt τ ss k j = true → provesAt σ ss k j = true := by
  intro ss
  induction ss with
  | nil => intro _ _ _ _ _ hp; simp [provesAt] at hp
  | cons s ss ih =>
    intro σ τ k j h hp
    by_cases hj : j = k
    · simp only [provesAt, hj, ite_true] at hp ⊢; exact proves_mono s σ τ h hp
    · simp only [provesAt, hj, ite_false] at hp ⊢
      exact ih (absStep s σ) (absStep s τ) (k + 1) j (absStep_mono s σ τ h) hp

/-- What a program writes for a table the state describes is described by the
    analysis's final state. -/
theorem run_sound : ∀ (ss : List Stmt) (σ : AState) (t t' : Table),
    gamma σ t → run (prog ss) t = some t' → gamma (absRun σ ss) t' := by
  intro ss
  induction ss with
  | nil => intro σ t t' ht hr; simp only [prog, List.map_nil, run, Option.some.injEq] at hr; subst hr; exact ht
  | cons s ss ih =>
    intro σ t t' ht hr
    simp only [prog, List.map_cons, run] at hr
    cases hs : step s t with
    | none => rw [hs] at hr; cases hr
    | some t1 =>
      rw [hs] at hr
      exact ih (absStep s σ) t1 t' (step_sound s σ t t1 ht hs) hr

/-! ## The facts of a table -/

def anyMissing (c : Nat) : Table → Bool
  | [] => false
  | r :: rs => (r c).isNone || anyMissing c rs

def lowest (c : Nat) : Table → Option Int
  | [] => none
  | r :: rs =>
    match r c, lowest c rs with
    | some x, some y => some (if x ≤ y then x else y)
    | some x, none => some x
    | none, l => l

def highest (c : Nat) : Table → Option Int
  | [] => none
  | r :: rs =>
    match r c, highest c rs with
    | some x, some y => some (if y ≤ x then x else y)
    | some x, none => some x
    | none, h => h

/-- The facts the generated code computes for a column: missing values, minimum
    and maximum (a column without present values gets infinite bounds). -/
def alpha (t : Table) : AState := fun c => ⟨anyMissing c t, lowest c t, highest c t⟩

theorem anyMissing_of (c : Nat) : ∀ (t : Table) (r : Row), r ∈ t → r c = none → anyMissing c t = true := by
  intro t
  induction t with
  | nil => intro r hr; cases hr
  | cons a as ih =>
    intro r hr hn
    simp only [anyMissing, Bool.or_eq_true]
    cases hr with
    | head => left; simp [hn]
    | tail _ hm => right; exact ih r hm hn

theorem lowest_some (c : Nat) : ∀ (t : Table) (r : Row) (x : Int), r ∈ t → r c = some x →
    ∃ m, lowest c t = some m ∧ m ≤ x := by
  intro t
  induction t with
  | nil => intro r x hr; cases hr
  | cons a as ih =>
    intro r x hr hx
    cases hr with
    | head =>
      simp only [lowest, hx]
      cases lowest c as with
      | none => exact ⟨x, rfl, Int.le_refl x⟩
      | some y => exact ⟨if x ≤ y then x else y, rfl, by split <;> omega⟩
    | tail _ hm =>
      obtain ⟨m, hm1, hm2⟩ := ih r x hm hx
      simp only [lowest, hm1]
      cases a c with
      | none => exact ⟨m, rfl, hm2⟩
      | some z => exact ⟨if z ≤ m then z else m, rfl, by split <;> omega⟩

theorem highest_some (c : Nat) : ∀ (t : Table) (r : Row) (x : Int), r ∈ t → r c = some x →
    ∃ m, highest c t = some m ∧ x ≤ m := by
  intro t
  induction t with
  | nil => intro r x hr; cases hr
  | cons a as ih =>
    intro r x hr hx
    cases hr with
    | head =>
      simp only [highest, hx]
      cases highest c as with
      | none => exact ⟨x, rfl, Int.le_refl x⟩
      | some y => exact ⟨if y ≤ x then x else y, rfl, by split <;> omega⟩
    | tail _ hm =>
      obtain ⟨m, hm1, hm2⟩ := ih r x hm hx
      simp only [highest, hm1]
      cases a c with
      | none => exact ⟨m, rfl, hm2⟩
      | some z => exact ⟨if m ≤ z then z else m, rfl, by split <;> omega⟩

theorem lowest_le (c : Nat) (t : Table) (r : Row) (x : Int) (hr : r ∈ t) (hx : r c = some x) :
    loOk (lowest c t) x := by
  obtain ⟨m, hm, hle⟩ := lowest_some c t r x hr hx
  rw [hm]; exact hle

theorem highest_ge (c : Nat) (t : Table) (r : Row) (x : Int) (hr : r ∈ t) (hx : r c = some x) :
    hiOk (highest c t) x := by
  obtain ⟨m, hm, hle⟩ := highest_some c t r x hr hx
  rw [hm]; exact hle

/-- A table's own facts describe it. -/
theorem gamma_alpha (t : Table) : gamma (alpha t) t := by
  intro r hr c
  cases hv : r c with
  | none => simp only [inCol, alpha]; exact anyMissing_of c t r hr hv
  | some x => simp only [inCol, alpha]; exact ⟨lowest_le c t r x hr hv, highest_ge c t r x hr hv⟩

/-! ## The core as an instance of Deopt.lean -/

/-- The core analysis, with every input admitted (the core has no types and no
    compile errors, the admission certificate's facts). -/
def analysis (ss : List Stmt) (removable : Nat → Bool) : Analysis Table AState Table where
  le := le
  le_trans := le_trans
  alpha := alpha
  init := id
  prog := prog ss
  removable := removable
  proves := fun σ k => provesAt σ ss 0 k = true
  admitted := fun _ => True
  mono := fun a b k h hp => provesAt_mono ss a b 0 k h hp
  sound := fun c k _ hp => provesAt_sound ss (alpha c) 0 c k (gamma_alpha c) hp

/-- The run-time check of an item certificate: every row's value of every
    certified column is inside the certificate (the generated code checks the
    same with the column's missing-value count, minimum and maximum). Columns
    outside `cols` are unconstrained in the certificate. -/
def inColB (a : Col) : Option Int → Bool
  | none => a.nullable
  | some x => (match a.lo with | none => true | some l => decide (l ≤ x)) &&
              (match a.hi with | none => true | some h => decide (x ≤ h))

def fits (cols : List Nat) (cert : AState) (t : Table) : Bool :=
  t.all (fun r => cols.all (fun c => inColB (cert c) (r c)))

def top : Col := ⟨true, none, none⟩

theorem inCol_top (v : Option Int) : inCol top v := by
  cases v <;> simp [inCol, top, loOk, hiOk]

theorem inCol_of_inColB (a : Col) (v : Option Int) (h : inColB a v = true) : inCol a v := by
  cases v with
  | none => simpa [inColB, inCol] using h
  | some x =>
    simp only [inColB, Bool.and_eq_true] at h
    simp only [inCol]
    constructor
    · cases hl : a.lo with
      | none => simp [loOk]
      | some l => rw [hl] at h; simp only [decide_eq_true_eq] at h; exact h.1
    · cases hh : a.hi with
      | none => simp [hiOk]
      | some u => rw [hh] at h; simp only [decide_eq_true_eq] at h; exact h.2

theorem gamma_of_fits (cols : List Nat) (cert : AState) (t : Table)
    (htop : ∀ c, c ∉ cols → cert c = top) (h : fits cols cert t = true) : gamma cert t := by
  intro r hr c
  have hrow := of_all _ t h r hr
  by_cases hc : c ∈ cols
  · have hall : ∀ c', c' ∈ cols → inColB (cert c') (r c') = true := by
      intro c' hc'
      exact (List.all_eq_true.mp hrow) c' hc'
    exact inCol_of_inColB _ _ (hall c hc)
  · rw [htop c hc]; exact inCol_top _

theorem fits_of_gamma (cols : List Nat) (cert : AState) (t : Table) (h : gamma cert t) : fits cols cert t = true := by
  apply all_of
  intro r hr
  apply List.all_eq_true.mpr
  intro c _
  have hc := h r hr c
  cases hv : r c with
  | none => simp only [hv, inCol] at hc; simp [inColB, hc]
  | some x =>
    simp only [hv, inCol] at hc
    simp only [inColB, Bool.and_eq_true]
    constructor
    · cases hl : (cert c).lo with
      | none => rfl
      | some l => have := hc.1; rw [hl] at this; simp only [loOk] at this; simp [this]
    · cases hh : (cert c).hi with
      | none => rfl
      | some u => have := hc.2; rw [hh] at this; simp only [hiOk] at this; simp [this]

/-- Theorem 5.1 for the core, end to end: if every removed statement's item
    certificate proves its removal (what the certifier's re-analysis establishes),
    the program that skips exactly the removals whose certificates the input fits
    computes on every input what the source program computes. -/
theorem core_deopt_exact (ss : List Stmt) (removable : Nat → Bool) (cols : Nat → List Nat)
    (cert : Nat → AState) (htop : ∀ k c, c ∉ cols k → cert k c = top)
    (verified : ∀ k, removable k = true → provesAt (cert k) ss 0 k = true) (t : Table) :
    runDeopt (fun k => removable k && fits (cols k) (cert k) t) (prog ss) 0 t = run (prog ss) t := by
  apply skip_noops
  intro j hj
  simp only [Bool.and_eq_true] at hj
  exact provesAt_sound ss (cert j) 0 t j (gamma_of_fits _ _ t (htop j) hj.2) (verified j hj.1)

/-- Theorem 6.1 for the core: stage 1 runs `ss1` on tables inside its admission
    certificate `adm1`; its guarantee is the analysis's final state. If that
    guarantee is at least as precise as the item certificate of a removal of
    stage 2, stage 2's run-time check admits every table stage 1 writes, so the
    removal is skipped and never runs again. -/
theorem core_compose (ss1 : List Stmt) (adm1 : AState) (t t' : Table)
    (ht : gamma adm1 t) (hrun : run (prog ss1) t = some t')
    (cols2 : List Nat) (cert2 : AState) (inside : le (absRun adm1 ss1) cert2) :
    fits cols2 cert2 t' = true :=
  fits_of_gamma cols2 cert2 t' (gamma_mono _ _ t' inside (run_sound ss1 adm1 t t' ht hrun))

end PrepLang.Core

#print axioms PrepLang.Core.core_deopt_exact
#print axioms PrepLang.Core.core_compose
#print axioms PrepLang.Core.provesAt_mono
