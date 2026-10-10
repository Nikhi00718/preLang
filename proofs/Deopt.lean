/-
  PrepLang: selective deoptimization and certificate composition, mechanized.

  A compiled pipeline is a straight-line list of instructions over concrete
  states S. An instruction may stop the run (a contract check that fails), so it
  is a function S → Option S. The optimizer removes the instructions that the
  analysis proved to do nothing. At run time a removed instruction is skipped
  only if the new input fits its item certificate; otherwise it runs after all
  (deoptimization).

  Theorem `deopt_correct`: for every admitted input, the deoptimizing program
  computes exactly what the source program computes, given
    (1) monotonicity: a more precise abstract input proves at least as much;
    (2) soundness: a no-op proof for the abstraction of an admitted input holds
        at the state the source program reaches at that instruction;
    (3) verification: analysing each item certificate itself proves its removal
        (`greedy_verified` shows the counterfactual search only ends on such
        certificates);
    (4) the run-time check of an item certificate is sound.
  Theorem `compose_never_deopts`: if stage 1's guarantee is inside an item
  certificate of stage 2, stage 2 never runs that removed instruction for an
  output of stage 1.

  Lean 4 core only (no Mathlib). Check with: lean proofs/Deopt.lean
-/

namespace PrepLang

variable {S : Type}

/-- The source program: run every instruction; `none` is a run stopped by a check. -/
def run : List (S → Option S) → S → Option S
  | [], s => some s
  | f :: fs, s =>
    match f s with
    | none => none
    | some s' => run fs s'

/-- The deoptimizing program: the instruction at absolute index k is skipped when
    `skip k` is true. `k` is the index of the first instruction of the list. -/
def runDeopt (skip : Nat → Bool) : List (S → Option S) → Nat → S → Option S
  | [], _, s => some s
  | f :: fs, k, s =>
    if skip k then runDeopt skip fs (k + 1) s
    else
      match f s with
      | none => none
      | some s' => runDeopt skip fs (k + 1) s'

/-- The instruction at absolute index j does nothing at the state the source
    program reaches there (when started at index k in state s). -/
def NoopAt : List (S → Option S) → Nat → S → Nat → Prop
  | [], _, _, _ => True
  | f :: fs, k, s, j =>
    (j = k → f s = some s) ∧
    (match f s with
     | none => True
     | some s' => NoopAt fs (k + 1) s' j)

/-- Skipping instructions that are no-ops along the source run changes nothing. -/
theorem skip_noops (skip : Nat → Bool) :
    ∀ (fs : List (S → Option S)) (k : Nat) (s : S),
      (∀ j, skip j = true → NoopAt fs k s j) → runDeopt skip fs k s = run fs s := by
  intro fs
  induction fs with
  | nil => intro k s _; rfl
  | cons f fs ih =>
    intro k s h
    -- what the hypothesis says about the rest of the program, once f s is known
    have rest : ∀ s', f s = some s' → ∀ j, skip j = true → NoopAt fs (k + 1) s' j := by
      intro s' hf j hj
      have hj' := (h j hj).2
      rw [hf] at hj'
      exact hj'
    cases hk : skip k with
    | true =>
      have hf : f s = some s := (h k hk).1 rfl
      simp only [runDeopt, hk, run, hf, ite_true]
      exact ih (k + 1) s (rest s hf)
    | false =>
      cases hfs : f s with
      | none => simp [runDeopt, hk, run, hfs]
      | some s' =>
        simp only [runDeopt, hk, run, hfs]
        exact ih (k + 1) s' (rest s' hfs)

/-- The setting the compiler works in. C: concrete inputs; A: abstract inputs
    (certificates are abstract inputs). -/
structure Analysis (C A S : Type) where
  le : A → A → Prop                    -- le a b: a is at least as precise as b (a ⊑ b)
  le_trans : ∀ a b c, le a b → le b c → le a c
  alpha : C → A                        -- the facts of a concrete input
  init : C → S                         -- the state a run starts in
  prog : List (S → Option S)           -- the source program
  removable : Nat → Bool               -- instructions the optimizer removed
  proves : A → Nat → Prop              -- analysing abstract input a proves removal k
  admitted : C → Prop                  -- the input satisfies the admission certificate
  mono : ∀ a b k, le a b → proves b k → proves a k
  sound : ∀ c k, admitted c → proves (alpha c) k → NoopAt prog 0 (init c) k

/-- The skip decision of the generated program: a removed instruction is skipped
    iff the input fits its item certificate. -/
def skipFor {C A S : Type} (_F : Analysis C A S) (removable : Nat → Bool) (fits : C → Nat → Bool) (c : C) :
    Nat → Bool :=
  fun k => removable k && fits c k

/-- Selective deoptimization is exact on every admitted input. -/
theorem deopt_correct {C A S : Type} (F : Analysis C A S) (cert : Nat → A)
    (verified : ∀ k, F.removable k = true → F.proves (cert k) k)
    (fits : C → Nat → Bool)
    (fits_sound : ∀ c k, fits c k = true → F.le (F.alpha c) (cert k))
    (c : C) (hc : F.admitted c) :
    runDeopt (skipFor F F.removable fits c) F.prog 0 (F.init c) = run F.prog (F.init c) := by
  apply skip_noops
  intro j hj
  have hboth : F.removable j = true ∧ fits c j = true := by
    simp [skipFor] at hj
    exact hj
  have hle := fits_sound c j hboth.2
  exact F.sound c j hc (F.mono (F.alpha c) (cert j) j hle (verified j hboth.1))

/-- Composition. Stage 1 guarantees that every output it writes for an admitted
    input has facts inside `guarantee`; if that guarantee is inside the item
    certificate of a removal of stage 2, and the run-time check of stage 2 is
    complete, stage 2 skips that removal for every such output: it is never
    deoptimized. -/
theorem compose_never_deopts {C1 C2 A1 A2 S1 S2 : Type}
    (F1 : Analysis C1 A1 S1) (F2 : Analysis C2 A2 S2)
    (output : C1 → C2)                                   -- what stage 1 writes
    (guarantee : A2)
    (guaranteed : ∀ c, F1.admitted c → F2.le (F2.alpha (output c)) guarantee)
    (cert2 : Nat → A2) (fits2 : C2 → Nat → Bool)
    (fits2_complete : ∀ o k, F2.le (F2.alpha o) (cert2 k) → fits2 o k = true)
    (k : Nat) (inside : F2.le guarantee (cert2 k))
    (c : C1) (hc : F1.admitted c) :
    fits2 (output c) k = true :=
  fits2_complete (output c) k (F2.le_trans _ _ _ (guaranteed c hc) inside)

/-- The counterfactual search: weakenings are tried one after the other and kept
    if the re-analysis still proves what is needed (`keep`). -/
def greedy {A : Type} (keep : A → Bool) : A → List (A → A) → A
  | a, [] => a
  | a, w :: ws => if keep (w a) then greedy keep (w a) ws else greedy keep a ws

/-- The search only ends on a certificate the re-analysis accepted. -/
theorem greedy_verified {A : Type} (keep : A → Bool) :
    ∀ (ws : List (A → A)) (a : A), keep a = true → keep (greedy keep a ws) = true := by
  intro ws
  induction ws with
  | nil => intro a h; exact h
  | cons w ws ih =>
    intro a h
    cases hw : keep (w a) with
    | true => simp only [greedy, hw, ite_true]; exact ih (w a) hw
    | false => simp only [greedy, hw]; exact ih a h

/-- And it only weakens: under a monotone `le`, if each weakening is above its
    input, the result is above the start (the certificate admits every input the
    compiled facts describe). -/
theorem greedy_weakens {A : Type} (le : A → A → Prop) (le_refl : ∀ a, le a a)
    (le_trans : ∀ a b c, le a b → le b c → le a c) (keep : A → Bool) :
    ∀ (ws : List (A → A)), (∀ w, w ∈ ws → ∀ a, le a (w a)) → ∀ a, le a (greedy keep a ws) := by
  intro ws
  induction ws with
  | nil => intro _ a; exact le_refl a
  | cons w ws ih =>
    intro hws a
    have tail : ∀ v, v ∈ ws → ∀ b, le b (v b) := fun v hv => hws v (List.mem_cons_of_mem w hv)
    cases hw : keep (w a) with
    | true =>
      simp only [greedy, hw, ite_true]
      exact le_trans _ _ _ (hws w (List.mem_cons_self) a) (ih tail (w a))
    | false =>
      simp only [greedy, hw]
      exact ih tail a

end PrepLang
